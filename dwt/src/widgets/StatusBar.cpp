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

#include <dwt/widgets/StatusBar.h>

#include <dwt/WidgetCreator.h>
#include <dwt/widgets/ToolTip.h>

#include <numeric>

#include "AppearanceDraw.h"

namespace dwt {

const TCHAR StatusBar::windowClass[] = STATUSCLASSNAME;

/* the minimum width of the part of the status bar that has to fill all the remaining space of the
bar (see Seed). if the bar is too narrow, only the "fill" part will be shown. */
const unsigned fillMin = 100;

StatusBar::Seed::Seed(unsigned parts_, unsigned fill_, bool sizeGrip) :
BaseType::Seed(WS_CHILD | WS_CLIPSIBLINGS),
font(0),
parts(parts_),
fill(fill_)
{
	assert(fill < parts);

	if(sizeGrip) {
		style |= SBARS_SIZEGRIP;
	}
}

StatusBar::StatusBar(Widget* parent) :
BaseType(parent, ChainingDispatcher::superClass<StatusBar>()),
fill(0),
tip(0),
tipPart(0)
{
}

void StatusBar::create(const Seed& cs) {
	parts.clear();
	parts.reserve(cs.parts);
	for(unsigned i = 0; i < cs.parts; ++i) {
		parts.emplace_back(std::make_unique<Part>());
	}
	fill = cs.fill;

	BaseType::create(cs);
	setFont(cs.font);

	tip = WidgetCreator<ToolTip>::create(this, ToolTip::Seed());
	tip->setTool(this, [this](tstring& text) { handleToolTip(text); });
	onDestroy([this] { tip->close(); tip = nullptr; });
}

void StatusBar::setSize(unsigned part, unsigned size) {
	dwtassert(part < parts.size(), "Invalid part number");
	parts[part]->desiredSize = size;
}

void StatusBar::setText(unsigned part, const tstring& text, bool alwaysResize) {
	dwtassert(part < parts.size(), "Invalid part number");
	Part& info = getPart(part);
	info.text = text;
	if(part != fill) {
		info.updateSize(this, alwaysResize);
	} else if(!text.empty()) {
		lastLines.push_back(text);
		while(lastLines.size() > MAX_LINES) {
			lastLines.erase(lastLines.begin());
		}
		tstring& tipStr = info.tip;
		tipStr.clear();
		for(size_t i = 0; i < lastLines.size(); ++i) {
			if(i > 0) {
				tipStr += _T("\r\n");
			}
			tipStr += lastLines[i];
		}
	}
	sendMessage(SB_SETTEXT, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str()));
}

void StatusBar::setIcon(unsigned part, const IconPtr& icon, bool alwaysResize) {
	dwtassert(part < parts.size(), "Invalid part number");
	Part& info = getPart(part);
	info.icon = icon;
	if(part != fill)
		info.updateSize(this, alwaysResize);
	sendMessage(SB_SETICON, part, icon ? reinterpret_cast<LPARAM>(icon->handle()) : 0);
}

void StatusBar::setToolTip(unsigned part, const tstring& text) {
	dwtassert(part < parts.size(), "Invalid part number");
	getPart(part).tip = text;
}

void StatusBar::setHelpId(unsigned part, unsigned id) {
	dwtassert(part < parts.size(), "Invalid part number");
	getPart(part).helpId = id;
}

void StatusBar::setWidget(unsigned part, Control* widget, const Rectangle& padding) {
	dwtassert(part < parts.size(), "Invalid part number");
	auto p = std::make_unique<WidgetPart>(widget, padding);
	p->desiredSize = p->preferredSize();
	p->helpId = widget->getHelpId();
	parts[part] = std::move(p);
}

void StatusBar::onClicked(unsigned part, const F& f) {
	dwtassert(part < parts.size(), "Invalid part number");
	getPart(part).clickF = f;

	// imitate the default onClicked but with a setCallback.
	setCallback(Message(WM_NOTIFY, NM_CLICK), Dispatchers::VoidVoid<>([this] { handleClicked(); }));
}

void StatusBar::onRightClicked(unsigned part, const F& f) {
	dwtassert(part < parts.size(), "Invalid part number");
	getPart(part).rightClickF = f;

	// imitate the default onRightClicked but with a setCallback.
	setCallback(Message(WM_NOTIFY, NM_RCLICK), Dispatchers::VoidVoid<>([this] { handleRightClicked(); }));
}

void StatusBar::onDblClicked(unsigned part, const F& f) {
	dwtassert(part < parts.size(), "Invalid part number");
	getPart(part).dblClickF = f;

	// imitate the default onDblClicked but with a setCallback.
	setCallback(Message(WM_NOTIFY, NM_DBLCLK), Dispatchers::VoidVoid<>([this] { handleDblClicked(); }));
}

int StatusBar::refresh() {
	// The status bar will auto-resize itself - all we need to do is to layout the sections
	sendMessage(WM_SIZE);

	auto sz = BaseType::getWindowSize();
	layoutSections(BaseType::getClientSize());
	return sz.y;
}

void StatusBar::appearanceChanged() {
	updatePartSizes();
	BaseType::appearanceChanged();
}

bool StatusBar::handleMessage(const MSG& msg, LRESULT& retVal) {
	if(tip) {
		if(msg.message == WM_MOUSEMOVE) {
			Part* part = getClickedPart();
			if(part && part != tipPart) {
				tip->refresh();
				tipPart = part;
			}
		}
		tip->relayEvent(msg);
	}

	const bool handled = BaseType::handleMessage(msg, retVal);
	if(msg.message == WM_SETFONT || msg.message == WM_DPICHANGED ||
		msg.message == WM_DPICHANGED_AFTERPARENT) {
		updatePartSizes();
	}
	if(handled || !isManualAppearance() ||
		getAppearancePolicy() == AppearancePolicy::Native) {
		return handled;
	}

	switch(msg.message) {
	case WM_ERASEBKGND:
		if(!appearance_detail::canDrawStatusBar(*this)) {
			break;
		}
		retVal = TRUE;
		return true;

	case WM_PAINT:
		{
			if(!appearance_detail::canDrawStatusBar(*this)) {
				break;
			}
			RECT update = { };
			if(!::GetUpdateRect(handle(), &update, FALSE)) {
				::GetClientRect(handle(), &update);
			}
			const Rectangle paint(update);
			BufferedCanvas<PaintCanvas> canvas(this, paint);
			appearance_detail::drawStatusBar(*this, canvas,
				getAppearance().getPalette());
			canvas.blast(paint);
			retVal = 0;
			return true;
		}

	case WM_PRINTCLIENT:
		if(msg.wParam && appearance_detail::canDrawStatusBar(*this)) {
			FreeCanvas canvas(reinterpret_cast<HDC>(msg.wParam));
			appearance_detail::drawStatusBar(*this, canvas,
				getAppearance().getPalette());
			retVal = 0;
			return true;
		}
		break;
	}
	return false;
}

unsigned StatusBar::Part::preferredSize(StatusBar* bar) const {
	unsigned newSize = 0;
	if(icon) {
		/* The appearance renderer draws status icons at the DPI-aware system
		 * small-icon size. Resource dimensions may still describe their 96-DPI
		 * source image, so reserve whichever width is larger. */
		newSize += static_cast<unsigned>(std::max<long>(icon->getSize().x,
			bar->getSystemMetric(SM_CXSMICON)));
	}
	if(!text.empty()) {
		if(icon) {
			newSize += static_cast<unsigned>(bar->scale(4));
		}
		newSize += bar->getTextSize(text).x;
	}
	if(newSize > 0) {
		/* Keep this in step with drawStatusBar: five pixels before the content,
		 * four after it, and one pixel of rounding room for DrawText. */
		newSize += static_cast<unsigned>(bar->scale(5) + bar->scale(4) +
			std::max(1, bar->scale(1)));
	}
	return newSize;
}

void StatusBar::Part::updateSize(StatusBar* bar, bool alwaysResize) {
	const auto newSize = preferredSize(bar);
	if(newSize > desiredSize || (alwaysResize && newSize != desiredSize)) {
		desiredSize = newSize;
		bar->layoutSections();
	}
}

unsigned StatusBar::WidgetPart::preferredSize() const {
	const auto content = widget->getPreferredSize().x;
	return static_cast<unsigned>(std::max(0L, content + padding.left() + padding.width()));
}

void StatusBar::WidgetPart::layout(POINT* offset) {
	::SetWindowPos(widget->handle(), HWND_TOP,
		offset[0].x + padding.left(), offset[0].y + padding.top(),
		offset[1].x - offset[0].x - padding.width(), offset[1].y - offset[0].y - padding.height(),
		SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

StatusBar::Part& StatusBar::getPart(unsigned part) {
	auto& ret = parts[part];
	if(auto p = dynamic_cast<Part*>(ret.get())) {
		return *p;
	}

	auto p = std::make_unique<Part>();
	auto raw = p.get();
	parts[part] = std::move(p);
	return *raw;
}

void StatusBar::updatePartSizes() {
	bool changed = false;
	for(size_t i = 0; i < parts.size(); ++i) {
		if(i == fill) {
			continue;
		}
		if(auto part = dynamic_cast<Part*>(parts[i].get())) {
			const auto size = part->preferredSize(this);
			if(size > part->desiredSize) {
				part->desiredSize = size;
				changed = true;
			}
		} else if(auto part = dynamic_cast<WidgetPart*>(parts[i].get())) {
			const auto size = part->preferredSize();
			if(size != part->desiredSize) {
				part->desiredSize = size;
				changed = true;
			}
		}
	}
	if(changed) {
		layoutSections();
	}
}

void StatusBar::layoutSections() {
	layoutSections(getClientSize());
}

void StatusBar::layoutSections(const Point& sz) {
	const auto parent = getParent();
	const auto parentHasSizeGrip = parent && parent->hasStyle(WS_THICKFRAME) && !::IsZoomed(parent->handle());
	const auto hasSizeGrip = hasStyle(SBARS_SIZEGRIP) || parentHasSizeGrip;
	const auto gripWidth = hasSizeGrip ? std::max(getSystemMetric(SM_CXVSCROLL), static_cast<int>(sz.y)) : 0;
	const auto width = static_cast<unsigned>(std::max(0L, sz.x - gripWidth));

	std::vector<unsigned> sizes(parts.size());
	for(size_t i = 0, n = sizes.size(); i < n; ++i)
		sizes[i] = parts[i]->desiredSize;

	sizes[fill] = 0;
	auto setParts = [&](const std::vector<unsigned>& widths) {
		std::vector<unsigned> edges(widths.size());
		unsigned offset = 0;
		for(size_t i = 0; i < widths.size(); ++i) {
			offset += widths[i];
			edges[i] = offset;
		}

		/* A -1 edge extends underneath a sizing grip, so reserve the grip
		 * explicitly. */
		edges.back() = hasSizeGrip ? width : static_cast<unsigned>(-1);
		sendMessage(SB_SETPARTS, edges.size(),
			reinterpret_cast<LPARAM>(edges.data()));
	};
	auto collapseParts = [&] {
		for(auto& part: parts) { part->actualSize = 0; }
		parts[fill]->actualSize = width;
		for(size_t i = 0; i < sizes.size(); ++i) {
			/* SB_SETPARTS expects right-edge coordinates. Parts before the fill
			 * part collapse at the left edge; parts after it collapse at the
			 * right. */
			sizes[i] = i < fill ? 0 : width;
		}
		sizes.back() = hasSizeGrip ? width : static_cast<unsigned>(-1);
		sendMessage(SB_SETPARTS, sizes.size(),
			reinterpret_cast<LPARAM>(sizes.data()));
	};

	const auto total = std::accumulate(sizes.begin(), sizes.end(), 0);
	if(total + fillMin < width) {
		/* SB_SETPARTS accepts right-edge coordinates, but the rectangles
		 * returned by SB_GETRECT are narrower because the common control
		 * applies its own outer and inter-part borders. Apply an initial
		 * layout, measure that loss for every fixed part, then reserve it in
		 * the assigned widths. This keeps the content width independent of
		 * the active Windows theme and common-controls version. */
		sizes[fill] = width - total;
		setParts(sizes);

		unsigned correction = 0;
		for(size_t i = 0; i < sizes.size(); ++i) {
			if(i == fill || !parts[i]->desiredSize) {
				continue;
			}

			RECT rect = { };
			if(sendMessage(SB_GETRECT, static_cast<WPARAM>(i),
				reinterpret_cast<LPARAM>(&rect))) {
				const auto actual = static_cast<unsigned>(
					std::max(0L, rect.right - rect.left));
				if(actual < parts[i]->desiredSize) {
					const auto missing = parts[i]->desiredSize - actual;
					sizes[i] += missing;
					correction += missing;
				}
			}
		}

		if(correction && sizes[fill] < fillMin + correction) {
			collapseParts();
		} else {
			sizes[fill] -= correction;
			for(size_t i = 0; i < sizes.size(); ++i) {
				parts[i]->actualSize = sizes[i];
			}
			if(correction) {
				setParts(sizes);
			}
		}
	} else {
		/* Only show the fill part if the status bar is too narrow. */
		collapseParts();
	}

	// reposition embedded widgets.
	for(auto i = parts.begin(); i != parts.end(); ++i) {
		auto wp = dynamic_cast<WidgetPart*>(i->get());
		if(wp) {
			POINT p[2];
			sendMessage(SB_GETRECT, static_cast<WPARAM>(i - parts.begin()), reinterpret_cast<LPARAM>(p));
			::MapWindowPoints(handle(), getParent()->handle(), p, 2);
			wp->layout(p);
		}
	}
}

StatusBar::Part* StatusBar::getClickedPart() {
	unsigned x = ClientCoordinate(ScreenCoordinate(Point::fromLParam(::GetMessagePos())), this).x();
	unsigned total = 0;
	for(auto& i: parts) {
		total += i->actualSize;
		if(total > x)
			return dynamic_cast<Part*>(i.get());
	}

	return 0;
}

void StatusBar::handleToolTip(tstring& text) {
	Part* part = getClickedPart();
	if(part) {
		text = part->tip;
		tip->setMaxTipWidth((text.find('\n') == tstring::npos) ? -1 : part->actualSize);
	} else {
		text.clear();
	}
}

void StatusBar::handleClicked() {
	Part* part = getClickedPart();
	if(part && part->clickF)
		part->clickF();
}

void StatusBar::handleRightClicked() {
	Part* part = getClickedPart();
	if(part && part->rightClickF)
		part->rightClickF();
}

void StatusBar::handleDblClicked() {
	Part* part = getClickedPart();
	if(part && part->dblClickF)
		part->dblClickF();
}

void StatusBar::helpImpl(unsigned& id) {
	// we have the help id of the whole status bar; convert to the one of the specific part the user just clicked on
	Part* part = getClickedPart();
	if(part && part->helpId)
		id = part->helpId;
}

}
