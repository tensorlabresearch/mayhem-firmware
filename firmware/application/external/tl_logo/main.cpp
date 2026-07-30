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

/* Include order matters: external_app.hpp and ui_navigation.hpp include each
 * other, and app_location_t comes from standalone_app.hpp which external_app.hpp
 * pulls in only AFTER ui_navigation.hpp. So ui_navigation.hpp has to be seen
 * first, or it parses before app_location_t exists. */
#include "ui.hpp"
#include "ui_tl_logo.hpp"
#include "ui_navigation.hpp"
#include "external_app.hpp"

namespace ui::external_app::tl_logo {
void initialize_app(ui::NavigationView& nav) {
    nav.push<TLLogoView>();
}
}  // namespace ui::external_app::tl_logo

extern "C" {

__attribute__((section(".external_app.app_tl_logo.application_information"), used)) application_information_t _application_information_tl_logo = {
    /*.memory_location = */ (uint8_t*)0x00000000,
    /*.externalAppEntry = */ ui::external_app::tl_logo::initialize_app,
    /*.header_version = */ CURRENT_HEADER_VERSION,
    /*.app_version = */ VERSION_MD5,

    /*.app_name = */ "TL Logo",
    /*.bitmap_data = */
    {
        /* 16x16, 1bpp, 2 bytes per row, LSB = leftmost pixel. Jellyfish. */
        0xF0, 0x0F,
        0xFC, 0x3F,
        0xFE, 0x7F,
        0xFF, 0xFF,
        0xFF, 0xFF,
        0xFE, 0x7F,
        0x6C, 0x1B,
        0xA4, 0x25,
        0xA8, 0x15,
        0xA8, 0x15,
        0x50, 0x0A,
        0x50, 0x0A,
        0xA0, 0x05,
        0xA0, 0x05,
        0x40, 0x02,
        0x40, 0x02},
    /*.icon_color = */ ui::Color::red().v,
    /*.menu_location = */ app_location_t::UTILITIES,
    /*.desired_menu_position = */ -1,

    /* No baseband/RF use at all, so no M4 image is needed. */
    /*.m4_app_tag = portapack::spi_flash::image_tag_none */ {0, 0, 0, 0},
    /*.m4_app_offset = */ 0x00000000,  // will be filled at compile time
};
}
