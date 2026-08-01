/*
 * Copyright (C) 2026 Tensor Lab
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ui_rf_notebook.hpp"

#include "baseband_api.hpp"
#include "file.hpp"
#include "portapack.hpp"
#include "string_format.hpp"
#include "ui_textentry.hpp"
#include "usb_serial_asyncmsg.hpp"

using namespace portapack;

namespace ui::external_app::rf_notebook {

namespace {

constexpr size_t note_max_length = 64;
const std::filesystem::path rfnote_root{u"/RFNOTE"};

/* Bins more than this many dB-units above the noise floor count as occupied. */
constexpr uint8_t occupancy_margin = 12;

std::string two(uint32_t v) { return to_string_dec_uint(v, 2, '0'); }

/* Mayhem paths are UTF-16. The only string->path helper in the tree is a static
 * inside the USB shell's header, which an app has no business including, so do
 * the widening locally. Filenames here are ASCII by construction. */
std::u16string widen(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    return out;
}

/* JSON string escaping, limited to what these fields can actually contain. */
std::string esc(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        if (c == '"' || c == '\\')
            out += '\\', out += c;
        else if (c == '\n' || c == '\r' || c == '\t')
            out += ' ';
        else
            out += c;
    }
    return out;
}

}  // namespace

RFNotebookView::RFNotebookView(NavigationView& nav)
    : nav_{nav} {
    add_children({&backdrop,
                  &text_session,
                  &text_freq,
                  &text_signal,
                  &text_sketch,
                  &text_events,
                  &field_frequency,
                  &text_note,
                  &text_status,
                  &button_mark,
                  &button_note,
                  &button_close});

    backdrop.load_first_of({tl_ui::logo_mid, tl_ui::logo_dim});

    /* Inherit whatever the radio is already tuned to, per plan Milestone 2
     * ("User can tune or inherit current frequency"). */
    field_frequency.set_value(receiver_model.target_frequency());
    field_frequency.on_change = [this](rf::Frequency f) {
        receiver_model.set_target_frequency(f);
        refresh();
    };

    button_mark.on_select = [this](Button&) { do_mark(); };

    button_note.on_select = [this](Button&) {
        text_prompt(nav_, note_, note_max_length, ENTER_KEYBOARD_MODE_ALPHA,
                    [this](std::string&) { refresh(); });
    };

    button_close.on_select = [this](Button&) { nav_.pop(); };

    session_ok_ = start_session();

    /* Spectrum analysis feeds the sketch. Receive-only: we never configure a
     * transmitter and the app declares no TX baseband. */
    receiver_model.set_sampling_rate(3'072'000);
    receiver_model.set_baseband_bandwidth(2'500'000);
    receiver_model.enable();
    baseband::spectrum_streaming_start();
    spectrum_running_ = true;

    reset_rssi();
    refresh();

    if (!session_ok_)
        set_status("ERR: cannot create session dir", false);
    else
        set_status("session " + session_id_, true);
}

RFNotebookView::~RFNotebookView() {
    if (spectrum_running_)
        baseband::spectrum_streaming_stop();
    receiver_model.disable();
}

void RFNotebookView::focus() {
    button_mark.focus();
}

void RFNotebookView::reset_rssi() {
    have_rssi_ = false;
    rssi_min_ = 255;
    rssi_max_ = 0;
    rssi_accum_ = 0;
    rssi_count_ = 0;
}

bool RFNotebookView::start_session() {
    const auto now = rtc_time::now();

    /* Device-local session id; the phone may later supply a UUID which the
     * importer reconciles by this id. Sortable so sessions list in order. */
    session_id_ = "S" + to_string_dec_uint(now.year(), 4, '0') + two(now.month()) + two(now.day()) +
                  "_" + two(now.hour()) + two(now.minute()) + two(now.second());

    session_dir_ = rfnote_root / session_id_;

    if (ensure_directory(session_dir_).code()) return false;
    if (ensure_directory(session_dir_ / u"sketches").code()) return false;

    /* session-device.json: what the DEVICE knows. Phone-side context (GPS,
     * venue, operator) is merged later by the desktop importer. */
    File f;
    if (f.create(session_dir_ / u"session-device.json").is_valid()) return false;

    std::string j = "{\"schema_version\":1";
    j += ",\"session_id\":\"" + session_id_ + "\"";
    j += ",\"device\":\"portapack-mayhem\"";
    j += ",\"started_at_local\":\"" + to_string_datetime(now, YMDHMS) + "\"";
    j += ",\"receive_only\":true";
    j += ",\"sketch_bins\":" + to_string_dec_uint(rfsk::bins);
    j += ",\"sketch_frames\":" + to_string_dec_uint(rfsk::frames);
    j += "}";
    return !f.write_line(j).is_valid();
}

void RFNotebookView::refresh() {
    text_session.set(session_ok_ ? session_id_ : "NO SESSION");
    text_freq.set("F " + to_string_short_freq(receiver_model.target_frequency()) + " MHz");

    if (have_rssi_ && rssi_count_) {
        const uint8_t avg = static_cast<uint8_t>(rssi_accum_ / rssi_count_);
        text_signal.set("RSSI " + to_string_dec_uint(rssi_min_) + "/" +
                        to_string_dec_uint(avg) + "/" + to_string_dec_uint(rssi_max_));
    } else {
        text_signal.set("RSSI --");
    }

    text_sketch.set(sketch_.has_data() ? "sketch ready" : "sketch filling...");
    text_events.set("events " + to_string_dec_uint(event_seq_));
    text_note.set(note_.empty() ? "note: (none)" : "note: " + note_.substr(0, 26));
    set_dirty();
}

void RFNotebookView::set_status(const std::string& msg, bool ok) {
    auto* theme = Theme::getInstance();
    text_status.set_style(ok ? theme->ok_dark : theme->error_dark);
    text_status.set(msg);
}

bool RFNotebookView::write_sketch(const std::filesystem::path& path, uint32_t seq,
                                  const std::array<uint8_t, rfsk::bins>& avg,
                                  uint8_t nf, uint16_t peak, uint32_t obw) {
    rfsk::Header hdr{};
    hdr.center_frequency_hz = static_cast<uint64_t>(receiver_model.target_frequency());
    hdr.device_monotonic_ms = chTimeNow();
    hdr.event_seq = seq;
    hdr.peak_bin = peak;
    hdr.occupied_bandwidth_hz = obw;
    hdr.noise_floor_dbfs = static_cast<int16_t>(nf);
    hdr.flags = have_rssi_ ? 1 : 0;
    if (have_rssi_ && rssi_count_) {
        const uint8_t r = static_cast<uint8_t>(rssi_accum_ / rssi_count_);
        hdr.rssi_dbfs = static_cast<int16_t>(r);
        hdr.snr_db = static_cast<int16_t>(r > nf ? r - nf : 0);
    }

    static std::array<uint8_t, rfsk::Accumulator::serialized_size> buf{};
    const size_t n = sketch_.serialize(hdr, avg, buf.data(), buf.size());
    if (n == 0) return false;

    File f;
    if (f.create(path).is_valid()) return false;
    return !f.write(buf.data(), n).is_error();
}

bool RFNotebookView::append_event(uint32_t seq, const std::array<uint8_t, rfsk::bins>& avg,
                                  uint8_t nf, uint16_t peak, uint32_t obw) {
    (void)avg;
    (void)peak;
    const auto now = rtc_time::now();

    File f;
    if (f.append(session_dir_ / u"events.jsonl").is_valid()) return false;

    const uint8_t rssi = (have_rssi_ && rssi_count_)
                             ? static_cast<uint8_t>(rssi_accum_ / rssi_count_)
                             : 0;
    const int snr = (have_rssi_ && rssi > nf) ? (rssi - nf) : 0;

    const std::string seqs = to_string_dec_uint(seq, 6, '0');

    /* Field names follow plan section 12 so the desktop importer can merge
     * device and phone records without translation. Fields the device cannot
     * know (GPS, venue, area_label, observed_at_utc) are omitted rather than
     * guessed -- the phone supplies them. */
    std::string j = "{\"schema_version\":1";
    j += ",\"session_id\":\"" + session_id_ + "\"";
    j += ",\"event_seq\":" + to_string_dec_uint(seq);
    j += ",\"event_id\":\"" + session_id_ + ":" + to_string_dec_uint(seq) + "\"";
    j += ",\"device_monotonic_ms\":" + to_string_dec_uint(chTimeNow());
    j += ",\"observed_at_device_local\":\"" + to_string_datetime(now, YMDHMS) + "\"";
    j += ",\"center_frequency_hz\":" + to_string_dec_uint(static_cast<uint32_t>(receiver_model.target_frequency()));
    j += ",\"sample_rate_sps\":" + to_string_dec_uint(receiver_model.sampling_rate());
    j += ",\"baseband_bandwidth_hz\":" + to_string_dec_uint(receiver_model.baseband_bandwidth());
    if (have_rssi_) {
        j += ",\"rssi_raw\":" + to_string_dec_uint(rssi);
        j += ",\"noise_floor_raw\":" + to_string_dec_uint(nf);
        j += ",\"snr_raw\":" + to_string_dec_uint(static_cast<uint32_t>(snr));
    } else {
        j += ",\"rssi_raw\":null,\"noise_floor_raw\":null,\"snr_raw\":null";
    }
    j += ",\"occupied_bandwidth_hz\":" + to_string_dec_uint(obw);
    j += ",\"labels\":[\"manual\"]";
    j += ",\"evidence_level\":1";
    j += ",\"spectral_sketch\":\"sketches/E" + seqs + ".rfsk\"";
    j += ",\"note\":\"" + esc(note_) + "\"";
    j += "}";

    return !f.write_line(j).is_valid();
}

void RFNotebookView::notify_phone(uint32_t seq, uint32_t obw, int snr) {
    /* Plan section 10 wants an event.created line pushed to the phone.
     * NOTE: Mayhem's only app-to-host push is UsbSerialAsyncmsg, whose own
     * header warns it is "not real async" and that concurrent transmissions
     * corrupt each other. It is gated behind portapack::async_tx_enabled, which
     * defaults off, so this is a no-op unless the host explicitly enables it
     * with `asyncmsg enable`. Treat as provisional: a polled or framed
     * transport should replace it before the phone side is relied on. */
    std::string m = "{\"v\":1,\"type\":\"event.created\"";
    m += ",\"session_id\":\"" + session_id_ + "\"";
    m += ",\"seq\":" + to_string_dec_uint(seq);
    m += ",\"freq_hz\":" + to_string_dec_uint(static_cast<uint32_t>(receiver_model.target_frequency()));
    m += ",\"snr_raw\":" + to_string_dec_uint(static_cast<uint32_t>(snr));
    m += ",\"obw_hz\":" + to_string_dec_uint(obw);
    m += ",\"artifact\":\"E" + to_string_dec_uint(seq, 6, '0') + ".rfsk\"}";
    UsbSerialAsyncmsg::asyncmsg(m);
}

void RFNotebookView::do_mark() {
    if (!session_ok_) {
        set_status("ERR: no session", false);
        return;
    }
    if (!sketch_.has_data()) {
        set_status("no spectrum yet - wait", false);
        return;
    }

    const uint32_t seq = event_seq_ + 1;

    std::array<uint8_t, rfsk::bins> avg{};
    sketch_.average(avg);
    const uint8_t nf = rfsk::Accumulator::noise_floor(avg);
    const uint16_t peak = rfsk::Accumulator::peak_bin(avg);
    const uint32_t obw = rfsk::Accumulator::occupied_bandwidth_hz(
        avg, nf, occupancy_margin, sketch_.span_hz());

    const auto sketch_name = widen("E" + to_string_dec_uint(seq, 6, '0') + ".rfsk");
    const auto sketch_path = session_dir_ / u"sketches" / std::filesystem::path{sketch_name.c_str()};

    if (!write_sketch(sketch_path, seq, avg, nf, peak, obw)) {
        set_status("ERR: sketch write failed", false);
        return;
    }
    if (!append_event(seq, avg, nf, peak, obw)) {
        set_status("ERR: event write failed", false);
        return;
    }

    event_seq_ = seq;

    const uint8_t rssi = (have_rssi_ && rssi_count_)
                             ? static_cast<uint8_t>(rssi_accum_ / rssi_count_)
                             : 0;
    notify_phone(seq, obw, (have_rssi_ && rssi > nf) ? (rssi - nf) : 0);

    /* Fresh statistics per event so successive marks do not share numbers. */
    reset_rssi();
    refresh();
    set_status("marked E" + to_string_dec_uint(seq, 6, '0'), true);
}

}  // namespace ui::external_app::rf_notebook
