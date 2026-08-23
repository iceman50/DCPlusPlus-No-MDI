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

#include <dwt/widgets/Rebar.h>

#include <dwt/Appearance.h>
#include <dwt/CanvasClasses.h>
#include <dwt/resources/Brush.h>
#include <dwt/resources/Pen.h>

namespace dwt {

namespace {

void drawBandBorder(Canvas& canvas, const Rectangle& rect, COLORREF color) {
	if(rect.width() <= 0 || rect.height() <= 0) {
		return;
	}
	Pen pen(color);
	auto select(canvas.select(pen));
	canvas.line(rect.left(), rect.bottom() - 1,
		rect.right() - 1, rect.bottom() - 1);
}

}

const TCHAR Rebar::windowClass[] = REBARCLASSNAME;

Rebar::Seed::Seed() :
BaseType::Seed(WS_CHILD | WS_CLIPCHILDREN | CCS_NODIVIDER | RBS_AUTOSIZE | RBS_VARHEIGHT, WS_EX_CONTROLPARENT)
{
}

Rebar::Rebar(Widget* parent) :
BaseType(parent, ChainingDispatcher::superClass<Rebar>())
{
}

void Rebar::create(const Seed& cs) {
	BaseType::create(cs);
}

int Rebar::refresh() {
	// use dummy sizes to avoid flickering; the rebar will figure out the proper sizes by itself.
	::MoveWindow(handle(), 0, 0, 0, 0, TRUE);
	return BaseType::getWindowSize().y;
}

void Rebar::add(Widget* w, unsigned style, const tstring& text) {
	if(size() == 0)
		setVisible(true);

	w->addRemoveStyle(CCS_NORESIZE, true);

	REBARBANDINFO info = { sizeof(REBARBANDINFO), RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE, style };

	if(!text.empty()) {
		info.fMask |= RBBIM_TEXT;
		info.lpText = const_cast<LPTSTR>(text.c_str());
	}

	info.hwndChild = w->handle();

	const Point size = w->getPreferredSize();
	info.cxMinChild = size.x;
	info.cyMinChild = size.y;

	sendMessage(RB_INSERTBAND, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(&info));
}

void Rebar::remove(Widget* w) {
	for(unsigned i = 0, n = size(); i < n; ++i) {
		REBARBANDINFO info = { sizeof(REBARBANDINFO), RBBIM_CHILD };
		if(sendMessage(RB_GETBANDINFO, i, reinterpret_cast<LPARAM>(&info)) && info.hwndChild == w->handle()) {
			sendMessage(RB_DELETEBAND, i);
			break;
		}
	}

	if(size() == 0)
		setVisible(false);
}

bool Rebar::empty() const {
	return size() == 0;
}

unsigned Rebar::size() const {
	return static_cast<unsigned>(sendMessage(RB_GETBANDCOUNT));
}

bool Rebar::handleMessage(const MSG& msg, LRESULT& retVal) {
	auto handled = BaseType::handleMessage(msg, retVal);
	if(!isManualAppearance() || msg.message != WM_NOTIFY ||
		!msg.lParam || (retVal && retVal != CDRF_DODEFAULT)) {
		return handled;
	}

	auto data = reinterpret_cast<NMCUSTOMDRAW*>(msg.lParam);
	if(data->hdr.code == NM_CUSTOMDRAW && data->dwDrawStage == CDDS_PREPAINT) {
		handlePainting(*data);
		retVal = CDRF_SKIPDEFAULT;
		return true;
	}
	return handled;
}

void Rebar::handlePainting(NMCUSTOMDRAW& data) {
	Rectangle bounds(data.rc);
	if(bounds.width() <= 0 || bounds.height() <= 0) {
		return;
	}

	const auto& palette = getAppearance().getPalette();
	const auto background = hasExplicitColors() ?
		getExplicitBackgroundColor() : palette.background;
	const auto textColor = hasExplicitColors() ?
		getExplicitTextColor() : palette.text;
	const auto style = static_cast<DWORD>(
		::GetWindowLongPtr(handle(), GWL_STYLE));
	const bool vertical = (style & CCS_VERT) != 0;

	BufferedCanvas<FreeCanvas> canvas(data.hdc, bounds);
	canvas.fill(bounds, Brush(background));
	auto font = getFont();
	HGDIOBJ oldFont = nullptr;
	if(font && font->handle()) {
		oldFont = ::SelectObject(canvas.handle(), font->handle());
	}
	canvas.setTextColor(textColor);
	auto backgroundMode = canvas.setBkMode(true);

	for(unsigned index = 0, count = size(); index < count; ++index) {
		RECT nativeBand = { 0 };
		if(!sendMessage(RB_GETRECT, index,
			reinterpret_cast<LPARAM>(&nativeBand))) {
			continue;
		}
		Rectangle band(nativeBand);
		if(band.width() <= 0 || band.height() <= 0) {
			continue;
		}
		canvas.fill(band, Brush(background));

		TCHAR text[256] = { 0 };
		REBARBANDINFO info = { sizeof(REBARBANDINFO) };
		info.fMask = RBBIM_STYLE | RBBIM_TEXT | RBBIM_HEADERSIZE | RBBIM_CHILD;
		info.lpText = text;
		info.cch = sizeof(text) / sizeof(text[0]);
		if(!sendMessage(RB_GETBANDINFO, index,
			reinterpret_cast<LPARAM>(&info)) || (info.fStyle & RBBS_HIDDEN)) {
			continue;
		}

		Rectangle header = band;
		if(info.hwndChild && ::IsWindow(info.hwndChild)) {
			RECT child = { 0 };
			::GetWindowRect(info.hwndChild, &child);
			::MapWindowPoints(HWND_DESKTOP, handle(),
				reinterpret_cast<POINT*>(&child), 2);
			if(vertical) {
				header.size.y = std::max(0L,
					static_cast<long>(child.top) - header.top());
			} else {
				header.size.x = std::max(0L,
					static_cast<long>(child.left) - header.left());
			}
		} else if(info.cxHeader) {
			if(vertical) {
				header.size.y = info.cxHeader;
			} else {
				header.size.x = info.cxHeader;
			}
		}

		if(!(info.fStyle & RBBS_NOGRIPPER)) {
			Pen gripPen(palette.border);
			auto select(canvas.select(gripPen));
			const int first = scale(3);
			const int last = scale(7);
			const int interval = std::max(2, scale(3));
			if(vertical) {
				for(long x = band.left() + first; x < band.right() - first;
					x += interval) {
					canvas.line(x, band.top() + scale(2), x,
						std::min(band.bottom() - scale(2), band.top() + last));
				}
				header.pos.y += scale(8);
				header.size.y = std::max(0L, header.size.y - scale(8));
			} else {
				for(long y = band.top() + first; y < band.bottom() - first;
					y += interval) {
					canvas.line(band.left() + scale(2), y,
						std::min(band.right() - scale(2), band.left() + last), y);
				}
				header.pos.x += scale(8);
				header.size.x = std::max(0L, header.size.x - scale(8));
			}
		}

		if(text[0] && header.width() > 0 && header.height() > 0) {
			if(vertical) {
				// Rebars rarely use vertical captions; keep the label readable.
				canvas.drawText(text, header,
					DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			} else {
				header.pos.x += scale(2);
				header.size.x = std::max(0L, header.size.x - scale(4));
				canvas.drawText(text, header,
					DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
			}
		}

		if(info.fStyle & RBBS_USECHEVRON) {
			Rectangle chevron = band;
			if(vertical) {
				chevron.pos.y = std::max(chevron.top(), chevron.bottom() - scale(16));
				chevron.size.y = std::min(static_cast<long>(scale(16)), chevron.height());
			} else {
				chevron.pos.x = std::max(chevron.left(), chevron.right() - scale(16));
				chevron.size.x = std::min(static_cast<long>(scale(16)), chevron.width());
			}
			canvas.drawText(_T("\u00bb"), chevron,
				DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}

		if((info.fStyle & RBBS_CHILDEDGE) || index + 1 < count) {
			drawBandBorder(canvas, band, palette.border);
		}
	}

	if(oldFont && oldFont != HGDI_ERROR) {
		::SelectObject(canvas.handle(), oldFont);
	}
	canvas.blast(bounds);
}

}
