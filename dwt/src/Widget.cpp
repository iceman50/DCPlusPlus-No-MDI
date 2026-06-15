/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka

  SmartWin++

  Copyright (c) 2005 Thomas Hansen

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
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

#include <dwt/Widget.h>

#include <dwt/DWTException.h>
#include <dwt/util/check.h>
#include <dwt/util/win32/ApiHelpers.h>
#include <dwt/util/win32/AccessibilityProvider.h>
#include <dwt/util/win32/Dpi.h>

namespace dwt {

GlobalAtom Widget::propAtom(_T("dwt::Widget*"));

#ifdef DWT_DEBUG_WIDGETS
int widgetCount;
#endif

Widget::Widget(Widget* parent_, Dispatcher& dispatcher_) :
	hwnd(NULL), parent(parent_), dispatcher(dispatcher_),
	dpi(util::win32::defaultDpi), previousDpi(util::win32::defaultDpi),
	accessibilityProvider(nullptr), accessibleControlType(accessibility::Custom),
	accessibleKeyboardFocusable(false)
{
#ifdef DWT_DEBUG_WIDGETS
	++widgetCount;
	printf("created a dwt widget; count: %d\n", widgetCount);
#endif
}

Widget::~Widget() {
	util::win32::detachAccessibilityProvider(accessibilityProvider);
	util::win32::releaseAccessibilityProvider(accessibilityProvider);
	if(hwnd) {
		::RemoveProp(hwnd, propAtom);
	}
#ifdef DWT_DEBUG_WIDGETS
	--widgetCount;
	printf("destroying a dwt widget; count: %d\n", widgetCount);
#endif
}

void Widget::kill() {
	delete this;
}

HWND Widget::create(const Seed & cs) {
	HWND hWnd = ::CreateWindowEx(cs.exStyle, getDispatcher().getClassName(), cs.caption.c_str(), cs.style,
		cs.location.x(), cs.location.y(), cs.location.width(), cs.location.height(),
		getParentHandle(), cs.menuHandle, ::GetModuleHandle(NULL), reinterpret_cast<LPVOID>(this));

	if(!hWnd) {
		// The most common error is to forget WS_CHILD in the styles
		throw Win32Exception("Unable to create widget");
	}

	return hWnd;
}

void Widget::setHandle(HWND h) {
	if(hwnd) {
		throw DWTException("You may not attach to a widget that's already attached");
	}

	hwnd = h;
	dpi = previousDpi = util::win32::getDpi(hwnd);

	::SetProp(hwnd, propAtom, reinterpret_cast<HANDLE>(this));

	::SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc::wndProc));
}

Widget* Widget::getRoot() const {
	return hwnd_cast<Widget*>(::GetAncestor(handle(), GA_ROOT));
}

void Widget::addRemoveStyle(DWORD addStyle, bool add) {
	util::win32::updateStyle(handle(), GWL_STYLE, addStyle, add);
}

void Widget::addRemoveExStyle(DWORD addStyle, bool add) {
	util::win32::updateStyle(handle(), GWL_EXSTYLE, addStyle, add);
}

Widget::CallbackIter Widget::addCallback(const Message& msg, const CallbackType& callback) {
	CallbackList& callbacks = handlers[msg];
	callbacks.push_back(callback);
	return --callbacks.end();
}

Widget::CallbackIter Widget::setCallback(const Message& msg, const CallbackType& callback) {
	CallbackList& callbacks = handlers[msg];
	callbacks.clear();
	callbacks.push_back(callback);
	return --callbacks.end();
}

void Widget::clearCallback(const Message& msg, const CallbackIter& i) {
	CallbackList& callbacks = handlers[msg];
	callbacks.erase(i);
	if(callbacks.empty()) {
		handlers.erase(msg);
	}
}

void Widget::clearCallbacks(const Message& msg) {
	handlers.erase(msg);
}

/// Make sure that handle is still valid before calling f
void checkCall(HWND handle, const Application::Callback& f) {
	/// @todo this might fail when the handle has already been re-used elsewhere
	if(::IsWindow(handle))
		f();
}

void Widget::callAsync(const Application::Callback& f) {
	HWND h = handle();
	Application::instance().callAsync([h, f] { checkCall(h, f); });
}

bool Widget::handleMessage(const MSG &msg, LRESULT &retVal) {
	const bool dpiChanged = msg.message == WM_DPICHANGED ||
		msg.message == WM_DPICHANGED_AFTERPARENT;
	const bool appearanceChanged = msg.message == WM_THEMECHANGED ||
		msg.message == WM_SYSCOLORCHANGE ||
		msg.message == WM_SETTINGCHANGE;
	if(dpiChanged) {
		previousDpi = dpi;
		dpi = msg.message == WM_DPICHANGED ?
			LOWORD(msg.wParam) : util::win32::getDpi(handle());

		if(msg.message == WM_DPICHANGED && !getParent() && msg.lParam) {
			const RECT& bounds = *reinterpret_cast<const RECT*>(msg.lParam);
			::SetWindowPos(handle(), nullptr, bounds.left, bounds.top,
				bounds.right - bounds.left, bounds.bottom - bounds.top,
				SWP_NOACTIVATE | SWP_NOZORDER);
		}

		if(previousDpi != dpi) {
			DpiResourceEvent event(previousDpi, dpi);
			for(auto& callback: dpiResourceCallbacks) {
				callback(event);
			}
		}
	}

	// UiaRootObjectId is -25 and is kept out of public headers to avoid pulling
	// the complete UI Automation SDK surface into every widget translation unit.
	if(msg.message == WM_GETOBJECT && msg.lParam == static_cast<LPARAM>(-25) &&
		accessibilityProvider) {
		retVal = util::win32::returnAccessibilityProvider(
			accessibilityProvider, msg.hwnd, msg.wParam, msg.lParam);
		return true;
	}

	// First we must create a "comparable" message...
	Message msgComparer(msg);
	auto i = handlers.find(msgComparer);
	bool handled = false;
	if(i != handlers.end()) {
		CallbackList& list = i->second;
		for(auto& j: list) {
			handled |= j(msg, retVal);
		}
	}
	if(dpiChanged) {
		layout();
	}
	if(appearanceChanged && hwnd && ::IsWindow(hwnd)) {
		layout();
		::InvalidateRect(hwnd, nullptr, TRUE);
	}
	return handled;
}

unsigned Widget::getDpi() const {
	return hwnd ? util::win32::getDpi(hwnd) : dpi;
}

int Widget::scale(int value) const {
	return util::win32::scale(value, getDpi());
}

Point Widget::scale(const Point& value) const {
	return util::win32::scale(value, getDpi());
}

Rectangle Widget::scale(const Rectangle& value) const {
	return util::win32::scale(value, getDpi());
}

int Widget::getSystemMetric(int index) const {
	return util::win32::getSystemMetricsForDpi(index, getDpi());
}

bool Widget::getSystemParameters(UINT action, UINT param, void* value,
	UINT flags) const
{
	return util::win32::systemParametersInfoForDpi(
		action, param, value, flags, getDpi());
}

bool Widget::isHighContrast() {
	HIGHCONTRAST value = { sizeof(HIGHCONTRAST) };
	return ::SystemParametersInfo(SPI_GETHIGHCONTRAST,
		sizeof(value), &value, 0) &&
		(value.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool Widget::adjustWindowRect(RECT& rect, bool hasMenu) const {
	auto style = static_cast<DWORD>(::GetWindowLongPtr(handle(), GWL_STYLE));
	auto exStyle = static_cast<DWORD>(::GetWindowLongPtr(handle(), GWL_EXSTYLE));
	return util::win32::adjustWindowRectForDpi(rect, style, hasMenu, exStyle, getDpi());
}

void Widget::enableAccessibility(long controlType) {
	accessibleControlType = controlType;
	if(!accessibilityProvider) {
		accessibilityProvider = util::win32::createAccessibilityProvider(this);
	}
}

bool Widget::accessibilityEnabled() const {
	return accessibilityProvider != nullptr;
}

void Widget::setAccessibleName(const tstring& value) {
	accessibleName = value;
}

void Widget::setAccessibleHelpText(const tstring& value) {
	accessibleHelpText = value;
}

void Widget::setAccessibleControlType(long value) {
	enableAccessibility(value);
}

void Widget::setAccessibleKeyboardFocusable(bool value) {
	accessibleKeyboardFocusable = value;
}

void Widget::setAccessibleRangeValue(
	const accessibility::RangeValueProvider& value)
{
	enableAccessibility(accessibleControlType);
	accessibleRangeValue.reset(new accessibility::RangeValueProvider(value));
}

void Widget::setAccessibleScroll(const accessibility::ScrollProvider& value) {
	enableAccessibility(accessibleControlType);
	accessibleScroll.reset(new accessibility::ScrollProvider(value));
}

void Widget::setAccessibleItems(const accessibility::ItemProvider& value) {
	enableAccessibility(accessibleControlType);
	accessibleItems.reset(new accessibility::ItemProvider(value));
}

tstring Widget::getAccessibleName() const {
	if(!accessibleName.empty() || !hwnd) {
		return accessibleName;
	}
	int length = ::GetWindowTextLength(hwnd);
	if(!length) {
		return tstring();
	}
	std::vector<TCHAR> text(static_cast<size_t>(length) + 1);
	::GetWindowText(hwnd, text.data(), static_cast<int>(text.size()));
	return text.data();
}

const tstring& Widget::getAccessibleHelpText() const {
	return accessibleHelpText;
}

long Widget::getAccessibleControlType() const {
	return accessibleControlType;
}

bool Widget::getAccessibleKeyboardFocusable() const {
	return accessibleKeyboardFocusable;
}

const accessibility::RangeValueProvider* Widget::getAccessibleRangeValue() const {
	return accessibleRangeValue.get();
}

const accessibility::ScrollProvider* Widget::getAccessibleScroll() const {
	return accessibleScroll.get();
}

const accessibility::ItemProvider* Widget::getAccessibleItems() const {
	return accessibleItems.get();
}

void Widget::raiseAccessibleEvent(long eventId) {
	util::win32::raiseAccessibilityEvent(accessibilityProvider, eventId);
}

void Widget::raiseAccessibleItemEvent(accessibility::ItemId item, long eventId) {
	util::win32::raiseAccessibilityItemEvent(
		accessibilityProvider, item, eventId);
}

void Widget::raiseAccessibleStructureChanged() {
	util::win32::raiseAccessibilityStructureChanged(accessibilityProvider);
}

void Widget::onDpiResourcesChanged(
	std::function<void (const DpiResourceEvent&)> callback)
{
	dpiResourceCallbacks.push_back(callback);
}

void Widget::setParent(Widget* widget) {
	::SetWindowLongPtr(handle(), GWLP_HWNDPARENT, widget->toLParam());
	parent = widget;
}

Point Widget::getPreferredSize() {
	return Point(0, 0);
}

void Widget::layout() { }

Point Widget::getPrimaryDesktopSize() {
	POINT pt = { 0 };
	return getDesktopSize(::MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY)).size;
}

Rectangle Widget::getDesktopSize() const {
	return getDesktopSize(::MonitorFromWindow(handle(), MONITOR_DEFAULTTONEAREST));
}

Rectangle Widget::getDesktopSize(HMONITOR mon) {
	MONITORINFO mi = { sizeof(MONITORINFO) };
	if(::GetMonitorInfo(mon, &mi)) {
		return Rectangle(mi.rcWork);
	}

	// the following should never be needed, but better be safe...
	RECT rc = { 0 };
	::GetWindowRect(::GetDesktopWindow(), &rc);
	return Rectangle(rc);
}

void Widget::setZOrder(HWND insertAfter) {
	::SetWindowPos(handle(), insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);
}

}
