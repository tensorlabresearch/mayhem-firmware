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

/* BLE advertising monitor -- anomaly detection on the advertising channels.
 *
 * WHAT THIS IS NOT: it is not a WiFi deauth detector. 802.11 deauth is a
 * management frame; detecting it requires OFDM demodulation and MAC-layer
 * parsing, neither of which exists in this firmware. No amount of spectrum
 * energy analysis can distinguish a deauth from any other burst, and a module
 * claiming otherwise would be guessing. That request cannot be met on this
 * hardware.
 *
 * WHAT THIS IS: BLE advertising packets ARE decodable here -- proc_btlerx emits
 * BLEPacketMessage carrying a real BlePacketData with MAC address, PDU type and
 * payload. So the closest genuine analogue to "deauth detection" is available:
 * BLE advertising floods (the "BLE spam" attack class) are a real, observable
 * phenomenon, and they are detectable by counting adverts and distinct source
 * MACs per time window.
 *
 * The heuristics below are labelled as heuristics. Per the project plan, an
 * elevated rate is reported as "unusual advertising rate", never as an
 * identified attack.
 */

#ifndef _UI_TL_BTMON
#define _UI_TL_BTMON

#include <array>
#include <cstdint>

#include "baseband_api.hpp"
#include "message.hpp"
#include "portapack.hpp"
#include "radio_state.hpp"
#include "receiver_model.hpp"
#include "string_format.hpp"
#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_receiver.hpp"
#include "ui_widget.hpp"

namespace ui::external_app::tl_btmon {

/* Rolling census of advertisers seen in the current window.
 *
 * Fixed-size on purpose: no allocation in the packet path, and a bounded table
 * cannot be exhausted by a flood -- which is precisely the condition being
 * detected. Overflow is counted and surfaced rather than silently dropped,
 * because "the table filled up" is itself the signal. */
class MacCensus {
   public:
    static constexpr size_t capacity = 48;

    void reset() {
        used_ = 0;
        overflow_ = 0;
        packets_ = 0;
    }

    void add(const uint8_t mac[6], uint8_t /*type*/) {
        packets_++;
        for (size_t i = 0; i < used_; i++) {
            if (match(entries_[i].mac, mac)) {
                if (entries_[i].hits < 0xFFFF) entries_[i].hits++;
                return;
            }
        }
        if (used_ >= capacity) {
            if (overflow_ < 0xFFFF) overflow_++;
            return;
        }
        for (size_t b = 0; b < 6; b++) entries_[used_].mac[b] = mac[b];
        entries_[used_].hits = 1;
        used_++;
    }

    size_t unique() const { return used_; }
    uint16_t overflow() const { return overflow_; }
    uint32_t packets() const { return packets_; }

    /* Most-repeated advertiser in this window. A single MAC hammering the
     * channel looks very different from many MACs each seen once, and the two
     * cases warrant different conclusions. */
    uint16_t busiest_hits() const {
        uint16_t m = 0;
        for (size_t i = 0; i < used_; i++)
            if (entries_[i].hits > m) m = entries_[i].hits;
        return m;
    }

   private:
    struct Entry {
        uint8_t mac[6];
        uint16_t hits;
    };
    static bool match(const uint8_t a[6], const uint8_t b[6]) {
        for (size_t i = 0; i < 6; i++)
            if (a[i] != b[i]) return false;
        return true;
    }
    std::array<Entry, capacity> entries_{};
    size_t used_{0};
    uint16_t overflow_{0};
    uint32_t packets_{0};
};

class BtMonView : public View {
   public:
    explicit BtMonView(NavigationView& nav);
    ~BtMonView();

    void focus() override;
    std::string title() const override { return "BLE Monitor"; };

   private:
    /* Window length in UI ticks (~30 display frames each), so roughly 4 seconds.
     * Long enough that a normal environment produces a stable baseline, short
     * enough that a flood is caught while it is happening. */
    static constexpr uint16_t window_ticks = 8;

    /* Thresholds are starting points, NOT calibrated. They must be checked
     * against a real environment before any output is trusted; a busy
     * conference floor has a far higher baseline than a lab. */
    static constexpr uint32_t flood_packets = 240;   // adverts per window
    static constexpr size_t flood_unique = 30;       // distinct MACs per window

    NavigationView& nav_;

    RxRadioState radio_state_{
        2402000000,
        4'000'000,
        4'000'000,
        ReceiverModel::Mode::WidebandFMAudio};

    MacCensus census_{};
    uint32_t ui_tick_{0};
    uint32_t windows_{0};
    uint32_t last_packets_{0};
    size_t last_unique_{0};
    uint32_t baseline_packets_{0};
    bool baseline_ready_{false};
    uint8_t adv_index_{0};
    bool alerting_{false};

    void next_advertising_channel();
    void close_window();
    void refresh();

    Text text_channel{{UI_POS_X(0), UI_POS_Y(0), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_rate{{UI_POS_X(0), UI_POS_Y(1), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_unique{{UI_POS_X(0), UI_POS_Y(2), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_baseline{{UI_POS_X(0), UI_POS_Y(3), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_verdict{{UI_POS_X(0), UI_POS_Y(5), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};
    Text text_note{{UI_POS_X(0), UI_POS_Y(7), UI_POS_MAXWIDTH, UI_POS_HEIGHT(1)}};

    Button button_reset{
        {UI_POS_X(0), UI_POS_Y_BOTTOM(6), UI_POS_WIDTH(14), UI_POS_HEIGHT(2)},
        "Rebaseline"};
    Button button_close{
        {UI_POS_X(16), UI_POS_Y_BOTTOM(6), UI_POS_WIDTH(13), UI_POS_HEIGHT(2)},
        "Close"};

    /* Real decoded advertising packets, not spectrum energy. */
    MessageHandlerRegistration message_handler_ble{
        Message::ID::BlePacket,
        [this](const Message* const p) {
            const auto* m = reinterpret_cast<const BLEPacketMessage*>(p);
            if (m && m->packet) census_.add(m->packet->macAddress, m->packet->type);
        }};

    MessageHandlerRegistration message_handler_frame_sync{
        Message::ID::DisplayFrameSync,
        [this](const Message* const) {
            if ((++ui_tick_ % 30) != 0) return;
            if ((ui_tick_ / 30) % window_ticks == 0) close_window();
            refresh();
        }};
};

}  // namespace ui::external_app::tl_btmon

#endif /*_UI_TL_BTMON*/
