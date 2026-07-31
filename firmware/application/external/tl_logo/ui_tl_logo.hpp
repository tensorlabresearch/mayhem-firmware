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

#ifndef _UI_TL_LOGO
#define _UI_TL_LOGO

#include "../tl_common/ui_tl_backdrop.hpp"
#include "ui.hpp"
#include "ui_navigation.hpp"
#include "ui_widget.hpp"

namespace ui::external_app::tl_logo {

class TLLogoView : public View {
   public:
    explicit TLLogoView(NavigationView& nav);

    void focus() override;

    std::string title() const override { return "TL Logo"; };

   private:
    /* Full-bleed backdrop. The asset is 200x320 and this rect is 16px shorter
     * than the screen, so it centre-crops 8 rows top and bottom -- preferable
     * to nearest-neighbour downscaling, which erases the 1px wireframe lines. */
    tl_ui::TLBackdrop backdrop{{0, 0, UI_POS_MAXWIDTH, UI_POS_HEIGHT_REMAINING(1)}};

    /* Added after the backdrop, so it paints on top of it. */
    Button button_close{
        {UI_POS_X_CENTER(9), UI_POS_Y_BOTTOM(3), UI_POS_WIDTH(9), UI_POS_HEIGHT(2)},
        "Close"};
};

}  // namespace ui::external_app::tl_logo

#endif /*_UI_TL_LOGO*/
