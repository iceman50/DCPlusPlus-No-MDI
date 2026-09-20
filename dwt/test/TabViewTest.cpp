// Copyright (C) 2026 iceman50
// Noninteractive regression checks for tab close-button mouse handling.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/TabView.h>
#include <dwt/widgets/Container.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

void paint(dwt::TabView* tabs) {
	if(!tabs->hasStyle(TCS_OWNERDRAWFIXED) && !dwt::Application::instance().getAppearance().isManual()) return;
	// Exercise the shared tab renderer without showing a window. Reserve space
	// for the extra item data installed by TabCtrl_SetItemExtra.
	struct {
		TCITEM item { TCIF_PARAM };
		ULONG_PTR extra[2] { };
	} data;
	check(TabCtrl_GetItem(tabs->handle(), 0, &data.item), "Cannot read tab item");
	DRAWITEMSTRUCT draw { };
	draw.CtlType = ODT_TAB;
	draw.itemState = ODS_SELECTED;
	draw.hwndItem = tabs->handle();
	draw.itemData = reinterpret_cast<ULONG_PTR>(&data.item.lParam);
	check(TabCtrl_GetItemRect(tabs->handle(), 0, &draw.rcItem), "Cannot read drawing bounds");
	draw.hDC = ::CreateCompatibleDC(nullptr);
	check(draw.hDC != nullptr, "Cannot create drawing context");
	dwt::TabView::handlePainting(draw);
	::DeleteDC(draw.hDC);
}

void click(dwt::TabView* tabs, POINT down, POINT up, WPARAM keys = 0) {
	::SendMessage(tabs->handle(), WM_LBUTTONDOWN, MK_LBUTTON | keys, MAKELPARAM(down.x, down.y));
	::SendMessage(tabs->handle(), WM_LBUTTONUP, keys, MAKELPARAM(up.x, up.y));
}

void checkTabs(dwt::Window* window, dwt::Application& app, bool ownerDraw, bool closeable) {
	auto& appearance = app.getAppearance();
	appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
	auto seed = dwt::TabView::Seed();
	if(!ownerDraw) seed.style &= ~TCS_OWNERDRAWFIXED;
	seed.widthConfig = ownerDraw ? 180 : 0;
	seed.closeable = closeable;
	auto tabs = window->addChild(seed);
	tabs->resize(dwt::Rectangle(0, 0, 600, 300));
	tabs->setVisible(true);
	auto page = dwt::WidgetCreator<dwt::Container>::create(tabs);
	page->setText(_T("Tab close regression"));
	int closeRequests = 0;
	page->onClosing([&] { ++closeRequests; return false; });
	tabs->add(page);
	paint(tabs);

	RECT rect { };
	check(TabCtrl_GetItemRect(tabs->handle(), 0, &rect), "Cannot read tab bounds");
	POINT close { rect.right - tabs->scale(12), (rect.top + rect.bottom) / 2 };
	POINT title { rect.left + tabs->scale(20), close.y };
	click(tabs, close, title);
	check(closeRequests == 0, "Releasing outside the close button closed the tab");
	click(tabs, title, close);
	check(closeRequests == 0, "Pressing outside the close button closed the tab");
	click(tabs, title, title);
	check(closeRequests == 0, "Clicking the title closed the tab");
	click(tabs, close, close);
	check(closeRequests == (closeable ? 1 : 0), "Dark-mode close button did not respect closeable");

	if(!ownerDraw) {
		closeRequests = 0;
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
		paint(tabs);
		click(tabs, close, close);
		check(closeRequests == 0, "Native light-mode tab retained an invisible close button");
		appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		paint(tabs);
		click(tabs, close, close);
		check(closeRequests == (closeable ? 1 : 0), "Close button failed after a live theme switch");
	}
	closeRequests = 0;
	click(tabs, title, title, MK_SHIFT);
	check(closeRequests == (closeable ? 1 : 0), "Shift-click close behavior changed");
	tabs->remove(page);
	::DestroyWindow(tabs->handle());
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	int result = 0;
	try {
		check(!app.getAppearance().isHighContrast(), "Run this check with Windows high contrast disabled");
		checkTabs(window, app, false, true);
		checkTabs(window, app, true, true);
		checkTabs(window, app, false, false);
		checkTabs(window, app, true, false);
		std::puts("PASS: tab close clicks, canceled clicks, theme switching, Shift-click and non-closeable tabs");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
