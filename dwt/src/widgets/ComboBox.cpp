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

#include <dwt/widgets/ComboBox.h>

#include <dwt/WidgetCreator.h>
#include <dwt/widgets/TextBox.h>

#include "AppearanceDraw.h"

namespace dwt {

const TCHAR ComboBox::windowClass[] = WC_COMBOBOX;

const TCHAR ComboBox::DropListBox::windowClass[] = _T("ComboLBox"); // undocumented

ComboBox::Seed::Seed() :
BaseType::Seed(CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_CHILD | WS_TABSTOP | WS_VSCROLL),
font(0),
extended(true)
{
}

ComboBox::ComboBox(Widget* parent) :
	BaseType(parent, ChainingDispatcher::superClass<ComboBox>()),
	listBox(nullptr),
	textBox(nullptr)
{
}

void ComboBox::create( const Seed & cs ) {
	BaseType::create(cs);
	setFont(cs.font);
	if(cs.extended)
		sendMessage(CB_SETEXTENDEDUI, TRUE);

	/* Combo boxes create their edit and drop-list HWNDs internally. Wrapping them
	keeps their color handling and appearance lifetime within DWT. */
	getListBox();
	getTextBox();
}

tstring ComboBox::getValue( int index ) {
	// Uses CB_GETLBTEXTLEN and CB_GETLBTEXT
	int txtLength = ComboBox_GetLBTextLen( handle(), index );
	tstring retVal(txtLength, '\0');
	ComboBox_GetLBText( handle(), index, &retVal[0] );
	return retVal;
}

int ComboBox::findString(const tstring& text) {
	return ComboBox_FindStringExact(handle(), -1, text.c_str()); 
}

ComboBox::DropListBoxPtr ComboBox::getListBox() {
	if(!listBox) {
		COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
		if(::GetComboBoxInfo(handle(), &info) && info.hwndList && info.hwndList != handle()) {
			/* unlike other controls, the list window doesn't send/receive WM_DESTROY/WN_NCDESTROY.
			as a result, the listBox widget leaks. the workaround is to "detach" from the list (by
			giving it back its initial window procedure) when the combo is being destroyed. */
			auto proc = ::GetWindowLongPtr(info.hwndList, GWLP_WNDPROC);
			listBox = WidgetCreator<DropListBox>::attach(this, info.hwndList);
			listBox->onDestroy([this] { listBox = nullptr; });
			onDestroy([this, proc] { if(listBox) { ::SetWindowLongPtr(listBox->handle(), GWLP_WNDPROC, proc); listBox->kill(); } });
		}
	}
	return listBox;
}

TextBoxPtr ComboBox::getTextBox() {
	if(!textBox) {
		COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };
		if(::GetComboBoxInfo(handle(), &info) && info.hwndItem && info.hwndItem != handle()) {
			/* combo edits do receive destruction messages, but we do the same crap as for lists
			to be on the safe side. */
			auto proc = ::GetWindowLongPtr(info.hwndItem, GWLP_WNDPROC);
			textBox = WidgetCreator<TextBox>::attach(this, info.hwndItem);
			textBox->onDestroy([this] { textBox = nullptr; });
			onDestroy([this, proc] { if(textBox) { ::SetWindowLongPtr(textBox->handle(), GWLP_WNDPROC, proc); textBox->kill(); } });
		}
	}
	return textBox;
}

void ComboBox::setColorImpl(COLORREF text, COLORREF background) {
	BaseType::setColorImpl(text, background);
	if(handle()) {
		if(auto list = getListBox()) {
			list->setColor(text, background);
		}
		if(auto edit = getTextBox()) {
			edit->setColor(text, background);
		}
	}
}

void ComboBox::clearColorImpl() {
	BaseType::clearColorImpl();
	if(handle()) {
		if(auto list = getListBox()) {
			list->clearColor();
		}
		if(auto edit = getTextBox()) {
			edit->clearColor();
		}
	}
}

bool ComboBox::handleMessage(const MSG& msg, LRESULT& retVal) {
	const bool handled = BaseType::handleMessage(msg, retVal);
	if(handled || !isManualAppearance() ||
		getAppearancePolicy() == AppearancePolicy::Native ||
		!appearance_detail::canDrawComboBox(*this)) {
		return handled;
	}

	switch(msg.message) {
	case WM_ERASEBKGND:
		retVal = TRUE;
		return true;

	case WM_PAINT:
		{
			RECT update = { };
			if(!::GetUpdateRect(handle(), &update, FALSE)) {
				::GetClientRect(handle(), &update);
			}
			const Rectangle paint(update);
			BufferedCanvas<PaintCanvas> canvas(this, paint);
			appearance_detail::drawComboBox(*this, canvas,
				getAppearance().getPalette());
			canvas.blast(paint);
			retVal = 0;
			return true;
		}

	case WM_PRINTCLIENT:
		if(msg.wParam) {
			FreeCanvas canvas(reinterpret_cast<HDC>(msg.wParam));
			appearance_detail::drawComboBox(*this, canvas,
				getAppearance().getPalette());
			retVal = 0;
			return true;
		}
		break;
	}
	return false;
}

Point ComboBox::getPreferredSize() {
	// Pixels between text and arrow
	const int MARGIN = 2;

	UpdateCanvas c(this);
	auto select(c.select(*getFont()));
	Point ret;
	for(size_t i = 0; i < size(); ++i) {
		Point ext = c.getTextExtent(getValue(static_cast<int>(i)));
		ret.x = std::max(ret.x, ext.x);
	}

	ret.x += ::GetSystemMetrics(SM_CYFIXEDFRAME) * 2 + ::GetSystemMetrics(SM_CXSMICON) + MARGIN;
	ret.y = static_cast<long>(sendMessage(CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0)) + ::GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
	return ret;
}

LPARAM ComboBox::getDataImpl(int i) {
	return sendMessage(CB_GETITEMDATA, i);
}

void ComboBox::setDataImpl(int i, LPARAM data) {
	sendMessage(CB_SETITEMDATA, i, data);
}

}
