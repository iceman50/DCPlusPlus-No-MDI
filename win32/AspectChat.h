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

#ifndef DCPLUSPLUS_WIN32_ASPECT_CHAT_H
#define DCPLUSPLUS_WIN32_ASPECT_CHAT_H

#include <dcpp/ChatMessage.h>
#include <dcpp/File.h>
#include <dcpp/HashManager.h>
#include <dcpp/RichText.h>
#include <dcpp/ShareManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/Tagger.h>
#include <dcpp/PluginManager.h>
#include <dcpp/User.h>

#include <dwt/WidgetCreator.h>
#include <dwt/widgets/Button.h>

#include <deque>
#include <memory>

#include "HoldRedraw.h"
#include "HtmlToRtf.h"
#include "RichTextEditorDlg.h"
#include "RichTextBox.h"
#include "WinUtil.h"

template<typename T>
class AspectChat : private HashManagerListener {
	typedef AspectChat<T> ThisType;

	const T& t() const { return *static_cast<const T*>(this); }
	T& t() { return *static_cast<T*>(this); }

protected:
	AspectChat() :
		chat(0),
		message(0),
		richTextButton(0),
		messageLines(1),
		chatFlushScheduled(false),
		chatAlive(std::make_shared<bool>(true)),
		richTextAvailable(false),
		curCommandPosition(0)
	{
		HashManager::getInstance()->addListener(this);
	}

	void createChat(dwt::Widget *parent) {
		{
			RichTextBox::Seed cs = WinUtil::Seeds::richTextBox;
			cs.style |= ES_READONLY;
			chat = dwt::WidgetCreator<RichTextBox>::create(parent, cs);
			chat->setTextLimit(1024*64*2);
			chat->onSearchStrNotFound([this](const tstring& text) { t().addStatus(T_("String not found : ") + text); });
		}

		{
			TextBox::Seed cs = WinUtil::Seeds::textBox;
			cs.style |= WS_VSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | ES_NOHIDESEL;
			message = t().addChild(cs);
			message->onUpdated([this] { this->handleMessageUpdated(); });
			message->onDragDrop([this](const TStringList& files, dwt::Point) { this->handleDroppedFiles(files); });
			message->setDragAcceptFiles(false);
		}
		t().onDragDrop([this](const TStringList& files, dwt::Point) { this->handleDroppedFiles(files); });
		t().setDragAcceptFiles(false);

		{
			auto cs = WinUtil::Seeds::button;
			cs.caption = T_("RTF...");
			richTextButton = t().addChild(cs);
			richTextButton->setVisible(false);
			richTextButton->onClicked([this] { this->openRichTextEditor(); });
		}

		t().addAccel(FALT, 'C', [this] { this->chat->setFocus(); });
		t().addAccel(FALT, 'M', [this] { this->message->setFocus(); });
		t().addAccel(FALT, 'S', [this] { this->sendMessage(); });
		t().addAccel(0, VK_ESCAPE, [this] { this->handleEscape(); });
		t().addAccel(FCONTROL, 'F', [this] { this->findText(false); });
		t().addAccel(0, VK_F3, [this] { this->findText(true); });
	}

	virtual ~AspectChat() {
		HashManager::getInstance()->removeListener(this);
		*chatAlive = false;
	}

	void setRichTextAvailable(bool available, const string& hubUrl) {
		richTextHubUrl = hubUrl;
		available = available && SETTING(ENABLE_RICH_TEXT);
		const auto acceptTempFiles = available && SETTING(ENABLE_RTF_TEMP_SHARES);
		message->setDragAcceptFiles(acceptTempFiles);
		t().setDragAcceptFiles(acceptTempFiles);
		if(richTextAvailable == available) return;
		richTextAvailable = available;
		richTextButton->setVisible(available);
		t().layout();
	}

	/// add a chat message with some formatting and call addedChat.
	void addChat(const tstring& message) {
		string tmp;

		Tagger tags(Text::fromT(message));
		ChatMessage::format(tags, tmp);

		PluginManager::getInstance()->onChatTags(tags);

		string htmlMessage = "<span id=\"systemMessage\" style=\"white-space: pre-wrap;\">"
			"<span id=\"timestamp\">" + SimpleXML::escape("[" + Util::getShortTimeString() + "]", tmp, false) + "</span> "
			"<span id=\"text\">" + tags.merge(tmp) + "</span></span>";

		PluginManager::getInstance()->onChatDisplay(htmlMessage);

		addChatHTML(htmlMessage, message, Util::emptyStringT, Util::emptyString, time(nullptr));
		t().addedChat(message);
	}

	/// add a ChatMessage and call addedChat.
	void addChat(const ChatMessage& message) {
		addChatHTML(message.htmlMessage, Text::toT(message.message), Text::toT(message.nick),
			message.from ? message.from->getCID().toBase32() : Util::emptyString, message.timestamp);
		t().addedChat(Text::toT(message.message));
	}

	/// add a plain text string directly, with no formatting.
	void addChatPlain(const tstring& message) {
		string tmp;
		addChatHTML("<span id=\"systemMessage\">" + SimpleXML::escape(Text::fromT(message), tmp, false) + "</span>",
			message, Util::emptyStringT, Util::emptyString, time(nullptr));
	}

	/// add an RTF-formatted message.
	void addChatRTF(tstring message, tstring plainText = Util::emptyStringT,
		tstring author = Util::emptyStringT, string userId = Util::emptyString, time_t timestamp = 0) {
		pendingChat.push_back({ std::move(message), std::move(plainText), std::move(author), std::move(userId), timestamp });
		if(chatFlushScheduled) return;
		chatFlushScheduled = true;
		auto alive = chatAlive;
		t().callAsync([this, alive] {
			if(*alive) flushChat();
		});
	}

	/// add an HTML-formatted message.
	void addChatHTML(const string& message, const tstring& plainText = Util::emptyStringT,
		tstring author = Util::emptyStringT, string userId = Util::emptyString, time_t timestamp = 0) {
		addChatRTF(HtmlToRtf::convert(message, chat), plainText, std::move(author), std::move(userId), timestamp);
	}

	void readLog(const string& logPath, const unsigned setting) {
		if(setting == 0)
			return;

		StringList lines;

		try {
			const int MAX_SIZE = 32 * 1024;

			File f(logPath.empty() ? t().getLogPath() : logPath, File::READ, File::OPEN);
			if(f.getSize() > MAX_SIZE) {
				f.setEndPos(-MAX_SIZE + 1);
			}

			lines = StringTokenizer<string>(f.read(MAX_SIZE), "\r\n").getTokens();
		} catch(const FileException&) { }

		if(lines.empty())
			return;

		// the last line in the log file is an empty line; remove it
		lines.pop_back();

		string html;
		string tmp;

		const size_t linesCount = lines.size();
		for(size_t i = (linesCount > setting) ? (linesCount - setting) : 0; i < linesCount; ++i) {
			html += SimpleXML::escape(lines[i], tmp, false) + "<br/>";
		}

		if(!html.empty()) {
			addChatHTML("<span id=\"log\" style=\"white-space: pre-wrap;\">" + html + "</span>");
		}
	}

	bool checkCommand(const tstring& cmd, const tstring& param, tstring& status) {
		if(Util::stricmp(cmd.c_str(), _T("clear")) == 0) {
			// A posted batch has already been received logically; discard it as part of the clear
			// rather than allowing its delayed UI flush to make old messages reappear.
			pendingChat.clear();
			unsigned linesToKeep = 0;
			if(!param.empty())
				linesToKeep = Util::toInt(Text::fromT(param));
			if(linesToKeep) {
				unsigned lineCount = chat->getLineCount();
				if(linesToKeep < lineCount) {
					dwt::RichTextBox::HoldScroll hold { chat };
					const auto removeEnd = chat->lineIndex(lineCount - linesToKeep);
					if(removeEnd > 0) {
						chat->setSelection(0, removeEnd);
						chat->replaceSelection(_T(""));
						chat->discardMessagePrefix(removeEnd);
					}
				}
			} else {
				chat->setSelection();
				chat->replaceSelection(_T(""));
				chat->clearMessageMetadata();
			}

		} else if(Util::stricmp(cmd.c_str(), _T("f")) == 0) {
			chat->findText(param.empty() ? chat->findTextPopup() : param);

		} else {
			return false;
		}
		return true;
	}

	bool handleMessageKeyDown(int c) {
		switch(c) {
		case VK_RETURN: {
			if(t().isShiftPressed() || t().isControlPressed() || t().isAltPressed()) {
				return false;
			}
			return sendMessage();
		}

		case VK_UP:
			if ( historyActive() ) {
				//scroll up in chat command history
				//currently beyond the last command?
				if (curCommandPosition > 0) {
					//check whether current command needs to be saved
					if (curCommandPosition == prevCommands.size()) {
						currentCommand = message->getText();
					}

					//replace current chat buffer with current command
					message->setText(prevCommands[--curCommandPosition]);
				}
				// move cursor to end of line
					auto pos = static_cast<int>(message->length());
					message->setSelection(pos, pos);
				return true;
			}
			break;
		case VK_DOWN:
			if ( historyActive() ) {
				//scroll down in chat command history

				//currently beyond the last command?
				if (curCommandPosition + 1 < prevCommands.size()) {
					//replace current chat buffer with current command
					message->setText(prevCommands[++curCommandPosition]);
				} else if (curCommandPosition + 1 == prevCommands.size()) {
					//revert to last saved, unfinished command

					message->setText(currentCommand);
					++curCommandPosition;
				}
				// move cursor to end of line
				auto pos = static_cast<int>(message->length());
				message->setSelection(pos, pos);
				return true;
			}
			break;
		case VK_PRIOR: // page up
			{
				chat->sendMessage(WM_VSCROLL, SB_PAGEUP);
				return true;
			} break;
		case VK_NEXT: // page down
			{
				chat->sendMessage(WM_VSCROLL, SB_PAGEDOWN);
				return true;
			} break;
		case VK_HOME:
			if (!prevCommands.empty() && historyActive() ) {
				curCommandPosition = 0;
				currentCommand = message->getText();

				message->setText(prevCommands[curCommandPosition]);
				return true;
			}
			break;
		case VK_END:
			if (historyActive()) {
				curCommandPosition = prevCommands.size();

				message->setText(currentCommand);
				return true;
			}
			break;
		}
		return false;
	}

	bool handleMessageChar(int c) {
		switch(c) {
		case VK_RETURN: {
			if(!(t().isShiftPressed() || t().isControlPressed() || t().isAltPressed())) {
				return true;
			}
		} break;
		}
		return false;
	}

	void handleEscape() {
		chat->sendMessage(WM_KEYDOWN, VK_ESCAPE);
		message->setFocus();
	}

	RichTextBox* chat;
	dwt::TextBoxPtr message;
	dwt::ButtonPtr richTextButton;

	unsigned messageLines;

private:
	struct PendingDrop {
		string realPath;
		string hubUrl;
		string name;
		int64_t size;
		uint32_t timestamp;
		bool inlineMedia;
	};

	static bool isInlineImage(const string& path) {
		const auto ext = Text::toLower(Util::getFileExt(path));
		return ext == ".bmp" || ext == ".gif" || ext == ".jpg" || ext == ".jpeg" ||
			ext == ".png" || ext == ".tif" || ext == ".tiff" || ext == ".webp";
	}

	void handleDroppedFiles(const TStringList& files) {
		if(!richTextAvailable || !SETTING(ENABLE_RTF_TEMP_SHARES) || richTextHubUrl.empty()) return;

		for(const auto& fileName: files) {
			const auto realPath = Text::fromT(fileName);
			try {
				File file(realPath, File::READ, File::OPEN | File::SHARED);
				PendingDrop pending { realPath, richTextHubUrl, Util::getFileName(realPath),
					file.getSize(), file.getLastModified(),
					SETTING(RTF_DROPPED_IMAGES_INLINE) && isInlineImage(realPath) };
				file.close();
				if(pending.size < 0 || pending.name.empty()) throw FileException("Invalid file");

				{
					Lock l(pendingDropsCs);
					pendingDrops.push_back(pending);
				}
				const auto tth = HashManager::getInstance()->getTTH(realPath, pending.size, pending.timestamp);
				if(tth) {
					completeDroppedFiles(realPath, *tth);
				} else {
					t().addStatus(T_("Hashing dropped attachment: ") + Text::toT(pending.name));
				}
			} catch(const Exception& e) {
				t().addStatus(T_("Unable to prepare dropped attachment: ") + Text::toT(e.getError()));
			}
		}
	}

	void on(HashManagerListener::TTHDone, const string& realPath, const TTHValue& tth) noexcept override {
		try {
			completeDroppedFiles(realPath, tth);
		} catch(...) {
			// Hash notifications must not unwind through the worker thread.
		}
	}

	void completeDroppedFiles(const string& realPath, const TTHValue& tth) {
		vector<PendingDrop> completed;
		{
			Lock l(pendingDropsCs);
			for(auto i = pendingDrops.begin(); i != pendingDrops.end();) {
				if(Util::stricmp(i->realPath, realPath) == 0) {
					completed.push_back(std::move(*i));
					i = pendingDrops.erase(i);
				} else {
					++i;
				}
			}
		}

		for(const auto& pending: completed) {
			const auto shared = ShareManager::getInstance()->addTempShare(pending.realPath,
				pending.size, pending.timestamp, tth, pending.hubUrl);
			const auto markdown = shared ? RichText::makeAttachmentMarkdown(
				pending.name, tth.toBase32(), pending.size, pending.inlineMedia) : string();
			auto alive = chatAlive;
			t().callAsync([this, alive, pending, markdown] {
				if(!*alive) return;
				if(markdown.empty()) {
					t().addStatus(T_("The dropped attachment changed or could not be added to the temporary share: ") +
						Text::toT(pending.name));
					return;
				}
				if(!richTextAvailable || Util::stricmp(richTextHubUrl, pending.hubUrl) != 0) return;
				insertDroppedAttachment(Text::toT(markdown));
			});
		}
	}

	void insertDroppedAttachment(const tstring& markdown) {
		auto text = message->getText();
		const auto text8 = Text::fromT(text);
		const auto hasCommand = text8.size() >= 4 && Util::strnicmp(text8.c_str(), "/rtf", 4) == 0 &&
			(text8.size() == 4 || text8[4] == ' ' || text8[4] == '\t' || text8[4] == '\r' || text8[4] == '\n');
		if(!hasCommand) text = _T("/rtf ") + text;
		else if(text.size() == 4) text += _T(' ');

		if(text.size() > 5 && !iswspace(text.back())) text += _T("\r\n");
		text += markdown;
		message->setText(text);
		const auto end = static_cast<int>(message->length());
		message->setSelection(end, end);
		message->setFocus();
	}

	void openRichTextEditor() {
		if(!richTextAvailable) return;

		auto initial = message->getText();
		const auto initial8 = Text::fromT(initial);
		if(initial8.size() >= 4 && Util::strnicmp(initial8.c_str(), "/rtf", 4) == 0 &&
			(initial8.size() == 4 || initial8[4] == ' ' || initial8[4] == '\t'))
		{
			size_t content = 4;
			while(content < initial.size() && (initial[content] == _T(' ') || initial[content] == _T('\t'))) ++content;
			initial.erase(0, content);
		}

		RichTextEditorDlg editor(&t(), richTextHubUrl, initial);
		if(editor.run() != IDOK) return;

		message->setText(_T("/rtf ") + editor.getText());
		const auto end = static_cast<int>(message->length());
		message->setSelection(end, end);
		message->setFocus();
	}

	void flushChat() {
		constexpr size_t maxBatchSize = 128;
		std::vector<RichTextBox::RenderedMessage> batch;
		batch.reserve(std::min(maxBatchSize, pendingChat.size()));
		while(!pendingChat.empty() && batch.size() < maxBatchSize) {
			batch.push_back(std::move(pendingChat.front()));
			pendingChat.pop_front();
		}
		chat->appendMessages(std::move(batch));

		if(pendingChat.empty()) {
			chatFlushScheduled = false;
		} else {
			auto alive = chatAlive;
			t().callAsync([this, alive] {
				if(*alive) flushChat();
			});
		}
	}

	std::deque<RichTextBox::RenderedMessage> pendingChat;
	bool chatFlushScheduled;
	std::shared_ptr<bool> chatAlive;
	bool richTextAvailable;
	string richTextHubUrl;
	CriticalSection pendingDropsCs;
	vector<PendingDrop> pendingDrops;

	TStringList prevCommands;
	tstring currentCommand;
	TStringList::size_type curCommandPosition; //can't use an iterator because StringList is a vector, and vector iterators become invalid after resizing

	void handleMessageUpdated() {
		unsigned lineCount = message->getLineCount();

		// make sure we don't resize to 0 lines...
		const unsigned min_setting = max(SETTING(MIN_MESSAGE_LINES), 1);
		const unsigned max_setting = max(SETTING(MAX_MESSAGE_LINES), 1);

		if(lineCount < min_setting)
			lineCount = min_setting;
		else if(lineCount > max_setting)
			lineCount = max_setting;

		if(lineCount != messageLines) {
			messageLines = lineCount;
			t().layout();
		}
	}

	bool historyActive() const {
		return t().isAltPressed() || (SETTING(USE_CTRL_FOR_LINE_HISTORY) && t().isControlPressed());
	}

	bool sendMessage() {
		tstring s = message->getText();
		if(s.empty()) {
			::MessageBeep(MB_ICONEXCLAMATION);
			return false;
		}

		// save command in history, reset current buffer pointer to the newest command
		curCommandPosition = prevCommands.size();		//this places it one position beyond a legal subscript
		if (curCommandPosition == 0 || (curCommandPosition > 0 && prevCommands[curCommandPosition - 1] != s)) {
			++curCommandPosition;
			prevCommands.push_back(s);
		}
		currentCommand = _T("");

		t().enterImpl(s);
		return true;
	}

	void findText(bool next) {
		tstring text;
		WinUtil::getChatSelText(chat, text, chat->getContextMenuPos());

		next ? chat->findTextNext(text) : chat->findTextNew(text);
	}
};

#endif // !defined(DCPLUSPLUS_WIN32_ASPECT_CHAT_H)
