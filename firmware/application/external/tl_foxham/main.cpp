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

/* Include order is load-bearing: external_app.hpp and ui_navigation.hpp include
 * each other, and app_location_t comes from standalone_app.hpp which
 * external_app.hpp pulls in only AFTER ui_navigation.hpp. ui_navigation.hpp must
 * be seen first or it parses before app_location_t exists. */
#include "ui.hpp"
#include "ui_tl_foxham.hpp"
#include "ui_navigation.hpp"
#include "external_app.hpp"

namespace ui::external_app::tl_foxham {
void initialize_app(ui::NavigationView& nav) {
    nav.push<FoxHamView>();
}
}  // namespace ui::external_app::tl_foxham

extern "C" {

__attribute__((section(".external_app.app_tl_foxham.application_information"), used)) application_information_t _application_information_tl_foxham = {
    /*.memory_location = */ (uint8_t*)0x00000000,
    /*.externalAppEntry = */ ui::external_app::tl_foxham::initialize_app,
    /*.header_version = */ CURRENT_HEADER_VERSION,
    /*.app_version = */ VERSION_MD5,

    /*.app_name = */ "Ham Fox Hunt",
    /*.bitmap_data = */
    {
        0x00, 0x03,
        0x00, 0x03,
        0xC0, 0x0F,
        0xF0, 0x3F,
        0xFC, 0xFF,
        0x00, 0x03,
        0x00, 0x03,
        0x00, 0x03,
        0xC0, 0x0F,
        0xF0, 0x3F,
        0x00, 0x03,
        0x00, 0x03,
        0xC0, 0x0F,
        0x00, 0x03,
        0x00, 0x03,
        0x00, 0x00},
    /*.icon_color = */ ui::Color::red().v,
    /*.menu_location = */ app_location_t::RX,
    /*.desired_menu_position = */ -1,

    /* WFM audio baseband. Receive only -- this is a hunt receiver, and the
     * project is receive-only by policy. */
    /*.m4_app_tag = */ {'P', 'W', 'F', 'M'},
    /*.m4_app_offset = */ 0x00000000,  // will be filled at compile time
};
}
