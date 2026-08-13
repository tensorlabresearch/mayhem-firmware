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

/* Shared fox-hunt view. The three band apps derive from this and supply only a
 * channel plan and a title, so the hunting behaviour is identical across bands.
 *
 * Header-only for the same reason as foxcore.hpp: external apps cannot share a
 * .cpp. Each app compiles its own copy into its own object.
 *
 * Two modes, deliberately separated:
 *   SURVEY  sweep the band plan, remember the best signal per channel
 *   HUNT    park on one channel and show instantaneous + peak-hold RSSI
 *
 * Sweeping while hunting would be actively misleading -- the reading would move
 * for two reasons at once (retune and antenna heading) and the operator could
 * not tell which. So the modes are mutually exclusive.
 */

#ifndef _TL_FOXVIEW_H
#define _TL_FOXVIEW_H

#include "foxcore.hpp"

#include "baseband_api.hpp"
#include "message.hpp"
#include "portapack.hpp"
#include "radio_state.hpp"
#include "receiver_model.hpp"
#include "string_format.hpp"
#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_rssi.hpp"
#include "ui_spectrum.hpp"  // filter_bandwidth_for_sampling_rate
#include "ui_widget.hpp"

namespace tl_fox {

class FoxHuntView : public ui::View {
   public:
    FoxHuntView(ui::NavigationView& nav,
                const Channel* plan,
                size_t plan_count,
                const char* title,
                uint32_t sample_rate)
        : nav_{nav}, title_{title} {
        add_children({&field_rf_amp,
                      &field_lna,
                      &field_vga,
                      &rssi_bar,
                      &text_channel,
                      &text_reading,
                      &text_survey,
                      &rssi_graph,
                      &button_mode,
                      &button_next,
                      &button_gain,
                      &button_close});

        sweep_.configure(plan, plan_count, dwell_ticks);
        plan_ = plan;
        plan_count_ = plan_count;

        rssi_bar.set_peak(true, 500);

        button_mode.on_select = [this](ui::Button&) {
            survey_ = !survey_;
            sweep_.reset();
            tracker_.reset();
            button_mode.set_text(survey_ ? "Mode: SURVEY" : "Mode: HUNT");
            retune();
            refresh();
        };

        /* In HUNT mode this steps to the next channel manually; in SURVEY it
         * jumps straight to the strongest channel found so far, which is the
         * normal way to go from "which channel" to "where is it". */
        button_next.on_select = [this](ui::Button&) {
            if (survey_) {
                park_index_ = sweep_.best();
                survey_ = false;
                button_mode.set_text("Mode: HUNT");
                tracker_.reset();
            } else {
                park_index_ = (park_index_ + 1) % (plan_count_ ? plan_count_ : 1);
            }
            retune();
            refresh();
        };

        /* Cycles the gain preset. Closing on a fox saturates the front end and
         * the reading goes flat exactly when it should be rising, so being able
         * to wind gain down quickly matters more than absolute sensitivity. */
        button_gain.on_select = [this](ui::Button&) {
            gain_step_ = (gain_step_ + 1) % 3;
            apply_gain();
            refresh();
        };

        button_close.on_select = [this](ui::Button&) { nav_.pop(); };

        /* An external app owning a baseband must start the M4 image itself;
         * built-ins get this for free. Without it, receiver_model.enable() and
         * spectrum_streaming_start() block on a baseband that never ran and the
         * UI thread hangs the instant the app opens. */
        baseband::run_prepared_image(portapack::memory::map::m4_code.base());

        portapack::receiver_model.set_sampling_rate(sample_rate);
        portapack::receiver_model.set_baseband_bandwidth(
            filter_bandwidth_for_sampling_rate(sample_rate));
        portapack::receiver_model.set_squelch_level(0);
        apply_gain();
        portapack::receiver_model.enable();

        retune();
        refresh();
    }

    ~FoxHuntView() {
        portapack::receiver_model.disable();
        baseband::shutdown();
    }

    void focus() override { button_mode.focus(); }
    std::string title() const override { return title_; }

   private:
    /* Dwell per channel in UI ticks. The tick runs every 30 display frames, so
     * this is roughly half a second per channel -- long enough for the PLL to
     * settle and for a bursty source to be caught, short enough that a 25
     * channel 5 GHz sweep is not unbearable. */
    static constexpr uint16_t dwell_ticks = 2;
    static constexpr uint16_t peak_hold_ticks = 8;

    ui::NavigationView& nav_;
    std::string title_;

    const Channel* plan_{nullptr};
    size_t plan_count_{0};
    size_t park_index_{0};
    bool survey_{true};
    uint8_t gain_step_{0};
    uint32_t ui_tick_{0};

    SweepEngine sweep_{};
    RssiTracker tracker_{};

    void apply_gain() {
        const GainPreset p = (gain_step_ == 0) ? gain_far
                             : (gain_step_ == 1) ? gain_near
                                                 : gain_ontop;
        portapack::receiver_model.set_rf_amp(p.amp);
        portapack::receiver_model.set_lna(p.lna_db);
        portapack::receiver_model.set_vga(p.vga_db);
        button_gain.set_text(gain_step_ == 0   ? "Gain: FAR"
                             : gain_step_ == 1 ? "Gain: NEAR"
                                               : "Gain: TOP");
    }

    void retune() {
        const Channel* c = survey_ ? sweep_.current()
                                   : (plan_ && park_index_ < plan_count_ ? &plan_[park_index_]
                                                                         : nullptr);
        if (c) portapack::receiver_model.set_target_frequency(c->hz);
    }

    void refresh() {
        const Channel* c = survey_ ? sweep_.current()
                                   : (plan_ && park_index_ < plan_count_ ? &plan_[park_index_]
                                                                         : nullptr);
        std::string ch = c ? std::string(c->label) : std::string("--");
        std::string mhz = c ? to_string_dec_uint(static_cast<uint32_t>(c->hz / 1000000)) : "0";
        text_channel.set(ch + "  " + mhz + " MHz");

        text_reading.set("now " + to_string_dec_uint(tracker_.instant()) +
                         "  avg " + to_string_dec_uint(tracker_.smoothed()) +
                         "  pk " + to_string_dec_uint(tracker_.peak()));

        if (survey_) {
            const size_t b = sweep_.best();
            const uint8_t m = sweep_.margin();
            if (!sweep_.complete()) {
                text_survey.set("survey " + to_string_dec_uint(sweep_.index() + 1) +
                                "/" + to_string_dec_uint(plan_count_));
            } else if (m < 6) {
                /* Say so rather than pointing confidently at noise. */
                text_survey.set("no clear peak (margin " + to_string_dec_uint(m) + ")");
            } else {
                text_survey.set("best " + std::string(plan_[b].label) +
                                "  margin " + to_string_dec_uint(m));
            }
        } else {
            text_survey.set("parked - sweep antenna");
        }
        set_dirty();
    }

    ui::RFAmpField field_rf_amp{{13 * 8, ui::Point{0, 0}.y()}};
    ui::LNAGainField field_lna{{15 * 8, 0}};
    ui::VGAGainField field_vga{{18 * 8, 0}};
    ui::RSSI rssi_bar{{UI_POS_X(21), 0, UI_POS_WIDTH_REMAINING(24), 4}};

    ui::Text text_channel{{UI_POS_X(0), UI_POS_Y(1), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    ui::Text text_reading{{UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    ui::Text text_survey{{UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};

    ui::RSSIGraph rssi_graph{{0, UI_POS_Y(4) + 4, ui::screen_width, 40}};

    ui::Button button_mode{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(8), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Mode: SURVEY"};
    ui::Button button_next{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(8), UI_POS_WIDTH(13), UI_POS_HEIGHT(2)},
        "Next/Best"};
    ui::Button button_gain{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(5), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Gain: FAR"};
    ui::Button button_close{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(5), UI_POS_WIDTH(13), UI_POS_HEIGHT(2)},
        "Close"};

    MessageHandlerRegistration message_handler_rssi{
        Message::ID::RSSIStatistics,
        [this](const Message* const p) {
            const auto& s = reinterpret_cast<const RSSIStatisticsMessage*>(p)->statistics;
            if (s.count == 0) return;
            const uint8_t avg = static_cast<uint8_t>(s.accumulator / s.count);
            tracker_.push(avg);
            rssi_graph.add_values(s.min, avg, s.max, 0);
        }};

    MessageHandlerRegistration message_handler_frame_sync{
        Message::ID::DisplayFrameSync,
        [this](const Message* const) {
            /* Throttled: repainting every frame would redraw far more often than
             * the values change, and in SURVEY mode would retune absurdly fast. */
            if ((++ui_tick_ % 30) != 0) return;

            tracker_.age(peak_hold_ticks);

            if (survey_ && sweep_.tick(tracker_.smoothed()))
                retune();

            refresh();
        }};
};

}  // namespace tl_fox

#endif /*_TL_FOXVIEW_H*/
