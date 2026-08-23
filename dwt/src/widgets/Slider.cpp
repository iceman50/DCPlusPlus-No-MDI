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

#include <dwt/widgets/Slider.h>

#include <dwt/Appearance.h>
#include <dwt/CanvasClasses.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Pen.h>

#include <uxtheme.h>

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

long valuePosition(int value, int minimum, int maximum, long start, long end, bool reverse)
{
	if(maximum <= minimum || end <= start) {
		return start;
	}
	const auto bounded = std::max(minimum, std::min(maximum, value));
	long position = start + static_cast<long>(
		static_cast<long long>(bounded - minimum) * (end - start) /
		(maximum - minimum));
	if(reverse) {
		position = end - (position - start);
	}
	return position;
}

}

const TCHAR Slider::windowClass[] = TRACKBAR_CLASS;

Slider::Seed::Seed() :
BaseType::Seed(WS_CHILD | WS_TABSTOP | TBS_NOTICKS | TBS_TOOLTIPS)
{
}

bool Slider::handleMessage(const MSG& msg, LRESULT& retVal) {
	auto handled = BaseType::handleMessage(msg, retVal);
	if(!isManualAppearance() || msg.message != WM_NOTIFY ||
		!msg.lParam || (retVal && retVal != CDRF_DODEFAULT)) {
		return handled;
	}

	auto data = reinterpret_cast<NMCUSTOMDRAW*>(msg.lParam);
	if(data->hdr.code == NM_CUSTOMDRAW) {
		if(data->dwDrawStage == CDDS_PREPAINT) {
			/* Trackbars only support CDRF_SKIPDEFAULT for individual parts.
			 * Suppress the native channel, tick and thumb bitmaps below, then
			 * render the complete control after their paint cycle has finished. */
			retVal = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
			return true;
		}
		if(data->dwDrawStage == CDDS_ITEMPREPAINT &&
			(data->dwItemSpec == TBCD_CHANNEL || data->dwItemSpec == TBCD_TICS ||
				data->dwItemSpec == TBCD_THUMB)) {
			retVal = CDRF_SKIPDEFAULT;
			return true;
		}
		if(data->dwDrawStage == CDDS_POSTPAINT) {
			handlePainting(*data);
			retVal = CDRF_DODEFAULT;
			return true;
		}
	}
	return handled;
}

void Slider::appearanceChanged() {
	const auto& appearance = getAppearance();
	if(HWND tip = getToolTip()) {
		if(isManualAppearance()) {
			::SetWindowTheme(tip, L"", L"");
			::SendMessage(tip, TTM_SETTIPBKCOLOR,
				appearance.getPalette().surface, 0);
			::SendMessage(tip, TTM_SETTIPTEXTCOLOR,
				appearance.getPalette().text, 0);
		} else {
			::SetWindowTheme(tip, nullptr, nullptr);
			::SendMessage(tip, TTM_SETTIPBKCOLOR,
				::GetSysColor(COLOR_INFOBK), 0);
			::SendMessage(tip, TTM_SETTIPTEXTCOLOR,
				::GetSysColor(COLOR_INFOTEXT), 0);
		}
	}
	BaseType::appearanceChanged();
}

void Slider::handlePainting(NMCUSTOMDRAW& data) {
	RECT nativeBounds = { 0 };
	::GetClientRect(handle(), &nativeBounds);
	Rectangle bounds(nativeBounds);
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return;
	}

	const auto& appearance = getAppearance();
	const auto& palette = appearance.getPalette();
	const auto background = hasExplicitColors() ?
		getExplicitBackgroundColor() : palette.background;
	const auto style = static_cast<DWORD>(
		::GetWindowLongPtr(handle(), GWL_STYLE));
	const bool vertical = (style & TBS_VERT) != 0;
	const bool reverse = (style & TBS_REVERSED) != 0;
	const bool showTicks = (style & TBS_NOTICKS) == 0;
	const bool ticksBoth = (style & TBS_BOTH) != 0;
	const bool ticksTopOrLeft = (style & TBS_TOP) != 0;

	BufferedCanvas<FreeCanvas> canvas(data.hdc, bounds);
	canvas.fill(bounds, Brush(background));

	auto channel = getChannelRect();
	if(channel.width() > 0 && channel.height() > 0) {
		canvas.fill(channel, Brush(palette.surface));
		drawBorder(canvas, channel, palette.border);

		if(style & TBS_ENABLESELRANGE) {
			const int minimum = getMinValue();
			const int maximum = getMaxValue();
			Rectangle selection = channel;
			if(vertical) {
				selection.pos.x += 1;
				selection.size.x = std::max(0L, selection.size.x - 2);
				const long start = valuePosition(getSelectionStart(), minimum,
					maximum, channel.top(), channel.bottom(), !reverse);
				const long end = valuePosition(getSelectionEnd(), minimum,
					maximum, channel.top(), channel.bottom(), !reverse);
				selection.pos.y = std::min(start, end);
				selection.size.y = std::max(1L, std::max(start, end) - selection.top());
			} else {
				selection.pos.y += 1;
				selection.size.y = std::max(0L, selection.size.y - 2);
				const long start = valuePosition(getSelectionStart(), minimum,
					maximum, channel.left(), channel.right(), reverse);
				const long end = valuePosition(getSelectionEnd(), minimum,
					maximum, channel.left(), channel.right(), reverse);
				selection.pos.x = std::min(start, end);
				selection.size.x = std::max(1L, std::max(start, end) - selection.left());
			}
			if(selection.width() > 0 && selection.height() > 0) {
				canvas.fill(selection, Brush(palette.accent));
			}
		}
	}

	if(showTicks) {
		Pen tickPen(getEnabled() ? palette.text : palette.disabledText);
		auto select(canvas.select(tickPen));
		auto positions = getTickPositions();
		if(positions.empty()) {
			positions.push_back(vertical ? channel.top() : channel.left());
			positions.push_back(vertical ? channel.bottom() - 1 : channel.right() - 1);
		}
		const auto thumb = getThumbRect();
		const long tickLength = std::max(2, scale(3));
		for(auto position: positions) {
			if(vertical) {
				if(ticksTopOrLeft || ticksBoth) {
					canvas.line(thumb.left() - tickLength, position,
						thumb.left() - 1, position);
				}
				if(!ticksTopOrLeft || ticksBoth) {
					canvas.line(thumb.right() + 1, position,
						thumb.right() + tickLength, position);
				}
			} else {
				if(ticksTopOrLeft || ticksBoth) {
					canvas.line(position, thumb.top() - tickLength,
						position, thumb.top() - 1);
				}
				if(!ticksTopOrLeft || ticksBoth) {
					canvas.line(position, thumb.bottom() + 1,
						position, thumb.bottom() + tickLength);
				}
			}
		}
	}

	auto thumb = getThumbRect();
	POINT cursor = { 0 };
	::GetCursorPos(&cursor);
	::ScreenToClient(handle(), &cursor);
	const bool hot = thumb.contains(Point(cursor));
	const bool pressed = hot && ::GetCapture() == handle();
	auto thumbColor = palette.surface;
	if(pressed) {
		thumbColor = Appearance::blend(palette.surface, palette.accent, 176);
	} else if(hot) {
		thumbColor = Appearance::blend(palette.surface, palette.accent, 80);
	}
	if(!getEnabled()) {
		thumbColor = Appearance::blend(background, palette.disabledText, 80);
	}

	Pen thumbPen(hot ? palette.accent : palette.border);
	Brush thumbBrush(thumbColor);
	auto selectPen(canvas.select(thumbPen));
	auto selectBrush(canvas.select(thumbBrush));
	if(showTicks && vertical && !ticksBoth) {
		const long point = std::max(2, scale(3));
		const long middle = thumb.top() + thumb.height() / 2;
		if(ticksTopOrLeft) {
			Point points[] = {
				Point(thumb.left(), middle), Point(thumb.left() + point, thumb.top()),
				Point(thumb.right(), thumb.top()), Point(thumb.right(), thumb.bottom()),
				Point(thumb.left() + point, thumb.bottom())
			};
			canvas.polygon(points, sizeof(points) / sizeof(points[0]));
		} else {
			Point points[] = {
				Point(thumb.left(), thumb.top()), Point(thumb.right() - point, thumb.top()),
				Point(thumb.right(), middle), Point(thumb.right() - point, thumb.bottom()),
				Point(thumb.left(), thumb.bottom())
			};
			canvas.polygon(points, sizeof(points) / sizeof(points[0]));
		}
	} else if(showTicks && !vertical && !ticksBoth) {
		const long point = std::max(2, scale(3));
		const long middle = thumb.left() + thumb.width() / 2;
		if(ticksTopOrLeft) {
			Point points[] = {
				Point(middle, thumb.top()), Point(thumb.right(), thumb.top() + point),
				Point(thumb.right(), thumb.bottom()), Point(thumb.left(), thumb.bottom()),
				Point(thumb.left(), thumb.top() + point)
			};
			canvas.polygon(points, sizeof(points) / sizeof(points[0]));
		} else {
			Point points[] = {
				Point(thumb.left(), thumb.top()), Point(thumb.right(), thumb.top()),
				Point(thumb.right(), thumb.bottom() - point),
				Point(middle, thumb.bottom()),
				Point(thumb.left(), thumb.bottom() - point)
			};
			canvas.polygon(points, sizeof(points) / sizeof(points[0]));
		}
	} else {
		canvas.rectangle(thumb);
	}

	if(::GetFocus() == handle()) {
		auto focus = bounds.toRECT();
		::InflateRect(&focus, -1, -1);
		::DrawFocusRect(canvas.handle(), &focus);
	}
	canvas.blast(bounds);
}

}
