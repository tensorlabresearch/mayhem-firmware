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

#ifndef _UI_RF_NOTEBOOK
#define _UI_RF_NOTEBOOK

#include "../tl_common/ui_tl_backdrop.hpp"
#include "message.hpp"
#include "receiver_model.hpp"
#include "rtc_time.hpp"
#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_widget.hpp"

namespace ui::external_app::rf_notebook {

/* One observation. Snapshotted at the moment the user logs it, not when the
 * view opened, so the numbers match what they were actually looking at. */
struct Observation {
    rtc::RTC when{};
    rf::Frequency frequency{0};
    uint32_t bandwidth{0};
    uint32_t sampling_rate{0};
    ReceiverModel::Mode modulation{ReceiverModel::Mode::NarrowbandFMAudio};
    std::string note{};

    /* RSSI is only meaningful while a baseband receiver is actually running.
     * When nothing is feeding us RSSIStatistics messages, have_rssi stays false
     * and we record "n/a" rather than a misleading zero. */
    bool have_rssi{false};
    uint8_t rssi_min{0};
    uint8_t rssi_avg{0};
    uint8_t rssi_max{0};
};

class RFNotebookView : public View {
   public:
    explicit RFNotebookView(NavigationView& nav);

    void focus() override;

    std::string title() const override { return "RF Notebook"; };

   private:
    NavigationView& nav_;

    /* Live note text. text_prompt() takes this by reference and needs it to
     * outlive the entry view, so it must be a member. */
    std::string note_{};

    /* Rolling RSSI, reset each time we log so successive entries do not share
     * stale statistics. */
    bool have_rssi_{false};
    uint8_t rssi_min_{255};
    uint8_t rssi_max_{0};
    uint32_t rssi_accum_{0};
    uint32_t rssi_count_{0};

    Observation snapshot() const;
    void refresh();
    void reset_rssi();
    void set_status(const std::string& msg, bool ok);

    void log_entry();
    void export_freqman();

    tl_ui::TLBackdrop backdrop{{0, 0, UI_POS_MAXWIDTH, UI_POS_HEIGHT_REMAINING(1)}};

    Text text_freq{{UI_POS_X(0), UI_POS_Y(0), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_mod{{UI_POS_X(0), UI_POS_Y(1), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_rssi{{UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_time{{UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};

    Text text_note_label{{UI_POS_X(0), UI_POS_Y(5), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}, "NOTE"};
    Text text_note_1{{UI_POS_X(0), UI_POS_Y(6), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_note_2{{UI_POS_X(0), UI_POS_Y(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};

    Text text_status{{UI_POS_X(0), UI_POS_Y_BOTTOM(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};

    Button button_note{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(6), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Edit Note"};
    Button button_log{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(6), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Log Entry"};
    Button button_freqman{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(3), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "To Freqman"};
    Button button_close{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(3), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Close"};

    MessageHandlerRegistration message_handler_rssi{
        Message::ID::RSSIStatistics,
        [this](const Message* const p) {
            const auto& s = static_cast<const RSSIStatisticsMessage*>(p)->statistics;
            if (s.count == 0)
                return;
            have_rssi_ = true;
            if (s.min < rssi_min_) rssi_min_ = s.min;
            if (s.max > rssi_max_) rssi_max_ = s.max;
            rssi_accum_ += s.accumulator;
            rssi_count_ += s.count;
        }};
};

}  // namespace ui::external_app::rf_notebook

#endif /*_UI_RF_NOTEBOOK*/
