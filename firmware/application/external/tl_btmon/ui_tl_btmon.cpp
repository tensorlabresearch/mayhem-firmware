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

#include "ui_tl_btmon.hpp"

namespace ui::external_app::tl_btmon {

namespace {

/* The three BLE advertising channels. Must be constexpr, not namespace-scope
 * objects with constructors -- see TENSORLAB_HACKRF_NOTES.md, that pattern
 * bricks the device at boot. */
constexpr uint64_t adv_channels[3] = {
    2402000000ULL,  // ch37
    2426000000ULL,  // ch38
    2480000000ULL,  // ch39
};
constexpr const char* adv_labels[3] = {"37", "38", "39"};

}  // namespace

BtMonView::BtMonView(NavigationView& nav)
    : nav_{nav} {
    add_children({&text_channel,
                  &text_rate,
                  &text_unique,
                  &text_baseline,
                  &text_verdict,
                  &text_note,
                  &button_reset,
                  &button_close});

    button_reset.on_select = [this](Button&) {
        baseline_ready_ = false;
        baseline_packets_ = 0;
        windows_ = 0;
        census_.reset();
        alerting_ = false;
        refresh();
    };

    button_close.on_select = [this](Button&) { nav_.pop(); };

    /* External apps must start the M4 image themselves. */
    baseband::run_prepared_image(portapack::memory::map::m4_code.base());

    portapack::receiver_model.set_sampling_rate(4'000'000);
    portapack::receiver_model.set_baseband_bandwidth(2'500'000);
    portapack::receiver_model.set_squelch_level(0);
    portapack::receiver_model.enable();

    next_advertising_channel();
    census_.reset();
    refresh();

    text_note.set("heuristic - not attack ID");
}

BtMonView::~BtMonView() {
    portapack::receiver_model.disable();
    baseband::shutdown();
}

void BtMonView::focus() {
    button_reset.focus();
}

/* Rotates across the three advertising channels. A flood confined to one channel
 * looks different from one spread across all three, and staring at a single
 * channel would miss the difference entirely. */
void BtMonView::next_advertising_channel() {
    adv_index_ = static_cast<uint8_t>((adv_index_ + 1) % 3);
    portapack::receiver_model.set_target_frequency(adv_channels[adv_index_]);
}

void BtMonView::close_window() {
    last_packets_ = census_.packets();
    last_unique_ = census_.unique();

    /* Learn a baseline from the first few windows, and only while not alerting.
     * Folding a live flood into "normal" would blind the detector to exactly the
     * condition it exists to catch -- the same trap as the RF Notebook detector. */
    if (!baseline_ready_) {
        baseline_packets_ = (windows_ == 0)
                                ? last_packets_
                                : (baseline_packets_ * 3 + last_packets_) / 4;
        if (++windows_ >= 4) baseline_ready_ = true;
    }

    /* Two independent signatures, because they mean different things:
     *  - many packets from few MACs  -> one device hammering the channel
     *  - many distinct MACs          -> spoofed/randomised source addresses,
     *                                   the classic BLE spam signature
     * Table overflow counts as evidence too: it only happens under flood. */
    const bool rate_anomaly =
        baseline_ready_ && baseline_packets_ > 0 &&
        last_packets_ > (baseline_packets_ * 3) && last_packets_ > 40;
    const bool absolute_flood = last_packets_ > flood_packets;
    const bool mac_churn = last_unique_ >= flood_unique || census_.overflow() > 0;

    alerting_ = rate_anomaly || absolute_flood || mac_churn;

    census_.reset();
    next_advertising_channel();
}

void BtMonView::refresh() {
    text_channel.set("adv ch" + std::string(adv_labels[adv_index_]) + "  " +
                     to_string_dec_uint(static_cast<uint32_t>(adv_channels[adv_index_] / 1000000)) +
                     " MHz");
    text_rate.set("pkts/win " + to_string_dec_uint(last_packets_) +
                  "  live " + to_string_dec_uint(census_.packets()));
    text_unique.set("macs " + to_string_dec_uint(last_unique_) +
                    "  ovf " + to_string_dec_uint(census_.overflow()) +
                    "  top " + to_string_dec_uint(census_.busiest_hits()));

    if (!baseline_ready_) {
        text_baseline.set("baseline " + to_string_dec_uint(windows_) + "/4");
    } else {
        text_baseline.set("baseline " + to_string_dec_uint(baseline_packets_) + " pkts/win");
    }

    auto* theme = Theme::getInstance();
    if (!baseline_ready_) {
        text_verdict.set_style(theme->fg_medium);
        text_verdict.set("learning...");
    } else if (alerting_) {
        /* Deliberately not "attack detected". The plan forbids presenting a
         * heuristic as an identification, and an elevated advertising rate has
         * innocent explanations -- a crowd, a beacon array, a phone in pairing. */
        text_verdict.set_style(theme->warning_dark);
        text_verdict.set("unusual advertising rate");
    } else {
        text_verdict.set_style(theme->ok_dark);
        text_verdict.set("nominal");
    }
    set_dirty();
}

}  // namespace ui::external_app::tl_btmon
