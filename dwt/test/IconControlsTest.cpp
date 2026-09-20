// Copyright (C) 2026 iceman50
// Noninteractive regression checks for native menu icons and report-list sizing.
#include <dwt/widgets/Window.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/Table.h>
#include <dwt/resources/Bitmap.h>
#include <dwt/resources/Icon.h>

#include <cstdio>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
	if(!condition) throw std::runtime_error(message);
}

dwt::IconPtr makeIcon(bool alpha) {
	BITMAPINFO format { };
	format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	format.bmiHeader.biWidth = 16;
	format.bmiHeader.biHeight = -16;
	format.bmiHeader.biPlanes = 1;
	format.bmiHeader.biBitCount = 32;
	DWORD* pixels = nullptr;
	dwt::Bitmap color(::CreateDIBSection(nullptr, &format, DIB_RGB_COLORS,
		reinterpret_cast<void**>(&pixels), nullptr, 0));
	check(color.handle() != nullptr, "CreateDIBSection");
	BYTE mask[32];
	for(int y = 0; y < 16; ++y) {
		mask[y * 2] = mask[y * 2 + 1] = y < 8 ? 0xff : 0;
		for(int x = 0; x < 16; ++x)
			pixels[y * 16 + x] = y < 8 ? 0 : alpha ? 0x80804020 : 0x00804020;
	}
	dwt::Bitmap mono(::CreateBitmap(16, 16, 1, 1, mask));
	ICONINFO info { };
	info.fIcon = TRUE;
	info.hbmColor = color.handle();
	info.hbmMask = mono.handle();
	auto icon = ::CreateIconIndirect(&info);
	check(icon != nullptr, "CreateIconIndirect");
	return new dwt::Icon(icon);
}

HBITMAP checkItem(dwt::Menu* menu, unsigned index, bool native, bool alpha) {
	MENUITEMINFO info { sizeof(MENUITEMINFO), MIIM_BITMAP | MIIM_FTYPE };
	check(::GetMenuItemInfo(menu->handle(), index, TRUE, &info), "GetMenuItemInfo");
	check(((info.fType & MFT_OWNERDRAW) == 0) == native, "Incorrect menu drawing mode");
	if(!native) {
		check(info.hbmpItem == nullptr, "Owner-drawn item retained a native bitmap");
		return nullptr;
	}
	check(info.hbmpItem != nullptr, "Native menu lost its icon");
	DIBSECTION dib { };
	check(::GetObject(info.hbmpItem, sizeof(dib), &dib) == sizeof(dib), "Native icon is not a DIB");
	check(dib.dsBm.bmWidth == 16 && dib.dsBm.bmHeight == 16 && dib.dsBm.bmBitsPixel == 32, "Wrong native bitmap size/depth");
	const auto pixels = static_cast<DWORD*>(dib.dsBm.bmBits);
	int transparent = 0, visible = 0;
	for(int i = 0; i < 256; ++i) {
		const auto a = pixels[i] >> 24;
		if(a == 0) { ++transparent; continue; }
		++visible;
		check(a == (alpha ? 128u : 255u), "Icon alpha was lost");
		if((pixels[i] & 0xffffff) != (alpha ? 0x402010u : 0x804020u)) {
			std::fprintf(stderr, "Pixel %d: %08lx (alpha icon: %d)\n", i, pixels[i], alpha);
			check(false, "Premultiplied icon color changed");
		}
	}
	check(transparent == 128 && visible == 128, "Icon transparency mask changed");
	return info.hbmpItem;
}
}

int dwtMain(dwt::Application& app) {
	auto window = new dwt::Window();
	auto seed = dwt::Window::Seed();
	seed.style &= ~WS_VISIBLE;
	window->create(seed);
	int result = 0;
	try {
		auto& appearance = app.getAppearance();
		check(!appearance.isHighContrast(), "Run this check with Windows high contrast disabled");
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
		auto rgba = makeIcon(true);
		auto legacy = makeIcon(false);
		auto menuSeed = dwt::Menu::Seed(false);
		menuSeed.popup = false;
		auto menu = window->addChild(menuSeed);
		auto popup = menu->appendPopup(_T("File"), rgba);
		popup->appendItem(_T("RGBA"), {}, rgba);
		popup->appendItem(_T("Legacy"), {}, legacy, false);
		auto nested = popup->appendPopup(_T("Nested"), rgba, false);
		nested->appendItem(_T("Child"), {}, rgba);
		menu->setMenu();
		const auto bitmap = checkItem(popup, 0, true, true);
		checkItem(popup, 1, true, false);
		checkItem(menu.get(), 0, true, true);
		checkItem(popup, 2, true, true);
		checkItem(nested, 0, true, true);
		appearance.configure(dwt::Appearance::Mode::Dark, dwt::Appearance::defaultPalette());
		checkItem(popup, 0, false, true);
		checkItem(nested, 0, false, true);
		popup->appendItem(_T("Added in dark mode"), {}, rgba);
		appearance.configure(dwt::Appearance::Mode::Light, dwt::Appearance::defaultPalette());
		check(checkItem(popup, 0, true, true) == bitmap, "Bitmap lifetime changed on theme switch");
		checkItem(popup, 1, true, false);
		checkItem(nested, 0, true, true);
		checkItem(popup, 3, true, true);
		popup->remove(0);
		BITMAP deleted { };
		check(!::GetObject(bitmap, sizeof(deleted), &deleted), "Removed item leaked its bitmap");
		checkItem(popup, 0, true, false);

		auto table = window->addChild(dwt::Table::Seed());
		table->addColumn(_T("User"), 180);
		table->insert({ _T("Example user") });
		table->setSelected(0);
		auto rowHeight = [&](int size) {
			dwt::ImageListPtr images = new dwt::ImageList(dwt::Point(size, size));
			images->add(*rgba->resized(dwt::Point(size, size)));
			table->setSmallImageList(images);
			RECT rect { };
			check(ListView_GetItemRect(table->handle(), 0, &rect, LVIR_BOUNDS), "Cannot read row height");
			check(table->countSelected() == 1, "Resizing lost the selected user");
			return rect.bottom - rect.top;
		};
		const auto small = rowHeight(16);
		const auto large = rowHeight(48);
		check(large >= 48 && large > small, "Large user icon did not increase row height");
		check(rowHeight(16) == small, "Small user icon did not restore row height");
		std::puts("PASS: native RGBA/masked menu icons, theme switching, bitmap lifetime, user row sizing and selection");
	} catch(const std::exception& error) {
		std::fprintf(stderr, "FAIL: %s\n", error.what());
		result = 1;
	}
	::DestroyWindow(window->handle());
	return result;
}
