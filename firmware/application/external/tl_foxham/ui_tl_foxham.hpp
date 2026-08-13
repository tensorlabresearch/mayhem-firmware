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

/* 2m / 70cm fox hunt.
 *
 * Mayhem already ships foxhunt_rx, with a GeoMap and marker logging. This module
 * exists for band-plan sweeping and a consistent gain workflow across the Tensor
 * Lab hunt apps; for a classic ARDF hunt with mapping, foxhunt_rx remains the
 * better tool. Contest fox frequencies should be entered from the organiser's
 * table -- the built-in plan is only a starting sweep.
 */

#ifndef _UI_TL_FOXHAM
#define _UI_TL_FOXHAM

#include "../tl_foxcore/foxview.hpp"

namespace ui::external_app::tl_foxham {

class FoxHamView : public tl_fox::FoxHuntView {
   public:
    explicit FoxHamView(NavigationView& nav)
        : tl_fox::FoxHuntView{nav,
                              tl_fox::plan_ham.data(),
                              tl_fox::plan_ham.size(),
                              "Ham Fox Hunt",
                              3'072'000} {}
};

}  // namespace ui::external_app::tl_foxham

#endif /*_UI_TL_FOXHAM*/
