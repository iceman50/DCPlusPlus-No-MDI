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

#ifndef DCPLUSPLUS_WIN32_RichTextBox_H_
#define DCPLUSPLUS_WIN32_RichTextBox_H_

#include <dcpp/typedefs.h>
#include <dcpp/Util.h>

#include <deque>
#include <vector>

#include <dwt/widgets/RichTextBox.h>

#include "forward.h"

/// our rich text boxes that provide find functions and handle links
class RichTextBox : public dwt::RichTextBox {
	typedef dwt::RichTextBox BaseType;
	friend class dwt::WidgetCreator<RichTextBox>;

	typedef std::function<bool (const tstring&)> LinkF;

public:
	typedef RichTextBox ThisType;
	
	typedef ThisType* ObjectType;

	struct Seed : public BaseType::Seed {
		typedef ThisType WidgetType;

		Seed();
	};

	explicit RichTextBox(dwt::Widget* parent);
	void create(const Seed& seed);

	bool handleMessage(const MSG& msg, LRESULT& retVal);

	MenuPtr getMenu(const tstring& searchText);

	tstring findTextPopup(const tstring& searchText = Util::emptyStringT);
	void findTextNew(const tstring& searchText);
	void findTextNext(const tstring& searchText = Util::emptyStringT);

	/// provides a chance to handle links differently
	void onLink(LinkF f);

	struct RenderedMessage {
		tstring rtf;
		tstring plainText;
		tstring author;
		string userId;
		time_t timestamp = 0;
	};

	/// Append a UI batch while retaining message boundaries for context actions.
	void appendMessages(std::vector<RenderedMessage> messages);
	void discardMessagePrefix(int characters);
	void clearMessageMetadata() { messages.clear(); }

private:
	struct MessageRange {
		uint64_t id;
		int begin;
		int end;
		tstring text;
		tstring author;
		string userId;
		time_t timestamp;
	};

	bool handleKeyDown(int c);
	LRESULT handleLink(ENLINK& link);
	void handleLinkTip(tstring& text);

	tstring getLinkText(const ENLINK& link);
	void openLink(const tstring& text);
	const MessageRange* messageAt(int character) const;
	void adjustMessageRanges(int removed);

	std::deque<MessageRange> messages;
	uint64_t nextMessageId = 1;

	ToolTipPtr linkTip;
	DWORD linkTipPos;
	tstring currentLink;
	LinkF linkF;
};

typedef RichTextBox::ObjectType RichTextBoxPtr;

#endif /*RichTextBox_H_*/
