/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "stdafx.h"
#include "RichTextBox.h"

#include <dwt/WidgetCreator.h>
#include <dwt/widgets/Menu.h>
#include <dwt/widgets/ToolTip.h>
#include <dwt/util/StringUtils.h>

#include "ParamDlg.h"
#include "resource.h"
#include "WinUtil.h"

RichTextBox::Seed::Seed() : 
BaseType::Seed()
{
}

RichTextBox::RichTextBox(dwt::Widget* parent) :
BaseType(parent),
linkTip(0),
linkTipPos(0),
linkF(nullptr)
{
}

void RichTextBox::create(const Seed& seed) {
	BaseType::create(seed);

	if((seed.events & ENM_LINK) == ENM_LINK) {
		linkTip = dwt::WidgetCreator<dwt::ToolTip>::create(this, dwt::ToolTip::Seed());
		linkTip->setTool(this, [this](tstring& text) { handleLinkTip(text); });
		onDestroy([this] { linkTip->close(); linkTip = nullptr; });

		onRaw([this](WPARAM, LPARAM lParam) { return handleLink(*reinterpret_cast<ENLINK*>(lParam)); },
			dwt::Message(WM_NOTIFY, EN_LINK));
	}
}

bool RichTextBox::handleMessage(const MSG& msg, LRESULT& retVal) {
	if(msg.message == WM_KEYDOWN) {
		switch(static_cast<int>(msg.wParam)) {
		case 'E': case 'J': case 'L': case 'R':
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			// these don't play well with DC++ since it is sometimes impossible to revert the change
			if(isControlPressed() && !isAltPressed())
				return true;
		}
	}

	if(msg.message == WM_SETCURSOR && !currentLink.empty() && ::GetMessagePos() != linkTipPos) {
		linkTip->setActive(false);
		currentLink.clear();
	}

	if(BaseType::handleMessage(msg, retVal))
		return true;

	switch(msg.message)
	{
		// we process these messages here to give the host a chance to handle them differently.

	case WM_KEYDOWN:
		{
			// imitate aspects::Keyboard
			return handleKeyDown(static_cast<int>(msg.wParam));
		}
	}

	return false;
}

MenuPtr RichTextBox::getMenu(const tstring& searchText) {
	auto menu = BaseType::getMenu();

	menu->appendSeparator();
	auto enabled = length() != 0;
	menu->appendItem(T_("&Find...\tCtrl+F"), [this, searchText] { findTextNew(searchText); }, dwt::IconPtr(), enabled);
	menu->appendItem(T_("Find &Next\tF3"), [this, searchText] { findTextNext(searchText); }, dwt::IconPtr(), enabled);

	if(!currentLink.empty()) {
		menu->appendSeparator();
		auto text = currentLink;
		auto linkMenu = menu->appendPopup(dwt::util::escapeMenu(text), WinUtil::menuIcon(IDI_LINKS));
		linkMenu->appendItem(T_("&Open"), [this, text] { openLink(text); }, WinUtil::menuIcon(IDI_RIGHT), true, true);
		linkMenu->appendItem(T_("&Copy"), [this, text] { WinUtil::setClipboard(text); });
	}

	return menu;
}

tstring RichTextBox::findTextPopup(const tstring& searchText) {
	ParamDlg lineFind(this, T_("Search"), T_("Specify search string"), searchText);
	if(lineFind.run() == IDOK) {
		return lineFind.getValue();
	}
	return Util::emptyStringT;
}

void RichTextBox::findTextNew(const tstring& searchText) {
	findText(findTextPopup(searchText));
}

void RichTextBox::findTextNext(const tstring& searchText) {
	if (currentNeedle.empty()) {
		findTextNew(searchText);
	} else {
		findText(currentNeedle);
	}
}

bool RichTextBox::handleKeyDown(int c) {
	switch(c) {
	case VK_F3:
		findTextNext();
		return true;
	case VK_ESCAPE:
		setSelection(-1, -1);
		scrollToBottom();
		clearCurrentNeedle();
		return true;
	}
	return false;
}

void RichTextBox::onLink(LinkF f) {
	linkF = f;
}

void RichTextBox::appendMessages(std::vector<RenderedMessage> batch) {
	if(batch.empty()) return;

	const int oldLength = static_cast<int>(length());
	bool addLine = oldLength != 0;
	std::vector<tstring> documents;
	documents.reserve(batch.size());
	for(auto& message: batch) {
		tstring document = _T("{\\urtf1\n");
		if(addLine) document += _T("\\line\n");
		document += message.rtf;
		document += _T("}\n");
		documents.push_back(std::move(document));
		addLine = true;
	}

	auto ranges = addTextSteadyBatch(documents);
	if(ranges.empty()) return;

	const int removed = std::max(0, oldLength - ranges.front().first);
	adjustMessageRanges(removed);
	for(size_t i = 0; i < ranges.size(); ++i) {
		if(ranges[i].second <= ranges[i].first) continue;
		messages.push_back({ nextMessageId++, ranges[i].first, ranges[i].second,
			std::move(batch[i].plainText), std::move(batch[i].author), std::move(batch[i].userId), batch[i].timestamp });
	}
}

const RichTextBox::MessageRange* RichTextBox::messageAt(int character) const {
	for(auto i = messages.rbegin(); i != messages.rend(); ++i) {
		if(character >= i->begin && character < i->end) return &*i;
		if(character > i->end) break;
	}
	return nullptr;
}

void RichTextBox::adjustMessageRanges(int removed) {
	if(removed <= 0) return;
	while(!messages.empty() && messages.front().end <= removed) messages.pop_front();
	for(auto& message: messages) {
		message.begin = std::max(0, message.begin - removed);
		message.end = std::max(0, message.end - removed);
	}
}

void RichTextBox::discardMessagePrefix(int characters) {
	adjustMessageRanges(characters);
}

LRESULT RichTextBox::handleLink(ENLINK& link) {
	/* the control doesn't handle click events, just "mouse down" & "mouse up". so we have to make
	sure the mouse hasn't moved between "down" & "up". */
	static LPARAM clickPos = 0;

	switch(link.msg) {
	case WM_LBUTTONDOWN:
		{
			clickPos = link.lParam;
			break;
		}

	case WM_LBUTTONUP:
		{
			if(link.lParam != clickPos)
				break;

			openLink(getLinkText(link));
			break;
		}

	case WM_SETCURSOR:
		{
			auto pos = ::GetMessagePos();
			if(pos == linkTipPos)
				break;
			linkTipPos = pos;

			currentLink = getLinkText(link);
			linkTip->refresh();
			break;
		}
	}
	return 0;
}

void RichTextBox::handleLinkTip(tstring& text) {
	text = currentLink;
}

tstring RichTextBox::getLinkText(const ENLINK& link) {
	const auto controlLength = static_cast<LONG>(std::min<size_t>(length(), LONG_MAX));
	const LONG begin = std::clamp<LONG>(link.chrg.cpMin, 0, controlLength);
	const LONG end = std::clamp<LONG>(link.chrg.cpMax, begin, controlLength);
	if(end <= begin) return tstring();

	auto buf = std::make_unique<TCHAR[]>(static_cast<size_t>(end - begin) + 1);
	buf[end - begin] = 0;
	TEXTRANGE text = { { begin, end }, buf.get() };
	const auto copied = std::clamp<LRESULT>(sendMessage(EM_GETTEXTRANGE, 0,
		reinterpret_cast<LPARAM>(&text)), 0, end - begin);
	return tstring(buf.get(), static_cast<size_t>(copied));
}

void RichTextBox::openLink(const tstring& text) {
	if(!linkF || !linkF(text)) {
		WinUtil::parseLink(text);
	}
}
