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

#ifndef _UI_TL_BACKDROP
#define _UI_TL_BACKDROP

#include <array>

#include "bmpfile.hpp"
#include "portapack.hpp"
#include "theme.hpp"
#include "ui.hpp"
#include "ui_widget.hpp"

/* Header-only on purpose. Each external app is linked as its own standalone
 * binary and collected by an object-name pattern in external.ld, so a shared
 * .cpp could not be pulled into two different app sections. Inlining lets each
 * app compile its own copy into its own object. */
namespace tl_ui {

/* Paints a BMP centred in its own rect: crops whatever overflows, letterboxes
 * whatever falls short. Deliberately NOT focusable and handles no input, so it
 * can sit underneath real widgets as a backdrop. Contrast with BMPViewer, which
 * is an interactive pan/zoom viewer and would swallow key events. */
class TLBackdrop : public ui::Widget {
   public:
    explicit TLBackdrop(ui::Rect parent_rect)
        : ui::Widget{parent_rect} {
        set_focusable(false);
    }

    TLBackdrop(const TLBackdrop&) = delete;
    TLBackdrop& operator=(const TLBackdrop&) = delete;

    /* Returns false if the file is missing or is not a BMP variant bmpfile.cpp
     * accepts (8/16/24/32 bpp, compression 0). */
    bool load(const std::filesystem::path& file) {
        loaded_ = bmp_.open(file, true);
        return loaded_;
    }

    bool is_loaded() const { return loaded_; }

    /* Tries each path in order, keeping the first that opens. */
    bool load_first_of(std::initializer_list<std::u16string_view> candidates) {
        for (auto c : candidates) {
            if (load(std::filesystem::path{c}))
                return true;
        }
        return false;
    }

    void paint(ui::Painter& painter) override {
        const auto rect = screen_rect();
        const ui::Dim vh = rect.height();
        auto* theme = ui::Theme::getInstance();
        const ui::Color bg = theme->bg_darkest->background;

        if (!loaded_) {
            painter.fill_rectangle(rect, bg);
            painter.draw_string({rect.left() + UI_POS_X(1), rect.top() + UI_POS_Y(1)},
                                *theme->fg_red, "logo asset missing:");
            painter.draw_string({rect.left() + UI_POS_X(1), rect.top() + UI_POS_Y(2)},
                                *theme->fg_medium, "/BMP/TLJELLYM.BMP");
            return;
        }

        const int32_t iw = static_cast<int32_t>(bmp_.get_width());
        const int32_t ih = static_cast<int32_t>(bmp_.get_real_height());

        /* screen_width is a runtime value (multi-resolution support) so it
         * cannot size a std::array. Use the largest panel dimension the
         * firmware supports and clamp, which avoids a heap allocation on
         * every repaint. */
        static constexpr size_t max_line_px = 320;
        std::array<ui::Color, max_line_px> line{};
        const ui::Dim span_w =
            (rect.width() > static_cast<ui::Dim>(max_line_px)) ? static_cast<ui::Dim>(max_line_px) : rect.width();

        /* Centre the image. A negative offset means that axis gets cropped. */
        const int32_t off_x = (static_cast<int32_t>(span_w) - iw) / 2;
        const int32_t off_y = (static_cast<int32_t>(vh) - ih) / 2;

        for (int32_t y = 0; y < static_cast<int32_t>(vh); y++) {
            line.fill(bg);

            const int32_t src_y = y - off_y;
            if (src_y >= 0 && src_y < ih) {
                /* Clip the horizontal span into the widget, then read that run
                 * of pixels in one go rather than seeking per pixel. */
                int32_t src_x = 0;
                int32_t dst_x = off_x;
                int32_t span = iw;

                if (dst_x < 0) {
                    src_x = -dst_x;
                    span = iw - src_x;
                    dst_x = 0;
                }
                if (dst_x + span > static_cast<int32_t>(span_w))
                    span = static_cast<int32_t>(span_w) - dst_x;

                if (span > 0 && bmp_.seek(static_cast<uint32_t>(src_x), static_cast<uint32_t>(src_y)))
                    bmp_.read_next_px_cnt(&line[dst_x], static_cast<uint32_t>(span), false);
            }

            portapack::display.draw_pixels({rect.left(), rect.top() + y, span_w, 1}, line.data(), span_w);
        }
    }

   private:
    BMPFile bmp_{};
    bool loaded_{false};
};

/* Watermark assets shipped in the SD /BMP folder. TLJELLYM is the
 * mid-brightness (62%) variant: the 38% one loses its 1px wireframe lines on a
 * transflective LCD in daylight, and the 100% one fights foreground widgets. */
static constexpr std::u16string_view logo_mid{u"/BMP/TLJELLYM.BMP"};
static constexpr std::u16string_view logo_dim{u"/BMP/TLJELLY.BMP"};

}  // namespace tl_ui

#endif /*_UI_TL_BACKDROP*/
