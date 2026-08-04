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

/* RF Field Notebook -- PortaPack device side, Milestone 2 of
 * rf_field_notebook_project_plan.md ("PortaPack manual event capture").
 *
 * Scope deliberately limited to the plan's conference MVP: manual MARK creates
 * an event, metadata is appended to events.jsonl, and a .rfsk spectral sketch is
 * written. No scanning, no automatic detection, no scoring model, no transmit --
 * those are Milestones 3+ and the plan is explicit that manual capture must be
 * solid first.
 *
 * Standalone by design: the SD card is the only output. The companion-device
 * side of the plan (sections 3.2, 9, 10) is deliberately NOT implemented -- it
 * is being deferred until the PortaPack has a wifi module, at which point the
 * transport will not be USB Web Serial and the protocol will need redesigning
 * anyway.
 */

#ifndef _UI_RF_NOTEBOOK
#define _UI_RF_NOTEBOOK

#include "message.hpp"
#include "radio_state.hpp"
#include "receiver_model.hpp"
#include "rfsk.hpp"
#include "rtc_time.hpp"
#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_widget.hpp"

namespace ui::external_app::rf_notebook {

class RFNotebookView : public View {
   public:
    explicit RFNotebookView(NavigationView& nav);
    ~RFNotebookView();

    void focus() override;

    std::string title() const override { return "RF Notebook"; };

   private:
    NavigationView& nav_;

    /* RAII radio ownership, exactly as waterfall_designer does it. Declared
     * early so it is constructed first and torn down last. Constructing it with
     * the mode is what was missing: driving receiver_model.enable() raw, without
     * this, hung the UI thread on app launch. */
    /* Frequency 0 means "keep whatever the radio is already tuned to", so the
     * operator can set it from Looking Glass, the console (setfreq) or any other
     * app and this inherits it. Plan Milestone 2: "tune or inherit current
     * frequency". */
    RxRadioState radio_state_{
        0,
        1'750'000,
        3'072'000,
        ReceiverModel::Mode::WidebandFMAudio};

    /* Session identity, minted device-local from the RTC. The plan envisaged a
     * companion device supplying a UUID and UTC anchor; with that deferred, the
     * device is the sole authority and the importer keys on this id. */
    std::string session_id_{};
    std::filesystem::path session_dir_{};
    uint32_t event_seq_{0};
    bool session_ok_{false};

    /* Note text for the next MARK. text_prompt() holds this by reference. */
    std::string note_{};

    /* Rolling spectral sketch, fed from ChannelSpectrum. */
    rfsk::Accumulator sketch_{};

    /* Serialisation scratch as a MEMBER, not a function-local static. A
     * function-local static would need __cxa_guard_acquire, and external apps
     * never run C++ start-up -- the same class of hazard as the namespace-scope
     * path that bricked boot. Keep all storage in the object. */
    std::array<uint8_t, rfsk::Accumulator::serialized_size> ser_buf_{};
    bool spectrum_running_{false};
    uint32_t ui_tick_{0};

    /* Milestone 3: fixed-frequency automatic detection. */
    rfsk::Detector detector_{};
    bool auto_armed_{false};
    uint32_t auto_events_{0};
    uint32_t last_auto_ms_{0};
    uint8_t last_score_{0};
    /* Per-channel cooldown. Without this a persistent carrier writes an event on
     * every evaluation and floods the log -- the false-positive failure the plan
     * warns about in section 19. */
    static constexpr uint32_t cooldown_ms = 5000;
    void evaluate_auto();

    /* RSSI statistics since the last MARK. */
    bool have_rssi_{false};
    uint8_t rssi_min_{255};
    uint8_t rssi_max_{0};
    uint32_t rssi_accum_{0};
    uint32_t rssi_count_{0};

    bool start_session();
    void reset_rssi();
    void refresh();
    void set_status(const std::string& msg, bool ok);
    void do_mark(bool automatic, uint8_t score,
                 bool narrowband, bool wideband, uint8_t persist);
    bool write_sketch(const std::filesystem::path& path, uint32_t seq,
                      const std::array<uint8_t, rfsk::bins>& avg,
                      uint8_t nf, uint16_t peak, uint32_t obw);
    bool append_event(uint32_t seq, const std::array<uint8_t, rfsk::bins>& avg,
                      uint8_t nf, uint16_t peak, uint32_t obw,
                      uint8_t nf_s, uint8_t spread_s, uint8_t snr_s,
                      bool automatic, uint8_t score,
                      bool narrowband, bool wideband, uint8_t persist);

    Text text_session{{UI_POS_X(0), UI_POS_Y(0), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_freq{{UI_POS_X(0), UI_POS_Y(1), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_signal{{UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_sketch{{UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_events{{UI_POS_X(0), UI_POS_Y(4), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_auto{{UI_POS_X(0), UI_POS_Y(6), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_note{{UI_POS_X(0), UI_POS_Y(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_status{{UI_POS_X(0), UI_POS_Y_BOTTOM(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};

    /* Frequency is editable here so the operator can tune without leaving the
     * app; it also inherits whatever the radio was already on. */
    FrequencyField field_frequency{{UI_POS_X(0), UI_POS_Y(5)}};

    Checkbox checkbox_auto{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(9)},
        11,
        "Auto-detect"};

    Button button_mark{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(6), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "MARK"};
    Button button_note{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(6), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Note"};
    Button button_close{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(3), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Close"};

    /* Spectrum plumbing, mirroring the pattern in ui_spectrum.hpp: the config
     * message hands us the FIFO, and we drain it on each display frame sync. */
    ChannelSpectrumFIFO* channel_fifo_{nullptr};

    /* Lets the console (setfreq) and, later, a companion device retune the app.
     * setfreq only broadcasts FreqChangeCommand -- an app must subscribe or it
     * is a no-op, which is why setfreq appeared to do nothing. */
    MessageHandlerRegistration message_handler_freqchg{
        Message::ID::FreqChangeCommand,
        [this](const Message* const p) {
            const auto m = static_cast<const FreqChangeCommandMessage*>(p);
            portapack::receiver_model.set_target_frequency(m->freq);
            field_frequency.set_value(m->freq);
            refresh();
        }};

    MessageHandlerRegistration message_handler_spectrum_config{
        Message::ID::ChannelSpectrumConfig,
        [this](const Message* const p) {
            channel_fifo_ = reinterpret_cast<const ChannelSpectrumConfigMessage*>(p)->fifo;
        }};

    MessageHandlerRegistration message_handler_frame_sync{
        Message::ID::DisplayFrameSync,
        [this](const Message* const) {
            if (channel_fifo_) {
                ChannelSpectrum s;
                while (channel_fifo_->out(s)) {
                    const uint32_t span = s.sampling_rate;
                    sketch_.push(s.db.data(), s.sampling_rate, span);
                }
            }
            /* The status lines were previously only redrawn on construction,
             * frequency change and after MARK, so "sketch filling..." and
             * "RSSI --" stayed on screen long after both were stale. Refresh
             * periodically, but not every frame -- that would repaint far more
             * often than the values change. */
            if ((++ui_tick_ % 30) == 0) {
                evaluate_auto();
                refresh();
            }
        }};

    MessageHandlerRegistration message_handler_rssi{
        Message::ID::RSSIStatistics,
        [this](const Message* const p) {
            const auto& s = reinterpret_cast<const RSSIStatisticsMessage*>(p)->statistics;
            if (s.count == 0) return;
            have_rssi_ = true;
            if (s.min < rssi_min_) rssi_min_ = s.min;
            if (s.max > rssi_max_) rssi_max_ = s.max;
            rssi_accum_ += s.accumulator;
            rssi_count_ += s.count;
        }};
};

}  // namespace ui::external_app::rf_notebook

#endif /*_UI_RF_NOTEBOOK*/
