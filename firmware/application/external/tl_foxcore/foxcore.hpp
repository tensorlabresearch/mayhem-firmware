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

/* Tensor Lab fox-hunt core, shared by the band-specific hunt apps.
 *
 * HEADER-ONLY BY NECESSITY, not by preference. Each external app links as its
 * own standalone binary and is collected by an object-name pattern in
 * external.ld, so a shared .cpp cannot be pulled into two different app
 * sections. Everything here is inline and gets its own copy per app -- keep it
 * small.
 *
 * Constraints this file is written against (see TENSORLAB_HACKRF_NOTES.md):
 *  - No namespace-scope objects with non-trivial constructors. They emit a
 *    _GLOBAL__sub_I initialiser inside the app's fake address region, which the
 *    firmware calls at boot into unmapped memory. That BRICKS the device.
 *  - No function-local statics (they need __cxa_guard_acquire).
 *  - Fixed-size storage only, no allocation in the RSSI path.
 *  - Total app budget is 32KB INCLUDING the baseband image.
 */

#ifndef _TL_FOXCORE_H
#define _TL_FOXCORE_H

#include <array>
#include <cstdint>

namespace tl_fox {

/* A tunable step in a band plan. `hz` is the centre; `label` is what the
 * operator sees (a WiFi channel number, a ham band name). */
struct Channel {
    uint64_t hz;
    const char* label;
};

/* Upper bound on any band plan we ship. 5 GHz U-NII is the largest at 25. */
constexpr size_t max_channels = 32;

/* Tracks signal strength for direction finding.
 *
 * Direction finding needs an INSTANTANEOUS reading plus a short peak hold: the
 * operator sweeps an antenna and watches for the peak. A cumulative min/avg/max
 * is useless for this -- that mistake is already recorded in the notes, where
 * RF Notebook's accumulator made it unusable for hunting. */
class RssiTracker {
   public:
    void reset() {
        instant_ = 0;
        ema_ = 0;
        peak_ = 0;
        peak_age_ = 0;
        seeded_ = false;
    }

    /* `v` is the raw RSSI ADC value from RSSIStatistics. */
    void push(uint8_t v) {
        instant_ = v;
        if (!seeded_) {
            ema_ = v;
            seeded_ = true;
        } else {
            /* Fast EMA (1/4) -- responsive enough to follow an antenna sweep
             * while still smoothing single-sample noise. Shift-based, no divide. */
            ema_ = static_cast<uint8_t>((ema_ * 3 + v) / 4);
        }
        if (v >= peak_) {
            peak_ = v;
            peak_age_ = 0;
        }
    }

    /* Call on each UI tick. Peak decays so a stale maximum from a previous
     * heading does not mislead the operator into walking the wrong way. */
    void age(uint16_t hold_ticks) {
        if (peak_age_ < 0xFFFF) peak_age_++;
        if (peak_age_ > hold_ticks && peak_ > instant_) {
            peak_ = static_cast<uint8_t>(peak_ - 1);  // slow bleed, not a cliff
            peak_age_ = hold_ticks;
        }
    }

    uint8_t instant() const { return instant_; }
    uint8_t smoothed() const { return ema_; }
    uint8_t peak() const { return peak_; }
    bool seeded() const { return seeded_; }

   private:
    uint8_t instant_{0};
    uint8_t ema_{0};
    uint8_t peak_{0};
    uint16_t peak_age_{0};
    bool seeded_{false};
};

/* Steps through a band plan, dwelling on each channel and remembering the best
 * signal seen per channel.
 *
 * This is a SURVEY, not a hunt. The workflow is: sweep once to find which
 * channel the target is on, then park on that channel and hunt by antenna
 * heading. Sweeping while hunting would be actively misleading, because the
 * reading would change for two reasons at once. */
class SweepEngine {
   public:
    void configure(const Channel* table, size_t count, uint16_t dwell_ticks) {
        table_ = table;
        count_ = (count > max_channels) ? max_channels : count;
        dwell_ = dwell_ticks ? dwell_ticks : 1;
        reset();
    }

    void reset() {
        index_ = 0;
        elapsed_ = 0;
        peaks_.fill(0);
        complete_ = false;
    }

    size_t count() const { return count_; }
    size_t index() const { return index_; }
    bool complete() const { return complete_; }

    const Channel* current() const {
        return (table_ && index_ < count_) ? &table_[index_] : nullptr;
    }

    uint8_t peak_at(size_t i) const { return (i < count_) ? peaks_[i] : 0; }

    /* Feeds the dwell. Returns true when the channel changed, so the caller
     * knows to retune. */
    bool tick(uint8_t rssi) {
        if (!table_ || count_ == 0) return false;
        if (rssi > peaks_[index_]) peaks_[index_] = rssi;

        if (++elapsed_ < dwell_) return false;
        elapsed_ = 0;
        index_++;
        if (index_ >= count_) {
            index_ = 0;
            complete_ = true;  // a full pass has been made; peaks are meaningful
        }
        return true;
    }

    /* Index of the strongest channel seen so far, or 0 when nothing stands out. */
    size_t best() const {
        size_t b = 0;
        for (size_t i = 1; i < count_; i++)
            if (peaks_[i] > peaks_[b]) b = i;
        return b;
    }

    /* Difference between the best and median channel. A large margin means the
     * survey actually found something; a small one means it is all noise and the
     * "best" channel is meaningless. Reported rather than hidden. */
    uint8_t margin() const {
        if (count_ < 3) return 0;
        std::array<uint8_t, max_channels> s = peaks_;
        /* insertion sort -- count is <= 32 and this runs once per pass */
        for (size_t i = 1; i < count_; i++) {
            uint8_t v = s[i];
            size_t j = i;
            while (j > 0 && s[j - 1] > v) {
                s[j] = s[j - 1];
                j--;
            }
            s[j] = v;
        }
        const uint8_t med = s[count_ / 2];
        const uint8_t hi = s[count_ - 1];
        return hi > med ? static_cast<uint8_t>(hi - med) : 0;
    }

   private:
    const Channel* table_{nullptr};
    size_t count_{0};
    size_t index_{0};
    uint16_t dwell_{1};
    uint16_t elapsed_{0};
    bool complete_{false};
    std::array<uint8_t, max_channels> peaks_{};
};

/* ---- Band plans -------------------------------------------------------- */

/* 2.4 GHz WiFi, channels 1-13 plus 14 (JP). Centres are the standard 20 MHz
 * channel centres; the PortaPack's channel filter is far narrower than 20 MHz,
 * so this samples the centre of each channel rather than covering it. */
constexpr std::array<Channel, 14> plan_wifi24{{
    {2412000000ULL, "ch1"},
    {2417000000ULL, "ch2"},
    {2422000000ULL, "ch3"},
    {2427000000ULL, "ch4"},
    {2432000000ULL, "ch5"},
    {2437000000ULL, "ch6"},
    {2442000000ULL, "ch7"},
    {2447000000ULL, "ch8"},
    {2452000000ULL, "ch9"},
    {2457000000ULL, "ch10"},
    {2462000000ULL, "ch11"},
    {2467000000ULL, "ch12"},
    {2472000000ULL, "ch13"},
    {2484000000ULL, "ch14"},
}};

/* 5 GHz U-NII 20 MHz channel centres, trimmed to the commonly used set.
 * NOTE: sensitivity here depends entirely on the antenna. A 2.4 GHz whip will
 * work poorly at 5 GHz and the operator should be told so rather than left to
 * conclude the band is quiet. */
constexpr std::array<Channel, 25> plan_wifi5{{
    {5180000000ULL, "36"},
    {5200000000ULL, "40"},
    {5220000000ULL, "44"},
    {5240000000ULL, "48"},
    {5260000000ULL, "52"},
    {5280000000ULL, "56"},
    {5300000000ULL, "60"},
    {5320000000ULL, "64"},
    {5500000000ULL, "100"},
    {5520000000ULL, "104"},
    {5540000000ULL, "108"},
    {5560000000ULL, "112"},
    {5580000000ULL, "116"},
    {5600000000ULL, "120"},
    {5620000000ULL, "124"},
    {5640000000ULL, "128"},
    {5660000000ULL, "132"},
    {5680000000ULL, "136"},
    {5700000000ULL, "140"},
    {5745000000ULL, "149"},
    {5765000000ULL, "153"},
    {5785000000ULL, "157"},
    {5805000000ULL, "161"},
    {5825000000ULL, "165"},
    {5865000000ULL, "173"},
}};

/* Ham fox-hunt calling and common hunt frequencies. Contest organisers publish
 * the actual fox frequencies; the plan document is explicit that these should be
 * entered from the contest table rather than hardcoded, so treat these as a
 * starting sweep, not gospel. */
constexpr std::array<Channel, 10> plan_ham{{
    {144000000ULL, "144.000"},
    {144200000ULL, "144.200"},
    {145000000ULL, "145.000"},
    {146520000ULL, "146.520"},
    {147000000ULL, "147.000"},
    {432000000ULL, "432.000"},
    {432100000ULL, "432.100"},
    {433920000ULL, "433.920"},
    {446000000ULL, "446.000"},
    {446500000ULL, "446.500"},
}};

/* ---- Gain guidance ----------------------------------------------------- */

/* Fox hunting saturates the front end as you close on the transmitter, and a
 * saturated receiver reads flat -- the signal appears to stop rising exactly
 * when you are getting warm. These are starting points; the operator must be
 * able to wind gain DOWN, which is why the apps expose the gain fields. */
struct GainPreset {
    uint8_t lna_db;
    uint8_t vga_db;
    bool amp;
};

constexpr GainPreset gain_far{32, 30, true};    // searching, weak signal
constexpr GainPreset gain_near{8, 10, false};   // closing in, avoid overload
constexpr GainPreset gain_ontop{0, 0, false};   // on top of it

}  // namespace tl_fox

#endif /*_TL_FOXCORE_H*/
