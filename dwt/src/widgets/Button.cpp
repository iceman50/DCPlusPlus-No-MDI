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

#include <dwt/widgets/Button.h>

#include <dwt/CanvasClasses.h>
#include <dwt/resources/ImageList.h>
#include <dwt/util/check.h>

#include "AppearanceDraw.h"

namespace dwt {

const TCHAR Button::windowClass[] = WC_BUTTON;

Button::Seed::Seed(const tstring& caption, DWORD style) :
BaseType::Seed(style | WS_CHILD | WS_TABSTOP, 0, caption),
font(0),
padding(3, 2)
{
}

Button::Button(Widget* parent) :
	BaseType(parent, ChainingDispatcher::superClass<ThisType>()),
	imageAlignment(BUTTON_IMAGELIST_ALIGN_LEFT),
	dropDownState(false),
	nativeDropDownState(false)
{
}

void Button::create(const Seed& cs) {
	BaseType::create(cs);
	setFont(cs.font);

	auto padding = scale(cs.padding);
	setTextMargin(Rectangle(padding.x, padding.y, 0, 0));

	onDpiResourcesChanged([this](const DpiResourceEvent& event) {
		if(imageList) {
			auto resized = imageList->resized(
				event.scale(imageList->getImageSize()));
			setImageList(resized, imageAlignment,
				Rectangle(event.scale(imageMargin.pos),
					event.scale(imageMargin.size)));
		}
		setTextMargin(Rectangle(event.scale(textMargin.pos),
			event.scale(textMargin.size)));
	});
}

void Button::setImage(BitmapPtr bitmap) {
	sendMessage(BM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap->handle()));
}

void Button::setImage(IconPtr icon) {
	sendMessage(BM_SETIMAGE, IMAGE_ICON, reinterpret_cast<LPARAM>(icon->handle()));
}

void Button::setNote(const tstring& note) {
	sendMessage(BCM_SETNOTE, 0, reinterpret_cast<LPARAM>(note.c_str()));
}

tstring Button::getNote() const {
	auto length = static_cast<size_t>(sendMessage(BCM_GETNOTELENGTH));
	std::vector<TCHAR> note(length + 1);
	DWORD size = static_cast<DWORD>(note.size());
	sendMessage(BCM_GETNOTE, reinterpret_cast<WPARAM>(&size),
		reinterpret_cast<LPARAM>(note.data()));
	return note.data();
}

void Button::setElevationRequired(bool required) {
	sendMessage(BCM_SETSHIELD, 0, required ? TRUE : FALSE);
}

void Button::setImageList(const ImageListPtr& images, UINT alignment,
	const Rectangle& margin)
{
	imageList = images;
	imageAlignment = alignment;
	imageMargin = margin;
	BUTTON_IMAGELIST value = { imageList ? imageList->handle() : nullptr,
		margin.toRECT(), alignment };
	sendMessage(BCM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(&value));
}

bool Button::getImageListInfo(BUTTON_IMAGELIST& info) const {
	info = { };
	if(sendMessage(BCM_GETIMAGELIST, 0,
		reinterpret_cast<LPARAM>(&info))) {
		return true;
	}
	if(!imageList) {
		return false;
	}
	info.himl = imageList->handle();
	info.margin = imageMargin.toRECT();
	info.uAlign = imageAlignment;
	return true;
}

void Button::setTextMargin(const Rectangle& margin) {
	textMargin = margin;
	auto value = margin.toRECT();
	sendMessage(BCM_SETTEXTMARGIN, 0, reinterpret_cast<LPARAM>(&value));
}

Rectangle Button::getTextMargin() const {
	RECT value = { };
	if(!sendMessage(BCM_GETTEXTMARGIN, 0, reinterpret_cast<LPARAM>(&value))) {
		return textMargin;
	}
	return Rectangle(value);
}

void Button::setSplitInfo(const BUTTON_SPLITINFO& info) {
	sendMessage(BCM_SETSPLITINFO, 0, reinterpret_cast<LPARAM>(&info));
}

void Button::setDropDownState(bool dropped) {
	dropDownState = dropped;
	nativeDropDownState = sendMessage(BCM_SETDROPDOWNSTATE,
		dropped ? TRUE : FALSE) != FALSE;
}

bool Button::getDropDownState() const {
	return nativeDropDownState ?
		(sendMessage(BM_GETSTATE) & BST_DROPDOWNPUSHED) != 0 : dropDownState;
}

void Button::onDropDown(std::function<void (const RECT&)> f) {
	addCallback(Message(WM_NOTIFY, BCN_DROPDOWN), [f](const MSG& msg, LRESULT&) -> bool {
		auto data = reinterpret_cast<NMBCDROPDOWN*>(msg.lParam);
		if(!data) {
			return false;
		}
		f(data->rcButton);
		return true;
	});
}

void Button::onHotChanged(std::function<void (bool, DWORD)> f) {
	addCallback(Message(WM_NOTIFY, BCN_HOTITEMCHANGE),
		[this, f](const MSG& msg, LRESULT&) -> bool {
			auto data = reinterpret_cast<NMBCHOTITEM*>(msg.lParam);
			if(!data) {
				return false;
			}
			const bool hot = (data->dwFlags & HICF_ENTERING) != 0 ? true :
				(data->dwFlags & HICF_LEAVING) != 0 ? false : isHot();
			f(hot, data->dwFlags);
			return true;
		});
}

void Button::onFocusChanged(std::function<void (bool)> f) {
	addCallback(Message(WM_COMMAND, BN_SETFOCUS),
		[f](const MSG&, LRESULT&) -> bool {
			f(true);
			return false;
		});
	addCallback(Message(WM_COMMAND, BN_KILLFOCUS),
		[f](const MSG&, LRESULT&) -> bool {
			f(false);
			return false;
		});
}

bool Button::isHot() const {
	return (sendMessage(BM_GETSTATE) & BST_HOT) != 0;
}

bool Button::handleMessage(const MSG& msg, LRESULT& retVal) {
	const bool handled = BaseType::handleMessage(msg, retVal);
	if(msg.message != WM_NOTIFY || !msg.lParam ||
		reinterpret_cast<NMHDR*>(msg.lParam)->code != NM_CUSTOMDRAW ||
		!isManualAppearance() ||
		getAppearancePolicy() == AppearancePolicy::Native) {
		return handled;
	}

	/* A non-default result belongs to an application custom-draw callback. */
	if(handled && retVal != 0 && retVal != CDRF_DODEFAULT) {
		return true;
	}
	retVal = appearance_detail::drawButton(*this,
		*reinterpret_cast<NMCUSTOMDRAW*>(msg.lParam),
		getAppearance().getPalette());
	return retVal != CDRF_DODEFAULT || handled;
}

Point Button::getPreferredSize() {
	SIZE size = { 0 };
	if(!sendMessage(BCM_GETIDEALSIZE, 0, reinterpret_cast<LPARAM>(&size))) {
		dwtassert(false, "Button: BCM_GETIDEALSIZE failed");
	}
	return Point(size.cx, size.cy);
}

}
