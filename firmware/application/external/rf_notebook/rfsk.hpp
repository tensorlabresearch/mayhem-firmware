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

/* Spectral sketch (.rfsk) -- the "Level 1" evidence format from the RF Field
 * Notebook plan. A compact power-over-frequency-and-time matrix that preserves
 * signal width, burst behaviour and repetition without retaining raw I/Q.
 *
 * Everything here is fixed-size and header-only: no dynamic allocation, and it
 * compiles into the including app's own object so external.ld can collect it.
 */

#ifndef _RFSK_H
#define _RFSK_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

#include "crc.hpp"

namespace rfsk {

constexpr size_t bins = 64;
constexpr size_t frames = 16;
constexpr uint16_t format_version = 1;

/* On-disk layout. Packed and little-endian; the desktop importer reads this
 * directly. CRC32 uses the SAME variant as Mayhem's `crc32` console command
 * (poly 0x04C11DB7, init/xorout 0xFFFFFFFF, MSB-first -- i.e. CRC-32/BZIP2, NOT
 * zlib's reflected form) so both sides agree. */
struct __attribute__((packed)) Header {
    char magic[4];  // "RFSK"
    uint16_t version;
    uint16_t n_bins;
    uint16_t n_frames;
    uint16_t flags;  // bit0: rssi fields valid
    uint64_t center_frequency_hz;
    uint32_t sample_rate_sps;
    uint32_t span_hz;
    uint32_t device_monotonic_ms;
    uint32_t event_seq;
    int16_t rssi_dbfs;
    int16_t noise_floor_dbfs;
    int16_t snr_db;
    uint16_t peak_bin;
    uint32_t occupied_bandwidth_hz;
};

/* 4+2+2+2+2 +8 +4+4+4+4 +2+2+2+2 +4 = 48. Pinned so the desktop importer and
 * this struct cannot drift apart silently. */
static_assert(sizeof(Header) == 48, "rfsk header layout changed");

/* Accumulates ChannelSpectrum frames into a rolling matrix.
 *
 * Downsamples 256 spectrum bins to 64 by taking the MAX of each group of four,
 * not the mean. A narrowband burst occupying one of four source bins survives
 * as a peak instead of being averaged into the noise -- the plan requires the
 * sketch to preserve visible signal width. */
class Accumulator {
   public:
    void reset() {
        matrix_.fill(0);
        write_row_ = 0;
        filled_ = 0;
        span_hz_ = 0;
        sample_rate_ = 0;
    }

    /* db: 256-entry spectrum from ChannelSpectrum. */
    void push(const uint8_t* db, uint32_t sample_rate, uint32_t span_hz) {
        sample_rate_ = sample_rate;
        span_hz_ = span_hz;

        uint8_t* row = &matrix_[write_row_ * bins];
        constexpr size_t group = 256 / bins;  // 4
        for (size_t b = 0; b < bins; b++) {
            uint8_t peak = 0;
            for (size_t k = 0; k < group; k++) {
                const uint8_t v = db[b * group + k];
                if (v > peak) peak = v;
            }
            row[b] = peak;
        }

        write_row_ = (write_row_ + 1) % frames;
        if (filled_ < frames) filled_++;
    }

    bool has_data() const { return filled_ > 0; }
    uint32_t sample_rate() const { return sample_rate_; }
    uint32_t span_hz() const { return span_hz_; }

    /* Mean of each bin across the frames captured so far. */
    void average(std::array<uint8_t, bins>& out) const {
        for (size_t b = 0; b < bins; b++) {
            uint32_t sum = 0;
            for (size_t f = 0; f < filled_; f++)
                sum += matrix_[f * bins + b];
            out[b] = filled_ ? static_cast<uint8_t>(sum / filled_) : 0;
        }
    }

    /* Rolling noise floor as the 25th percentile of the averaged spectrum.
     * A percentile is robust against a few strong bins, where a plain mean
     * would be dragged upward by the very signal we are trying to measure. */
    static uint8_t noise_floor(const std::array<uint8_t, bins>& avg) {
        std::array<uint8_t, bins> sorted = avg;
        std::sort(sorted.begin(), sorted.end());
        return sorted[bins / 4];
    }

    static uint16_t peak_bin(const std::array<uint8_t, bins>& avg) {
        uint16_t idx = 0;
        for (uint16_t b = 1; b < bins; b++)
            if (avg[b] > avg[idx]) idx = b;
        return idx;
    }

    /* Count of contiguous-ish bins more than `margin` above the noise floor,
     * converted to Hz. Deliberately crude: it is a bandwidth ESTIMATE and the
     * plan forbids presenting heuristics as fact. */
    static uint32_t occupied_bandwidth_hz(const std::array<uint8_t, bins>& avg,
                                          uint8_t nf, uint8_t margin, uint32_t span_hz) {
        size_t n = 0;
        for (size_t b = 0; b < bins; b++)
            if (avg[b] > nf + margin) n++;
        return static_cast<uint32_t>((static_cast<uint64_t>(span_hz) * n) / bins);
    }

    /* Serialises header + avg[64] + matrix[16][64] + CRC32 into `out`.
     * Returns the byte count written. The matrix is emitted oldest-frame-first
     * so the time axis reads left-to-right in the importer. */
    size_t serialize(const Header& hdr_in,
                     const std::array<uint8_t, bins>& avg,
                     uint8_t* out, size_t out_size) const {
        const size_t need = sizeof(Header) + bins + (frames * bins) + 4;
        if (out_size < need) return 0;

        Header hdr = hdr_in;
        std::memcpy(hdr.magic, "RFSK", 4);
        hdr.version = format_version;
        hdr.n_bins = bins;
        hdr.n_frames = frames;
        hdr.sample_rate_sps = sample_rate_;
        hdr.span_hz = span_hz_;

        size_t off = 0;
        std::memcpy(out + off, &hdr, sizeof(Header));
        off += sizeof(Header);
        std::memcpy(out + off, avg.data(), bins);
        off += bins;

        /* oldest first: write_row_ is where the NEXT frame would land, so it is
         * also the oldest entry once the ring has wrapped. */
        for (size_t f = 0; f < frames; f++) {
            const size_t src = (write_row_ + f) % frames;
            std::memcpy(out + off, &matrix_[src * bins], bins);
            off += bins;
        }

        CRC<32> crc{0x04c11db7, 0xffffffff, 0xffffffff};
        crc.process_bytes(out, off);
        const uint32_t sum = crc.checksum();
        std::memcpy(out + off, &sum, 4);
        off += 4;

        return off;
    }

    static constexpr size_t serialized_size =
        sizeof(Header) + bins + (frames * bins) + 4;

   private:
    std::array<uint8_t, frames * bins> matrix_{};
    size_t write_row_{0};
    size_t filled_{0};
    uint32_t span_hz_{0};
    uint32_t sample_rate_{0};
};

}  // namespace rfsk

#endif /*_RFSK_H*/
