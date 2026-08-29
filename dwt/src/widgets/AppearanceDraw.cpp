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

#include "AppearanceDraw.h"

#include <dwt/resources/Brush.h>
#include <dwt/resources/Font.h>
#include <dwt/resources/Pen.h>
#include <dwt/widgets/Button.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Header.h>
#include <dwt/widgets/Spinner.h>
#include <dwt/widgets/StatusBar.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/ToolBar.h>

#include <algorithm>
#include <vector>

namespace dwt { namespace appearance_detail {

namespace {

Appearance::Palette effectivePalette(Control& control, const Appearance::Palette& configured)
{
	auto palette = configured;
	if(control.hasExplicitColors()) {
		palette.text = control.getExplicitTextColor();
		palette.background = control.getExplicitBackgroundColor();
		palette.surface = palette.background;
		palette.disabledText = Appearance::blend(
			palette.text, palette.background, 145);
	}
	return palette;
}

void drawSurface(Canvas& canvas, const Rectangle& bounds, COLORREF background, COLORREF border)
{
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return;
	}
	Brush brush(background);
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

void drawFocusFrame(Canvas& canvas, Rectangle bounds, COLORREF color) {
	if(bounds.width() <= 1 || bounds.height() <= 1) {
		return;
	}
	bounds.size -= Point(1, 1);
	Pen pen(color, Pen::Dot, 1);
	auto selectPen = canvas.select(pen);
	canvas.line(bounds);
}

void drawArrow(Canvas& canvas, const Rectangle& bounds, bool vertical, bool towardStart, COLORREF color, int radius)
{
	const long x = bounds.left() + bounds.width() / 2;
	const long y = bounds.top() + bounds.height() / 2;
	radius = std::max(2, radius);
	POINT points[3];
	if(vertical) {
		points[0] = { x - radius, y + (towardStart ? 1 : -1) };
		points[1] = { x + radius, y + (towardStart ? 1 : -1) };
		points[2] = { x, y + (towardStart ? -radius : radius) };
	} else {
		points[0] = { x + (towardStart ? 1 : -1), y - radius };
		points[1] = { x + (towardStart ? 1 : -1), y + radius };
		points[2] = { x + (towardStart ? -radius : radius), y };
	}
	Pen pen(color, Pen::Solid, 1);
	Brush brush(color);
	auto selectPen = canvas.select(pen);
	auto selectBrush = canvas.select(brush);
	canvas.polygon(points, 3);
}

unsigned buttonTextFormat(DWORD style) {
	unsigned format = DT_END_ELLIPSIS;
	switch(style & (BS_LEFT | BS_RIGHT | BS_CENTER)) {
	case BS_LEFT: format |= DT_LEFT; break;
	case BS_RIGHT: format |= DT_RIGHT; break;
	default: format |= DT_CENTER; break;
	}
	switch(style & (BS_TOP | BS_BOTTOM | BS_VCENTER)) {
	case BS_TOP: format |= DT_TOP; break;
	case BS_BOTTOM: format |= DT_BOTTOM; break;
	default: format |= DT_VCENTER; break;
	}
	format |= (style & BS_MULTILINE) ? DT_WORDBREAK : DT_SINGLELINE;
	return format;
}

bool isRightToLeft(HWND hwnd) {
	return (::GetWindowLongPtr(hwnd, GWL_EXSTYLE) &
		(WS_EX_LAYOUTRTL | WS_EX_RTLREADING)) != 0;
}

bool cursorIn(HWND hwnd, const Rectangle& bounds) {
	POINT cursor = { 0, 0 };
	if(!::GetCursorPos(&cursor) || !::ScreenToClient(hwnd, &cursor)) {
		return false;
	}
	return bounds.contains(Point(cursor));
}

struct ButtonImage {
	ButtonImage() : list(nullptr), icon(nullptr), bitmap(nullptr), index(0),
		alignment(BUTTON_IMAGELIST_ALIGN_LEFT), size() { }

	HIMAGELIST list;
	HICON icon;
	HBITMAP bitmap;
	int index;
	UINT alignment;
	RECT margin = { 0, 0, 0, 0 };
	Point size;
};

ButtonImage getButtonImage(Button& button, bool disabled, bool pressed, bool hot, bool defaultButton)
{
	ButtonImage image;
	BUTTON_IMAGELIST info = { };
	if(button.getImageListInfo(info) && info.himl) {
		image.list = info.himl;
		image.alignment = info.uAlign;
		image.margin = info.margin;
		int width = 0;
		int height = 0;
		ImageList_GetIconSize(info.himl, &width, &height);
		image.size = Point(width, height);
		const int count = ImageList_GetImageCount(info.himl);
		image.index = disabled ? 3 : pressed ? 2 : hot ? 1 :
			defaultButton ? 4 : 0;
		image.index = count <= 1 ? 0 : std::min(image.index, count - 1);
		return image;
	}

	image.icon = reinterpret_cast<HICON>(button.sendMessage(BM_GETIMAGE,
		IMAGE_ICON));
	if(image.icon) {
		ICONINFO info = { };
		BITMAP bitmap = { };
		if(::GetIconInfo(image.icon, &info)) {
			::GetObject(info.hbmColor ? info.hbmColor : info.hbmMask,
				sizeof(bitmap), &bitmap);
			if(info.hbmColor) ::DeleteObject(info.hbmColor);
			if(info.hbmMask) ::DeleteObject(info.hbmMask);
		}
		image.size = Point(bitmap.bmWidth ? bitmap.bmWidth : button.scale(16),
			bitmap.bmHeight ? bitmap.bmHeight : button.scale(16));
		return image;
	}

	image.bitmap = reinterpret_cast<HBITMAP>(button.sendMessage(BM_GETIMAGE,
		IMAGE_BITMAP));
	if(image.bitmap) {
		BITMAP bitmap = { };
		if(::GetObject(image.bitmap, sizeof(bitmap), &bitmap)) {
			image.size = Point(bitmap.bmWidth, bitmap.bmHeight);
		}
	}
	return image;
}

void drawButtonImage(Canvas& canvas, const ButtonImage& image, const Rectangle& bounds, Rectangle& textRect, bool hasText, bool disabled)
{
	if(image.size.x <= 0 || image.size.y <= 0) {
		return;
	}

	long x = bounds.left() + std::max<long>(0,
		(bounds.width() - image.size.x) / 2);
	long y = bounds.top() + std::max<long>(0,
		(bounds.height() - image.size.y) / 2);
	const long gap = hasText ? 5 : 0;
	switch(image.alignment) {
	case BUTTON_IMAGELIST_ALIGN_RIGHT:
		x = bounds.right() - image.size.x - image.margin.right;
		textRect.size.x = std::max(0L,
			x - image.margin.left - gap - textRect.left());
		break;
	case BUTTON_IMAGELIST_ALIGN_TOP:
		y = bounds.top() + image.margin.top;
		textRect.pos.y = y + image.size.y + image.margin.bottom + gap;
		textRect.size.y = std::max(0L,
			bounds.bottom() - textRect.top() - image.margin.bottom);
		break;
	case BUTTON_IMAGELIST_ALIGN_BOTTOM:
		y = bounds.bottom() - image.size.y - image.margin.bottom;
		textRect.size.y = std::max(0L,
			y - image.margin.top - gap - textRect.top());
		break;
	case BUTTON_IMAGELIST_ALIGN_CENTER:
		break;
	default:
		x = bounds.left() + image.margin.left;
		textRect.pos.x = x + image.size.x + image.margin.right + gap;
		textRect.size.x = std::max(0L,
			bounds.right() - textRect.left() - image.margin.right);
		break;
	}

	if(image.list) {
		ImageList_Draw(image.list, image.index, canvas.handle(), x, y,
			ILD_TRANSPARENT);
	} else if(image.icon) {
		::DrawIconEx(canvas.handle(), x, y, image.icon, image.size.x,
			image.size.y, 0, nullptr, disabled ? DI_NORMAL : DI_NORMAL);
	} else if(image.bitmap) {
		const UINT state = DST_BITMAP | (disabled ? DSS_DISABLED : DSS_NORMAL);
		::DrawState(canvas.handle(), nullptr, nullptr,
			reinterpret_cast<LPARAM>(image.bitmap), 0, x, y,
			image.size.x, image.size.y, state);
	}
}

void drawSmallCheck(Canvas& canvas, const Rectangle& box, bool checked, bool indeterminate, bool disabled, const Appearance::Palette& colors)
{
	auto background = checked ? colors.accent : colors.surface;
	auto border = checked ? colors.accent : colors.border;
	if(disabled) {
		background = Appearance::blend(background, colors.background, 110);
		border = Appearance::blend(border, colors.background, 90);
	}
	drawSurface(canvas, box, background, border);
	if(!checked) {
		return;
	}
	const auto mark = disabled ? Appearance::blend(colors.highlightText,
		background, 105) : colors.highlightText;
	Pen pen(mark, Pen::Solid, std::max(1L, box.width() / 7));
	auto selectPen = canvas.select(pen);
	if(indeterminate) {
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

}

LRESULT drawButton(Button& button, NMCUSTOMDRAW& data, const Appearance::Palette& configured)
{
	if(data.dwDrawStage != CDDS_PREPAINT || !data.hdc) {
		return CDRF_DODEFAULT;
	}

	const DWORD style = static_cast<DWORD>(::GetWindowLongPtr(
		button.handle(), GWL_STYLE));
	const DWORD buttonType = style & BS_TYPEMASK;
	if(buttonType == BS_GROUPBOX || buttonType == BS_OWNERDRAW) {
		return CDRF_DODEFAULT;
	}

	const auto colors = effectivePalette(button, configured);
	const auto state = static_cast<UINT>(button.sendMessage(BM_GETSTATE));
	const bool disabled = !button.getEnabled() ||
		(data.uItemState & CDIS_DISABLED) != 0;
	bool pressed = (data.uItemState & CDIS_SELECTED) != 0 ||
		(state & (BST_PUSHED | BST_DROPDOWNPUSHED)) != 0;
	const bool hot = (data.uItemState & CDIS_HOT) != 0 ||
		(state & BST_HOT) != 0;
	const bool focused = (data.uItemState & CDIS_FOCUS) != 0 ||
		(state & BST_FOCUS) != 0 || ::GetFocus() == button.handle();
	const auto uiState = static_cast<UINT>(button.sendMessage(WM_QUERYUISTATE));

	FreeCanvas canvas(data.hdc);
	Rectangle bounds(data.rc);
	Brush backdrop(colors.background);
	canvas.fill(bounds, backdrop);

	const bool check = buttonType == BS_CHECKBOX ||
		buttonType == BS_AUTOCHECKBOX || buttonType == BS_3STATE ||
		buttonType == BS_AUTO3STATE;
	const bool radio = buttonType == BS_RADIOBUTTON ||
		buttonType == BS_AUTORADIOBUTTON;
	const auto checkState = static_cast<UINT>(button.sendMessage(BM_GETCHECK));
	const bool checked = checkState != BST_UNCHECKED;
	const bool pushLike = (style & BS_PUSHLIKE) != 0;
	if((check || radio) && !pushLike) {
		const long glyphMargin = button.scale(radio ? 2 : 4);
		const long glyphSize = std::max(8L, std::min<long>(
			button.scale(radio ? 13 : 15),
			std::max(8L, bounds.height() - glyphMargin)));
		const bool rightGlyph = (style & BS_LEFTTEXT) != 0;
		Rectangle glyph(rightGlyph ? bounds.right() - glyphSize - 1 :
			bounds.left() + 1, bounds.top() + std::max(0L,
				(bounds.height() - glyphSize) / 2), glyphSize, glyphSize);
		auto fill = radio ? colors.surface : checked ? colors.accent : colors.surface;
		auto border = checked ? colors.accent : colors.border;
		if(pressed) {
			const auto pressedSurface = Appearance::blend(colors.surface, colors.accent, 38);
			fill = radio || !checked ? pressedSurface : Appearance::blend(colors.accent, RGB(0, 0, 0), 45);
			if(radio && checked) border = Appearance::blend(colors.accent, RGB(0, 0, 0), 45);
		} else if(hot) {
			const auto hotSurface = Appearance::blend(colors.surface, colors.accent, 18);
			fill = radio || !checked ? hotSurface : Appearance::blend(colors.accent, RGB(0, 0, 0), 25);
			border = Appearance::blend(border, colors.accent, 110);
		}
		if(disabled) {
			fill = Appearance::blend(fill, colors.background, 120);
			border = Appearance::blend(border, colors.background, 90);
		}

		if(radio) {
			Brush brush(fill);
			Pen pen(border, Pen::Solid, 1);
			auto selectBrush = canvas.select(brush);
			auto selectPen = canvas.select(pen);
			canvas.ellipse(glyph);
		} else {
			drawSurface(canvas, glyph, fill, border);
		}

		if(checked) {
			const auto checkMark = disabled ? Appearance::blend(colors.highlightText, fill, 105) : colors.highlightText;
			const auto mark = radio ? border : checkMark;
			Brush markBrush(mark);
			Pen markPen(mark, Pen::Solid, std::max(1, button.scale(radio ? 1 : 2)));
			auto selectMarkBrush = canvas.select(markBrush);
			auto selectMarkPen = canvas.select(markPen);
			if(radio) {
				const long inset = std::max(3L, glyphSize / 3);
				canvas.ellipse(Rectangle(glyph.left() + inset,
					glyph.top() + inset,
					std::max(1L, glyph.width() - inset * 2),
					std::max(1L, glyph.height() - inset * 2)));
			} else if(checkState == BST_INDETERMINATE) {
				canvas.line(Point(glyph.left() + glyphSize / 4,
					glyph.top() + glyphSize / 2),
					Point(glyph.right() - glyphSize / 4,
						glyph.top() + glyphSize / 2));
			} else {
				canvas.line(Point(glyph.left() + glyphSize / 4,
					glyph.top() + glyphSize / 2),
					Point(glyph.left() + glyphSize * 2 / 5,
						glyph.bottom() - glyphSize / 4));
				canvas.line(Point(glyph.left() + glyphSize * 2 / 5,
					glyph.bottom() - glyphSize / 4),
					Point(glyph.right() - glyphSize / 5,
						glyph.top() + glyphSize / 4));
			}
		}

		auto selectFont = canvas.select(*button.getFont());
		const auto text = button.getText();
		const long gap = button.scale(8);
		const long textLeft = rightGlyph ? bounds.left() : glyph.right() + gap;
		const long textRight = rightGlyph ? glyph.left() - gap : bounds.right();
		Rectangle textRect(textLeft, bounds.top(),
			std::max(0L, textRight - textLeft), bounds.height());
		auto transparent = canvas.setBkMode(true);
		canvas.setTextColor(disabled ? colors.disabledText : colors.text);
		unsigned format = DT_END_ELLIPSIS |
			((style & BS_MULTILINE) ? DT_WORDBREAK :
				(DT_SINGLELINE | DT_VCENTER));
		switch(style & (BS_LEFT | BS_RIGHT | BS_CENTER)) {
		case BS_CENTER: format |= DT_CENTER; break;
		case BS_RIGHT: format |= DT_RIGHT; break;
		default: format |= DT_LEFT; break;
		}
		if(uiState & UISF_HIDEACCEL) format |= DT_HIDEPREFIX;
		if(isRightToLeft(button.handle())) format |= DT_RTLREADING;
		if(!text.empty()) {
			canvas.drawText(text, textRect, format);
		}
		if(focused && !disabled && !(uiState & UISF_HIDEFOCUS) &&
			!text.empty()) {
			const auto extent = canvas.getTextExtent(text);
			const long width = std::min(textRect.width(), extent.x + 4);
			const long height = std::min(textRect.height(), extent.y + 2);
			const long x = (format & DT_RIGHT) ? textRect.right() - width :
				(format & DT_CENTER) ? textRect.left() +
					(textRect.width() - width) / 2 : textRect.left();
			const long y = textRect.top() +
				std::max(0L, (textRect.height() - height) / 2);
			drawFocusFrame(canvas, Rectangle(x, y, width, height),
				colors.disabledText);
		}
		return CDRF_SKIPDEFAULT;
	}

	if(pushLike && checked) {
		pressed = true;
	}
	const bool defaultButton = buttonType == BS_DEFPUSHBUTTON ||
		buttonType == BS_DEFSPLITBUTTON ||
		buttonType == BS_DEFCOMMANDLINK ||
		(data.uItemState & CDIS_DEFAULT) != 0;
	const bool splitButton = buttonType == BS_SPLITBUTTON ||
		buttonType == BS_DEFSPLITBUTTON;
	const bool commandLink = buttonType == BS_COMMANDLINK ||
		buttonType == BS_DEFCOMMANDLINK;
	auto background = colors.surface;
	auto foreground = colors.text;
	auto border = defaultButton ? colors.accent : colors.border;
	if(defaultButton) {
		background = pressed ? Appearance::blend(colors.accent,
			RGB(0, 0, 0), 55) : hot ? Appearance::blend(colors.accent,
				RGB(0, 0, 0), 30) : colors.accent;
		foreground = colors.highlightText;
		border = background;
	} else if(pressed || hot) {
		background = Appearance::blend(colors.surface, colors.accent,
			pressed ? 38 : 20);
		border = Appearance::blend(colors.border, colors.accent,
			hot ? 100 : 70);
	}
	if(disabled) {
		background = Appearance::blend(colors.surface, colors.background, 120);
		foreground = Appearance::blend(colors.disabledText, colors.surface, 100);
		border = Appearance::blend(colors.border, colors.surface, 80);
	}
	drawSurface(canvas, bounds, background, border);

	auto margin = button.getTextMargin();
	Rectangle textRect(bounds.left() + std::max<long>(button.scale(5), margin.left()),
		bounds.top() + std::max<long>(2, margin.top()),
		std::max(0L, bounds.width() - std::max<long>(button.scale(10),
			margin.left() + margin.width())),
		std::max(0L, bounds.height() - std::max<long>(4,
			margin.top() + margin.height())));
	if(splitButton) {
		BUTTON_SPLITINFO info = { BCSIF_SIZE };
		button.sendMessage(BCM_GETSPLITINFO, 0,
			reinterpret_cast<LPARAM>(&info));
		const long arrowWidth = std::min<long>(info.size.cx > 0 ? info.size.cx :
			button.scale(20), std::max(0L, bounds.width() / 3));
		const long separator = bounds.right() - arrowWidth;
		Pen separatorPen(colors.border, Pen::Solid, 1);
		{
			auto selectSeparator = canvas.select(separatorPen);
			canvas.line(Point(separator, bounds.top() + 1),
				Point(separator, bounds.bottom() - 1));
		}
		drawArrow(canvas, Rectangle(separator, bounds.top(), arrowWidth,
			bounds.height()), true, false, foreground,
			std::max(2, button.scale(3)));
		textRect.size.x = std::max(0L,
			separator - textRect.left() - button.scale(3));
	}

	const auto text = button.getText();
	const auto image = getButtonImage(button, disabled, pressed, hot,
		defaultButton);
	const auto imageBounds = textRect;
	drawButtonImage(canvas, image, imageBounds, textRect,
		!text.empty(), disabled);
	if(pressed) textRect.pos += Point(1, 1);
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(foreground);
	if(commandLink && !text.empty()) {
		const auto note = button.getNote();
		auto bold = button.getFont()->makeBold();
		{
			auto selectFont = canvas.select(*bold);
			Rectangle titleRect = textRect;
			titleRect.size.y = std::min(titleRect.height(),
				canvas.getTextExtent(text).y + button.scale(3));
			unsigned format = DT_LEFT | DT_TOP | DT_SINGLELINE |
				DT_END_ELLIPSIS;
			if(uiState & UISF_HIDEACCEL) format |= DT_HIDEPREFIX;
			if(isRightToLeft(button.handle())) format |= DT_RTLREADING;
			canvas.drawText(text, titleRect, format);
			textRect.pos.y = titleRect.bottom();
			textRect.size.y = std::max(0L,
				bounds.bottom() - textRect.top() - button.scale(2));
		}
		if(!note.empty() && textRect.height() > 0) {
			auto selectFont = canvas.select(*button.getFont());
			canvas.setTextColor(disabled ? colors.disabledText : foreground);
			unsigned format = DT_LEFT | DT_TOP | DT_WORDBREAK |
				DT_END_ELLIPSIS | DT_NOPREFIX;
			if(isRightToLeft(button.handle())) format |= DT_RTLREADING;
			canvas.drawText(note, textRect, format);
		}
	} else if(!text.empty()) {
		auto selectFont = canvas.select(*button.getFont());
		auto format = buttonTextFormat(style);
		if(uiState & UISF_HIDEACCEL) format |= DT_HIDEPREFIX;
		if(isRightToLeft(button.handle())) format |= DT_RTLREADING;
		canvas.drawText(text, textRect, format);
	}
	if(focused && !disabled && !(uiState & UISF_HIDEFOCUS)) {
		drawFocusFrame(canvas, Rectangle(bounds.left() + 3,
			bounds.top() + 3, std::max(0L, bounds.width() - 6),
			std::max(0L, bounds.height() - 6)), foreground);
	}
	return CDRF_SKIPDEFAULT;
}

LRESULT drawHeader(Header& header, NMCUSTOMDRAW& data, const Appearance::Palette& configured)
{
	const auto colors = effectivePalette(header, configured);
	if(data.dwDrawStage == CDDS_PREPAINT) {
		return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
	}
	if(data.dwDrawStage == CDDS_POSTPAINT) {
		RECT nativeClient = { };
		::GetClientRect(header.handle(), &nativeClient);
		Rectangle client(nativeClient);
		long paintedRight = client.left();
		for(int i = 0, count = Header_GetItemCount(header.handle());
			i < count; ++i) {
			RECT nativeItem = { };
			if(Header_GetItemRect(header.handle(), i, &nativeItem)) {
				paintedRight = std::max(paintedRight,
					Rectangle(nativeItem).right());
			}
		}
		client.pos.x = std::min(paintedRight, client.right());
		client.size.x = std::max(0L, nativeClient.right - client.pos.x);
		FreeCanvas canvas(data.hdc);
		if(client.width() > 0) {
			Brush brush(colors.surface);
			canvas.fill(client, brush);
		}
		Pen border(colors.border, Pen::Solid, 1);
		auto selectBorder = canvas.select(border);
		canvas.line(Point(nativeClient.left, nativeClient.bottom - 1),
			Point(nativeClient.right, nativeClient.bottom - 1));
		return CDRF_DODEFAULT;
	}
	if(data.dwDrawStage != CDDS_ITEMPREPAINT || !data.hdc) {
		return CDRF_DODEFAULT;
	}

	TCHAR text[512] = { };
	HDITEM item = { HDI_TEXT | HDI_FORMAT | HDI_IMAGE | HDI_BITMAP };
	item.pszText = text;
	item.cchTextMax = static_cast<int>(_countof(text));
	if(!Header_GetItem(header.handle(), static_cast<int>(data.dwItemSpec),
		&item) || (item.fmt & HDF_OWNERDRAW)) {
		return CDRF_DODEFAULT;
	}

	auto background = colors.surface;
	if(data.uItemState & CDIS_HOT) {
		background = Appearance::blend(background, colors.accent, 25);
	}
	if(data.uItemState & CDIS_SELECTED) {
		background = Appearance::blend(background, colors.accent, 50);
	}
	const bool disabled = (data.uItemState & CDIS_DISABLED) != 0;
	FreeCanvas canvas(data.hdc);
	Rectangle bounds(data.rc);
	{
		Brush brush(background);
		canvas.fill(bounds, brush);
	}
	{
		Pen border(colors.border, Pen::Solid, 1);
		auto selectBorder = canvas.select(border);
		canvas.line(Point(bounds.right() - 1, bounds.top()),
			Point(bounds.right() - 1, bounds.bottom()));
		canvas.line(Point(bounds.left(), bounds.bottom() - 1),
			Point(bounds.right(), bounds.bottom() - 1));
	}

	Rectangle content(bounds.left() + header.scale(6), bounds.top(),
		std::max(0L, bounds.width() - header.scale(12)), bounds.height());
	if((::GetWindowLongPtr(header.handle(), GWL_STYLE) & HDS_FILTERBAR) != 0) {
		for(HWND child = ::GetWindow(header.handle(), GW_CHILD); child;
			child = ::GetWindow(child, GW_HWNDNEXT)) {
			RECT childRect = { };
			if(::GetWindowRect(child, &childRect)) {
				::MapWindowPoints(nullptr, header.handle(),
					reinterpret_cast<POINT*>(&childRect), 2);
				if(childRect.right > bounds.left() &&
					childRect.left < bounds.right()) {
					content.size.y = std::max(0L,
						childRect.top - content.top() - header.scale(1));
					break;
				}
			}
		}
	}

	const bool split = (item.fmt & HDF_SPLITBUTTON) != 0;
	const bool sortUp = (item.fmt & HDF_SORTUP) != 0;
	const bool sortDown = (item.fmt & HDF_SORTDOWN) != 0;
	if(split) {
		const long width = std::min<long>(header.scale(18),
			std::max(0L, content.width() / 3));
		Rectangle arrow(content.right() - width, content.top(), width,
			content.height());
		drawArrow(canvas, arrow, true, false,
			disabled ? colors.disabledText : colors.text,
			std::max(2, header.scale(3)));
		content.size.x = std::max(0L,
			arrow.left() - header.scale(2) - content.left());
	}
	if(sortUp || sortDown) {
		const long width = std::min<long>(header.scale(16),
			std::max(0L, content.width() / 3));
		Rectangle arrow(content.right() - width, content.top(), width,
			content.height());
		drawArrow(canvas, arrow, true, sortUp,
			disabled ? colors.disabledText : colors.text,
			std::max(2, header.scale(3)));
		content.size.x = std::max(0L,
			arrow.left() - header.scale(2) - content.left());
	}

	if(item.fmt & HDF_CHECKBOX) {
		const long size = std::min<long>(header.scale(13),
			std::max(0L, content.height() - header.scale(4)));
		Rectangle box(content.left(), content.top() +
			std::max(0L, (content.height() - size) / 2), size, size);
		drawSmallCheck(canvas, box, (item.fmt & HDF_CHECKED) != 0,
			false, disabled, colors);
		content.pos.x = box.right() + header.scale(5);
		content.size.x = std::max(0L,
			bounds.right() - header.scale(6) - content.left());
	}

	if((item.fmt & HDF_BITMAP) && item.hbm) {
		BITMAP bitmap = { };
		if(::GetObject(item.hbm, sizeof(bitmap), &bitmap)) {
			const bool right = (item.fmt & HDF_BITMAP_ON_RIGHT) != 0;
			const long x = right ? content.right() - bitmap.bmWidth :
				content.left();
			const long y = content.top() + std::max(0L,
				(content.height() - bitmap.bmHeight) / 2);
			::DrawState(canvas.handle(), nullptr, nullptr,
				reinterpret_cast<LPARAM>(item.hbm), 0, x, y,
				bitmap.bmWidth, bitmap.bmHeight,
				DST_BITMAP | (disabled ? DSS_DISABLED : DSS_NORMAL));
			if(right) {
				content.size.x = std::max(0L,
					x - header.scale(4) - content.left());
			} else {
				content.pos.x = x + bitmap.bmWidth + header.scale(4);
				content.size.x = std::max(0L,
					bounds.right() - header.scale(6) - content.left());
			}
		}
	}
	if((item.fmt & HDF_IMAGE) && item.iImage >= 0) {
		if(auto list = header.getImageListHandle()) {
			int width = 0;
			int height = 0;
			ImageList_GetIconSize(list, &width, &height);
			ImageList_Draw(list, item.iImage, canvas.handle(), content.left(),
				content.top() + std::max(0L,
					(content.height() - height) / 2), ILD_TRANSPARENT);
			content.pos.x += width + header.scale(4);
			content.size.x = std::max(0L,
				bounds.right() - header.scale(6) - content.left());
		}
	}

	if(text[0] && content.width() > 0 && content.height() > 0) {
		auto selectFont = canvas.select(*header.getFont());
		auto transparent = canvas.setBkMode(true);
		canvas.setTextColor(disabled ? colors.disabledText : colors.text);
		unsigned format = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
			DT_NOPREFIX;
		format |= (item.fmt & HDF_RIGHT) ? DT_RIGHT :
			(item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
		if(isRightToLeft(header.handle())) format |= DT_RTLREADING;
		canvas.drawText(text, content, format);
	}
	return CDRF_SKIPDEFAULT;
}

LRESULT drawToolBar(ToolBar& toolbar, NMTBCUSTOMDRAW& data, const Appearance::Palette& configured)
{
	const auto colors = effectivePalette(toolbar, configured);
	if(data.nmcd.dwDrawStage == CDDS_PREPAINT) {
		FreeCanvas canvas(data.nmcd.hdc);
		Brush brush(colors.background);
		canvas.fill(Rectangle(data.nmcd.rc), brush);
		data.clrBtnFace = colors.background;
		data.clrBtnHighlight = Appearance::blend(
			colors.surface, colors.text, 25);
		data.clrHighlightHotTrack = Appearance::blend(
			colors.surface, colors.accent, 35);
		data.clrText = colors.text;
		data.clrTextHighlight = colors.highlightText;
		data.clrMark = colors.accent;
		return CDRF_NOTIFYITEMDRAW | TBCDRF_USECDCOLORS;
	}
	if(data.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
		const bool hot = (data.nmcd.uItemState & CDIS_HOT) != 0;
		const bool pressed = (data.nmcd.uItemState &
			(CDIS_SELECTED | CDIS_CHECKED)) != 0;
		const bool disabled = (data.nmcd.uItemState & CDIS_DISABLED) != 0;
		FreeCanvas canvas(data.nmcd.hdc);
		Rectangle bounds(data.nmcd.rc);
		Brush brush(colors.background);
		canvas.fill(bounds, brush);
		if(hot || pressed) {
			auto face = Appearance::blend(colors.surface, colors.accent,
				pressed ? 58 : 26);
			if(disabled) {
				face = Appearance::blend(face, colors.background, 100);
			}
			drawSurface(canvas, bounds, face, Appearance::blend(
				colors.border, colors.accent, hot ? 125 : 175));
		}
		data.clrText = disabled ? colors.disabledText : colors.text;
		return TBCDRF_USECDCOLORS | TBCDRF_NOEDGES |
			TBCDRF_NOBACKGROUND;
	}
	return TBCDRF_USECDCOLORS | TBCDRF_NOEDGES;
}

void drawGroupBox(GroupBox& group, Canvas& canvas, const Appearance::Palette& configured)
{
	const auto colors = effectivePalette(group, configured);
	const Rectangle bounds(group.getClientSize());
	Brush background(colors.background);
	canvas.fill(bounds, background);
	auto selectFont = canvas.select(*group.getFont());
	const auto caption = group.getText();
	const auto textSize = canvas.getTextExtent(caption);
	const long top = std::max(1L, textSize.y / 2);
	const Rectangle frame(0, top, std::max(0L, bounds.width() - 1),
		std::max(0L, bounds.height() - top - 1));
	{
		Pen border(colors.border, Pen::Solid, 1);
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectBorder = canvas.select(border);
		auto selectHollow = canvas.select(hollow);
		canvas.rectangle(frame);
	}
	if(caption.empty()) {
		return;
	}

	const auto style = static_cast<DWORD>(::GetWindowLongPtr(
		group.handle(), GWL_STYLE));
	long textLeft = group.scale(13);
	const auto alignment = style & (BS_LEFT | BS_RIGHT | BS_CENTER);
	if(alignment == BS_CENTER) {
		textLeft = std::max(0L, (bounds.width() - textSize.x) / 2);
	} else if(alignment == BS_RIGHT) {
		textLeft = std::max<long>(group.scale(5),
			bounds.right() - textSize.x - group.scale(13));
	}
	Rectangle captionBackground(std::max(0L, textLeft - group.scale(5)), 0,
		textSize.x + group.scale(10), textSize.y + group.scale(2));
	canvas.fill(captionBackground, background);
	Rectangle textRect(textLeft, 0, textSize.x + group.scale(2),
		textSize.y + group.scale(2));
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(group.getEnabled() ? colors.text : colors.disabledText);
	unsigned format = DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS;
	if(group.sendMessage(WM_QUERYUISTATE) & UISF_HIDEACCEL) {
		format |= DT_HIDEPREFIX;
	}
	if(isRightToLeft(group.handle())) format |= DT_RTLREADING;
	canvas.drawText(caption, textRect, format);
}

bool canDrawComboBox(const ComboBox& combo) {
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(
		combo.handle(), GWL_STYLE));
	return (style & CBS_OWNERDRAWFIXED) == 0 &&
		(style & CBS_OWNERDRAWVARIABLE) == 0 &&
		(style & 0x0003) != CBS_SIMPLE;
}

void drawComboBox(ComboBox& combo, Canvas& canvas, const Appearance::Palette& configured)
{
	const auto colors = effectivePalette(combo, configured);
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(
		combo.handle(), GWL_STYLE));
	const Rectangle bounds(combo.getClientSize());
	const bool enabled = combo.getEnabled();
	const bool focused = ::GetFocus() == combo.handle() ||
		(combo.getTextBox() && ::GetFocus() == combo.getTextBox()->handle());
	const bool dropped = combo.sendMessage(CB_GETDROPPEDSTATE) != FALSE;
	const auto background = enabled ? colors.surface :
		Appearance::blend(colors.surface, colors.background, 110);
	const auto foreground = enabled ? colors.text : colors.disabledText;
	const auto border = focused || dropped ? colors.accent : colors.border;
	Brush backgroundBrush(background);
	canvas.fill(bounds, backgroundBrush);

	COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
	::GetComboBoxInfo(combo.handle(), &info);
	Rectangle arrow(info.rcButton);
	if(arrow.width() <= 0 || arrow.height() <= 0 ||
		arrow.right() > bounds.right() || arrow.bottom() > bounds.bottom()) {
		const long width = std::min<long>(std::max(combo.scale(22),
			static_cast<int>(bounds.height())), bounds.width());
		arrow = Rectangle(bounds.right() - width, bounds.top(), width,
			bounds.height());
	}
	const bool arrowHot = cursorIn(combo.handle(), arrow);
	if(dropped || arrowHot) {
		Brush arrowBackground(Appearance::blend(background, colors.accent,
			dropped ? 42 : 22));
		canvas.fill(arrow, arrowBackground);
	}
	{
		Pen separator(colors.border, Pen::Solid, 1);
		auto selectSeparator = canvas.select(separator);
		const long x = arrow.left() > bounds.left() ? arrow.left() :
			arrow.right() - 1;
		canvas.line(Point(x, arrow.top() + 1),
			Point(x, arrow.bottom() - 1));
	}
	drawArrow(canvas, arrow, true, false, foreground,
		std::max(2, combo.scale(4)));

	if((style & 0x0003) == CBS_DROPDOWNLIST) {
		Rectangle textRect(info.rcItem);
		if(textRect.width() <= 0 || textRect.height() <= 0) {
			textRect = Rectangle(bounds.left() + combo.scale(7), bounds.top(),
				std::max(0L, arrow.left() - bounds.left() - combo.scale(10)),
				bounds.height());
		} else {
			textRect.pos.x += combo.scale(4);
			textRect.size.x = std::max(0L,
				textRect.width() - combo.scale(7));
		}
		auto selectFont = canvas.select(*combo.getFont());
		auto transparent = canvas.setBkMode(true);
		canvas.setTextColor(foreground);
		const auto text = combo.getText();
		if(!text.empty()) {
			unsigned format = DT_LEFT | DT_VCENTER | DT_SINGLELINE |
				DT_END_ELLIPSIS | DT_NOPREFIX;
			if(isRightToLeft(combo.handle())) format |= DT_RTLREADING;
			canvas.drawText(text, textRect, format);
		}
	}
	{
		Pen borderPen(border, Pen::Solid, 1);
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectBorder = canvas.select(borderPen);
		auto selectHollow = canvas.select(hollow);
		canvas.rectangle(Rectangle(bounds.left(), bounds.top(),
			std::max(0L, bounds.width() - 1),
			std::max(0L, bounds.height() - 1)));
	}
}

void drawSpinner(Spinner& spinner, Canvas& canvas, const Appearance::Palette& configured)
{
	const auto colors = effectivePalette(spinner, configured);
	const Rectangle bounds(spinner.getClientSize());
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return;
	}
	const auto style = static_cast<DWORD>(::GetWindowLongPtr(
		spinner.handle(), GWL_STYLE));
	const bool horizontal = (style & UDS_HORZ) != 0;
	Rectangle first = bounds;
	Rectangle second = bounds;
	if(horizontal) {
		first.size.x /= 2;
		second.pos.x = first.right();
		second.size.x = bounds.right() - second.left();
	} else {
		first.size.y /= 2;
		second.pos.y = first.bottom();
		second.size.y = bounds.bottom() - second.top();
	}
	const bool firstHot = cursorIn(spinner.handle(), first);
	const bool secondHot = cursorIn(spinner.handle(), second);
	const bool pressed = ::GetCapture() == spinner.handle() &&
		(::GetKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool enabled = spinner.getEnabled();
	auto partColor = [&](bool hot) {
		auto result = hot ? Appearance::blend(colors.surface, colors.accent,
			pressed ? 48 : 24) : colors.surface;
		return enabled ? result : Appearance::blend(result,
			colors.background, 100);
	};
	{
		Brush brush(partColor(firstHot));
		canvas.fill(first, brush);
	}
	{
		Brush brush(partColor(secondHot));
		canvas.fill(second, brush);
	}
	{
		Pen border(colors.border, Pen::Solid, 1);
		auto selectBorder = canvas.select(border);
		if(horizontal) {
			canvas.line(Point(first.right(), bounds.top() + 1),
				Point(first.right(), bounds.bottom() - 1));
		} else {
			canvas.line(Point(bounds.left() + 1, first.bottom()),
				Point(bounds.right() - 1, first.bottom()));
		}
		Brush hollow(static_cast<HBRUSH>(::GetStockObject(HOLLOW_BRUSH)), false);
		auto selectHollow = canvas.select(hollow);
		canvas.rectangle(Rectangle(bounds.left(), bounds.top(),
			std::max(0L, bounds.width() - 1),
			std::max(0L, bounds.height() - 1)));
	}
	const auto arrow = enabled ? colors.text : colors.disabledText;
	drawArrow(canvas, first, !horizontal, true, arrow,
		std::max(2, spinner.scale(3)));
	drawArrow(canvas, second, !horizontal, false, arrow,
		std::max(2, spinner.scale(3)));
}

bool canDrawStatusBar(const StatusBar& status) {
	const bool simple = status.sendMessage(SB_ISSIMPLE) != FALSE;
	const int count = simple ? 1 :
		static_cast<int>(status.sendMessage(SB_GETPARTS));
	for(int i = 0; i < count; ++i) {
		const WPARAM part = simple ? SB_SIMPLEID : static_cast<WPARAM>(i);
		const auto info = status.sendMessage(SB_GETTEXTLENGTH, part);
		if(HIWORD(info) & SBT_OWNERDRAW) {
			return false;
		}
	}
	return true;
}

void drawStatusBar(StatusBar& status, Canvas& canvas, const Appearance::Palette& configured)
{
	const auto colors = effectivePalette(status, configured);
	const Rectangle bounds(status.getClientSize());
	Brush surface(colors.surface);
	canvas.fill(bounds, surface);
	{
		Pen top(colors.border, Pen::Solid, 1);
		auto selectTop = canvas.select(top);
		canvas.line(Point(bounds.left(), bounds.top()),
			Point(bounds.right(), bounds.top()));
	}
	auto selectFont = canvas.select(*status.getFont());
	auto transparent = canvas.setBkMode(true);
	canvas.setTextColor(status.getEnabled() ? colors.text :
		colors.disabledText);

	const bool simple = status.sendMessage(SB_ISSIMPLE) != FALSE;
	const int count = simple ? 1 :
		static_cast<int>(status.sendMessage(SB_GETPARTS));
	for(int i = 0; i < count; ++i) {
		const WPARAM index = simple ? SB_SIMPLEID : static_cast<WPARAM>(i);
		RECT nativePart = { };
		if(simple) {
			nativePart = bounds;
		} else if(!status.sendMessage(SB_GETRECT, index,
			reinterpret_cast<LPARAM>(&nativePart))) {
			continue;
		}
		Rectangle part(nativePart);
		const auto textInfo = status.sendMessage(SB_GETTEXTLENGTH, index);
		const auto textLength = LOWORD(textInfo);
		const auto textStyle = HIWORD(textInfo);
		std::vector<TCHAR> text(textLength + 1, 0);
		if(textLength) {
			status.sendMessage(SB_GETTEXT, index,
				reinterpret_cast<LPARAM>(text.data()));
		}

		long left = part.left() + status.scale(5);
		if(auto icon = reinterpret_cast<HICON>(status.sendMessage(
			SB_GETICON, index))) {
			const int width = status.getSystemMetric(SM_CXSMICON);
			const int height = status.getSystemMetric(SM_CYSMICON);
			::DrawIconEx(canvas.handle(), left, part.top() +
				std::max(0L, (part.height() - height) / 2), icon,
				width, height, 0, nullptr, DI_NORMAL);
			left += width + status.scale(4);
		}
		if(textLength) {
			Rectangle textRect(left, part.top(), std::max(0L,
				part.right() - left - status.scale(4)), part.height());
			unsigned format = DT_LEFT | DT_VCENTER | DT_SINGLELINE |
				DT_END_ELLIPSIS | DT_NOPREFIX;
			if(textStyle & SBT_RTLREADING) format |= DT_RTLREADING;
			canvas.drawText(text.data(), textRect, format);
		}
		if(!(textStyle & SBT_NOBORDERS) && !simple) {
			Pen border(colors.border, Pen::Solid, 1);
			auto selectBorder = canvas.select(border);
			canvas.line(Point(part.right() - 1, part.top() + 2),
				Point(part.right() - 1, part.bottom() - 2));
			if(textStyle & SBT_POPOUT) {
				const auto light = Appearance::blend(colors.border,
					colors.text, 45);
				Pen highlight(light, Pen::Solid, 1);
				auto selectHighlight = canvas.select(highlight);
				canvas.line(Point(part.left(), part.top() + 1),
					Point(part.left(), part.bottom() - 1));
			}
		}
	}

	const auto parent = status.getParent();
	const bool parentGrip = parent &&
		(::GetWindowLongPtr(parent->handle(), GWL_STYLE) & WS_THICKFRAME) &&
		!::IsZoomed(parent->handle());
	if((::GetWindowLongPtr(status.handle(), GWL_STYLE) & SBARS_SIZEGRIP) ||
		parentGrip) {
		Pen grip(colors.border, Pen::Solid, 1);
		auto selectGrip = canvas.select(grip);
		for(int offset = status.scale(4); offset <= status.scale(12);
			offset += std::max(1, status.scale(4))) {
			canvas.line(Point(bounds.right() - offset, bounds.bottom() - 1),
				Point(bounds.right() - 1, bounds.bottom() - offset));
		}
	}
}

} }
