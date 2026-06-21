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

#include <dwt/widgets/Notification.h>

#include <dwt/Application.h>
#include <dwt/Widget.h>

namespace dwt {

const UINT Notification::message = ::RegisterWindowMessage(_T("dwt::Notification"));

static const UINT taskbarMsg = ::RegisterWindowMessage(_T("TaskbarCreated"));

NOTIFYICONDATA Notification::makeNID() const {
	NOTIFYICONDATA nid = { static_cast<DWORD>(sizeof(NOTIFYICONDATA)), parent->handle() };
	nid.uFlags |= NIF_SHOWTIP;
	if(guid) {
		nid.uFlags |= NIF_GUID;
		nid.guidItem = *guid;
	}
	return nid;
}

Notification::Notification(Widget* parent) :
parent(parent),
visible(false),
ignoreNextClick(false),
lastClickTick(0),
lastNotifyError(ERROR_SUCCESS),
onlyBalloons(false),
lastTick(0)
{
}

Notification::~Notification() {
	setVisible(false);
}

void Notification::create(const Notification::Seed& seed) {
	icon = seed.icon;
	tip = seed.tip;

	// TODO Allow more than one icon per window
	parent->setCallback(Message(message), [this](const MSG& msg, LRESULT&) { return trayHandler(msg); });
	parent->setCallback(Message(taskbarMsg), [this](const MSG&, LRESULT&) { return redisplay(); });
}

void Notification::setIcon(const IconPtr& icon_) {
	icon = icon_;
	redisplay();
}

void Notification::setVisible(bool visible_) {
	if(visible == visible_) {
		if(visible && onlyBalloons)
			onlyBalloons = false;
		return;
	}

	NOTIFYICONDATA nid = makeNID();

	if(visible_) {
		nid.uFlags |= NIF_MESSAGE;
		nid.uCallbackMessage = message;

		if(!tip.empty()) {
			nid.uFlags |= NIF_TIP;
			tip.copy(nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]) - 1);
		}

		if(icon) {
			nid.uFlags |= NIF_ICON;
			nid.hIcon = icon->handle();
		}

		if(!notify(NIM_ADD, nid)) {
			visible = false;
			return;
		}
		nid.uVersion = NOTIFYICON_VERSION_4;
		if(!notify(NIM_SETVERSION, nid)) {
			notify(NIM_DELETE, nid);
			visible = false;
			return;
		}
		visible = true;

	} else {
		notify(NIM_DELETE, nid);
		visible = false;
	}
}

void Notification::setTooltip(const tstring& tip_) {
	tip = tip_;
	lastTick = ::GetTickCount64();

	if(visible) {
		NOTIFYICONDATA nid = makeNID();
		nid.uFlags |= NIF_TIP;
		tip.copy(nid.szTip, sizeof(nid.szTip) / sizeof(nid.szTip[0]) - 1);
		notify(NIM_MODIFY, nid);
	}
}

void Notification::addMessage(const tstring& title, const tstring& msg, const Callback& callback, const IconPtr& balloonIcon) {
	MessageOptions options;
	options.callback = callback;
	options.balloonIcon = balloonIcon;
	addMessage(title, msg, options);
}

void Notification::addMessage(const tstring& title, const tstring& msg,
	const MessageOptions& options)
{
	if(!visible) {
		setVisible(true);
		if(!visible) {
			return;
		}
		onlyBalloons = true;
	}

	balloons.emplace_back(options.callback, options.balloonIcon);

	NOTIFYICONDATA nid = makeNID();
	nid.uFlags |= NIF_INFO;
	if(options.realTime) {
		nid.uFlags |= NIF_REALTIME;
	}

	msg.copy(nid.szInfo, sizeof(nid.szInfo) / sizeof(nid.szInfo[0]) - 1);

	title.copy(nid.szInfoTitle, sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]) - 1);

	if(options.balloonIcon) {
		nid.dwInfoFlags = NIIF_USER;
		nid.hBalloonIcon = options.balloonIcon->handle();
	} else {
		nid.dwInfoFlags = NIIF_INFO;
	}

	if(options.respectQuietTime) {
		nid.dwInfoFlags |= NIIF_RESPECT_QUIET_TIME;
	}
	if(options.largeIcon) {
		nid.dwInfoFlags |= NIIF_LARGE_ICON;
	}
	if(options.noSound) {
		nid.dwInfoFlags |= NIIF_NOSOUND;
	}

	if(!notify(NIM_MODIFY, nid)) {
		if(!balloons.empty()) {
			balloons.pop_back();
		}
		if(onlyBalloons && balloons.empty()) {
			setVisible(false);
		}
	}
}

void Notification::setGuid(const GUID& value) {
	const bool wasVisible = visible;
	if(wasVisible) {
		auto nid = makeNID();
		notify(NIM_DELETE, nid);
		visible = false;
	}
	guid = value;
	if(wasVisible) {
		setVisible(true);
	}
}

void Notification::clearGuid() {
	const bool wasVisible = visible;
	if(wasVisible) {
		auto nid = makeNID();
		notify(NIM_DELETE, nid);
		visible = false;
	}
	guid.reset();
	if(wasVisible) {
		setVisible(true);
	}
}

bool Notification::redisplay() {
	if(visible) {
		auto nid = makeNID();
		notify(NIM_DELETE, nid);
		visible = false;
		setVisible(true);
	}
	return false;
}

bool Notification::trayHandler(const MSG& msg) {
	switch(LOWORD(msg.lParam)) {

	case WM_LBUTTONUP:
	case NIN_SELECT:
	case NIN_KEYSELECT:
		{
			auto now = ::GetTickCount64();
			if(ignoreNextClick) {
				ignoreNextClick = false;
			} else if(now - lastClickTick >= 250 && iconClicked) {
				lastClickTick = now;
				iconClicked();
			}
			break;
		}

	case WM_LBUTTONDBLCLK:
		{
			if(iconDbClicked) {
				iconDbClicked();
				ignoreNextClick = true;
				lastClickTick = ::GetTickCount64();
			}
			break;
		}

	case WM_RBUTTONUP:
		{
			if(contextMenu) {
				// Work-around for windows bug (KB135788)
				::SetForegroundWindow(parent->handle());
				contextMenu();
				parent->postMessage(WM_NULL);
			}
			break;
		}

	case WM_MOUSEMOVE:
		{
			if(updateTip) {
				auto now = ::GetTickCount64();
				if(now - 1000 > lastTick) {
					updateTip();
					lastTick = now;
				}
			}
			break;
		}

	case NIN_BALLOONUSERCLICK:
		{
			if(!balloons.empty() && balloons.front().first) {
				balloons.front().first();
			}
		} // fall through
	case NIN_BALLOONHIDE: // fall through
	case NIN_BALLOONTIMEOUT:
		{
			if(!balloons.empty()) {
				balloons.pop_front();
			}
			if(onlyBalloons && balloons.empty()) {
				setVisible(false);
			}
			break;
		}

	case NIN_BALLOONSHOW:
		if(balloonShown) {
			balloonShown();
		}
		break;

	case NIN_POPUPOPEN:
		if(popupOpened) {
			popupOpened();
		}
		break;

	case NIN_POPUPCLOSE:
		if(popupClosed) {
			popupClosed();
		}
		break;
	}

	return true;
}

void Notification::setFocus() {
	auto nid = makeNID();
	notify(NIM_SETFOCUS, nid);
}

Rectangle Notification::getRect() const {
	NOTIFYICONIDENTIFIER identifier = { sizeof(NOTIFYICONIDENTIFIER), parent->handle(), 0 };
	if(guid) {
		identifier.guidItem = *guid;
	}
	RECT rect = { 0 };
	return SUCCEEDED(::Shell_NotifyIconGetRect(&identifier, &rect)) ?
		Rectangle(rect) : Rectangle();
}

bool Notification::notify(DWORD message_, NOTIFYICONDATA& nid) {
	if(::Shell_NotifyIcon(message_, &nid)) {
		lastNotifyError = ERROR_SUCCESS;
		return true;
	}
	lastNotifyError = ::GetLastError();
	if(lastNotifyError == ERROR_SUCCESS) {
		lastNotifyError = ERROR_INVALID_FUNCTION;
	}
	return false;
}

}
