/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DWT_WIDGETS_APPEARANCEDRAW_H
#define DWT_WIDGETS_APPEARANCEDRAW_H

#include <dwt/Appearance.h>
#include <dwt/CanvasClasses.h>

namespace dwt {

class Button;
class ComboBox;
class GroupBox;
class Header;
class Spinner;
class StatusBar;
class ToolBar;

/** Private renderers for native controls whose visual styles cannot consume a
 * caller-supplied dark palette. Input, metrics and accessibility remain owned by
 * the underlying Windows control; these functions replace painting only. */
namespace appearance_detail {

LRESULT drawButton(Button& button, NMCUSTOMDRAW& data, const Appearance::Palette& palette);
LRESULT drawHeader(Header& header, NMCUSTOMDRAW& data, const Appearance::Palette& palette);
LRESULT drawToolBar(ToolBar& toolbar, NMTBCUSTOMDRAW& data, const Appearance::Palette& palette);

void drawGroupBox(GroupBox& group, Canvas& canvas, const Appearance::Palette& palette);
void drawComboBox(ComboBox& combo, Canvas& canvas, const Appearance::Palette& palette);
void drawSpinner(Spinner& spinner, Canvas& canvas, const Appearance::Palette& palette);
void drawStatusBar(StatusBar& status, Canvas& canvas, const Appearance::Palette& palette);

bool canDrawComboBox(const ComboBox& combo);
bool canDrawStatusBar(const StatusBar& status);

}

}

#endif
