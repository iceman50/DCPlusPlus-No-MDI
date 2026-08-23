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

#include <dwt/Appearance.h>

#include <dwt/Application.h>
#include <dwt/CanvasClasses.h>
#include <dwt/Widget.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Pen.h>
#include <dwt/widgets/Composite.h>
#include <dwt/widgets/Control.h>

#include <algorithm>

namespace dwt {

namespace {

enum ScrollDecorationType {
	SCROLL_DECORATION_VERTICAL,
	SCROLL_DECORATION_HORIZONTAL,
	SCROLL_DECORATION_CORNER
};

enum ScrollPart {
	SCROLL_PART_NONE,
	SCROLL_PART_DECREMENT,
	SCROLL_PART_PAGE_DECREMENT,
	SCROLL_PART_THUMB,
	SCROLL_PART_PAGE_INCREMENT,
	SCROLL_PART_INCREMENT
};

struct ScrollDecorationData {
	HWND host;
	ScrollDecorationType type;
	Appearance* appearance;
};

LPCTSTR scrollDecorationClass() {
	return _T("dwt.Appearance.ScrollDecoration");
}

LPCTSTR scrollDecorationProperty(ScrollDecorationType type) {
	switch(type) {
	case SCROLL_DECORATION_VERTICAL:
		return _T("dwt.Appearance.VerticalScrollDecoration");
	case SCROLL_DECORATION_HORIZONTAL:
		return _T("dwt.Appearance.HorizontalScrollDecoration");
	default:
		return _T("dwt.Appearance.ScrollCornerDecoration");
	}
}

LPCTSTR scrollDecorationRefreshProperty() {
	return _T("dwt.Appearance.ScrollDecorationRefreshPending");
}

LPCTSTR scrollDecorationClipStyleProperty() {
	return _T("dwt.Appearance.ScrollDecorationAddedClipSiblings");
}

UINT scrollDecorationRefreshMessage() {
	static const UINT message = ::RegisterWindowMessage(
		_T("dwt.Appearance.RefreshScrollDecorations"));
	return message;
}

HWND getScrollDecoration(HWND host, ScrollDecorationType type) {
	const auto decoration = reinterpret_cast<HWND>(
		::GetProp(host, scrollDecorationProperty(type)));
	if(decoration && !::IsWindow(decoration)) {
		::RemoveProp(host, scrollDecorationProperty(type));
		return nullptr;
	}
	return decoration;
}

void redrawScrollDecoration(HWND host, ScrollDecorationType type) {
	const auto decoration = getScrollDecoration(host, type);
	if(decoration && ::IsWindowVisible(decoration)) {
		::InvalidateRect(decoration, nullptr, FALSE);
	}
}

void redrawScrollDecorations(HWND host) {
	redrawScrollDecoration(host, SCROLL_DECORATION_VERTICAL);
	redrawScrollDecoration(host, SCROLL_DECORATION_HORIZONTAL);
	redrawScrollDecoration(host, SCROLL_DECORATION_CORNER);
}

ScrollPart scrollPartAt(const SCROLLBARINFO& info, bool vertical, const POINT& point)
{
	if((info.rgstate[0] & (STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_INVISIBLE |
		STATE_SYSTEM_OFFSCREEN)) || !::PtInRect(&info.rcScrollBar, point)) {
		return SCROLL_PART_NONE;
	}

	const long start = vertical ? info.rcScrollBar.top : info.rcScrollBar.left;
	const long end = vertical ? info.rcScrollBar.bottom : info.rcScrollBar.right;
	const long position = vertical ? point.y : point.x;
	const long line = std::max(0L,
		std::min<long>(info.dxyLineButton, (end - start) / 2));
	if(position < start + line) {
		return (info.rgstate[1] & STATE_SYSTEM_UNAVAILABLE) ?
			SCROLL_PART_NONE : SCROLL_PART_DECREMENT;
	}
	if(position >= end - line) {
		return (info.rgstate[5] & STATE_SYSTEM_UNAVAILABLE) ?
			SCROLL_PART_NONE : SCROLL_PART_INCREMENT;
	}

	const long thumbStart = start + info.xyThumbTop;
	const long thumbEnd = start + info.xyThumbBottom;
	if(!(info.rgstate[3] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) &&
		position >= thumbStart && position < thumbEnd) {
		return SCROLL_PART_THUMB;
	}
	if(position < thumbStart) {
		return (info.rgstate[2] & STATE_SYSTEM_UNAVAILABLE) ?
			SCROLL_PART_NONE : SCROLL_PART_PAGE_DECREMENT;
	}
	return (info.rgstate[4] & STATE_SYSTEM_UNAVAILABLE) ?
		SCROLL_PART_NONE : SCROLL_PART_PAGE_INCREMENT;
}

void drawScrollSurface(Canvas& canvas, const Rectangle& bounds, COLORREF background, COLORREF border)
{
	Brush backgroundBrush(background);
	canvas.fill(bounds, backgroundBrush);
	if(bounds.width() > 1 && bounds.height() > 1) {
		Pen borderPen(border, Pen::Solid, 1);
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectPen = canvas.select(borderPen);
		auto selectBrush = canvas.select(hollow);
		canvas.rectangle(Rectangle(bounds.x(), bounds.y(),
			bounds.width() - 1, bounds.height() - 1));
	}
}

void drawScrollArrow(Canvas& canvas, const Rectangle& bounds, bool vertical, bool decrement, COLORREF color)
{
	const long x = bounds.left() + bounds.width() / 2;
	const long y = bounds.top() + bounds.height() / 2;
	const long radius = std::max(2L,
		std::min<long>(4, std::min(bounds.width(), bounds.height()) / 4));
	POINT points[3];
	if(vertical) {
		points[0] = { x - radius, y + (decrement ? 1 : -1) };
		points[1] = { x + radius, y + (decrement ? 1 : -1) };
		points[2] = { x, y + (decrement ? -radius : radius) };
	} else {
		points[0] = { x + (decrement ? 1 : -1), y - radius };
		points[1] = { x + (decrement ? 1 : -1), y + radius };
		points[2] = { x + (decrement ? -radius : radius), y };
	}
	Pen pen(color, Pen::Solid, 1);
	Brush brush(color);
	auto selectPen = canvas.select(pen);
	auto selectBrush = canvas.select(brush);
	canvas.polygon(points, 3);
}

bool drawScrollDecoration(HWND hwnd, Canvas& canvas) {
	const auto data = reinterpret_cast<ScrollDecorationData*>(
		::GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if(!data || !data->appearance || !data->appearance->isManual() ||
		!::IsWindow(data->host)) {
		return false;
	}

	RECT nativeBounds = { 0 };
	::GetClientRect(hwnd, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return true;
	}

	const auto& colors = data->appearance->getPalette();
	Brush surface(colors.surface);
	canvas.fill(bounds, surface);
	if(data->type == SCROLL_DECORATION_CORNER) {
		drawScrollSurface(canvas, bounds, colors.surface, colors.border);
		return true;
	}

	const bool vertical = data->type == SCROLL_DECORATION_VERTICAL;
	SCROLLBARINFO info = { sizeof(SCROLLBARINFO) };
	if(!::GetScrollBarInfo(data->host,
		vertical ? OBJID_VSCROLL : OBJID_HSCROLL, &info)) {
		return true;
	}

	POINT cursor = { 0 };
	const auto hovered = ::GetCursorPos(&cursor) ?
		scrollPartAt(info, vertical, cursor) : SCROLL_PART_NONE;
	const bool enabled = ::IsWindowEnabled(data->host) != FALSE;
	const bool captured = ::GetCapture() == data->host &&
		(::GetKeyState(VK_LBUTTON) & 0x8000) != 0;
	const long extent = vertical ? bounds.height() : bounds.width();
	const long line = std::max(0L,
		std::min<long>(info.dxyLineButton, extent / 2));
	Rectangle decrement = bounds;
	Rectangle increment = bounds;
	if(vertical) {
		decrement.size.y = line;
		increment.pos.y = bounds.bottom() - line;
		increment.size.y = line;
	} else {
		decrement.size.x = line;
		increment.pos.x = bounds.right() - line;
		increment.size.x = line;
	}

	auto drawButton = [&](const Rectangle& button, int stateIndex,
		ScrollPart part, bool decrementing) {
		const bool unavailable = !enabled ||
			(info.rgstate[stateIndex] & STATE_SYSTEM_UNAVAILABLE) != 0;
		const bool hot = hovered == part;
		const COLORREF background = hot ? Appearance::blend(colors.surface,
			colors.accent, captured ? 48 : 24) : colors.surface;
		drawScrollSurface(canvas, button, background, colors.border);
		drawScrollArrow(canvas, button, vertical, decrementing,
			unavailable ? colors.disabledText : colors.text);
	};
	drawButton(decrement, 1, SCROLL_PART_DECREMENT, true);
	drawButton(increment, 5, SCROLL_PART_INCREMENT, false);

	if(!(info.rgstate[3] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))) {
		const long trackStart = vertical ? decrement.bottom() : decrement.right();
		const long trackEnd = vertical ? increment.top() : increment.left();
		const long thumbStart = std::max(trackStart,
			std::min<long>(info.xyThumbTop, trackEnd));
		const long thumbEnd = std::max(thumbStart,
			std::min<long>(info.xyThumbBottom, trackEnd));
		Rectangle thumb = bounds;
		if(vertical) {
			thumb.pos.y = thumbStart;
			thumb.size.y = thumbEnd - thumbStart;
		} else {
			thumb.pos.x = thumbStart;
			thumb.size.x = thumbEnd - thumbStart;
		}
		if(thumb.width() > 0 && thumb.height() > 0) {
			const bool hot = hovered == SCROLL_PART_THUMB;
			drawScrollSurface(canvas, thumb,
				Appearance::blend(colors.surface,
					hot ? colors.accent : colors.text,
					captured && hot ? 92 : hot ? 68 : 42), colors.border);
		}
	}
	return true;
}

LRESULT CALLBACK scrollDecorationProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto data = reinterpret_cast<ScrollDecorationData*>(
		::GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if(message == WM_NCCREATE) {
		data = reinterpret_cast<ScrollDecorationData*>(
			reinterpret_cast<CREATESTRUCT*>(lParam)->lpCreateParams);
		::SetWindowLongPtr(hwnd, GWLP_USERDATA,
			reinterpret_cast<LONG_PTR>(data));
	}

	switch(message) {
	case WM_ERASEBKGND:
		return TRUE;
	case WM_PAINT:
		{
			PaintCanvas target(hwnd);
			RECT nativeBounds = { 0 };
			::GetClientRect(hwnd, &nativeBounds);
			const Rectangle bounds(nativeBounds);
			if(bounds.width() > 0 && bounds.height() > 0) {
				BufferedCanvas<FreeCanvas> buffer(target.handle(), bounds);
				if(drawScrollDecoration(hwnd, buffer)) {
					buffer.blast(bounds);
					return 0;
				}
			}
			break;
		}
	case WM_PRINTCLIENT:
		if(wParam) {
			FreeCanvas canvas(reinterpret_cast<HDC>(wParam));
			if(drawScrollDecoration(hwnd, canvas)) {
				return 0;
			}
		}
		break;
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_GETOBJECT:
		return 0;
	case WM_NCDESTROY:
		if(data) {
			if(::IsWindow(data->host) &&
				getScrollDecoration(data->host, data->type) == hwnd) {
				::RemoveProp(data->host,
					scrollDecorationProperty(data->type));
			}
			delete data;
			::SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
		}
		break;
	}
	return ::DefWindowProc(hwnd, message, wParam, lParam);
}

bool ensureScrollDecorationClass() {
	const auto instance = ::GetModuleHandle(nullptr);
	WNDCLASS existing = { 0 };
	if(::GetClassInfo(instance, scrollDecorationClass(), &existing)) {
		return true;
	}

	WNDCLASS cls = { 0 };
	cls.style = CS_HREDRAW | CS_VREDRAW;
	cls.lpfnWndProc = scrollDecorationProc;
	cls.hInstance = instance;
	cls.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
	cls.lpszClassName = scrollDecorationClass();
	return ::RegisterClass(&cls) != 0 ||
		::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND createScrollDecoration(HWND host, ScrollDecorationType type, Appearance* appearance)
{
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(host, GWL_STYLE));
	const bool popup = (style & WS_POPUP) != 0;
	const auto parent = popup ? host : ::GetParent(host);
	if(!parent || !ensureScrollDecorationClass()) {
		return nullptr;
	}

	auto data = new ScrollDecorationData { host, type, appearance };
	/* WM_NCHITTEST passes mouse input through to the native scrollbar.  The
	 * decoration itself is opaque: WS_EX_TRANSPARENT would force the host to
	 * repaint first and create a non-client paint feedback loop. */
	const auto decoration = ::CreateWindowEx(WS_EX_NOACTIVATE |
			WS_EX_NOPARENTNOTIFY |
			(popup ? WS_EX_TOOLWINDOW : 0),
		scrollDecorationClass(), nullptr,
		(popup ? WS_POPUP : WS_CHILD) | WS_CLIPSIBLINGS,
		0, 0, 0, 0, parent, nullptr,
		::GetModuleHandle(nullptr), data);
	if(!decoration) {
		delete data;
		return nullptr;
	}
	if(!::SetProp(host, scrollDecorationProperty(type),
		reinterpret_cast<HANDLE>(decoration))) {
		::DestroyWindow(decoration);
		return nullptr;
	}
	return decoration;
}

void destroyScrollDecoration(HWND host, ScrollDecorationType type) {
	if(!host) {
		return;
	}
	const auto decoration = reinterpret_cast<HWND>(
		::RemoveProp(host, scrollDecorationProperty(type)));
	if(decoration && ::IsWindow(decoration)) {
		::DestroyWindow(decoration);
	}
}

void destroyScrollDecorations(HWND host) {
	destroyScrollDecoration(host, SCROLL_DECORATION_VERTICAL);
	destroyScrollDecoration(host, SCROLL_DECORATION_HORIZONTAL);
	destroyScrollDecoration(host, SCROLL_DECORATION_CORNER);
	if(host && ::IsWindow(host) &&
		::RemoveProp(host, scrollDecorationClipStyleProperty())) {
		const auto style = static_cast<DWORD>(
			::GetWindowLongPtr(host, GWL_STYLE));
		::SetWindowLongPtr(host, GWL_STYLE, style & ~WS_CLIPSIBLINGS);
	}
}

bool getVisibleScrollInfo(HWND host, bool vertical, SCROLLBARINFO& info) {
	info.cbSize = sizeof(SCROLLBARINFO);
	return ::IsWindowVisible(host) &&
		::GetScrollBarInfo(host,
			vertical ? OBJID_VSCROLL : OBJID_HSCROLL, &info) &&
		!(info.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) &&
		info.rcScrollBar.right > info.rcScrollBar.left &&
		info.rcScrollBar.bottom > info.rcScrollBar.top;
}

void positionScrollDecoration(HWND host, ScrollDecorationType type, const RECT& screenBounds, bool visible, Appearance* appearance)
{
	auto decoration = getScrollDecoration(host, type);
	if(!visible) {
		if(decoration && ::IsWindowVisible(decoration)) {
			::ShowWindow(decoration, SW_HIDE);
		}
		return;
	}
	if(!decoration) {
		decoration = createScrollDecoration(host, type, appearance);
		if(!decoration) {
			return;
		}
	}

	RECT bounds = screenBounds;
	const RECT screen = screenBounds;
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(host, GWL_STYLE));
	if(!(style & WS_POPUP)) {
		::MapWindowPoints(HWND_DESKTOP, ::GetParent(host),
			reinterpret_cast<POINT*>(&bounds), 2);
	}
	RECT current = { 0 };
	::GetWindowRect(decoration, &current);
	const bool shown = ::IsWindowVisible(decoration) != FALSE;
	if(!shown || !::EqualRect(&current, &screen)) {
		::SetWindowPos(decoration, HWND_TOP, bounds.left, bounds.top,
			bounds.right - bounds.left, bounds.bottom - bounds.top,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING |
				SWP_SHOWWINDOW | (shown ? SWP_NOZORDER : 0));
	}
	::InvalidateRect(decoration, nullptr, FALSE);
}

void syncScrollDecorations(HWND host, Appearance* appearance) {
	const auto widget = host ? hwnd_cast<Widget*>(host) : nullptr;
	if(!host || !::IsWindow(host) || !appearance || !appearance->isManual() ||
		(widget && widget->getAppearancePolicy() == AppearancePolicy::Native)) {
		destroyScrollDecorations(host);
		return;
	}
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(host, GWL_STYLE));
	const bool popup = (style & WS_POPUP) != 0;
	if(!(style & (WS_VSCROLL | WS_HSCROLL)) ||
		(!popup && !::GetParent(host))) {
		destroyScrollDecorations(host);
		return;
	}

	if(!popup && !(style & WS_CLIPSIBLINGS)) {
		if(::SetProp(host, scrollDecorationClipStyleProperty(),
			reinterpret_cast<HANDLE>(1))) {
			::SetWindowLongPtr(host, GWL_STYLE, style | WS_CLIPSIBLINGS);
		}
	}
	SCROLLBARINFO vertical = { sizeof(SCROLLBARINFO) };
	SCROLLBARINFO horizontal = { sizeof(SCROLLBARINFO) };
	const bool showVertical = (style & WS_VSCROLL) != 0 &&
		getVisibleScrollInfo(host, true, vertical);
	const bool showHorizontal = (style & WS_HSCROLL) != 0 &&
		getVisibleScrollInfo(host, false, horizontal);
	positionScrollDecoration(host, SCROLL_DECORATION_VERTICAL,
		vertical.rcScrollBar, showVertical, appearance);
	positionScrollDecoration(host, SCROLL_DECORATION_HORIZONTAL,
		horizontal.rcScrollBar, showHorizontal, appearance);

	RECT corner = { 0 };
	if(showVertical && showHorizontal) {
		corner.left = vertical.rcScrollBar.left;
		corner.top = horizontal.rcScrollBar.top;
		corner.right = vertical.rcScrollBar.right;
		corner.bottom = horizontal.rcScrollBar.bottom;
	}
	positionScrollDecoration(host, SCROLL_DECORATION_CORNER, corner,
		showVertical && showHorizontal && corner.right > corner.left &&
			corner.bottom > corner.top, appearance);
}

void scheduleScrollDecorationRefresh(HWND host, bool descendants) {
	if(!host || !::IsWindow(host)) {
		return;
	}
	const auto pending = reinterpret_cast<UINT_PTR>(
		::GetProp(host, scrollDecorationRefreshProperty()));
	const UINT_PTR requested = descendants ? 2 : 1;
	if(pending >= requested) {
		return;
	}
	::SetProp(host, scrollDecorationRefreshProperty(),
		reinterpret_cast<HANDLE>(requested));
	if(!pending) {
		::PostMessage(host, scrollDecorationRefreshMessage(), 0, 0);
	}
}

}

Appearance::Palette Appearance::defaultPalette() {
	Palette palette = {
		RGB(32, 32, 32),
		RGB(45, 45, 48),
		RGB(241, 241, 241),
		RGB(160, 160, 160),
		RGB(73, 73, 73),
		RGB(0, 120, 215),
		RGB(255, 255, 255)
	};
	return palette;
}

Appearance::Appearance() :
	mode(Mode::System), configured(defaultPalette()), active(configured),
	highContrast(false), systemDark(false), dark(false), applying(false),
	systemRefreshPending(false), systemRefreshRequiresApply(false), generation(1)
{
	refreshSystemState();
}

Appearance::~Appearance() {
	for(auto widget: widgets) {
		if(widget) {
			destroyScrollDecorations(widget->handle());
			widget->appearance = nullptr;
		}
	}
}

void Appearance::configure(Mode mode_, const Palette& palette) {
	mode = mode_;
	configured = sanitize(palette);
	const bool stateChanged = refreshSystemState();
	if(!stateChanged) {
		updateActivePalette();
		changed();
	}
}

bool Appearance::refreshSystemState() {
	const bool previousHighContrast = highContrast;
	const bool previousSystemDark = systemDark;
	const bool previousDark = dark;

	highContrast = queryHighContrast();
	systemDark = querySystemDarkPreference();
	dark = !highContrast && (mode == Mode::Dark ||
		(mode == Mode::System && systemDark));
	updateActivePalette();

	const bool result = previousHighContrast != highContrast ||
		previousSystemDark != systemDark || previousDark != dark;
	if(result) {
		changed();
	}
	return result;
}

COLORREF Appearance::getContentBackground() const {
	return highContrast ? ::GetSysColor(COLOR_WINDOW) : active.background;
}

COLORREF Appearance::getContentText() const {
	return highContrast ? ::GetSysColor(COLOR_WINDOWTEXT) : active.text;
}

bool Appearance::isColorSchemeMessage(const MSG& msg) const {
	if(applying) {
		return false;
	}
	if(msg.message == WM_THEMECHANGED || msg.message == WM_SYSCOLORCHANGE) {
		return true;
	}
	if(msg.message != WM_SETTINGCHANGE) {
		return false;
	}
	if(msg.wParam == SPI_SETHIGHCONTRAST) {
		return true;
	}
	if(!msg.lParam) {
		return false;
	}
	const auto section = reinterpret_cast<LPCTSTR>(msg.lParam);
	return ::lstrcmpi(section, _T("ImmersiveColorSet")) == 0 ||
		::lstrcmpi(section, _T("Accessibility")) == 0;
}

void Appearance::applyAll() {
	if(applying) {
		return;
	}
	applying = true;
	const auto copy = widgets;
	for(auto widget: copy) {
		if(std::find(widgets.begin(), widgets.end(), widget) != widgets.end()) {
			apply(widget);
		}
	}
	applying = false;
}

Appearance::CallbackIter Appearance::onChanged(const Callback& callback) {
	callbacks.push_back(callback);
	return --callbacks.end();
}

void Appearance::removeChanged(const CallbackIter& callback) {
	callbacks.erase(callback);
}

COLORREF Appearance::blend(COLORREF base, COLORREF overlay, BYTE weight) {
	const unsigned inverse = 255 - weight;
	return RGB(
		(GetRValue(base) * inverse + GetRValue(overlay) * weight) / 255,
		(GetGValue(base) * inverse + GetGValue(overlay) * weight) / 255,
		(GetBValue(base) * inverse + GetBValue(overlay) * weight) / 255);
}

void Appearance::addWidget(Widget* widget) {
	if(!widget || std::find(widgets.begin(), widgets.end(), widget) != widgets.end()) {
		return;
	}
	widgets.push_back(widget);
	apply(widget);
}

void Appearance::removeWidget(Widget* widget) {
	if(widget) {
		destroyScrollDecorations(widget->handle());
	}
	widgets.erase(std::remove(widgets.begin(), widgets.end(), widget), widgets.end());
}

void Appearance::apply(Widget* widget) {
	if(!widget || !widget->handle() || !::IsWindow(widget->handle())) {
		return;
	}
	widget->appearanceGeneration = generation;
	widget->appearanceChanged();
	if(widget->appearancePolicy == AppearancePolicy::Native) {
		destroyScrollDecorations(widget->handle());
	} else {
		syncScrollDecorations(widget->handle(), this);
	}
}

bool Appearance::handleMessage(Widget* widget, const MSG& msg, LRESULT& retVal) {
	if(!widget) {
		return false;
	}

	const auto style = widget->handle() ?
		static_cast<DWORD>(::GetWindowLongPtr(widget->handle(), GWL_STYLE)) : 0;
	if(!(style & WS_CHILD) && isColorSchemeMessage(msg)) {
		scheduleSystemRefresh(msg.message == WM_THEMECHANGED ||
			msg.message == WM_SYSCOLORCHANGE ||
			msg.wParam == SPI_SETHIGHCONTRAST);
	}

	if(msg.message == scrollDecorationRefreshMessage()) {
		/* Keep the request marked pending while synchronizing.  Moving an overlay
		 * can synchronously re-enter the host; clearing it first would allow that
		 * nested message to post an endless series of refreshes. */
		const auto refresh = reinterpret_cast<UINT_PTR>(
			::GetProp(msg.hwnd, scrollDecorationRefreshProperty()));
		if(!refresh) {
			retVal = 0;
			return true;
		}
		if(refresh > 1) {
			/* Showing or hiding a composite changes effective visibility for its
			 * descendants, but it must not repaint every decorated scrollbar in
			 * the application. Restrict the refresh to this window subtree. */
			for(auto registered: widgets) {
				if(registered && registered->handle() &&
					(registered->handle() == msg.hwnd ||
						::IsChild(msg.hwnd, registered->handle()))) {
					syncScrollDecorations(registered->handle(), this);
				}
			}
		} else {
			syncScrollDecorations(msg.hwnd, this);
		}
		const auto pending = reinterpret_cast<UINT_PTR>(
			::RemoveProp(msg.hwnd, scrollDecorationRefreshProperty()));
		if(pending > refresh) {
			scheduleScrollDecorationRefresh(msg.hwnd, pending > 1);
		}
		retVal = 0;
		return true;
	}

	switch(msg.message) {
	case WM_DESTROY:
		destroyScrollDecorations(msg.hwnd);
		break;
	case WM_SHOWWINDOW:
	case WM_STYLECHANGED:
		scheduleScrollDecorationRefresh(msg.hwnd, true);
		break;
	case WM_WINDOWPOSCHANGED:
	case WM_SIZE:
		scheduleScrollDecorationRefresh(msg.hwnd, false);
		break;
	case WM_NCPAINT:
	case WM_NCACTIVATE:
	case WM_NCMOUSEMOVE:
	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONUP:
	case WM_HSCROLL:
	case WM_VSCROLL:
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	case WM_KEYDOWN:
	case WM_KEYUP:
		redrawScrollDecorations(msg.hwnd);
		break;
	default:
		break;
	}

	if(isManual() && widget->getAppearancePolicy() != AppearancePolicy::Native) {
		auto control = dynamic_cast<Control*>(widget);
		if(msg.message == WM_ERASEBKGND &&
			dynamic_cast<Composite*>(widget) && control &&
			!control->hasExplicitColors() &&
			widget->getAppearancePolicy() != AppearancePolicy::ApplicationContent) {
			HDC dc = reinterpret_cast<HDC>(msg.wParam);
			if(dc) {
				RECT rect = { 0 };
				::GetClientRect(widget->handle(), &rect);
				FreeCanvas canvas(dc);
				canvas.fill(Rectangle(rect), *backgroundBrush);
				retVal = TRUE;
				return true;
			}
		}
		if(control && !control->hasExplicitColors()) {
			HDC dc = reinterpret_cast<HDC>(msg.wParam);
			if(dc && (msg.message == WM_CTLCOLOREDIT ||
				msg.message == WM_CTLCOLORLISTBOX)) {
				::SetTextColor(dc, active.text);
				::SetBkColor(dc, active.surface);
				retVal = reinterpret_cast<LRESULT>(surfaceBrush->handle());
				return true;
			}
			if(dc && msg.message == WM_CTLCOLORBTN) {
				::SetTextColor(dc, control->getEnabled() ? active.text :
					active.disabledText);
				::SetBkColor(dc, active.background);
				::SetBkMode(dc, TRANSPARENT);
				retVal = reinterpret_cast<LRESULT>(backgroundBrush->handle());
				return true;
			}
			if(dc && (msg.message == WM_CTLCOLORSTATIC ||
				msg.message == WM_CTLCOLORDLG ||
				msg.message == WM_CTLCOLORMSGBOX)) {
				::SetTextColor(dc, control->getEnabled() ? active.text :
					active.disabledText);
				::SetBkColor(dc, active.background);
				if(msg.message != WM_CTLCOLORDLG) {
					::SetBkMode(dc, TRANSPARENT);
				}
				retVal = reinterpret_cast<LRESULT>(backgroundBrush->handle());
				return true;
			}
		}
	}

	return false;
}

void Appearance::updateActivePalette() {
	if(!highContrast) {
		active = configured;
	} else {
		active.background = ::GetSysColor(COLOR_BTNFACE);
		active.surface = ::GetSysColor(COLOR_WINDOW);
		active.text = ::GetSysColor(COLOR_BTNTEXT);
		active.disabledText = ::GetSysColor(COLOR_GRAYTEXT);
		active.border = ::GetSysColor(COLOR_WINDOWFRAME);
		active.accent = ::GetSysColor(COLOR_HIGHLIGHT);
		active.highlightText = ::GetSysColor(COLOR_HIGHLIGHTTEXT);
	}

	backgroundBrush = new Brush(active.background);
	surfaceBrush = new Brush(active.surface);
}

void Appearance::changed() {
	++generation;
	applyAll();
	const std::vector<Callback> copy(callbacks.begin(), callbacks.end());
	for(const auto& callback: copy) {
		callback();
	}
}

void Appearance::scheduleSystemRefresh(bool forceApply) {
	systemRefreshRequiresApply |= forceApply;
	if(systemRefreshPending) {
		return;
	}
	systemRefreshPending = true;
	Application::instance().callAsync([this] {
		systemRefreshPending = false;
		const bool apply = systemRefreshRequiresApply;
		systemRefreshRequiresApply = false;
		if(!refreshSystemState() && apply) {
			// System colors and visual-style resources can change without changing
			// the selected light/dark mode or the high-contrast flag.
			updateActivePalette();
			changed();
		}
	});
}

Appearance::Palette Appearance::sanitize(Palette palette) {
	palette.background &= 0x00ffffff;
	palette.surface &= 0x00ffffff;
	palette.text &= 0x00ffffff;
	palette.disabledText &= 0x00ffffff;
	palette.border &= 0x00ffffff;
	palette.accent &= 0x00ffffff;
	palette.highlightText &= 0x00ffffff;
	return palette;
}

bool Appearance::queryHighContrast() {
	HIGHCONTRAST value = { sizeof(HIGHCONTRAST) };
	return ::SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0) &&
		(value.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool Appearance::querySystemDarkPreference() {
	DWORD useLightTheme = 1;
	DWORD size = sizeof(useLightTheme);
	return ::RegGetValue(HKEY_CURRENT_USER,
		_T("Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
		_T("AppsUseLightTheme"), RRF_RT_REG_DWORD, nullptr, &useLightTheme,
		&size) == ERROR_SUCCESS && useLightTheme == 0;
}

}
