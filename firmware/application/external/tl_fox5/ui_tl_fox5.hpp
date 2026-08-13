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

/* 5 GHz WiFi fox hunt. Same limits as the 2.4 GHz module, plus two of its own.
 *
 * A 25-channel sweep across 725 MHz is slow. And sensitivity depends entirely on
 * the antenna: a 2.4 GHz whip will read this band as quiet even when it is busy,
 * so a weak result here means "check the antenna" before it means "no signal".
 */

#ifndef _UI_TL_FOX5
#define _UI_TL_FOX5

#include "../tl_foxcore/foxview.hpp"

namespace ui::external_app::tl_fox5 {

class Fox5View : public tl_fox::FoxHuntView {
   public:
    explicit Fox5View(NavigationView& nav)
        : tl_fox::FoxHuntView{nav,
                              tl_fox::plan_wifi5.data(),
                              tl_fox::plan_wifi5.size(),
                              "WiFi 5G Fox",
                              3'072'000} {}
};

}  // namespace ui::external_app::tl_fox5

#endif /*_UI_TL_FOX5*/
