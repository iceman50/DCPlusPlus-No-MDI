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

#include <dwt/widgets/Frame.h>

#include <dwt/Appearance.h>
#include <dwt/CanvasClasses.h>
#include <dwt/LibraryLoader.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Font.h>
#include <dwt/resources/Pen.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace dwt {

namespace {

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

bool setNonClientRendering(HWND hwnd, bool disabled) {
	auto function = getDwmSetWindowAttribute();
	if(!function) {
		return false;
	}

	const int policy = disabled ? DWT_DWMNCRP_DISABLED :
		DWT_DWMNCRP_USEWINDOWSTYLE;
	return SUCCEEDED(function(hwnd, DWT_DWMWA_NCRENDERING_POLICY,
		&policy, sizeof(policy)));
}

bool isCaptionButton(int hitTest) {
	return hitTest == HTCLOSE || hitTest == HTMINBUTTON ||
		hitTest == HTMAXBUTTON || hitTest == HTHELP;
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

bool buttonVisible(DWORD state) {
	return (state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) == 0;
}

bool systemCommandEnabled(HWND hwnd, UINT command) {
	const auto menu = ::GetSystemMenu(hwnd, FALSE);
	if(!menu) {
		return true;
	}
	const auto state = ::GetMenuState(menu, command, MF_BYCOMMAND);
	return state == static_cast<UINT>(-1) ||
		(state & (MF_DISABLED | MF_GRAYED)) == 0;
}

CaptionLayout getCaptionLayout(const Frame& frame) {
	CaptionLayout result = { };
	const HWND hwnd = frame.handle();
	RECT window = { 0 };
	if(!::GetWindowRect(hwnd, &window)) {
		return result;
	}
	result.origin = Point(window.left, window.top);
	result.window = Rectangle(0, 0, window.right - window.left,
		window.bottom - window.top);

	const auto style = static_cast<DWORD>(
		::GetWindowLongPtr(hwnd, GWL_STYLE));
	const auto exStyle = static_cast<DWORD>(
		::GetWindowLongPtr(hwnd, GWL_EXSTYLE));
	result.rightToLeft = (exStyle & WS_EX_LAYOUTRTL) != 0;
	result.toolWindow = (exStyle & WS_EX_TOOLWINDOW) != 0;

	WINDOWINFO windowInfo = { sizeof(WINDOWINFO) };
	if(::GetWindowInfo(hwnd, &windowInfo)) {
		result.borderX = windowInfo.cxWindowBorders;
		result.borderY = windowInfo.cyWindowBorders;
	} else {
		result.borderX = (style & WS_THICKFRAME) ?
			frame.getSystemMetric(SM_CXFRAME) : frame.getSystemMetric(SM_CXBORDER);
		result.borderY = (style & WS_THICKFRAME) ?
			frame.getSystemMetric(SM_CYFRAME) : frame.getSystemMetric(SM_CYBORDER);
	}

	if((style & WS_CAPTION) != WS_CAPTION) {
		return result;
	}

	TITLEBARINFOEX title = { };
	title.cbSize = sizeof(title);
	::SendMessage(hwnd, WM_GETTITLEBARINFOEX, 0,
		reinterpret_cast<LPARAM>(&title));
	const auto reportedCaption = intersect(
		localRectangle(title.rcTitleBar, window), result.window);
	if(valid(reportedCaption)) {
		/* rcTitleBar deliberately excludes the window-menu area. Painting that
		 * rectangle verbatim leaves an unpainted strip between the frame and the
		 * application icon. Retain its vertical metrics, but extend the caption
		 * surface across the complete inner frame. */
		const long left = std::max(0L, std::min(result.borderX,
			result.window.width() / 2));
		result.caption = Rectangle(left, reportedCaption.top(),
			std::max(0L, result.window.width() - left * 2),
			reportedCaption.height());
	} else {
		const long height = frame.getSystemMetric(result.toolWindow ?
			SM_CYSMCAPTION : SM_CYCAPTION);
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
		auto bounds = intersect(localRectangle(title.rgrect[index], window),
			result.caption);
		if(valid(bounds) && buttonVisible(title.rgstate[index])) {
			result.buttons.push_back({ hitTests[i], bounds,
				(title.rgstate[index] & STATE_SYSTEM_UNAVAILABLE) != 0 });
		}
	}

	if(result.buttons.empty()) {
		const long width = std::max(1, frame.getSystemMetric(
			result.toolWindow ? SM_CXSMSIZE : SM_CXSIZE));
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
			addButton(HTCLOSE, !systemCommandEnabled(hwnd, SC_CLOSE));
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
		/* TITLEBARINFOEX reserves rgrect[1]; it does not describe the window
		 * menu or icon. Position the icon from documented system metrics. */
		const long width = frame.getSystemMetric(SM_CXSMICON);
		const long height = frame.getSystemMetric(SM_CYSMICON);
		const long margin = frame.scale(5);
		const long left = result.rightToLeft ?
			result.caption.right() - margin - width :
			result.caption.left() + margin;
		result.icon = intersect(Rectangle(left,
			result.caption.top() + (result.caption.height() - height) / 2,
			width, height), result.caption);
	}
	return result;
}

const CaptionButton* findButtonByHitTest(const CaptionLayout& layout, int hitTest)
{
	for(const auto& button: layout.buttons) {
		if(button.hitTest == hitTest) {
			return &button;
		}
	}
	return nullptr;
}

Point windowPoint(const CaptionLayout& layout, const Point& screenPoint) {
	return Point(screenPoint.x - layout.origin.x,
		screenPoint.y - layout.origin.y);
}

const CaptionButton* findButtonAt(const CaptionLayout& layout, const Point& screenPoint)
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
	if(const auto button = findButtonAt(layout, screenPoint)) {
		return button->hitTest;
	}

	/* USER32 may retain DWM-sized button hit rectangles after an application
	 * disables native non-client rendering. Do not let those stale rectangles
	 * select a button that DWT paints elsewhere. Resize borders and every other
	 * native non-client result remain untouched. */
	if(isCaptionButton(nativeHitTest)) {
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

UINT captionCommand(HWND hwnd, int hitTest) {
	switch(hitTest) {
	case HTCLOSE:
		return SC_CLOSE;
	case HTMINBUTTON:
		return SC_MINIMIZE;
	case HTMAXBUTTON:
		return ::IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE;
	case HTHELP:
		return SC_CONTEXTHELP;
	default:
		return 0;
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
			Rectangle text = bounds;
			canvas.setTextColor(color);
			auto transparent = canvas.setBkMode(true);
			canvas.drawText(_T("?"), text, DT_CENTER | DT_VCENTER |
				DT_SINGLELINE | DT_NOPREFIX);
		}
		break;
	}
}

}

Frame::Frame(Widget* parent, Dispatcher& dispatcher) :
	Composite(parent, dispatcher), smallIcon(0), largeIcon(0),
	hotCaptionButton(HTNOWHERE), pressedCaptionButton(HTNOWHERE),
	captionActive(false), trackingCaptionMouse(false),
	manualNonClient(false)
{
	addCallback(Message(WM_CREATE),
		[this](const MSG&, LRESULT&) -> bool {
			captionActive = ::GetActiveWindow() == handle();
			updateCaptionAppearance();
			return false;
		});
	addCallback(Message(WM_ERASEBKGND),
		[this](const MSG& msg, LRESULT& ret) -> bool {
			if(!handleEraseBackground(reinterpret_cast<HDC>(msg.wParam))) {
				return false;
			}
			ret = TRUE;
			return true;
		});
}

Frame::~Frame() {
	if(handle() && ::IsWindow(handle()) && manualNonClient) {
		setNonClientRendering(handle(), false);
	}
}

void Frame::appearanceChanged() {
	BaseType::appearanceChanged();
	updateCaptionAppearance();
	if(handle() && ::IsWindow(handle())) {
		redrawWindow(RDW_INVALIDATE | RDW_FRAME);
	}
}

bool Frame::handleMessage(const MSG& msg, LRESULT& retVal) {
	bool handled = BaseType::handleMessage(msg, retVal);

	if(msg.message == WM_NCACTIVATE) {
		captionActive = msg.wParam != FALSE;
	} else if(msg.message == WM_ACTIVATE) {
		captionActive = LOWORD(msg.wParam) != WA_INACTIVE;
	}

	if(!isManualFrame()) {
		return handled;
	}

	if(!handled && handleCaptionInput(msg, retVal)) {
		return true;
	}

	switch(msg.message) {
	case WM_NCHITTEST:
		{
			if(!handled) {
				retVal = getDispatcher().chain(msg);
			}
			const auto layout = getCaptionLayout(*this);
			retVal = reconcileCaptionHitTest(layout, Point::fromMSG(msg),
				static_cast<int>(retVal));
			return true;
		}

	case WM_NCPAINT:
	case WM_NCACTIVATE:
		if(!handled) {
			retVal = getDispatcher().chain(msg);
		}
		paintNonClient();
		return true;

	case WM_SETTEXT:
	case WM_SETICON:
		if(!handled) {
			retVal = getDispatcher().chain(msg);
		}
		/* Title and icon are commonly updated together (for example while a
		 * property sheet changes pages). Invalidate the non-client area so those
		 * updates coalesce into one buffered caption paint. */
		redrawNonClient();
		return true;

	case WM_ENABLE:
	case WM_SIZE:
	case WM_STYLECHANGED:
	case WM_WINDOWPOSCHANGED:
	case WM_DPICHANGED:
		redrawNonClient();
		break;
	}

	return handled;
}

bool Frame::handleEraseBackground(HDC dc) {
	if(!dc || !isManualAppearance() ||
		getAppearancePolicy() == AppearancePolicy::ApplicationContent ||
		hasExplicitColors()) {
		return false;
	}

	FreeCanvas canvas(dc);
	canvas.fill(Rectangle(getClientSize()), *getAppearance().getBackgroundBrush());
	return true;
}

bool Frame::handleCaptionInput(const MSG& msg, LRESULT& retVal) {
	switch(msg.message) {
	case WM_NCMOUSEMOVE:
		{
			const auto layout = getCaptionLayout(*this);
			const int hitTest = reconcileCaptionHitTest(layout,
				Point::fromMSG(msg), static_cast<int>(msg.wParam));
			const auto button = isCaptionButton(hitTest) ?
				findButtonByHitTest(layout, hitTest) : nullptr;
			const int hot = button && !button->disabled ?
				button->hitTest : HTNOWHERE;
			if(!trackingCaptionMouse) {
				TRACKMOUSEEVENT tracking = { sizeof(TRACKMOUSEEVENT),
					TME_LEAVE | TME_NONCLIENT, handle(), HOVER_DEFAULT };
				trackingCaptionMouse = ::TrackMouseEvent(&tracking) != FALSE;
			}
			// Let USER32 retain its native tooltip and accessibility bookkeeping,
			// then replace any pixels it drew with the DWT palette.
			MSG nativeMessage = msg;
			nativeMessage.wParam = hitTest;
			retVal = getDispatcher().chain(nativeMessage);
			if(hotCaptionButton != hot) {
				hotCaptionButton = hot;
				paintNonClient();
			} else if(button) {
				paintNonClient();
			}
			return true;
		}

	case WM_NCMOUSELEAVE:
		trackingCaptionMouse = false;
		hotCaptionButton = HTNOWHERE;
		retVal = getDispatcher().chain(msg);
		paintNonClient();
		return true;

	case WM_NCLBUTTONDOWN:
	case WM_NCLBUTTONDBLCLK:
		{
			const auto layout = getCaptionLayout(*this);
			const int hitTest = reconcileCaptionHitTest(layout,
				Point::fromMSG(msg), static_cast<int>(msg.wParam));
			const auto button = isCaptionButton(hitTest) ?
				findButtonByHitTest(layout, hitTest) : nullptr;
			if(!button || button->disabled) {
				return false;
			}
			pressedCaptionButton = hotCaptionButton = button->hitTest;
			::SetCapture(handle());
			paintNonClient();
			retVal = 0;
			return true;
		}

	case WM_MOUSEMOVE:
	case WM_NCLBUTTONUP:
	case WM_LBUTTONUP:
		if(pressedCaptionButton != HTNOWHERE) {
			POINT cursor = { 0 };
			const auto layout = getCaptionLayout(*this);
			const auto button = ::GetCursorPos(&cursor) ?
				findButtonAt(layout, Point(cursor)) : nullptr;
			const int hit = button ? button->hitTest : HTNOWHERE;
			const int hot = hit == pressedCaptionButton ? hit : HTNOWHERE;
			if(msg.message == WM_MOUSEMOVE) {
				if(hotCaptionButton != hot) {
					hotCaptionButton = hot;
					paintNonClient();
				}
			} else {
				const int pressed = pressedCaptionButton;
				pressedCaptionButton = HTNOWHERE;
				hotCaptionButton = hot;
				if(::GetCapture() == handle()) {
					::ReleaseCapture();
				}
				paintNonClient();
				if(hit == pressed) {
					const auto command = captionCommand(handle(), pressed);
					if(command) {
						::PostMessage(handle(), WM_SYSCOMMAND, command, 0);
					}
				}
			}
			retVal = 0;
			return true;
		}
		break;

	case WM_CANCELMODE:
	case WM_CAPTURECHANGED:
		if(pressedCaptionButton != HTNOWHERE) {
			pressedCaptionButton = HTNOWHERE;
			hotCaptionButton = HTNOWHERE;
			if(msg.message == WM_CANCELMODE && ::GetCapture() == handle()) {
				::ReleaseCapture();
			}
			paintNonClient();
		}
		break;
	}
	return false;
}

bool Frame::isManualFrame() const {
	return handle() && ::IsWindow(handle()) && isManualAppearance();
}

void Frame::paintNonClient() {
	if(!isManualFrame() || !::IsWindowVisible(handle())) {
		return;
	}

	const auto layout = getCaptionLayout(*this);
	if(!valid(layout.window)) {
		return;
	}
	WindowUpdateCanvas target(this);
	if(!target.handle()) {
		return;
	}

	const auto& colors = getAppearance().getPalette();
	const COLORREF captionColor = captionActive ? colors.surface :
		Appearance::blend(colors.background, colors.surface, 96);
	const COLORREF borderColor = captionActive ? colors.border :
		Appearance::blend(colors.background, colors.border, 112);
	Brush borderBrush(borderColor);
	const long borderX = std::max(0L, std::min(layout.borderX,
		layout.window.width() / 2));
	const long borderY = std::max(0L, std::min(layout.borderY,
		layout.window.height() / 2));
	if(borderX) {
		target.fill(Rectangle(0, 0, borderX, layout.window.height()),
			borderBrush);
		target.fill(Rectangle(layout.window.right() - borderX, 0, borderX,
			layout.window.height()), borderBrush);
	}
	if(borderY) {
		target.fill(Rectangle(0, 0, layout.window.width(), borderY),
			borderBrush);
		target.fill(Rectangle(0, layout.window.bottom() - borderY,
			layout.window.width(), borderY), borderBrush);
	}

	if(!valid(layout.caption)) {
		return;
	}
	BufferedCanvas<FreeCanvas> canvas(target.handle(), layout.caption);
	Brush captionBrush(captionColor);
	canvas.fill(layout.caption, captionBrush);

	NONCLIENTMETRICS metrics = { };
	metrics.cbSize = sizeof(metrics);
	FontPtr font;
	if(getSystemParameters(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
		&metrics)) {
		font = new Font(layout.toolWindow ? metrics.lfSmCaptionFont :
			metrics.lfCaptionFont);
	} else {
		font = new Font(Font::DefaultGui);
	}
	auto selectFont = canvas.select(*font);
	auto transparent = canvas.setBkMode(true);

	for(const auto& button: layout.buttons) {
		COLORREF background = captionColor;
		if(!button.disabled && button.hitTest == pressedCaptionButton &&
			button.hitTest == hotCaptionButton) {
			background = Appearance::blend(captionColor, colors.accent, 128);
		} else if(!button.disabled && button.hitTest == hotCaptionButton) {
			background = Appearance::blend(captionColor, colors.accent, 64);
		}
		Brush buttonBrush(background);
		canvas.fill(button.bounds, buttonBrush);
		drawCaptionGlyph(canvas, button.bounds, button.hitTest,
			::IsZoomed(handle()) != FALSE,
			button.disabled || !::IsWindowEnabled(handle()) ?
				colors.disabledText : (captionActive ? colors.text :
					colors.disabledText), std::max(1, scale(1)));
	}

	if(valid(layout.icon)) {
		HICON iconHandle = smallIcon ? smallIcon->handle() :
			reinterpret_cast<HICON>(::SendMessage(handle(), WM_GETICON,
				ICON_SMALL2, 0));
		if(!iconHandle) {
			iconHandle = reinterpret_cast<HICON>(::GetClassLongPtr(handle(),
				GCLP_HICONSM));
		}
		if(!iconHandle && largeIcon) {
			iconHandle = largeIcon->handle();
		}
		if(iconHandle) {
			IconPtr icon = new Icon(iconHandle, false);
			canvas.drawIcon(icon, layout.icon);
		}
	}

	Rectangle text = layout.caption;
	const long padding = scale(6);
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
		canvas.setTextColor(captionActive ? colors.text : colors.disabledText);
		unsigned format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS |
			DT_NOPREFIX;
		if(layout.rightToLeft) {
			format |= DT_RTLREADING | DT_RIGHT;
		}
		canvas.drawText(getText(), text, format);
	}

	Pen separator(borderColor, Pen::Solid, 1);
	{
		auto selectPen = canvas.select(separator);
		canvas.line(layout.caption.left(), layout.caption.bottom() - 1,
			layout.caption.right(), layout.caption.bottom() - 1);
	}
	canvas.blast(layout.caption);
}

void Frame::redrawNonClient() {
	if(handle() && ::IsWindow(handle())) {
		::RedrawWindow(handle(), nullptr, nullptr,
			RDW_INVALIDATE | RDW_FRAME);
	}
}

void Frame::updateCaptionAppearance() {
	if(!handle() || !::IsWindow(handle())) {
		return;
	}

	const bool manual = isManualAppearance();
	if(manualNonClient == manual) {
		if(manual) {
			paintNonClient();
		}
		return;
	}

	manualNonClient = manual;
	if(::GetCapture() == handle() && pressedCaptionButton != HTNOWHERE) {
		::ReleaseCapture();
	}
	if(trackingCaptionMouse) {
		TRACKMOUSEEVENT cancel = { sizeof(TRACKMOUSEEVENT),
			TME_CANCEL | TME_LEAVE | TME_NONCLIENT, handle(), 0 };
		::TrackMouseEvent(&cancel);
	}
	pressedCaptionButton = hotCaptionButton = HTNOWHERE;
	trackingCaptionMouse = false;
	setNonClientRendering(handle(), manual);
	::SetWindowPos(handle(), nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
		SWP_FRAMECHANGED);
	redrawNonClient();
}

}
