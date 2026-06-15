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

#include <dwt/widgets/ScrolledContainer.h>

#include <algorithm>
#include <utility>

namespace dwt {

namespace {

int scrollDelta(accessibility::ScrollAmount amount, int page, int line) {
	switch(amount) {
	case accessibility::LargeDecrement: return -page;
	case accessibility::SmallDecrement: return -line;
	case accessibility::LargeIncrement: return page;
	case accessibility::SmallIncrement: return line;
	default: return 0;
	}
}

}

Point ScrolledContainer::getPreferredSize() {
	auto child = getChild();
	return child ? child->getPreferredSize() : Point(0, 0);
}

void ScrolledContainer::layout() {
	BaseType::layout();
	auto child = getChild();
	if(!child) {
		return;
	}

	auto clientSize = getClientSize();
	auto childSize = child->getPreferredSize();

	::ShowScrollBar(handle(), SB_HORZ, childSize.x > clientSize.x);
	::ShowScrollBar(handle(), SB_VERT, childSize.y > clientSize.y);

	clientSize = getClientSize();

	setScrollInfo(SB_HORZ, clientSize.x, std::max(childSize.x, clientSize.x));
	setScrollInfo(SB_VERT, clientSize.y, std::max(childSize.y, clientSize.y));

	child->resize(Rectangle(0, 0, std::max(childSize.x, clientSize.x), std::max(childSize.y, clientSize.y)));
}

void ScrolledContainer::setScrollInfo(int type, int page, int max, int pos) {
	SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL, 0, max - 1, static_cast<UINT>(page), pos };
	::SetScrollInfo(handle(), type, &si, TRUE);
}

bool ScrolledContainer::handleMessage(const MSG &msg, LRESULT &retVal) {
	if(!(msg.message == WM_HSCROLL || msg.message == WM_VSCROLL)) {
		return false;
	}

	auto child = getChild();

	if(!child) {
		return false;
	}

	SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };

	auto type = msg.message == WM_HSCROLL ? SB_HORZ : SB_VERT;
    ::GetScrollInfo(handle(), type, &si);

    auto orig = si.nPos;

    switch (LOWORD (msg.wParam)) {
    case SB_TOP:
    	si.nPos = 0;
    	break;
    case SB_BOTTOM:
    	si.nPos = si.nMax;
    	break;
    case SB_LINELEFT:
        si.nPos -= scale(10);
        break;
    case SB_LINERIGHT:
        si.nPos += scale(10);
        break;
    case SB_PAGELEFT:
        si.nPos -= si.nPage;
        break;
    case SB_PAGERIGHT:
        si.nPos += si.nPage;
        break;
    case SB_THUMBTRACK:
        si.nPos = si.nTrackPos;
        break;
    }

    ::SetScrollInfo(handle(), type, &si, TRUE);

	::GetScrollInfo(handle(), type, &si);
	auto hDiff = type == SB_HORZ ? orig - si.nPos : 0;
	auto vDiff = type == SB_VERT ? orig - si.nPos : 0;

	::ScrollWindowEx(handle(), hDiff, vDiff, NULL, NULL, NULL, NULL, SW_INVALIDATE | SW_SCROLLCHILDREN);

	return true;
}

void ScrolledContainer::create(const Seed& cs) {
	BaseType::create(cs);
	onWindowPosChanged([this] (const Rectangle &) { this->layout(); });
	if(getAccessibleName().empty()) {
		setAccessibleName(_T("Scrollable container"));
	}
	setAccessibleKeyboardFocusable(true);
	onKeyDown([this](int key) { return handleKeyDown(key); });

	accessibility::ScrollProvider provider;
	provider.scroll = [this](accessibility::ScrollAmount horizontal,
		accessibility::ScrollAmount vertical) {
		scrollAccessible(horizontal, vertical);
	};
	provider.setPercent = [this](double horizontal, double vertical) {
		setAccessibleScrollPercent(horizontal, vertical);
	};
	provider.horizontalPercent = [this] {
		return getAccessibleScrollPercent(SB_HORZ);
	};
	provider.verticalPercent = [this] {
		return getAccessibleScrollPercent(SB_VERT);
	};
	provider.horizontalViewSize = [this] {
		return getAccessibleViewSize(SB_HORZ);
	};
	provider.verticalViewSize = [this] {
		return getAccessibleViewSize(SB_VERT);
	};
	provider.horizontallyScrollable = [this] {
		return isAccessibleScrollable(SB_HORZ);
	};
	provider.verticallyScrollable = [this] {
		return isAccessibleScrollable(SB_VERT);
	};
	setAccessibleScroll(provider);
}

bool ScrolledContainer::handleKeyDown(int key) {
	switch(key) {
	case VK_LEFT:
		scrollAccessible(accessibility::SmallDecrement,
			accessibility::NoAmount);
		return true;
	case VK_RIGHT:
		scrollAccessible(accessibility::SmallIncrement,
			accessibility::NoAmount);
		return true;
	case VK_UP:
		scrollAccessible(accessibility::NoAmount,
			accessibility::SmallDecrement);
		return true;
	case VK_DOWN:
		scrollAccessible(accessibility::NoAmount,
			accessibility::SmallIncrement);
		return true;
	case VK_PRIOR:
		scrollAccessible(accessibility::NoAmount,
			accessibility::LargeDecrement);
		return true;
	case VK_NEXT:
		scrollAccessible(accessibility::NoAmount,
			accessibility::LargeIncrement);
		return true;
	case VK_HOME:
		setAccessibleScrollPercent(-1., 0.);
		return true;
	case VK_END:
		setAccessibleScrollPercent(-1., 100.);
		return true;
	default:
		return false;
	}
}

void ScrolledContainer::scrollAccessible(accessibility::ScrollAmount horizontal,
	accessibility::ScrollAmount vertical)
{
	for(auto item: { std::make_pair(SB_HORZ, horizontal),
		std::make_pair(SB_VERT, vertical) }) {
		if(item.second == accessibility::NoAmount) {
			continue;
		}
		SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
		::GetScrollInfo(handle(), item.first, &info);
		setAccessibleScrollPosition(item.first,
			info.nPos + scrollDelta(item.second, static_cast<int>(info.nPage),
				scale(10)));
	}
}

void ScrolledContainer::setAccessibleScrollPercent(double horizontal,
	double vertical)
{
	for(auto item: { std::make_pair(SB_HORZ, horizontal),
		std::make_pair(SB_VERT, vertical) }) {
		if(item.second < 0) {
			continue;
		}
		SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
		::GetScrollInfo(handle(), item.first, &info);
		auto maximum = std::max(info.nMin,
			info.nMax - static_cast<int>(info.nPage) + 1);
		auto position = info.nMin + static_cast<int>(
			(maximum - info.nMin) * item.second / 100.);
		setAccessibleScrollPosition(item.first, position);
	}
}

double ScrolledContainer::getAccessibleScrollPercent(int type) const {
	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
	::GetScrollInfo(handle(), type, &info);
	auto maximum = std::max(info.nMin,
		info.nMax - static_cast<int>(info.nPage) + 1);
	return maximum > info.nMin ?
		100. * (info.nPos - info.nMin) / (maximum - info.nMin) : -1.;
}

double ScrolledContainer::getAccessibleViewSize(int type) const {
	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
	::GetScrollInfo(handle(), type, &info);
	auto range = info.nMax - info.nMin + 1;
	return range > 0 ? std::min(100., 100. * info.nPage / range) : 100.;
}

bool ScrolledContainer::isAccessibleScrollable(int type) const {
	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
	::GetScrollInfo(handle(), type, &info);
	return info.nPage && info.nMax - info.nMin + 1 > static_cast<int>(info.nPage);
}

void ScrolledContainer::setAccessibleScrollPosition(int type, int position) {
	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
	::GetScrollInfo(handle(), type, &info);
	auto original = info.nPos;
	info.fMask = SIF_POS;
	info.nPos = position;
	::SetScrollInfo(handle(), type, &info, TRUE);
	info.fMask = SIF_POS;
	::GetScrollInfo(handle(), type, &info);
	auto horizontal = type == SB_HORZ ? original - info.nPos : 0;
	auto vertical = type == SB_VERT ? original - info.nPos : 0;
	::ScrollWindowEx(handle(), horizontal, vertical, nullptr, nullptr, nullptr,
		nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN);
}

}
