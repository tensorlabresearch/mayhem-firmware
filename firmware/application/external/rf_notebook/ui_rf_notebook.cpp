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

#include "file.hpp"
#include "freqman_db.hpp"
#include "log_file.hpp"
#include "portapack.hpp"
#include "string_format.hpp"
#include "ui_textentry.hpp"

using namespace portapack;

namespace ui::external_app::rf_notebook {

namespace {

constexpr size_t note_max_length = 60;
constexpr size_t note_line_chars = 30;

/* Both the human-readable log and the freqman export live under stable names so
 * repeated sessions append rather than scatter files. */
const std::filesystem::path notebook_dir{u"/NOTEBOOK"};
constexpr std::string_view freqman_stem{"RFNOTE"};

std::string mode_name(ReceiverModel::Mode m) {
    switch (m) {
        case ReceiverModel::Mode::AMAudio:
            return "AM";
        case ReceiverModel::Mode::NarrowbandFMAudio:
            return "NFM";
        case ReceiverModel::Mode::WidebandFMAudio:
            return "WFM";
        case ReceiverModel::Mode::SpectrumAnalysis:
            return "SPEC";
        case ReceiverModel::Mode::AMAudioFMApt:
            return "AM/APT";
        case ReceiverModel::Mode::WFMAudioAMApt:
            return "WFM/APT";
        case ReceiverModel::Mode::Capture:
            return "CAPT";
        default:
            return "?";
    }
}

/* Commas would collide with the CSV-ish log format, and quotes would need
 * escaping. Strip both rather than emit a line that a parser trips over. */
std::string sanitize_note(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == ',' || c == '"' || c == '\r' || c == '\n')
            out += ' ';
        else
            out += c;
    }
    return out;
}

}  // namespace

RFNotebookView::RFNotebookView(NavigationView& nav)
    : nav_{nav} {
    /* Backdrop first so everything else paints over it. */
    add_children({&backdrop,
                  &text_freq,
                  &text_mod,
                  &text_rssi,
                  &text_time,
                  &text_note_label,
                  &text_note_1,
                  &text_note_2,
                  &text_status,
                  &button_note,
                  &button_log,
                  &button_freqman,
                  &button_close});

    backdrop.load_first_of({tl_ui::logo_mid, tl_ui::logo_dim});

    button_note.on_select = [this](Button&) {
        text_prompt(nav_, note_, note_max_length, ENTER_KEYBOARD_MODE_ALPHA, [this](std::string&) {
            refresh();
        });
    };

    button_log.on_select = [this](Button&) { log_entry(); };
    button_freqman.on_select = [this](Button&) { export_freqman(); };
    button_close.on_select = [this](Button&) { nav_.pop(); };

    reset_rssi();
    refresh();
}

void RFNotebookView::focus() {
    button_log.focus();
}

void RFNotebookView::reset_rssi() {
    have_rssi_ = false;
    rssi_min_ = 255;
    rssi_max_ = 0;
    rssi_accum_ = 0;
    rssi_count_ = 0;
}

Observation RFNotebookView::snapshot() const {
    Observation o{};
    o.when = rtc_time::now();
    o.frequency = receiver_model.target_frequency();
    o.bandwidth = receiver_model.baseband_bandwidth();
    o.sampling_rate = receiver_model.sampling_rate();
    o.modulation = receiver_model.modulation();
    o.note = note_;

    if (have_rssi_ && rssi_count_ > 0) {
        o.have_rssi = true;
        o.rssi_min = rssi_min_;
        o.rssi_max = rssi_max_;
        o.rssi_avg = static_cast<uint8_t>(rssi_accum_ / rssi_count_);
    }
    return o;
}

void RFNotebookView::refresh() {
    const auto o = snapshot();

    text_freq.set("F " + to_string_short_freq(o.frequency) + " MHz");
    text_mod.set(mode_name(o.modulation) +
                 "  BW " + to_string_dec_uint(o.bandwidth / 1000) + "k" +
                 "  SR " + to_string_dec_uint(o.sampling_rate / 1000) + "k");

    if (o.have_rssi) {
        text_rssi.set("RSSI " + to_string_dec_uint(o.rssi_min) +
                      "/" + to_string_dec_uint(o.rssi_avg) +
                      "/" + to_string_dec_uint(o.rssi_max));
    } else {
        text_rssi.set("RSSI n/a (no active RX)");
    }

    text_time.set(to_string_datetime(o.when, YMDHMS));

    /* Wrap the note across the two available lines. */
    if (note_.empty()) {
        text_note_1.set("(none)");
        text_note_2.set("");
    } else {
        text_note_1.set(note_.substr(0, note_line_chars));
        text_note_2.set(note_.size() > note_line_chars ? note_.substr(note_line_chars, note_line_chars) : "");
    }

    set_dirty();
}

void RFNotebookView::set_status(const std::string& msg, bool ok) {
    auto* theme = Theme::getInstance();
    text_status.set_style(ok ? theme->ok_dark : theme->error_dark);
    text_status.set(msg);
}

void RFNotebookView::log_entry() {
    const auto o = snapshot();

    /* One file per day keeps them browsable on the device. TimeFormat has no
     * date-only option, so build it from the RTC fields directly. */
    const std::string filename = "RFNOTE_" +
                                 to_string_dec_uint(o.when.year(), 4, '0') +
                                 to_string_dec_uint(o.when.month(), 2, '0') +
                                 to_string_dec_uint(o.when.day(), 2, '0') +
                                 ".TXT";

    LogFile log;
    if (log.append(notebook_dir / filename).is_valid()) {
        set_status("ERR: cannot open log", false);
        return;
    }

    std::string line = to_string_dec_uint(static_cast<uint32_t>(o.frequency / 1000)) + "kHz, " +
                       mode_name(o.modulation) +
                       ", bw=" + to_string_dec_uint(o.bandwidth) +
                       ", sr=" + to_string_dec_uint(o.sampling_rate) +
                       ", rssi=";
    if (o.have_rssi) {
        line += to_string_dec_uint(o.rssi_min) + "/" +
                to_string_dec_uint(o.rssi_avg) + "/" +
                to_string_dec_uint(o.rssi_max);
    } else {
        line += "n/a";
    }
    line += ", note=\"" + sanitize_note(o.note) + "\"";

    if (log.write_entry(o.when, line).is_valid()) {
        set_status("ERR: write failed", false);
        return;
    }

    /* Fresh statistics for the next observation. */
    reset_rssi();
    refresh();
    set_status("logged " + filename, true);
}

void RFNotebookView::export_freqman() {
    const auto o = snapshot();

    freqman_entry entry{};
    entry.type = freqman_type::Single;
    entry.frequency_a = o.frequency;
    /* Description carries the note so the entry is self-explanatory in Recon
     * and Scanner, which only surface the description field. */
    entry.description = o.note.empty()
                            ? ("RFNOTE " + to_string_datetime(o.when, YMDHMS))
                            : sanitize_note(o.note);

    File f;
    const auto path = get_freqman_path(std::string{freqman_stem});
    if (f.append(path).is_valid()) {
        set_status("ERR: cannot open freqman", false);
        return;
    }

    if (f.write_line(to_freqman_string(entry)).is_valid()) {
        set_status("ERR: freqman write failed", false);
        return;
    }

    set_status("freqman += RFNOTE", true);
}

}  // namespace ui::external_app::rf_notebook
