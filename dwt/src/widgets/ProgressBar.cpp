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

#include <dwt/widgets/ProgressBar.h>

#include <dwt/Appearance.h>
#include <dwt/CanvasClasses.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Pen.h>

#include <algorithm>

namespace dwt {

namespace {

void drawBorder(Canvas& canvas, const Rectangle& rect, COLORREF color) {
	if(rect.width() <= 0 || rect.height() <= 0) {
		return;
	}
	Pen pen(color);
	auto select(canvas.select(pen));
	canvas.line(rect.left(), rect.top(), rect.right() - 1, rect.top());
	canvas.line(rect.right() - 1, rect.top(), rect.right() - 1, rect.bottom() - 1);
	canvas.line(rect.right() - 1, rect.bottom() - 1, rect.left(), rect.bottom() - 1);
	canvas.line(rect.left(), rect.bottom() - 1, rect.left(), rect.top());
}

Rectangle inset(Rectangle rect, long amount) {
	rect.pos.x += amount;
	rect.pos.y += amount;
	rect.size.x = std::max(0L, rect.size.x - amount * 2);
	rect.size.y = std::max(0L, rect.size.y - amount * 2);
	return rect;
}

}

const TCHAR ProgressBar::windowClass[] = PROGRESS_CLASS;

ProgressBar::Seed::Seed() :
BaseType::Seed(WS_CHILD | PBS_SMOOTH)
{
}

COLORREF ProgressBar::setBarColor(COLORREF color) {
	barColor = color == CLR_DEFAULT ? NaC : color;
	barColorExplicit = color != CLR_DEFAULT;
	return static_cast<COLORREF>(sendMessage(PBM_SETBARCOLOR, 0, color));
}

COLORREF ProgressBar::setBackgroundColor(COLORREF color) {
	backgroundColor = color == CLR_DEFAULT ? NaC : color;
	backgroundColorExplicit = color != CLR_DEFAULT;
	return static_cast<COLORREF>(sendMessage(PBM_SETBKCOLOR, 0, color));
}

bool ProgressBar::handleMessage(const MSG& msg, LRESULT& retVal) {
	auto handled = BaseType::handleMessage(msg, retVal);
	if(!isManualAppearance() || msg.message != WM_NOTIFY ||
		!msg.lParam || (retVal && retVal != CDRF_DODEFAULT)) {
		return handled;
	}

	auto data = reinterpret_cast<NMCUSTOMDRAW*>(msg.lParam);
	if(data->hdr.code == NM_CUSTOMDRAW && data->dwDrawStage == CDDS_PREPAINT) {
		handlePainting(*data);
		retVal = CDRF_SKIPDEFAULT;
		return true;
	}
	return handled;
}

void ProgressBar::handlePainting(NMCUSTOMDRAW& data) {
	Rectangle bounds(data.rc);
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return;
	}

	const auto& appearance = getAppearance();
	const auto& palette = appearance.getPalette();
	const auto background = backgroundColorExplicit ? backgroundColor : palette.surface;
	auto fillColor = barColorExplicit ? barColor : palette.accent;
	switch(getState()) {
	case Error:
		if(!barColorExplicit) {
			fillColor = Appearance::blend(palette.accent, RGB(232, 17, 35), 208);
		}
		break;
	case Paused:
		if(!barColorExplicit) {
			fillColor = Appearance::blend(palette.accent, RGB(255, 185, 0), 208);
		}
		break;
	default:
		break;
	}
	if(!getEnabled()) {
		fillColor = Appearance::blend(background, palette.disabledText, 128);
	}

	BufferedCanvas<FreeCanvas> canvas(data.hdc, bounds);
	canvas.fill(bounds, Brush(background));
	drawBorder(canvas, bounds, palette.border);
	auto inner = inset(bounds, std::max(1, scale(2)));
	if(inner.width() <= 0 || inner.height() <= 0) {
		canvas.blast(bounds);
		return;
	}

	const bool vertical = hasStyle(PBS_VERTICAL);
	const long extent = vertical ? inner.height() : inner.width();
	long filled = 0;
	if(hasStyle(PBS_MARQUEE)) {
		const long block = std::max(static_cast<long>(scale(12)), extent / 3);
		const long period = extent + block;
		const long start = period > 0 ?
			static_cast<long>((::GetTickCount() / 24) % period) - block : 0;
		Rectangle chunk = inner;
		if(vertical) {
			chunk.pos.y = inner.bottom() - start - block;
			chunk.size.y = block;
			const long top = std::max(chunk.top(), inner.top());
			const long bottom = std::min(chunk.bottom(), inner.bottom());
			chunk.pos.y = top;
			chunk.size.y = std::max(0L, bottom - top);
		} else {
			chunk.pos.x = inner.left() + start;
			chunk.size.x = block;
			const long left = std::max(chunk.left(), inner.left());
			const long right = std::min(chunk.right(), inner.right());
			chunk.pos.x = left;
			chunk.size.x = std::max(0L, right - left);
		}
		if(chunk.width() > 0 && chunk.height() > 0) {
			canvas.fill(chunk, Brush(fillColor));
		}
		canvas.blast(bounds);
		return;
	}

	PBRANGE range = { 0 };
	sendMessage(PBM_GETRANGE, TRUE, reinterpret_cast<LPARAM>(&range));
	const long long span = static_cast<long long>(range.iHigh) - range.iLow;
	if(span > 0) {
		const long long position = std::max<long long>(range.iLow,
			std::min<long long>(range.iHigh, getPosition()));
		filled = static_cast<long>((position - range.iLow) * extent / span);
	}
	if(filled <= 0) {
		canvas.blast(bounds);
		return;
	}

	if(hasStyle(PBS_SMOOTH)) {
		Rectangle progress = inner;
		if(vertical) {
			progress.pos.y = inner.bottom() - filled;
			progress.size.y = filled;
		} else {
			progress.size.x = filled;
		}
		canvas.fill(progress, Brush(fillColor));
	} else {
		const long chunkLength = std::max(static_cast<long>(scale(4)),
			(vertical ? inner.width() : inner.height()) * 2 / 3);
		const long gap = std::max(1, scale(2));
		for(long offset = 0; offset < filled; offset += chunkLength + gap) {
			const long length = std::min(chunkLength, filled - offset);
			Rectangle chunk = inner;
			if(vertical) {
				chunk.pos.y = inner.bottom() - offset - length;
				chunk.size.y = length;
			} else {
				chunk.pos.x += offset;
				chunk.size.x = length;
			}
			canvas.fill(chunk, Brush(fillColor));
		}
	}
	canvas.blast(bounds);
}

}
