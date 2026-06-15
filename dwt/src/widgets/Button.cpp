/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka

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

namespace dwt {

const TCHAR Button::windowClass[] = WC_BUTTON;

Button::Seed::Seed(const tstring& caption, DWORD style) :
BaseType::Seed(style | WS_CHILD | WS_TABSTOP, 0, caption),
font(0),
padding(3, 2)
{
}

Button::Button(Widget* parent) :
	BaseType(parent, ChainingDispatcher::superClass<ThisType>())
{
}

void Button::create(const Seed& cs) {
	BaseType::create(cs);
	setFont(cs.font);

	auto padding = scale(cs.padding);
	::RECT rect = { padding.x, padding.y, padding.x, padding.y };
	sendMessage(BCM_SETTEXTMARGIN, 0, reinterpret_cast<LPARAM>(&rect));

	onDpiResourcesChanged([this](const DpiResourceEvent& event) {
		if(!imageList) {
			return;
		}
		BUTTON_IMAGELIST current = { };
		if(!sendMessage(BCM_GETIMAGELIST, 0,
			reinterpret_cast<LPARAM>(&current))) {
			return;
		}
		auto resized = imageList->resized(
			event.scale(imageList->getImageSize()));
		Rectangle margin(current.margin);
		setImageList(resized, current.uAlign,
			Rectangle(event.scale(margin.pos), event.scale(margin.size)));
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
	BUTTON_IMAGELIST value = { imageList ? imageList->handle() : nullptr,
		margin.toRECT(), alignment };
	sendMessage(BCM_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(&value));
}

void Button::setSplitInfo(const BUTTON_SPLITINFO& info) {
	sendMessage(BCM_SETSPLITINFO, 0, reinterpret_cast<LPARAM>(&info));
}

void Button::onDropDown(std::function<void (const RECT&)> f) {
	addCallback(Message(WM_NOTIFY, BCN_DROPDOWN), [f](const MSG& msg, LRESULT&) -> bool {
		f(reinterpret_cast<NMBCDROPDOWN*>(msg.lParam)->rcButton);
		return true;
	});
}

Point Button::getPreferredSize() {
	SIZE size = { 0 };
	sendMessage(BCM_GETIDEALSIZE, 0, reinterpret_cast<LPARAM>(&size));
	return Point(size.cx, size.cy);
}

}
