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

#include "ui_tl_logo.hpp"

#include <array>

#include "portapack.hpp"

namespace ui::external_app::tl_logo {

/* Watermark asset, shipped in the SD /BMP folder. TLJELLYM is the mid-brightness
 * (62%) variant: the 38% one loses its 1px wireframe lines on a transflective
 * LCD in daylight, and the 100% one fights foreground widgets. */
static constexpr std::u16string_view logo_primary{u"/BMP/TLJELLYM.BMP"};
static constexpr std::u16string_view logo_fallback{u"/BMP/TLJELLY.BMP"};

TLBackdrop::TLBackdrop(Rect parent_rect)
    : Widget{parent_rect} {
    set_focusable(false);
}

bool TLBackdrop::load(const std::filesystem::path& file) {
    loaded_ = bmp_.open(file, true);
    return loaded_;
}

void TLBackdrop::paint(Painter& painter) {
    const auto rect = screen_rect();
    const Dim vw = rect.width();
    const Dim vh = rect.height();
    auto* theme = Theme::getInstance();
    const Color bg = theme->bg_darkest->background;

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

    /* Centre the image. A negative offset means that axis gets cropped. */
    const int32_t off_x = (static_cast<int32_t>(vw) - iw) / 2;
    const int32_t off_y = (static_cast<int32_t>(vh) - ih) / 2;

    /* screen_width is a runtime value (multi-resolution support), so it can't
     * size a std::array. Use the largest panel dimension the firmware supports
     * and clamp, which avoids a heap allocation on every repaint. */
    static constexpr size_t max_line_px = 320;
    std::array<Color, max_line_px> line{};
    const Dim span_w = (vw > static_cast<Dim>(max_line_px)) ? static_cast<Dim>(max_line_px) : vw;

    for (int32_t y = 0; y < static_cast<int32_t>(vh); y++) {
        line.fill(bg);

        const int32_t src_y = y - off_y;
        if (src_y >= 0 && src_y < ih) {
            /* Clip the horizontal span into the widget, then read that run of
             * pixels in one go rather than seeking per pixel. */
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

TLLogoView::TLLogoView(NavigationView& nav) {
    /* Backdrop first so it paints beneath the button. */
    add_children({&backdrop, &button_close});

    if (!backdrop.load(std::filesystem::path{logo_primary}))
        backdrop.load(std::filesystem::path{logo_fallback});

    button_close.on_select = [&nav](Button&) { nav.pop(); };
}

void TLLogoView::focus() {
    button_close.focus();
}

}  // namespace ui::external_app::tl_logo
