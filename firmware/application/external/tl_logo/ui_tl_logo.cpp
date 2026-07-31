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

namespace ui::external_app::tl_logo {

TLLogoView::TLLogoView(NavigationView& nav) {
    /* Backdrop first so it paints beneath the button. */
    add_children({&backdrop, &button_close});

    backdrop.load_first_of({tl_ui::logo_mid, tl_ui::logo_dim});

    button_close.on_select = [&nav](Button&) { nav.pop(); };
}

void TLLogoView::focus() {
    button_close.focus();
}

}  // namespace ui::external_app::tl_logo
