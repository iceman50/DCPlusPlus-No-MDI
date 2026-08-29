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

#include <dwt/util/win32/NativeDialogAppearance.h>

#include <dwt/Appearance.h>
#include <dwt/CanvasClasses.h>
#include <dwt/LibraryLoader.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Font.h>
#include <dwt/resources/Pen.h>
#include <dwt/tstring.h>
#include <dwt/util/win32/Dpi.h>

#include <algorithm>
#include <array>
#include <commctrl.h>
#include <cstring>
#include <richedit.h>
#include <tchar.h>
#include <uxtheme.h>
#include <vector>

namespace dwt { namespace util { namespace win32 {

namespace {

enum class NativeControl {
	Dialog,
	Button,
	ComboBox,
	Edit,
	Header,
	Link,
	ListBox,
	ListView,
	ProgressBar,
	ScrollBar,
	Static,
	TreeView,
	Unknown
};

struct SharedAppearance {
	explicit SharedAppearance(const Appearance::Palette& palette_, bool preserveDialogClientPainting_) :
		references(0), palette(palette_), background(palette.background), surface(palette.surface),
		preserveDialogClientPainting(preserveDialogClientPainting_) { }

	void retain() {
		::InterlockedIncrement(&references);
	}

	void release() {
		if(!::InterlockedDecrement(&references)) {
			delete this;
		}
	}

	volatile LONG references;
	Appearance::Palette palette;
	Brush background;
	Brush surface;
	bool preserveDialogClientPainting;
};

struct NativeWindowData {
	NativeWindowData(SharedAppearance* appearance_, NativeControl control_) :
		appearance(appearance_), control(control_), hot(false),
		hotCaptionButton(HTNOWHERE), pressedCaptionButton(HTNOWHERE),
		captionActive(false), trackingCaptionMouse(false),
		manualNonClient(false) {
		appearance->retain();
	}

	~NativeWindowData() {
		appearance->release();
	}

	SharedAppearance* appearance;
	NativeControl control;
	bool hot;
	int hotCaptionButton;
	int pressedCaptionButton;
	bool captionActive;
	bool trackingCaptionMouse;
	bool manualNonClient;
};

const UINT_PTR nativeDialogSubclassId = 0x44575441;

typedef HRESULT (WINAPI* DwmSetWindowAttributeFunction)(HWND, DWORD, LPCVOID, DWORD);

enum DwmWindowAttribute {
	DWT_DWMWA_NCRENDERING_POLICY = 2
};

enum DwmNcRenderingPolicy {
	DWT_DWMNCRP_USEWINDOWSTYLE,
	DWT_DWMNCRP_DISABLED
};

struct CaptionButton {
	int hitTest;
	Rectangle bounds;
	bool disabled;
};

struct CaptionLayout {
	Point origin;
	Rectangle window;
	Rectangle caption;
	Rectangle icon;
	std::vector<CaptionButton> buttons;
	long borderX;
	long borderY;
	bool rightToLeft;
	bool toolWindow;
};

DwmSetWindowAttributeFunction getDwmSetWindowAttribute() {
	static LibraryLoader library(_T("dwmapi.dll"), true);
	static const auto function = [] {
		DwmSetWindowAttributeFunction result = nullptr;
		if(library.loaded()) {
			const auto address = library.getProcAddress(
				_T("DwmSetWindowAttribute"));
			static_assert(sizeof(result) == sizeof(address),
				"Function pointer representations must have equal size");
			std::memcpy(&result, &address, sizeof(result));
		}
		return result;
	}();
	return function;
}

bool setNonClientRendering(HWND window, bool disabled) {
	auto function = getDwmSetWindowAttribute();
	if(!function) {
		return false;
	}
	const int policy = disabled ? DWT_DWMNCRP_DISABLED :
		DWT_DWMNCRP_USEWINDOWSTYLE;
	return SUCCEEDED(function(window, DWT_DWMWA_NCRENDERING_POLICY,
		&policy, sizeof(policy)));
}

tstring windowClass(HWND window) {
	std::vector<TCHAR> value(128);
	for(;;) {
		const auto length = ::GetClassName(window, value.data(),
			static_cast<int>(value.size()));
		if(length <= 0) {
			return tstring();
		}
		if(static_cast<size_t>(length + 1) < value.size()) {
			return tstring(value.data(), length);
		}
		value.resize(value.size() * 2);
	}
}

tstring windowText(HWND window) {
	const auto length = ::GetWindowTextLength(window);
	std::vector<TCHAR> value(static_cast<size_t>(std::max(0, length)) + 1);
	::GetWindowText(window, value.data(), static_cast<int>(value.size()));
	return value.data();
}

bool equals(const tstring& lhs, LPCTSTR rhs) {
	return _tcsicmp(lhs.c_str(), rhs) == 0;
}

NativeControl classify(HWND window) {
	const auto name = windowClass(window);
	if(equals(name, _T("#32770"))) return NativeControl::Dialog;
	if(equals(name, WC_BUTTON)) return NativeControl::Button;
	if(equals(name, WC_COMBOBOX)) return NativeControl::ComboBox;
	if(equals(name, WC_EDIT) || equals(name, _T("RichEdit20A")) ||
		equals(name, _T("RichEdit20W")) || equals(name, _T("RICHEDIT50W"))) {
		return NativeControl::Edit;
	}
	if(equals(name, WC_HEADER)) return NativeControl::Header;
	if(equals(name, WC_LINK)) return NativeControl::Link;
	if(equals(name, WC_LISTBOX) || equals(name, _T("ComboLBox"))) {
		return NativeControl::ListBox;
	}
	if(equals(name, WC_LISTVIEW)) return NativeControl::ListView;
	if(equals(name, PROGRESS_CLASS)) return NativeControl::ProgressBar;
	if(equals(name, WC_SCROLLBAR)) return NativeControl::ScrollBar;
	if(equals(name, WC_STATIC)) return NativeControl::Static;
	if(equals(name, WC_TREEVIEW)) return NativeControl::TreeView;
	return NativeControl::Unknown;
}

bool valid(const Rectangle& rectangle) {
	return rectangle.width() > 0 && rectangle.height() > 0;
}

Rectangle localRectangle(const RECT& rectangle, const RECT& window) {
	return Rectangle(rectangle.left - window.left,
		rectangle.top - window.top, rectangle.right - rectangle.left,
		rectangle.bottom - rectangle.top);
}

Rectangle intersect(const Rectangle& first, const Rectangle& second) {
	const long left = std::max(first.left(), second.left());
	const long top = std::max(first.top(), second.top());
	const long right = std::min(first.right(), second.right());
	const long bottom = std::min(first.bottom(), second.bottom());
	return Rectangle(left, top, std::max(0L, right - left),
		std::max(0L, bottom - top));
}

bool captionButton(int hitTest) {
	return hitTest == HTCLOSE || hitTest == HTMINBUTTON ||
		hitTest == HTMAXBUTTON || hitTest == HTHELP;
}

bool buttonVisible(DWORD state) {
	return (state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) == 0;
}

bool systemCommandEnabled(HWND window, UINT command) {
	const auto menu = ::GetSystemMenu(window, FALSE);
	if(!menu) {
		return true;
	}
	const auto state = ::GetMenuState(menu, command, MF_BYCOMMAND);
	return state == static_cast<UINT>(-1) ||
		(state & (MF_DISABLED | MF_GRAYED)) == 0;
}

CaptionLayout getCaptionLayout(HWND window) {
	CaptionLayout result = { };
	RECT nativeWindow = { 0 };
	if(!::GetWindowRect(window, &nativeWindow)) {
		return result;
	}
	result.origin = Point(nativeWindow.left, nativeWindow.top);
	result.window = Rectangle(0, 0,
		nativeWindow.right - nativeWindow.left,
		nativeWindow.bottom - nativeWindow.top);

	const auto style = static_cast<DWORD>(
		::GetWindowLongPtr(window, GWL_STYLE));
	const auto exStyle = static_cast<DWORD>(
		::GetWindowLongPtr(window, GWL_EXSTYLE));
	result.rightToLeft = (exStyle & WS_EX_LAYOUTRTL) != 0;
	result.toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;
	const auto dpi = getDpi(window);

	WINDOWINFO windowInfo = { sizeof(WINDOWINFO) };
	if(::GetWindowInfo(window, &windowInfo)) {
		result.borderX = windowInfo.cxWindowBorders;
		result.borderY = windowInfo.cyWindowBorders;
	} else {
		result.borderX = (style & WS_THICKFRAME) ?
			getSystemMetricsForDpi(SM_CXFRAME, dpi) :
			getSystemMetricsForDpi(SM_CXBORDER, dpi);
		result.borderY = (style & WS_THICKFRAME) ?
			getSystemMetricsForDpi(SM_CYFRAME, dpi) :
			getSystemMetricsForDpi(SM_CYBORDER, dpi);
	}

	if((style & WS_CAPTION) != WS_CAPTION) {
		return result;
	}

	TITLEBARINFOEX title = { };
	title.cbSize = sizeof(title);
	::SendMessage(window, WM_GETTITLEBARINFOEX, 0,
		reinterpret_cast<LPARAM>(&title));
	const auto reportedCaption = intersect(
		localRectangle(title.rcTitleBar, nativeWindow), result.window);
	if(valid(reportedCaption)) {
		/* rcTitleBar excludes the window-menu strip. The palette surface must
		 * cover the full inner frame so no native-colored gap remains. */
		const long left = std::max(0L, std::min(result.borderX,
			result.window.width() / 2));
		result.caption = Rectangle(left, reportedCaption.top(),
			std::max(0L, result.window.width() - left * 2),
			reportedCaption.height());
	} else {
		const long height = getSystemMetricsForDpi(result.toolWindow ?
			SM_CYSMCAPTION : SM_CYCAPTION, dpi);
		result.caption = Rectangle(result.borderX, result.borderY,
			std::max(0L, result.window.width() - 2 * result.borderX),
			std::min(height, std::max(0L,
				result.window.height() - result.borderY)));
	}

	const std::array<int, 4> titleIndices = { 5, 3, 2, 4 };
	const std::array<int, 4> hitTests = {
		HTCLOSE, HTMAXBUTTON, HTMINBUTTON, HTHELP
	};
	for(size_t i = 0; i < titleIndices.size(); ++i) {
		const int index = titleIndices[i];
		auto bounds = intersect(localRectangle(title.rgrect[index], nativeWindow),
			result.caption);
		if(valid(bounds) && buttonVisible(title.rgstate[index])) {
			result.buttons.push_back({ hitTests[i], bounds,
				(title.rgstate[index] & STATE_SYSTEM_UNAVAILABLE) != 0 });
		}
	}

	if(result.buttons.empty()) {
		const long width = std::max(1,
			getSystemMetricsForDpi(result.toolWindow ?
				SM_CXSMSIZE : SM_CXSIZE, dpi));
		long position = result.rightToLeft ? result.caption.left() :
			result.caption.right();
		auto addButton = [&](int hitTest, bool disabled) {
			Rectangle bounds;
			if(result.rightToLeft) {
				bounds = Rectangle(position, result.caption.top(), width,
					result.caption.height());
				position += width;
			} else {
				position -= width;
				bounds = Rectangle(position, result.caption.top(), width,
					result.caption.height());
			}
			bounds = intersect(bounds, result.caption);
			if(valid(bounds)) {
				result.buttons.push_back({ hitTest, bounds, disabled });
			}
		};

		if(style & WS_SYSMENU) {
			addButton(HTCLOSE, !systemCommandEnabled(window, SC_CLOSE));
			if(exStyle & WS_EX_CONTEXTHELP) {
				addButton(HTHELP, false);
			} else if(style & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX)) {
				addButton(HTMAXBUTTON, (style & WS_MAXIMIZEBOX) == 0);
				addButton(HTMINBUTTON, (style & WS_MINIMIZEBOX) == 0);
			}
		}
	}

	if((style & WS_SYSMENU) && !result.toolWindow &&
		!(exStyle & WS_EX_DLGMODALFRAME)) {
		const long width = getSystemMetricsForDpi(SM_CXSMICON, dpi);
		const long height = getSystemMetricsForDpi(SM_CYSMICON, dpi);
		const long margin = scale(5, dpi);
		const long left = result.rightToLeft ?
			result.caption.right() - margin - width :
			result.caption.left() + margin;
		result.icon = intersect(Rectangle(left,
			result.caption.top() + (result.caption.height() - height) / 2,
			width, height), result.caption);
	}
	return result;
}

Point windowPoint(const CaptionLayout& layout, const Point& screenPoint) {
	return Point(screenPoint.x - layout.origin.x,
		screenPoint.y - layout.origin.y);
}

const CaptionButton* findCaptionButton(const CaptionLayout& layout, int hitTest)
{
	for(const auto& button: layout.buttons) {
		if(button.hitTest == hitTest) {
			return &button;
		}
	}
	return nullptr;
}

const CaptionButton* findCaptionButtonAt(const CaptionLayout& layout, const Point& screenPoint)
{
	const auto point = windowPoint(layout, screenPoint);
	for(const auto& button: layout.buttons) {
		if(button.bounds.contains(point)) {
			return &button;
		}
	}
	return nullptr;
}

int reconcileCaptionHitTest(const CaptionLayout& layout, const Point& screenPoint, int nativeHitTest)
{
	if(const auto button = findCaptionButtonAt(layout, screenPoint)) {
		return button->hitTest;
	}
	if(captionButton(nativeHitTest)) {
		const auto point = windowPoint(layout, screenPoint);
		if(layout.icon.contains(point)) {
			return HTSYSMENU;
		}
		if(layout.caption.contains(point)) {
			return HTCAPTION;
		}
	}
	return nativeHitTest;
}

UINT captionCommand(HWND window, int hitTest) {
	switch(hitTest) {
	case HTCLOSE: return SC_CLOSE;
	case HTMINBUTTON: return SC_MINIMIZE;
	case HTMAXBUTTON: return ::IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE;
	case HTHELP: return SC_CONTEXTHELP;
	default: return 0;
	}
}

void drawCaptionGlyph(Canvas& canvas, const Rectangle& bounds, int hitTest, bool maximized, COLORREF color, int lineWidth)
{
	if(!valid(bounds)) {
		return;
	}
	const long centerX = bounds.left() + bounds.width() / 2;
	const long centerY = bounds.top() + bounds.height() / 2;
	const long radius = std::max(3L,
		std::min(bounds.width(), bounds.height()) / 5);
	Pen pen(color, Pen::Solid, lineWidth);
	auto selectPen = canvas.select(pen);

	switch(hitTest) {
	case HTCLOSE:
		/* GDI's LineTo excludes the destination pixel. Extend each descending
		 * stroke so both lower endpoints remain part of the visible glyph. */
		canvas.line(centerX - radius, centerY - radius, centerX + radius + 1, centerY + radius + 1);
		canvas.line(centerX + radius, centerY - radius, centerX - radius - 1, centerY + radius + 1);
		break;
	case HTMINBUTTON:
		canvas.line(centerX - radius, centerY + radius / 2,
			centerX + radius, centerY + radius / 2);
		break;
	case HTMAXBUTTON:
		{
			Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)),
				false);
			auto selectBrush = canvas.select(hollow);
			if(maximized) {
				canvas.rectangle(Rectangle(centerX - radius + lineWidth,
					centerY - radius, radius * 2, radius * 2));
				canvas.rectangle(Rectangle(centerX - radius,
					centerY - radius + lineWidth * 2, radius * 2, radius * 2));
			} else {
				canvas.rectangle(Rectangle(centerX - radius, centerY - radius,
					radius * 2, radius * 2));
			}
		}
		break;
	case HTHELP:
		{
			Rectangle text(bounds);
			canvas.setTextColor(color);
			auto transparent = canvas.setBkMode(true);
			canvas.drawText(_T("?"), text, DT_CENTER | DT_VCENTER |
				DT_SINGLELINE | DT_NOPREFIX);
		}
		break;
	}
}

void redrawNonClient(HWND window) {
	::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
}

void paintNonClient(HWND window, NativeWindowData& data) {
	if(!data.manualNonClient || !::IsWindowVisible(window)) {
		return;
	}
	const auto layout = getCaptionLayout(window);
	if(!valid(layout.window)) {
		return;
	}
	WindowUpdateCanvas target(window);
	if(!target.handle()) {
		return;
	}

	const auto& colors = data.appearance->palette;
	const COLORREF captionColor = data.captionActive ? colors.surface :
		Appearance::blend(colors.background, colors.surface, 96);
	const COLORREF borderColor = data.captionActive ? colors.border :
		Appearance::blend(colors.background, colors.border, 112);
	Brush borderBrush(borderColor);
	const long borderX = std::max(0L, std::min(layout.borderX,
		layout.window.width() / 2));
	const long borderY = std::max(0L, std::min(layout.borderY,
		layout.window.height() / 2));
	if(borderX) {
		target.fill(Rectangle(0, 0, borderX, layout.window.height()), borderBrush);
		target.fill(Rectangle(layout.window.right() - borderX, 0, borderX,
			layout.window.height()), borderBrush);
	}
	if(borderY) {
		target.fill(Rectangle(0, 0, layout.window.width(), borderY), borderBrush);
		target.fill(Rectangle(0, layout.window.bottom() - borderY,
			layout.window.width(), borderY), borderBrush);
	}
	if(!valid(layout.caption)) {
		return;
	}

	BufferedCanvas<FreeCanvas> canvas(target.handle(), layout.caption);
	Brush captionBrush(captionColor);
	canvas.fill(layout.caption, captionBrush);
	const auto dpi = getDpi(window);
	NONCLIENTMETRICS metrics = { };
	metrics.cbSize = sizeof(metrics);
	FontPtr font;
	if(systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
		&metrics, 0, dpi)) {
		font = new Font(layout.toolWindow ? metrics.lfSmCaptionFont :
			metrics.lfCaptionFont);
	} else {
		font = new Font(Font::DefaultGui);
	}
	auto selectFont = canvas.select(*font);
	auto transparent = canvas.setBkMode(true);

	for(const auto& button: layout.buttons) {
		COLORREF background = captionColor;
		if(!button.disabled && button.hitTest == data.pressedCaptionButton &&
			button.hitTest == data.hotCaptionButton) {
			background = Appearance::blend(captionColor, colors.accent, 128);
		} else if(!button.disabled && button.hitTest == data.hotCaptionButton) {
			background = Appearance::blend(captionColor, colors.accent, 64);
		}
		Brush buttonBrush(background);
		canvas.fill(button.bounds, buttonBrush);
		drawCaptionGlyph(canvas, button.bounds, button.hitTest,
			::IsZoomed(window) != FALSE,
			button.disabled || !::IsWindowEnabled(window) ?
				colors.disabledText : (data.captionActive ? colors.text :
					colors.disabledText), std::max(1, scale(1, dpi)));
	}

	if(valid(layout.icon)) {
		HICON iconHandle = reinterpret_cast<HICON>(
			::SendMessage(window, WM_GETICON, ICON_SMALL2, 0));
		if(!iconHandle) {
			iconHandle = reinterpret_cast<HICON>(
				::SendMessage(window, WM_GETICON, ICON_SMALL, 0));
		}
		if(!iconHandle) {
			iconHandle = reinterpret_cast<HICON>(
				::GetClassLongPtr(window, GCLP_HICONSM));
		}
		if(iconHandle) {
			IconPtr icon = new Icon(iconHandle, false);
			canvas.drawIcon(icon, layout.icon);
		}
	}

	Rectangle text = layout.caption;
	const long padding = scale(6, dpi);
	if(layout.rightToLeft) {
		text.pos.x += padding;
		for(const auto& button: layout.buttons) {
			text.pos.x = std::max(text.left(), button.bounds.right() + padding);
		}
		if(valid(layout.icon)) {
			text.size.x = std::max(0L, layout.icon.left() - padding - text.left());
		} else {
			text.size.x = std::max(0L,
				layout.caption.right() - padding - text.left());
		}
	} else {
		text.pos.x = valid(layout.icon) ? layout.icon.right() + padding :
			layout.caption.left() + padding;
		long right = layout.caption.right() - padding;
		for(const auto& button: layout.buttons) {
			right = std::min(right, button.bounds.left() - padding);
		}
		text.size.x = std::max(0L, right - text.left());
	}
	if(valid(text)) {
		canvas.setTextColor(data.captionActive ? colors.text : colors.disabledText);
		unsigned format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
			DT_NOPREFIX;
		if(layout.rightToLeft) {
			format |= DT_RTLREADING | DT_RIGHT;
		}
		canvas.drawText(windowText(window), text, format);
	}

	Pen separator(borderColor, Pen::Solid, 1);
	{
		auto selectPen = canvas.select(separator);
		canvas.line(layout.caption.left(), layout.caption.bottom() - 1,
			layout.caption.right(), layout.caption.bottom() - 1);
	}
	canvas.blast(layout.caption);
}

void drawSurface(Canvas& canvas, const Rectangle& bounds, COLORREF fill, COLORREF border)
{
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return;
	}
	Brush brush(fill);
	canvas.fill(bounds, brush);
	if(bounds.width() > 1 && bounds.height() > 1) {
		Pen pen(border, Pen::Solid, 1);
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectPen = canvas.select(pen);
		auto selectBrush = canvas.select(hollow);
		canvas.rectangle(Rectangle(bounds.left(), bounds.top(),
			bounds.width() - 1, bounds.height() - 1));
	}
}

void drawFocus(Canvas& canvas, Rectangle bounds, COLORREF color) {
	if(bounds.width() <= 2 || bounds.height() <= 2) {
		return;
	}
	Pen pen(color, Pen::Dot, 1);
	Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
	auto selectPen = canvas.select(pen);
	auto selectBrush = canvas.select(hollow);
	canvas.rectangle(Rectangle(bounds.left(), bounds.top(),
		bounds.width() - 1, bounds.height() - 1));
}

void drawArrow(Canvas& canvas, const Rectangle& bounds, bool vertical, bool towardEnd, COLORREF color)
{
	const long radius = std::max(2L,
		std::min<long>(4, std::min(bounds.width(), bounds.height()) / 4));
	const long x = bounds.left() + bounds.width() / 2;
	const long y = bounds.top() + bounds.height() / 2;
	POINT points[3];
	if(vertical) {
		points[0] = { x - radius, y + (towardEnd ? -radius / 2 : radius / 2) };
		points[1] = { x + radius, y + (towardEnd ? -radius / 2 : radius / 2) };
		points[2] = { x, y + (towardEnd ? radius / 2 + 1 : -radius / 2 - 1) };
	} else {
		points[0] = { x + (towardEnd ? -radius / 2 : radius / 2), y - radius };
		points[1] = { x + (towardEnd ? -radius / 2 : radius / 2), y + radius };
		points[2] = { x + (towardEnd ? radius / 2 + 1 : -radius / 2 - 1), y };
	}
	Pen pen(color, Pen::Solid, 1);
	Brush brush(color);
	auto selectPen = canvas.select(pen);
	auto selectBrush = canvas.select(brush);
	canvas.polygon(points, 3);
}

Font windowFont(HWND window) {
	auto font = reinterpret_cast<HFONT>(::SendMessage(window, WM_GETFONT, 0, 0));
	if(!font) {
		font = static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
	}
	return Font(font, false);
}

tstring buttonNote(HWND window) {
	const auto length = static_cast<DWORD>(
		::SendMessage(window, BCM_GETNOTELENGTH, 0, 0));
	if(!length) {
		return tstring();
	}
	std::vector<TCHAR> value(static_cast<size_t>(length) + 1);
	auto size = static_cast<DWORD>(value.size());
	if(!::SendMessage(window, BCM_GETNOTE,
		reinterpret_cast<WPARAM>(&size),
		reinterpret_cast<LPARAM>(value.data()))) {
		return tstring();
	}
	return value.data();
}

long drawButtonImage(HWND window, Canvas& canvas, const Rectangle& bounds, bool hasText, bool disabled, bool pressed, bool hot)
{
	BUTTON_IMAGELIST imageList = { };
	if(::SendMessage(window, BCM_GETIMAGELIST, 0,
		reinterpret_cast<LPARAM>(&imageList)) && imageList.himl) {
		int width = 0;
		int height = 0;
		ImageList_GetIconSize(imageList.himl, &width, &height);
		const auto count = ImageList_GetImageCount(imageList.himl);
		const auto index = count <= 1 ? 0 : std::min(count - 1,
			disabled ? 3 : pressed ? 2 : hot ? 1 : 0);
		const long x = hasText ? bounds.left() : bounds.left() +
			std::max(0L, (bounds.width() - width) / 2);
		const long y = bounds.top() + std::max(0L,
			(bounds.height() - height) / 2);
		ImageList_Draw(imageList.himl, index, canvas.handle(), x, y,
			ILD_TRANSPARENT);
		return width;
	}

	const auto icon = reinterpret_cast<HICON>(
		::SendMessage(window, BM_GETIMAGE, IMAGE_ICON, 0));
	if(icon) {
		const long width = ::GetSystemMetrics(SM_CXSMICON);
		const long height = ::GetSystemMetrics(SM_CYSMICON);
		const long x = hasText ? bounds.left() : bounds.left() +
			std::max(0L, (bounds.width() - width) / 2);
		const long y = bounds.top() + std::max(0L,
			(bounds.height() - height) / 2);
		::DrawIconEx(canvas.handle(), x, y, icon, width, height, 0,
			nullptr, DI_NORMAL);
		return width;
	}

	const auto bitmap = reinterpret_cast<HBITMAP>(
		::SendMessage(window, BM_GETIMAGE, IMAGE_BITMAP, 0));
	BITMAP info = { };
	if(bitmap && ::GetObject(bitmap, sizeof(info), &info)) {
		const long x = hasText ? bounds.left() : bounds.left() +
			std::max(0L, (bounds.width() - info.bmWidth) / 2);
		const long y = bounds.top() + std::max(0L,
			(bounds.height() - info.bmHeight) / 2);
		::DrawState(canvas.handle(), nullptr, nullptr,
			reinterpret_cast<LPARAM>(bitmap), 0, x, y,
			info.bmWidth, info.bmHeight,
			DST_BITMAP | (disabled ? DSS_DISABLED : DSS_NORMAL));
		return info.bmWidth;
	}
	return 0;
}

unsigned textFormat(DWORD style, bool multiline = false) {
	unsigned format = multiline ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER;
	format |= DT_END_ELLIPSIS;
	switch(style & (BS_LEFT | BS_RIGHT | BS_CENTER)) {
	case BS_LEFT: format |= DT_LEFT; break;
	case BS_RIGHT: format |= DT_RIGHT; break;
	default: format |= DT_CENTER; break;
	}
	return format;
}

void drawCheck(Canvas& canvas, const Rectangle& box, bool radio, bool checked, bool indeterminate, bool disabled, bool pressed, bool hot, const Appearance::Palette& palette)
{
	auto fill = radio ? palette.surface : checked ? palette.accent : palette.surface;
	auto border = checked ? palette.accent : palette.border;
	if(pressed) {
		const auto pressedSurface = Appearance::blend(palette.surface, palette.accent, 38);
		fill = radio || !checked ? pressedSurface : Appearance::blend(palette.accent, RGB(0, 0, 0), 45);
		if(radio && checked) border = Appearance::blend(palette.accent, RGB(0, 0, 0), 45);
	} else if(hot) {
		const auto hotSurface = Appearance::blend(palette.surface, palette.accent, 18);
		fill = radio || !checked ? hotSurface : Appearance::blend(palette.accent, RGB(0, 0, 0), 25);
		border = Appearance::blend(border, palette.accent, 110);
	}
	if(disabled) {
		fill = Appearance::blend(fill, palette.background, 120);
		border = Appearance::blend(border, palette.background, 90);
	}
	if(radio) {
		Brush brush(fill);
		Pen pen(border, Pen::Solid, 1);
		auto selectBrush = canvas.select(brush);
		auto selectPen = canvas.select(pen);
		canvas.ellipse(box);
	} else {
		drawSurface(canvas, box, fill, border);
	}
	if(!checked) {
		return;
	}
	const auto checkMark = disabled ? Appearance::blend(palette.highlightText, fill, 105) : palette.highlightText;
	const auto mark = radio ? border : checkMark;
	Pen pen(mark, Pen::Solid, radio ? std::max(1L, box.width() / 15) : std::max(1L, box.width() / 7));
	Brush brush(mark);
	auto selectPen = canvas.select(pen);
	auto selectBrush = canvas.select(brush);
	if(radio) {
		const long inset = std::max(3L, box.width() / 3);
		canvas.ellipse(Rectangle(box.left() + inset, box.top() + inset,
			std::max(1L, box.width() - inset * 2),
			std::max(1L, box.height() - inset * 2)));
	} else if(indeterminate) {
		canvas.line(Point(box.left() + box.width() / 4,
			box.top() + box.height() / 2),
			Point(box.right() - box.width() / 4,
				box.top() + box.height() / 2));
	} else {
		canvas.line(Point(box.left() + box.width() / 4,
			box.top() + box.height() / 2),
			Point(box.left() + box.width() * 2 / 5,
				box.bottom() - box.height() / 4));
		canvas.line(Point(box.left() + box.width() * 2 / 5,
			box.bottom() - box.height() / 4),
			Point(box.right() - box.width() / 5,
				box.top() + box.height() / 4));
	}
}

void drawButton(HWND window, Canvas& canvas, NativeWindowData& data) {
	RECT nativeBounds = { 0 };
	::GetClientRect(window, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	const auto& palette = data.appearance->palette;
	Brush backdrop(palette.background);
	canvas.fill(bounds, backdrop);

	const DWORD style = static_cast<DWORD>(
		::GetWindowLongPtr(window, GWL_STYLE));
	const DWORD type = style & BS_TYPEMASK;
	const bool disabled = ::IsWindowEnabled(window) == FALSE;
	const auto state = static_cast<UINT>(::SendMessage(window, BM_GETSTATE, 0, 0));
	const bool pressed = (state & BST_PUSHED) != 0;
	const bool focused = ::GetFocus() == window || (state & BST_FOCUS) != 0;
	const bool group = type == BS_GROUPBOX;
	const bool check = type == BS_CHECKBOX || type == BS_AUTOCHECKBOX ||
		type == BS_3STATE || type == BS_AUTO3STATE;
	const bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
	const auto text = windowText(window);
	auto font = windowFont(window);
	auto selectFont = canvas.select(font);
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(disabled ? palette.disabledText : palette.text);

	if(group) {
		const auto extent = canvas.getTextExtent(text);
		const long top = std::max(1L, extent.y / 2);
		Pen pen(palette.border, Pen::Solid, 1);
		auto selectPen = canvas.select(pen);
		canvas.line(Point(bounds.left(), top), Point(bounds.left(), bounds.bottom() - 1));
		canvas.line(Point(bounds.left(), bounds.bottom() - 1), Point(bounds.right() - 1, bounds.bottom() - 1));
		canvas.line(Point(bounds.right() - 1, bounds.bottom() - 1), Point(bounds.right() - 1, top));
		canvas.line(Point(bounds.right() - 1, top), Point(bounds.left(), top));
		if(!text.empty()) {
			Rectangle label(bounds.left() + 8, 0, extent.x + 6,
				std::max<long>(extent.y, top + 2));
			canvas.fill(label, backdrop);
			label.pos.x += 3;
			label.size.x = std::max(0L, label.size.x - 6);
			canvas.drawText(text, label, DT_LEFT | DT_TOP | DT_SINGLELINE);
		}
		return;
	}

	if((check || radio) && !(style & BS_PUSHLIKE)) {
		const auto dpi = getDpi(window);
		const long minimumSize = scale(9, dpi);
		const long size = std::max(minimumSize, std::min<long>(
			scale(radio ? 13 : 15, dpi),
			std::max(minimumSize, bounds.height() - scale(radio ? 2 : 4, dpi))));
		const bool right = (style & BS_LEFTTEXT) != 0;
		Rectangle glyph(right ? bounds.right() - size - 1 : bounds.left() + 1,
			bounds.top() + std::max(0L, (bounds.height() - size) / 2),
			size, size);
		const auto checkState = static_cast<UINT>(
			::SendMessage(window, BM_GETCHECK, 0, 0));
		drawCheck(canvas, glyph, radio, checkState != BST_UNCHECKED, checkState == BST_INDETERMINATE, disabled, pressed, data.hot, palette);
		const long gap = scale(7, dpi);
		Rectangle textBounds(right ? bounds.left() : glyph.right() + gap,
			bounds.top(), right ? std::max(0L, glyph.left() - gap - bounds.left()) :
				std::max(0L, bounds.right() - glyph.right() - gap), bounds.height());
		unsigned format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
			((style & BS_RIGHT) ? DT_RIGHT : DT_LEFT);
		if(::GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_RTLREADING) {
			format |= DT_RTLREADING;
		}
		if(!text.empty()) {
			canvas.drawText(text, textBounds, format);
		}
		if(focused && !disabled && !(::SendMessage(window,
			WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
			drawFocus(canvas, Rectangle(textBounds.left(), textBounds.top() + 2,
				std::max(0L, textBounds.width() - 1),
				std::max(0L, textBounds.height() - 4)), palette.disabledText);
		}
		return;
	}

	const bool defaultButton = type == BS_DEFPUSHBUTTON ||
		type == BS_DEFCOMMANDLINK || type == BS_DEFSPLITBUTTON;
	const bool commandLink = type == BS_COMMANDLINK ||
		type == BS_DEFCOMMANDLINK;
	auto background = defaultButton ? palette.accent : palette.surface;
	auto foreground = defaultButton ? palette.highlightText : palette.text;
	auto border = defaultButton ? palette.accent : palette.border;
	if(pressed || data.hot) {
		background = Appearance::blend(background, palette.accent,
			pressed ? 55 : 24);
		border = Appearance::blend(border, palette.accent, 90);
	}
	if(disabled) {
		background = Appearance::blend(palette.surface,
			palette.background, 120);
		foreground = palette.disabledText;
		border = Appearance::blend(palette.border, palette.surface, 90);
	}
	drawSurface(canvas, bounds, background, border);
	canvas.setTextColor(foreground);
	Rectangle textBounds(bounds.left() + 6, bounds.top() + 2,
		std::max(0L, bounds.width() - 12), std::max(0L, bounds.height() - 4));
	const auto imageWidth = drawButtonImage(window, canvas, textBounds,
		!text.empty(), disabled, pressed, data.hot);
	if(imageWidth && !text.empty()) {
		textBounds.pos.x += imageWidth + 6;
		textBounds.size.x = std::max(0L, textBounds.size.x - imageWidth - 6);
	}
	if(pressed) {
		textBounds.pos += Point(1, 1);
	}
	if(commandLink && !text.empty()) {
		auto bold = font.makeBold();
		Rectangle titleBounds(textBounds);
		{
			auto selectBold = canvas.select(*bold);
			const auto extent = canvas.getTextExtent(text);
			titleBounds.size.y = std::min(titleBounds.height(), extent.y + 3);
			unsigned format = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS;
			if(::GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_RTLREADING) {
				format |= DT_RTLREADING;
			}
			canvas.drawText(text, titleBounds, format);
		}
		const auto note = buttonNote(window);
		if(!note.empty()) {
			Rectangle noteBounds(textBounds.left(), titleBounds.bottom(),
				textBounds.width(), std::max(0L,
					textBounds.bottom() - titleBounds.bottom()));
			unsigned format = DT_LEFT | DT_TOP | DT_WORDBREAK |
				DT_END_ELLIPSIS | DT_NOPREFIX;
			if(::GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_RTLREADING) {
				format |= DT_RTLREADING;
			}
			canvas.drawText(note, noteBounds, format);
		}
	} else if(!text.empty()) {
		unsigned format = textFormat(style, (style & BS_MULTILINE) != 0);
		if(::GetWindowLongPtr(window, GWL_EXSTYLE) & WS_EX_RTLREADING) {
			format |= DT_RTLREADING;
		}
		canvas.drawText(text, textBounds, format);
	}
	if(focused && !disabled && !(::SendMessage(window,
		WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
		drawFocus(canvas, Rectangle(bounds.left() + 3, bounds.top() + 3,
			std::max(0L, bounds.width() - 6),
			std::max(0L, bounds.height() - 6)), foreground);
	}
}

void drawComboBox(HWND window, Canvas& canvas, NativeWindowData& data) {
	RECT nativeBounds = { 0 };
	::GetClientRect(window, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	const auto& palette = data.appearance->palette;
	const bool disabled = ::IsWindowEnabled(window) == FALSE;
	const long buttonWidth = std::min<long>(::GetSystemMetrics(SM_CXVSCROLL),
		std::max(0L, bounds.width() / 2));
	const bool dropped = ::SendMessage(window, CB_GETDROPPEDSTATE, 0, 0) != 0;
	auto button = dropped || data.hot ? Appearance::blend(palette.surface,
		palette.accent, dropped ? 48 : 22) : palette.surface;
	drawSurface(canvas, bounds, palette.surface, palette.border);
	Rectangle arrow(bounds.right() - buttonWidth, bounds.top(), buttonWidth,
		bounds.height());
	drawSurface(canvas, arrow, button, palette.border);
	drawArrow(canvas, arrow, true, true,
		disabled ? palette.disabledText : palette.text);

	const DWORD style = static_cast<DWORD>(
		::GetWindowLongPtr(window, GWL_STYLE));
	if((style & CBS_DROPDOWNLIST) == CBS_DROPDOWNLIST) {
		const auto selected = static_cast<int>(
			::SendMessage(window, CB_GETCURSEL, 0, 0));
		if(selected != CB_ERR) {
			const auto length = static_cast<int>(
				::SendMessage(window, CB_GETLBTEXTLEN, selected, 0));
			std::vector<TCHAR> value(static_cast<size_t>(std::max(0, length)) + 1);
			::SendMessage(window, CB_GETLBTEXT, selected,
				reinterpret_cast<LPARAM>(value.data()));
			auto font = windowFont(window);
			auto selectFont = canvas.select(font);
			auto transparent = canvas.setBkMode(true);
			canvas.setTextColor(disabled ? palette.disabledText : palette.text);
			Rectangle textBounds(bounds.left() + 4, bounds.top(),
				std::max(0L, arrow.left() - bounds.left() - 7), bounds.height());
			canvas.drawText(value.data(), textBounds,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
		}
	}
}

void drawHeader(HWND window, Canvas& canvas, NativeWindowData& data) {
	RECT nativeBounds = { 0 };
	::GetClientRect(window, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	const auto& palette = data.appearance->palette;
	Brush background(palette.surface);
	canvas.fill(bounds, background);
	auto font = windowFont(window);
	auto selectFont = canvas.select(font);
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(palette.text);
	const auto count = static_cast<int>(
		::SendMessage(window, HDM_GETITEMCOUNT, 0, 0));
	for(int index = 0; index < count; ++index) {
		RECT nativeItem = { 0 };
		if(!::SendMessage(window, HDM_GETITEMRECT, index,
			reinterpret_cast<LPARAM>(&nativeItem))) {
			continue;
		}
		const Rectangle item(nativeItem);
		drawSurface(canvas, item, data.hot ? Appearance::blend(
			palette.surface, palette.accent, 12) : palette.surface,
			palette.border);
		std::vector<TCHAR> value(512);
		HDITEM header = { HDI_TEXT | HDI_FORMAT };
		header.pszText = value.data();
		header.cchTextMax = static_cast<int>(value.size());
		if(!::SendMessage(window, HDM_GETITEM, index,
			reinterpret_cast<LPARAM>(&header))) {
			continue;
		}
		Rectangle textBounds(item.left() + 5, item.top(),
			std::max(0L, item.width() - 10), item.height());
		if(header.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
			const long arrowWidth = std::min<long>(14, textBounds.width());
			Rectangle arrow(textBounds.right() - arrowWidth, textBounds.top(),
				arrowWidth, textBounds.height());
			drawArrow(canvas, arrow, true,
				(header.fmt & HDF_SORTDOWN) != 0, palette.text);
			textBounds.size.x = std::max(0L,
				textBounds.width() - arrowWidth - 2);
		}
		unsigned format = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
		format |= (header.fmt & HDF_RIGHT) ? DT_RIGHT :
			(header.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
		canvas.drawText(value.data(), textBounds, format);
	}
}

void drawProgressBar(HWND window, Canvas& canvas, NativeWindowData& data) {
	RECT nativeBounds = { 0 };
	::GetClientRect(window, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	const auto& palette = data.appearance->palette;
	drawSurface(canvas, bounds, palette.surface, palette.border);
	if(::GetWindowLongPtr(window, GWL_STYLE) & PBS_MARQUEE) {
		const long track = std::max(0L, bounds.width() - 4);
		const long chunk = std::max(1L, track / 3);
		const auto cycle = static_cast<unsigned long>(track + chunk);
		const long start = cycle ? static_cast<long>((::GetTickCount() / 12) %
			cycle) - chunk : 0;
		const long left = std::max(0L, start);
		const long right = std::min(track, start + chunk);
		if(right > left) {
			Brush accent(palette.accent);
			canvas.fill(Rectangle(bounds.left() + 2 + left,
				bounds.top() + 2, right - left,
				std::max(0L, bounds.height() - 4)), accent);
		}
		return;
	}
	PBRANGE range = { 0, 100 };
	::SendMessage(window, PBM_GETRANGE, FALSE,
		reinterpret_cast<LPARAM>(&range));
	const auto position = static_cast<int>(
		::SendMessage(window, PBM_GETPOS, 0, 0));
	const auto span = std::max(1, range.iHigh - range.iLow);
	const auto completed = std::max(0, std::min(span, position - range.iLow));
	Rectangle value(bounds.left() + 2, bounds.top() + 2,
		std::max(0L, (bounds.width() - 4) * completed / span),
		std::max(0L, bounds.height() - 4));
	if(value.width() > 0) {
		Brush accent(palette.accent);
		canvas.fill(value, accent);
	}
}

void drawScrollBar(HWND window, Canvas& canvas, NativeWindowData& data) {
	RECT nativeBounds = { 0 };
	::GetClientRect(window, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	const auto& palette = data.appearance->palette;
	Brush surface(palette.surface);
	canvas.fill(bounds, surface);
	const bool vertical = bounds.height() > bounds.width();
	const long extent = vertical ? bounds.height() : bounds.width();
	const long cross = vertical ? bounds.width() : bounds.height();
	const long line = std::min(extent / 2, cross);
	Rectangle decrement(bounds);
	Rectangle increment(bounds);
	if(vertical) {
		decrement.size.y = line;
		increment.pos.y = bounds.bottom() - line;
		increment.size.y = line;
	} else {
		decrement.size.x = line;
		increment.pos.x = bounds.right() - line;
		increment.size.x = line;
	}
	drawSurface(canvas, decrement, palette.surface, palette.border);
	drawSurface(canvas, increment, palette.surface, palette.border);
	drawArrow(canvas, decrement, vertical, false,
		::IsWindowEnabled(window) ? palette.text : palette.disabledText);
	drawArrow(canvas, increment, vertical, true,
		::IsWindowEnabled(window) ? palette.text : palette.disabledText);

	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
	if(!::GetScrollInfo(window, SB_CTL, &info) || info.nMax <= info.nMin) {
		return;
	}
	const long trackLength = std::max(0L, extent - line * 2);
	const auto range = static_cast<unsigned long long>(
		info.nMax - info.nMin) + 1;
	const long thumbLength = std::max(cross / 2,
		static_cast<long>(trackLength * std::min<unsigned long long>(
			info.nPage ? info.nPage : 1, range) / range));
	const auto denominator = std::max<long long>(1,
		static_cast<long long>(info.nMax) - info.nMin -
		static_cast<long long>(info.nPage ? info.nPage - 1 : 0));
	const long thumbOffset = static_cast<long>((trackLength - thumbLength) *
		(static_cast<long long>(info.nPos) - info.nMin) / denominator);
	Rectangle thumb(bounds);
	if(vertical) {
		thumb.pos.y = bounds.top() + line + thumbOffset;
		thumb.size.y = thumbLength;
	} else {
		thumb.pos.x = bounds.left() + line + thumbOffset;
		thumb.size.x = thumbLength;
	}
	drawSurface(canvas, thumb, Appearance::blend(palette.surface,
		palette.text, 42), palette.border);
}

void drawStatic(HWND window, Canvas& canvas, NativeWindowData& data) {
	const auto style = static_cast<DWORD>(
		::GetWindowLongPtr(window, GWL_STYLE)) & SS_TYPEMASK;
	if(style != SS_ETCHEDHORZ && style != SS_ETCHEDVERT &&
		style != SS_ETCHEDFRAME) {
		return;
	}
	RECT nativeBounds = { 0 };
	::GetClientRect(window, &nativeBounds);
	const Rectangle bounds(nativeBounds);
	Brush background(data.appearance->palette.background);
	canvas.fill(bounds, background);
	Pen border(data.appearance->palette.border, Pen::Solid, 1);
	auto selectPen = canvas.select(border);
	if(style == SS_ETCHEDHORZ) {
		canvas.line(Point(bounds.left(), bounds.top() + bounds.height() / 2),
			Point(bounds.right(), bounds.top() + bounds.height() / 2));
	} else if(style == SS_ETCHEDVERT) {
		canvas.line(Point(bounds.left() + bounds.width() / 2, bounds.top()),
			Point(bounds.left() + bounds.width() / 2, bounds.bottom()));
	} else {
		canvas.line(Rectangle(bounds.left(), bounds.top(),
			std::max(0L, bounds.width() - 1),
			std::max(0L, bounds.height() - 1)));
	}
}

bool drawsClient(const NativeWindowData& data, HWND window) {
	if(data.control == NativeControl::Dialog) {
		return !data.appearance->preserveDialogClientPainting;
	}
	if(data.control == NativeControl::Button) {
		const auto type = static_cast<DWORD>(
			::GetWindowLongPtr(window, GWL_STYLE)) & BS_TYPEMASK;
		return type != BS_OWNERDRAW && type != BS_USERBUTTON;
	}
	if(data.control == NativeControl::ComboBox) {
		const auto style = static_cast<DWORD>(
			::GetWindowLongPtr(window, GWL_STYLE));
		return !(style & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE));
	}
	if(data.control == NativeControl::Header) {
		const auto count = static_cast<int>(
			::SendMessage(window, HDM_GETITEMCOUNT, 0, 0));
		for(int index = 0; index < count; ++index) {
			HDITEM item = { HDI_FORMAT };
			if(::SendMessage(window, HDM_GETITEM, index,
				reinterpret_cast<LPARAM>(&item)) &&
				(item.fmt & HDF_OWNERDRAW)) {
				return false;
			}
		}
		return true;
	}
	if(data.control == NativeControl::ProgressBar ||
		data.control == NativeControl::ScrollBar) {
		return true;
	}
	if(data.control == NativeControl::Static) {
		const auto style = static_cast<DWORD>(
			::GetWindowLongPtr(window, GWL_STYLE)) & SS_TYPEMASK;
		return style == SS_ETCHEDHORZ || style == SS_ETCHEDVERT ||
			style == SS_ETCHEDFRAME;
	}
	return false;
}

void drawClient(HWND window, Canvas& canvas, NativeWindowData& data) {
	switch(data.control) {
	case NativeControl::Dialog:
		{
			RECT nativeBounds = { 0 };
			::GetClientRect(window, &nativeBounds);
			canvas.fill(Rectangle(nativeBounds), data.appearance->background);
		}
		break;
	case NativeControl::Button: drawButton(window, canvas, data); break;
	case NativeControl::ComboBox: drawComboBox(window, canvas, data); break;
	case NativeControl::Header: drawHeader(window, canvas, data); break;
	case NativeControl::ProgressBar: drawProgressBar(window, canvas, data); break;
	case NativeControl::ScrollBar: drawScrollBar(window, canvas, data); break;
	case NativeControl::Static: drawStatic(window, canvas, data); break;
	default: break;
	}
}

LRESULT CALLBACK nativeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData);

NativeWindowData* getWindowData(HWND window) {
	DWORD_PTR value = 0;
	if(!::GetWindowSubclass(window, nativeWindowProc,
		nativeDialogSubclassId, &value)) {
		return nullptr;
	}
	return reinterpret_cast<NativeWindowData*>(value);
}

void applyWindow(HWND window, SharedAppearance* appearance);

BOOL CALLBACK applyChild(HWND window, LPARAM data) {
	applyWindow(window, reinterpret_cast<SharedAppearance*>(data));
	return TRUE;
}

void applyTree(HWND window, SharedAppearance* appearance) {
	applyWindow(window, appearance);
	::EnumChildWindows(window, applyChild,
		reinterpret_cast<LPARAM>(appearance));
	::RedrawWindow(window, nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

SharedAppearance* inheritedAppearance(HWND window) {
	for(auto current = ::GetParent(window); current;
		current = ::GetParent(current)) {
		if(auto data = getWindowData(current)) {
			return data->appearance;
		}
	}
	for(auto current = ::GetWindow(window, GW_OWNER); current;
		current = ::GetWindow(current, GW_OWNER)) {
		if(auto data = getWindowData(current)) {
			return data->appearance;
		}
	}
	return nullptr;
}

void configureNativeControl(HWND window, NativeWindowData& data) {
	const auto control = data.control;
	auto& appearance = *data.appearance;
	const auto& palette = appearance.palette;
	switch(control) {
	case NativeControl::Button:
	case NativeControl::ComboBox:
	case NativeControl::Edit:
	case NativeControl::Header:
	case NativeControl::Link:
	case NativeControl::ListBox:
	case NativeControl::ListView:
	case NativeControl::ProgressBar:
	case NativeControl::ScrollBar:
	case NativeControl::Static:
	case NativeControl::TreeView:
		/* An empty visual-style class suppresses palette-unaware themed paint.
		 * Each affected standard control is then painted here or through its
		 * documented color messages. This is available on Windows 7 and does not
		 * select a version-specific dark theme. */
		::SetWindowTheme(window, L"", L"");
		break;
	case NativeControl::Dialog:
		/* A preserved common dialog owns its client theme. Disabling the root
		 * theme also lets USER32 restore a classic caption button after Shell has
		 * completed its initialization. The non-client frame is painted below. */
		if(!appearance.preserveDialogClientPainting) {
			::SetWindowTheme(window, L"", L"");
		}
		break;
	default:
		break;
	}

	switch(control) {
	case NativeControl::Edit:
		::SendMessage(window, EM_SETBKGNDCOLOR, 0, palette.surface);
		break;
	case NativeControl::ListView:
		ListView_SetBkColor(window, palette.background);
		ListView_SetTextBkColor(window, palette.background);
		ListView_SetTextColor(window, palette.text);
		break;
	case NativeControl::TreeView:
		TreeView_SetBkColor(window, palette.background);
		TreeView_SetTextColor(window, palette.text);
		TreeView_SetLineColor(window, palette.border);
		break;
	case NativeControl::ProgressBar:
		::SendMessage(window, PBM_SETBKCOLOR, 0, palette.surface);
		::SendMessage(window, PBM_SETBARCOLOR, 0, palette.accent);
		break;
	case NativeControl::Dialog:
		data.captionActive = true;
		data.manualNonClient = true;
		setNonClientRendering(window, true);
		::SetWindowPos(window, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
			SWP_FRAMECHANGED);
		break;
	default:
		break;
	}
}

void applyWindow(HWND window, SharedAppearance* appearance) {
	if(!window || !::IsWindow(window) || getWindowData(window)) {
		return;
	}
	const auto control = classify(window);
	/* Shell file dialogs create tiny SCROLLBAR helper windows as part of their
	 * DirectUI layout. They are not standalone scroll bars and custom division
	 * into arrow, track and thumb rectangles corrupts the lower-right corner. */
	if(appearance->preserveDialogClientPainting && control == NativeControl::ScrollBar) {
		return;
	}
	if(control == NativeControl::Unknown) {
		return;
	}
	auto data = new NativeWindowData(appearance, control);
	if(!::SetWindowSubclass(window, nativeWindowProc,
		nativeDialogSubclassId, reinterpret_cast<DWORD_PTR>(data))) {
		delete data;
		return;
	}
	configureNativeControl(window, *data);
}

LRESULT colorControl(NativeWindowData& data, UINT message, HDC canvas) {
	const auto& palette = data.appearance->palette;
	::SetTextColor(canvas, palette.text);
	if(message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN ||
		message == WM_CTLCOLORDLG) {
		::SetBkMode(canvas, TRANSPARENT);
		::SetBkColor(canvas, palette.background);
		return reinterpret_cast<LRESULT>(data.appearance->background.handle());
	}
	::SetBkMode(canvas, OPAQUE);
	::SetBkColor(canvas, palette.surface);
	return reinterpret_cast<LRESULT>(data.appearance->surface.handle());
}

LRESULT drawLink(NMCUSTOMDRAW& draw, NativeWindowData& data) {
	if(draw.dwDrawStage == CDDS_PREPAINT) {
		return CDRF_NOTIFYITEMDRAW;
	}
	if(draw.dwDrawStage != CDDS_ITEMPREPAINT) {
		return CDRF_DODEFAULT;
	}

	const auto& palette = data.appearance->palette;
	auto foreground = palette.accent;
	if(!::IsWindowEnabled(draw.hdr.hwndFrom) ||
		(draw.uItemState & CDIS_DISABLED)) {
		foreground = palette.disabledText;
	} else if(draw.uItemState & (CDIS_HOT | CDIS_SELECTED)) {
		foreground = Appearance::blend(
			foreground, palette.highlightText, 64);
	}
	::SetTextColor(draw.hdc, foreground);
	::SetBkColor(draw.hdc, palette.background);
	::SetBkMode(draw.hdc, TRANSPARENT);
	return CDRF_NEWFONT;
}

bool handleCaptionInput(HWND window, NativeWindowData& data, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	switch(message) {
	case WM_NCMOUSEMOVE:
		{
			const auto layout = getCaptionLayout(window);
			const int hitTest = reconcileCaptionHitTest(layout,
				Point::fromLParam(lParam), static_cast<int>(wParam));
			const auto button = captionButton(hitTest) ?
				findCaptionButton(layout, hitTest) : nullptr;
			const int hot = button && !button->disabled ?
				button->hitTest : HTNOWHERE;
			if(!data.trackingCaptionMouse) {
				TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT),
					TME_LEAVE | TME_NONCLIENT, window, HOVER_DEFAULT };
				data.trackingCaptionMouse =
					::TrackMouseEvent(&tracking) != FALSE;
			}
			result = ::DefSubclassProc(window, message, hitTest, lParam);
			if(data.hotCaptionButton != hot) {
				data.hotCaptionButton = hot;
				paintNonClient(window, data);
			} else if(button) {
				/* USER32 may repaint its caption button while maintaining native
				 * tooltip state. Restore the palette pixels immediately. */
				paintNonClient(window, data);
			}
			return true;
		}

	case WM_NCMOUSELEAVE:
		data.trackingCaptionMouse = false;
		data.hotCaptionButton = HTNOWHERE;
		result = ::DefSubclassProc(window, message, wParam, lParam);
		paintNonClient(window, data);
		return true;

	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONDBLCLK:
		{
			const auto layout = getCaptionLayout(window);
			const int hitTest = reconcileCaptionHitTest(layout,
				Point::fromLParam(lParam), static_cast<int>(wParam));
			const auto button = captionButton(hitTest) ?
				findCaptionButton(layout, hitTest) : nullptr;
			if(!button || button->disabled) {
				return false;
			}
			data.pressedCaptionButton = data.hotCaptionButton = button->hitTest;
			::SetCapture(window);
			paintNonClient(window, data);
			result = 0;
			return true;
		}

	case WM_MOUSEMOVE:
	case WM_NCLBUTTONUP:
	case WM_LBUTTONUP:
		if(data.pressedCaptionButton != HTNOWHERE) {
			POINT cursor = { 0 };
			const auto layout = getCaptionLayout(window);
			const auto button = ::GetCursorPos(&cursor) ?
				findCaptionButtonAt(layout, Point(cursor)) : nullptr;
			const int hit = button ? button->hitTest : HTNOWHERE;
			const int hot = hit == data.pressedCaptionButton ? hit : HTNOWHERE;
			if(message == WM_MOUSEMOVE) {
				if(data.hotCaptionButton != hot) {
					data.hotCaptionButton = hot;
					paintNonClient(window, data);
				}
			} else {
				const int pressed = data.pressedCaptionButton;
				data.pressedCaptionButton = HTNOWHERE;
				data.hotCaptionButton = hot;
				if(::GetCapture() == window) {
					::ReleaseCapture();
				}
				paintNonClient(window, data);
				if(hit == pressed) {
					const auto command = captionCommand(window, pressed);
					if(command) {
						::PostMessage(window, WM_SYSCOMMAND, command, 0);
					}
				}
			}
			result = 0;
			return true;
		}
		break;

	case WM_CANCELMODE:
	case WM_CAPTURECHANGED:
		if(data.pressedCaptionButton != HTNOWHERE) {
			data.pressedCaptionButton = HTNOWHERE;
			data.hotCaptionButton = HTNOWHERE;
			if(message == WM_CANCELMODE && ::GetCapture() == window) {
				::ReleaseCapture();
			}
			paintNonClient(window, data);
		}
		break;
	}
	return false;
}

LRESULT CALLBACK nativeWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR refData)
{
	auto data = reinterpret_cast<NativeWindowData*>(refData);
	if(!data) {
		return ::DefSubclassProc(window, message, wParam, lParam);
	}
	if(data->control == NativeControl::Dialog) {
		LRESULT result = 0;
		if(handleCaptionInput(window, *data, message, wParam, lParam, result)) {
			return result;
		}
	}

	switch(message) {
	case WM_NCHITTEST:
		if(data->control == NativeControl::Dialog) {
			const auto nativeResult = ::DefSubclassProc(
				window, message, wParam, lParam);
			const auto layout = getCaptionLayout(window);
			return reconcileCaptionHitTest(layout, Point::fromLParam(lParam),
				static_cast<int>(nativeResult));
		}
		break;

	case WM_NCACTIVATE:
		if(data->control == NativeControl::Dialog) {
			data->captionActive = wParam != FALSE;
			/* -1 updates USER32's activation state without asking it to repaint
			 * the frame over DWT's palette-rendered caption. */
			const auto result = ::DefSubclassProc(window, message, wParam, static_cast<LPARAM>(-1));
			paintNonClient(window, *data);
			return result;
		}
		break;

	case WM_NCPAINT:
		if(data->control == NativeControl::Dialog) {
			paintNonClient(window, *data);
			return 0;
		}
		break;

	case WM_ACTIVATE:
		if(data->control == NativeControl::Dialog) {
			data->captionActive = LOWORD(wParam) != WA_INACTIVE;
			const auto result = ::DefSubclassProc(window, message, wParam, lParam);
			paintNonClient(window, *data);
			return result;
		}
		break;

	case WM_THEMECHANGED:
	case WM_DWMCOMPOSITIONCHANGED:
		if(data->control == NativeControl::Dialog) {
			const auto result = ::DefSubclassProc(window, message, wParam, lParam);
			setNonClientRendering(window, true);
			redrawNonClient(window);
			return result;
		}
		break;

	case WM_SIZE:
	case WM_STYLECHANGED:
	case WM_WINDOWPOSCHANGED:
	case WM_DPICHANGED:
		if(data->control == NativeControl::Dialog) {
			const auto result = ::DefSubclassProc(window, message, wParam, lParam);
			redrawNonClient(window);
			return result;
		}
		break;

	case WM_CTLCOLORBTN:
	case WM_CTLCOLORDLG:
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORMSGBOX:
	case WM_CTLCOLORSCROLLBAR:
	case WM_CTLCOLORSTATIC:
		return colorControl(*data, message, reinterpret_cast<HDC>(wParam));

	case WM_ERASEBKGND:
		if(data->control == NativeControl::Dialog &&
			!data->appearance->preserveDialogClientPainting) {
			RECT bounds = { 0 };
			::GetClientRect(window, &bounds);
			FreeCanvas canvas(reinterpret_cast<HDC>(wParam));
			canvas.fill(Rectangle(bounds), data->appearance->background);
			return 1;
		}
		break;

	case WM_NOTIFY:
		if(lParam) {
			auto header = reinterpret_cast<NMHDR*>(lParam);
			const auto childWindow = header->hwndFrom;
			auto child = getWindowData(childWindow);
			if(child && child->control == NativeControl::Link &&
				header->code == NM_CUSTOMDRAW) {
				const auto nativeResult = ::DefSubclassProc(
					window, message, wParam, lParam);
				if(nativeResult && nativeResult != CDRF_DODEFAULT) {
					return nativeResult;
				}
				child = getWindowData(childWindow);
				if(!child || child->control != NativeControl::Link) {
					return nativeResult;
				}
				return drawLink(*reinterpret_cast<NMCUSTOMDRAW*>(lParam),
					*child);
			}
		}
		break;

	case WM_PAINT:
		if(drawsClient(*data, window)) {
			PaintCanvas target(window);
			RECT nativeBounds = { 0 };
			::GetClientRect(window, &nativeBounds);
			const Rectangle bounds(nativeBounds);
			BufferedCanvas<FreeCanvas> canvas(target.handle(), bounds);
			drawClient(window, canvas, *data);
			canvas.blast(bounds);
			return 0;
		}
		break;

	case WM_PRINTCLIENT:
		if(drawsClient(*data, window) && wParam) {
			FreeCanvas canvas(reinterpret_cast<HDC>(wParam));
			drawClient(window, canvas, *data);
			return 0;
		}
		break;

	case WM_PARENTNOTIFY:
		if(LOWORD(wParam) == WM_CREATE && lParam) {
			applyTree(reinterpret_cast<HWND>(lParam), data->appearance);
		}
		break;

	case WM_MOUSEMOVE:
		if(drawsClient(*data, window) && !data->hot) {
			data->hot = true;
			TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT),
				TME_LEAVE, window, 0 };
			::TrackMouseEvent(&tracking);
			::InvalidateRect(window, nullptr, FALSE);
		}
		break;

	case WM_MOUSELEAVE:
		if(data->hot) {
			data->hot = false;
			::InvalidateRect(window, nullptr, FALSE);
		}
		break;

	case WM_TIMER:
		if(data->control == NativeControl::ProgressBar &&
			(::GetWindowLongPtr(window, GWL_STYLE) & PBS_MARQUEE)) {
			const auto result = ::DefSubclassProc(window, message, wParam, lParam);
			::InvalidateRect(window, nullptr, FALSE);
			return result;
		}
		break;

	case WM_ENABLE:
	case WM_SETFOCUS:
	case WM_KILLFOCUS:
	case WM_SETTEXT:
	case WM_SETFONT:
	case BM_SETCHECK:
	case BM_SETSTATE:
	case CB_SHOWDROPDOWN:
	case PBM_SETPOS:
	case PBM_SETRANGE:
	case PBM_SETRANGE32:
		{
			const auto result = ::DefSubclassProc(window, message, wParam, lParam);
			if(message == CB_SHOWDROPDOWN && data->control == NativeControl::ComboBox) {
				COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
				if(::GetComboBoxInfo(window, &info) && info.hwndList) {
					applyTree(info.hwndList, data->appearance);
				}
			}
			if(drawsClient(*data, window)) {
				::InvalidateRect(window, nullptr, FALSE);
			}
			if(data->control == NativeControl::Dialog &&
				(message == WM_SETTEXT || message == WM_ENABLE)) {
				redrawNonClient(window);
			}
			return result;
		}

	case WM_NCDESTROY:
		{
			if(data->manualNonClient) {
				setNonClientRendering(window, false);
			}
			const auto result = ::DefSubclassProc(window, message, wParam, lParam);
			::RemoveWindowSubclass(window, nativeWindowProc,
				nativeDialogSubclassId);
			delete data;
			return result;
		}
	}

	return ::DefSubclassProc(window, message, wParam, lParam);
}

}

class NativeDialogAppearance::Impl {
public:
	explicit Impl(const Appearance& appearance, bool preserveDialogClientPainting_) :
		enabled(appearance.isManual()), preserveDialogClientPainting(preserveDialogClientPainting_), hook(nullptr), previous(nullptr) {
		if(!enabled) {
			return;
		}
		palette = appearance.getPalette();
		previous = current;
		current = this;
		if(!previous) {
			hook = ::SetWindowsHookEx(WH_CBT, hookProc, nullptr,
				::GetCurrentThreadId());
			if(!hook) {
				current = previous;
				enabled = false;
			}
		}
	}

	~Impl() {
		if(!enabled) {
			return;
		}
		if(hook) {
			::UnhookWindowsHookEx(hook);
		}
		current = previous;
	}

	void apply(HWND window) const {
		if(!enabled || !window || !::IsWindow(window)) {
			return;
		}
		if(auto inherited = inheritedAppearance(window)) {
			applyTree(window, inherited);
			return;
		}
		auto shared = new SharedAppearance(palette, preserveDialogClientPainting);
		applyTree(window, shared);
		/* applyWindow owns every successful reference. No supported native dialog
		 * reaches here without a recognized root class. */
		if(!shared->references) {
			delete shared;
		}
	}

	void consider(HWND window) const {
		if(!enabled || !window || !::IsWindow(window)) {
			return;
		}
		if(auto inherited = inheritedAppearance(window)) {
			applyWindow(window, inherited);
			return;
		}
		if(classify(window) == NativeControl::Dialog &&
			!(::GetWindowLongPtr(window, GWL_STYLE) & WS_CHILD)) {
			apply(window);
		}
	}

	static LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam) {
		/* HCBT_CREATEWND is intentionally not used: a window has not processed
		 * WM_NCCREATE at that point, and subclassing or sending control messages
		 * there is unsafe. Activation occurs after complete construction; later
		 * child creation is observed through WM_PARENTNOTIFY. */
		if(code == HCBT_ACTIVATE) {
			if(current) {
				current->consider(reinterpret_cast<HWND>(wParam));
			}
		}
		return ::CallNextHookEx(nullptr, code, wParam, lParam);
	}

	bool enabled;
	bool preserveDialogClientPainting;
	Appearance::Palette palette;
	HHOOK hook;
	Impl* previous;
	static thread_local Impl* current;
};

thread_local NativeDialogAppearance::Impl* NativeDialogAppearance::Impl::current = nullptr;

NativeDialogAppearance::NativeDialogAppearance(const Appearance& appearance, bool preserveDialogClientPainting) :
	impl(new Impl(appearance, preserveDialogClientPainting))
{
}

NativeDialogAppearance::~NativeDialogAppearance() {
}

void NativeDialogAppearance::apply(HWND dialog) const {
	impl->apply(dialog);
}

bool NativeDialogAppearance::active() const {
	return impl->enabled;
}

} } }
