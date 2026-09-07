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

#include "SystemFrame.h"

#include <iterator>

#include <dcpp/DirectoryListing.h>
#include <dcpp/File.h>
#include <dcpp/LogManager.h>
#include <dcpp/ShareManager.h>
#include <dcpp/SimpleXML.h>

#include "DirectoryListingFrame.h"
#include "HtmlToRtf.h"
#include "RichTextBox.h"
#include "ShellMenu.h"
#include "WinUtil.h"
#include "resource.h"

const string SystemFrame::id = "SystemLog";
const string& SystemFrame::getId() const { return id; }

SystemFrame::SystemFrame(TabViewPtr parent) :
	BaseType(parent, T_("System Log"), IDH_SYSTEM_LOG, IDI_DCPP),
	log(0),
	logFlushScheduled(false),
	logAlive(std::make_shared<std::atomic_bool>(true))
{
	{
		RichTextBox::Seed cs = WinUtil::Seeds::richTextBox;
		cs.style |= WS_VSCROLL | ES_MULTILINE | ES_NOHIDESEL | ES_READONLY;
		log = dwt::WidgetCreator<RichTextBox>::create(this, cs);
		log->setColor(SETTING(LOG_COLOR), SETTING(LOG_BG_COLOR));
		log->setTextLimit(96 * 1024);
		addWidget(log);
		log->onContextMenu([this](const dwt::ScreenCoordinate &sc) { return handleContextMenu(sc); });
		log->onLeftMouseDblClick([this](const dwt::MouseEvent &me) { return handleDoubleClick(me); });
		WinUtil::handleDblClicks(log);
	}

	initStatus();

	status->onDblClicked(STATUS_STATUS, [] {
		WinUtil::openFile(Text::toT(Util::validateFileName(LogManager::getInstance()->getPath(LogManager::SYSTEM))));
	});

	layout();

	LogManager::getInstance()->addListener(this);
	LogManager::List oldMessages = LogManager::getInstance()->getLastLogs();
	SettingsManager::getInstance()->addListener(this);
	addLines(oldMessages);
}

SystemFrame::~SystemFrame() {
	logAlive->store(false);
}

void SystemFrame::addLines(const std::deque<LogMessagePtr>& batch, bool remember) {
	if(batch.empty())
		return;

	const auto maxMessages = LogManager::getHistoryLimit();
	std::vector<tstring> documents;
	documents.reserve(std::min(batch.size(), maxMessages));
	bool addLine = log->length() != 0;
	auto first = batch.begin();
	if(batch.size() > maxMessages)
		std::advance(first, batch.size() - maxMessages);
	for(auto i = first; i != batch.end(); ++i) {
		const auto& message = *i;
		if(!message)
			continue;
		if(remember) {
			if(std::find(messages.begin(), messages.end(), message) != messages.end())
				continue;
			while(messages.size() >= maxMessages)
				messages.pop_front();
			messages.push_back(message);
		}

		const char* style = "log";
		unsigned icon = IDI_HELP;
		const char* levelName = _("Unknown");
		switch(message->getSeverity()) {
		case LogMessage::SEV_VERBOSE:
			style = "systemLogVerbose";
			icon = IDI_LOGS;
			levelName = _("Verbose");
			break;
		case LogMessage::SEV_INFO:
			style = "systemLogInfo";
			icon = IDI_GET_STARTED;
			levelName = _("Info");
			break;
		case LogMessage::SEV_WARNING:
			style = "systemLogWarning";
			icon = IDI_DCPP_WARNING;
			levelName = _("Warning");
			break;
		case LogMessage::SEV_ERROR:
			style = "systemLogError";
			icon = IDI_EXIT;
			levelName = _("Error");
			break;
		default:
			break;
		}

		auto escape = [](string text) {
			SimpleXML::escape(text, false);
			return text;
		};
		const auto level = escape(levelName);
		const auto timestamp = escape("[" + Util::getShortTimeString(message->getTime()) + "]");
		const auto area = escape("[" + message->getArea() + "]");
		const auto text = escape(message->getText());
		const auto html = "<span id=\"" + string(style) + "\"><resourceicon id=\"" + Util::toString(icon) +
			"\"/> [" + level + "] " + timestamp + " " +
			"<span id=\"systemLogArea\">" + area + "</span> " + text + "</span>";

		tstring document = _T("{\\urtf1\n");
		if(addLine)
			document += _T("\\line\n");
		document += HtmlToRtf::convert(html, log);
		document += _T("}\n");
		documents.push_back(std::move(document));
		addLine = true;
	}

	if(documents.empty())
		return;
	log->addTextSteadyBatch(documents);
	if(remember)
		setDirty(SettingsManager::BOLD_SYSTEM_LOG);
}

void SystemFrame::flushLog() {
	std::deque<LogMessagePtr> batch;
	{
		std::lock_guard<std::mutex> lock(pendingMessagesMutex);
		batch.swap(pendingMessages);
		logFlushScheduled = false;
	}
	const auto maxMessages = LogManager::getHistoryLimit();
	while(batch.size() > maxMessages)
		batch.pop_front();
	addLines(batch);
}

void SystemFrame::refreshLog() {
	const auto maxMessages = LogManager::getHistoryLimit();
	while(messages.size() > maxMessages)
		messages.pop_front();

	// Clear first: changing RichEdit defaults otherwise preserves formatting one character at a time.
	log->setText(Util::emptyStringT);
	log->clearMessageMetadata();
	dwt::FontPtr font;
	WinUtil::updateFont(font, SettingsManager::LOG_FONT);
	log->setFont(font ? font : WinUtil::Seeds::richTextBox.font);
	log->setColor(SETTING(LOG_COLOR), SETTING(LOG_BG_COLOR));
	addLines(messages, false);
}

void SystemFrame::openFile(const string& path) const {
	// see if we are opening our own file list.
	if(path == ShareManager::getInstance()->getBZXmlFile()) {
		DirectoryListingFrame::openOwnList(getParent());
		return;
	}

	// see if we are opening a file list.
	auto u = DirectoryListing::getUserFromFilename(path);
	if(u) {
		DirectoryListingFrame::openWindow(getParent(), Text::toT(path), Util::emptyStringT,
			HintedUser(u, Util::emptyString), 0, DirectoryListingFrame::FORCE_ACTIVE);
		return;
	}

	WinUtil::openFile(Text::toT(path));
}

void SystemFrame::layout() {
	dwt::Rectangle r(this->getClientSize());

	r.size.y -= status->refresh();

	log->resize(r);
}

bool SystemFrame::preClosing() {
	logAlive->store(false);
	LogManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
	{
		std::lock_guard<std::mutex> lock(pendingMessagesMutex);
		pendingMessages.clear();
		logFlushScheduled = false;
	}
	return true;
}

bool SystemFrame::handleContextMenu(const dwt::ScreenCoordinate& pt) {
	tstring text = log->textUnderCursor(pt, true);
	string path_a = Text::fromT(text);
	if(File::getSize(path_a) != -1) {
		auto menu = addChild(ShellMenu::Seed(WinUtil::Seeds::menu));
		menu->setTitle(escapeMenu(text), WinUtil::fileImages->getIcon(static_cast<unsigned>(WinUtil::getFileIcon(path_a))));

		auto tth = ShareManager::getInstance()->getTTHFromReal(path_a);
		if (tth) {
			WinUtil::addHashItems(menu.get(), tth.value(), Util::getFileName(text), File::getSize(path_a));
		} 

		menu->appendItem(T_("&Open"), [this, path_a] { openFile(path_a); }, dwt::IconPtr(), true, true);
		menu->appendItem(T_("Open &folder"), [text] { WinUtil::openFolder(text); });
		
		menu->appendShellMenu(StringList(1, path_a));
		menu->open(pt);
	} else {
		WinUtil::getChatSelText(log, text, pt);
		auto menu = log->getMenu();
		WinUtil::addSearchMenu(menu.get(), text);
		menu->open(pt);
	}
	
	return true;
}

bool SystemFrame::handleDoubleClick(const dwt::MouseEvent& mouseEvent) {
	string path = Text::fromT(log->textUnderCursor(mouseEvent.pos, true));
	if(File::getSize(path) != -1) {
		openFile(path);
		return true;
	}
	return false;
}

void SystemFrame::on(Message, const LogMessagePtr& message) noexcept {
	if(!message || !logAlive->load())
		return;

	bool schedule = false;
	{
		std::lock_guard<std::mutex> lock(pendingMessagesMutex);
		if(!logAlive->load())
			return;
		const auto maxMessages = LogManager::getHistoryLimit();
		while(pendingMessages.size() >= maxMessages)
			pendingMessages.pop_front();
		pendingMessages.push_back(message);
		if(!logFlushScheduled) {
			logFlushScheduled = true;
			schedule = true;
		}
	}

	if(schedule) {
		auto alive = logAlive;
		callAsync([this, alive] {
			if(alive->load())
				flushLog();
		});
	}
}

void SystemFrame::on(SettingsManagerListener::Save, SimpleXML&) noexcept {
	auto alive = logAlive;
	callAsync([this, alive] {
		if(alive->load())
			refreshLog();
	});
}
