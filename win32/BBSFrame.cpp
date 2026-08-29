/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"

#include "BBSFrame.h"

#include <dcpp/AdcHub.h>
#include <dcpp/RichText.h>

#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/Rebar.h>
#include <dwt/widgets/SplitterContainer.h>
#include <dwt/widgets/TextBox.h>
#include <dwt/widgets/ToolBar.h>

#include "BBSPostDlg.h"
#include "HoldRedraw.h"
#include "HtmlToRtf.h"
#include "HubFrame.h"
#include "RichTextBox.h"
#include "TypedTable.h"

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::Rebar;
using dwt::SplitterContainer;
using dwt::TextBox;
using dwt::ToolBar;

namespace {

const string BUTTON_REFRESH = "Refresh";
const string BUTTON_SUBSCRIBE = "Subscribe";
const string BUTTON_LEAVE = "Leave";
const string BUTTON_NEW = "New";
const string BUTTON_REPLY = "Reply";
const string BUTTON_FETCH = "Fetch";
const string BUTTON_WITHDRAW = "Withdraw";
const string BUTTON_COPY_TTH = "CopyTTH";

enum RefreshFlag {
	REFRESH_BOARDS = 1 << 0,
	REFRESH_ENTRIES = 1 << 1,
	REFRESH_DOCUMENT = 1 << 2,
	REFRESH_CONNECTION = 1 << 3,
	REFRESH_MARK_DIRTY = 1 << 4
};

tstring formatIndexedTime(uint64_t timestamp) {
	const auto now = static_cast<uint64_t>(time(nullptr));
	if(timestamp > now + 24 * 60 * 60) return T_("Unknown");
	return Text::toT(Util::formatTime("%Y-%m-%d %H:%M", static_cast<time_t>(timestamp)));
}

string getThreadKey(const BBSEntry& entry) {
	if(entry.parent.empty()) return entry.tth;
	return entry.thread.empty() ? entry.parent : entry.thread;
}

}

const string BBSFrame::id = "BBS";
const string& BBSFrame::getId() const { return id; }

static const ColumnInfo boardColumns[] = {
	{ N_("Board"), 180, false },
	{ N_("Name"), 120, false },
	{ N_("State"), 110, false },
	{ N_("Posts"), 70, true }
};

static const ColumnInfo entryColumns[] = {
	{ N_("Subject"), 260, false },
	{ N_("Author"), 120, false },
	{ N_("Indexed"), 120, false },
	{ N_("Size"), 80, true },
	{ N_("Document"), 100, false }
};

vector<BBSFrame*> BBSFrame::frames;

BBSFrame::BoardInfo::BoardInfo(const BBSBoard& aBoard) : board(aBoard) {
	columns[BOARD_COLUMN_TITLE] = Text::toT(BBSManager::sanitizeDisplayText(board.title.empty() ? board.name : board.title));
	columns[BOARD_COLUMN_NAME] = Text::toT(board.name);
	columns[BOARD_COLUMN_STATE] = board.subscribed ? T_("Subscribed") : board.canSubscribe() ? T_("Available") : T_("Read only");
	if(board.gap) columns[BOARD_COLUMN_STATE] += T_("; history gap");
	columns[BOARD_COLUMN_POSTS] = board.postCount >= 0 ? Text::toT(std::to_string(board.postCount)) : _T("?");
}

BBSFrame::EntryInfo::EntryInfo(const BBSEntry& aEntry, const std::optional<BBSDocument>& document, bool pending, size_t depth) : entry(aEntry) {
	const auto subject = BBSManager::sanitizeDisplayText(document && !document->subject.empty() ? document->subject : entry.subject);
	const auto prefix = depth == 0 ? string() : string(std::min<size_t>(depth, 8) * 2, ' ') + "> ";
	columns[ENTRY_COLUMN_SUBJECT] = Text::toT(prefix + (subject.empty() ? _("(no subject)") : subject));
	columns[ENTRY_COLUMN_AUTHOR] = Text::toT(BBSManager::sanitizeDisplayText(entry.nick.empty() ? entry.authorId : entry.nick, 128));
	columns[ENTRY_COLUMN_TIME] = formatIndexedTime(entry.timestamp);
	columns[ENTRY_COLUMN_SIZE] = entry.size >= 0 ? Text::toT(Util::formatBytes(entry.size)) : _T("?");
	columns[ENTRY_COLUMN_STATE] = document ? T_("Verified") : pending ? T_("Fetching") : T_("Index metadata");
}

WindowParams BBSFrame::getWindowParams() const {
	WindowParams result;
	result["Address"] = WindowParam(url, WindowParam::FLAG_IDENTIFIES);
	return result;
}

void BBSFrame::openWindow(TabViewPtr parent, string url, HubFrame* hubFrame, bool activate) {
	Util::sanitizeUrl(url);
	if(url.empty() || (!Util::isAdcUrl(url) && !Util::isAdcsUrl(url))) return;
	if(!hubFrame) {
		auto existingHub = std::find_if(HubFrame::frames.begin(), HubFrame::frames.end(), [&](HubFrame* frame) { return hubHintsEqual(frame->url, url); });
		if(existingHub != HubFrame::frames.end()) hubFrame = *existingHub;
	}
	auto existing = std::find_if(frames.begin(), frames.end(), [&](BBSFrame* frame) { return hubHintsEqual(frame->url, url); });
	if(existing != frames.end()) {
		if(hubFrame) {
			(*existing)->hubFrame = hubFrame;
			(*existing)->queueRefresh(REFRESH_CONNECTION | REFRESH_BOARDS | REFRESH_ENTRIES | REFRESH_DOCUMENT);
		}
		if(activate) (*existing)->activate();
		return;
	}
	auto frame = new BBSFrame(parent, std::move(url), hubFrame);
	if(activate) frame->activate();
}

void BBSFrame::parseWindowParams(TabViewPtr parent, const WindowParams& params) {
	auto address = params.find("Address");
	if(address != params.end()) openWindow(parent, address->second, nullptr, parseActivateParam(params));
}

void BBSFrame::attachHub(HubFrame* hubFrame) {
	if(!hubFrame) return;
	for(auto frame: frames) {
		if(hubHintsEqual(frame->url, hubFrame->url)) {
			frame->hubFrame = hubFrame;
			frame->queueRefresh(REFRESH_CONNECTION | REFRESH_BOARDS | REFRESH_ENTRIES | REFRESH_DOCUMENT);
		}
	}
}

void BBSFrame::detachHub(HubFrame* hubFrame) {
	if(!hubFrame) return;
	for(auto frame: frames) {
		if(frame->hubFrame == hubFrame) {
			frame->hubFrame = nullptr;
			frame->queueRefresh(REFRESH_CONNECTION | REFRESH_BOARDS | REFRESH_ENTRIES | REFRESH_DOCUMENT);
		}
	}
}

void BBSFrame::hubStateChanged(HubFrame* hubFrame) {
	if(!hubFrame) return;
	for(auto frame: frames) {
		if(frame->hubFrame == hubFrame) frame->queueRefresh(REFRESH_CONNECTION | REFRESH_BOARDS | REFRESH_ENTRIES | REFRESH_DOCUMENT);
	}
}

BBSFrame::BBSFrame(TabViewPtr parent, string&& aUrl, HubFrame* aHubFrame) :
	BaseType(parent, T_("Bulletin boards"), IDH_HUB, IDI_CHAT),
	hubFrame(aHubFrame),
	url(std::move(aUrl)),
	rebar(nullptr),
	toolbar(nullptr),
	filter(nullptr),
	paned(nullptr),
	contentPaned(nullptr),
	boards(nullptr),
	entries(nullptr),
	documentGrid(nullptr),
	documentTitle(nullptr),
	documentMeta(nullptr),
	documentView(nullptr),
	refreshing(false),
	refreshFlags(0),
	alive(std::make_shared<std::atomic<bool>>(true))
{
	paned = addChild(SplitterContainer::Seed(SETTING(BBSFRAME_PANED_POS)));

	{
		auto seed = WinUtil::Seeds::table;
		seed.style |= LVS_NOSORTHEADER;
		boards = paned->addChild(WidgetBoards::Seed(seed));
		addWidget(boards);
		WinUtil::makeColumns(boards, boardColumns, BOARD_COLUMN_LAST, SETTING(BBSFRAME_BOARDS_ORDER), SETTING(BBSFRAME_BOARDS_WIDTHS));
		boards->onSelectionChanged([this] { handleBoardSelection(); });
		boards->onContextMenu([this](const dwt::ScreenCoordinate& point) { return handleBoardContextMenu(point); });
	}

	contentPaned = paned->addChild(SplitterContainer::Seed(SETTING(BBSFRAME_CONTENT_PANED_POS)));

	{
		auto seed = WinUtil::Seeds::table;
		seed.style |= LVS_NOSORTHEADER;
		entries = contentPaned->addChild(WidgetEntries::Seed(seed));
		addWidget(entries, ALWAYS_FOCUS);
		WinUtil::makeColumns(entries, entryColumns, ENTRY_COLUMN_LAST, SETTING(BBSFRAME_ENTRIES_ORDER), SETTING(BBSFRAME_ENTRIES_WIDTHS));
		entries->onSelectionChanged([this] { handleEntrySelection(); });
		entries->onDblClicked([this] { handleFetch(); });
		entries->onKeyDown([this](int key) { return handleEntriesKeyDown(key); });
		entries->onContextMenu([this](const dwt::ScreenCoordinate& point) { return handleEntryContextMenu(point); });
	}

	{
		documentGrid = contentPaned->addChild(Grid::Seed(3, 1));
		documentGrid->column(0).mode = GridInfo::FILL;
		documentGrid->row(2).mode = GridInfo::FILL;
		documentGrid->row(2).align = GridInfo::STRETCH;
		documentGrid->setSpacing(6);

		documentTitle = documentGrid->addChild(Label::Seed());
		documentGrid->setWidget(documentTitle, 0, 0);
		documentMeta = documentGrid->addChild(Label::Seed());
		documentGrid->setWidget(documentMeta, 1, 0);

		RichTextBox::Seed seed = WinUtil::Seeds::richTextBox;
		seed.style |= ES_READONLY | WS_VSCROLL;
		documentView = dwt::WidgetCreator<RichTextBox>::create(documentGrid, seed);
		WinUtil::setColor(documentView);
		documentView->setTextLimit(static_cast<int>(BBSManager::MAX_DOCUMENT_SIZE * 2));
		documentGrid->setWidget(documentView, 2, 0);
		addWidget(documentView);
	}

	{
		rebar = addChild(Rebar::Seed());
		auto toolbarSeed = ToolBar::Seed();
		toolbarSeed.style &= ~CCS_ADJUSTABLE;
		toolbar = addChild(toolbarSeed);
		toolbar->addButton(BUTTON_REFRESH, WinUtil::toolbarIcon(IDI_REFRESH), nullptr, T_("Refresh"), false, IDH_HUB, [this] { refreshBoards(); });
		toolbar->addButton(BUTTON_SUBSCRIBE, WinUtil::toolbarIcon(IDI_PLAY), nullptr, T_("Subscribe"), false, IDH_HUB, [this] { handleSubscribe(); });
		toolbar->addButton(BUTTON_LEAVE, WinUtil::toolbarIcon(IDI_PAUSE), nullptr, T_("Leave board"), false, IDH_HUB, [this] { handleLeave(); });
		toolbar->addButton(BUTTON_NEW, WinUtil::toolbarIcon(IDI_NOTEPAD), nullptr, T_("New thread"), false, IDH_HUB, [this] { handleNewThread(); });
		toolbar->addButton(BUTTON_REPLY, WinUtil::toolbarIcon(IDI_CHAT), nullptr, T_("Reply"), false, IDH_HUB, [this] { handleReply(); });
		toolbar->addButton(BUTTON_FETCH, WinUtil::toolbarIcon(IDI_DOWNLOAD), nullptr, T_("Fetch and verify post"), false, IDH_HUB, [this] { handleFetch(); });
		toolbar->addButton(BUTTON_WITHDRAW, WinUtil::toolbarIcon(IDI_DELETE), nullptr, T_("Withdraw post"), false, IDH_HUB, [this] { handleWithdraw(); });
		toolbar->addButton(BUTTON_COPY_TTH, WinUtil::toolbarIcon(IDI_MAGNET), nullptr, T_("Copy post TTH"), false, IDH_HUB, [this] { handleCopyTTH(); });
		toolbar->setLayout({ BUTTON_REFRESH, string(), BUTTON_SUBSCRIBE, BUTTON_LEAVE, string(), BUTTON_NEW, BUTTON_REPLY, BUTTON_FETCH, BUTTON_WITHDRAW, BUTTON_COPY_TTH });
		rebar->add(toolbar, RBBS_NOGRIPPER);

		filter = addChild(WinUtil::Seeds::textBox);
		filter->setCue(T_("Filter posts"));
		filter->onUpdated([this] { if(!refreshing) refreshEntries(); });
		addWidget(filter);
		rebar->add(filter, RBBS_NOGRIPPER);
		rebar->sendMessage(RB_MAXIMIZEBAND, 1);
	}

	initStatus();
	status->setIcon(STATUS_CONNECTION, WinUtil::statusIcon(IDI_HUB_OFF));
	status->setToolTip(STATUS_CONNECTION, T_("BBS0 connection state"));

	addAccel(FCONTROL, 'N', [this] { handleNewThread(); });
	addAccel(FCONTROL, 'R', [this] { handleReply(); });
	initAccels();

	frames.push_back(this);
	BBSManager::getInstance()->addListener(this);
	updateTitle();
	refreshBoards();
	layout();
}

BBSFrame::~BBSFrame() {
}

void BBSFrame::layout() {
	dwt::Rectangle area { getClientSize() };
	const auto toolbarHeight = rebar->refresh();
	area.pos.y += toolbarHeight;
	area.size.y -= toolbarHeight;
	area.size.y -= status->refresh();
	paned->resize(area);
}

bool BBSFrame::preClosing() {
	alive->store(false);
	BBSManager::getInstance()->removeListener(this);
	frames.erase(std::remove(frames.begin(), frames.end(), this), frames.end());
	hubFrame = nullptr;
	return true;
}

void BBSFrame::postClosing() {
	SettingsManager::getInstance()->set(SettingsManager::BBSFRAME_PANED_POS, paned->getSplitterPos(0));
	SettingsManager::getInstance()->set(SettingsManager::BBSFRAME_CONTENT_PANED_POS, contentPaned->getSplitterPos(0));
	SettingsManager::getInstance()->set(SettingsManager::BBSFRAME_BOARDS_ORDER, WinUtil::toString(boards->getColumnOrder()));
	SettingsManager::getInstance()->set(SettingsManager::BBSFRAME_BOARDS_WIDTHS, WinUtil::toString(boards->getColumnWidths()));
	SettingsManager::getInstance()->set(SettingsManager::BBSFRAME_ENTRIES_ORDER, WinUtil::toString(entries->getColumnOrder()));
	SettingsManager::getInstance()->set(SettingsManager::BBSFRAME_ENTRIES_WIDTHS, WinUtil::toString(entries->getColumnWidths()));
}

void BBSFrame::tabMenuImpl(dwt::Menu* menu) {
	menu->appendItem(T_("Open &hub"), [this] { handleOpenHub(); }, WinUtil::menuIcon(IDI_HUB));
	menu->appendItem(T_("Copy hub &address"), [this] { WinUtil::setClipboard(Text::toT(url)); });
	menu->appendSeparator();
}

void BBSFrame::updateTitle() {
	string hubName = url;
	if(hubFrame && hubFrame->client) hubName = hubFrame->client->getHubName();
	setText(T_("Bulletin boards") + _T(" - ") + Text::toT(BBSManager::sanitizeDisplayText(hubName, 256)));
	const auto online = isOnline();
	status->setText(STATUS_CONNECTION, online ? T_("Online") : T_("Offline cache"), true);
	status->setIcon(STATUS_CONNECTION, WinUtil::statusIcon(online ? IDI_HUB : IDI_HUB_OFF), true);
}

void BBSFrame::updateActions() {
	const auto board = getSelectedBoard();
	const auto entry = getSelectedEntry();
	const auto online = isOnline();
	const auto document = entry ? BBSManager::getInstance()->getDocument(entry->tth) : std::nullopt;
	const auto pending = entry && BBSManager::getInstance()->isDocumentPending(url, currentBoard, entry->tth);
	const auto myCID = getMyCID();
	const auto ownEntry = entry && !myCID.empty() && entry->authorId == myCID;
	const auto canWithdraw = board && entry && online && (board->canWithdrawAny() || (ownEntry && board->canWithdrawOwn()));

	toolbar->setButtonEnabled(BUTTON_SUBSCRIBE, board && online && board->canSubscribe() && !board->subscribed);
	toolbar->setButtonEnabled(BUTTON_LEAVE, board && online && board->subscribed);
	toolbar->setButtonEnabled(BUTTON_NEW, board && online && board->canPost() && board->maxSize > 0);
	toolbar->setButtonEnabled(BUTTON_REPLY, board && entry && online && board->canReply() && board->maxSize > 0);
	toolbar->setButtonEnabled(BUTTON_FETCH, entry && !document && !pending);
	toolbar->setButtonEnabled(BUTTON_WITHDRAW, canWithdraw);
	toolbar->setButtonEnabled(BUTTON_COPY_TTH, static_cast<bool>(entry));
}

void BBSFrame::updateStatusCounts() {
	status->setText(STATUS_BOARD, currentBoard.empty() ? T_("No board") : Text::toT(currentBoard), true);
	status->setText(STATUS_ENTRIES, str(TF_("%1% posts") % entries->size()), true);
}

void BBSFrame::setStatus(const tstring& text) {
	status->setText(STATUS_STATUS, Text::toT("[" + Util::getShortTimeString() + "] ") + text);
}

void BBSFrame::queueRefresh(unsigned flags, bool markDirty) noexcept {
	if(markDirty) flags |= REFRESH_MARK_DIRTY;
	const auto previous = refreshFlags.fetch_or(flags);
	if(previous != 0) return;
	auto guard = alive;
	callAsync([this, guard] {
		if(!guard->load()) return;
		const auto flags = refreshFlags.exchange(0);
		if(flags & REFRESH_CONNECTION) updateTitle();
		if(flags & REFRESH_BOARDS) refreshBoards();
		else if(flags & REFRESH_ENTRIES) refreshEntries();
		else if(flags & REFRESH_DOCUMENT) refreshDocument();
		if(flags & REFRESH_MARK_DIRTY) setDirty(SettingsManager::BOLD_HUB);
		updateActions();
	});
}

void BBSFrame::refreshBoards() {
	const auto snapshot = BBSManager::getInstance()->getBoards(url);
	const auto previousBoard = currentBoard;
	refreshing = true;
	HoldRedraw hold { boards };
	boards->clear();
	int selected = -1;
	for(const auto& board: snapshot) {
		const auto index = boards->insert(new BoardInfo(board));
		if(board.name == previousBoard) selected = index;
	}
	if(selected < 0 && !snapshot.empty()) selected = 0;
	currentBoard.clear();
	if(selected >= 0) {
		boards->select(selected);
		boards->ensureVisible(selected);
		currentBoard = boards->getData(selected)->getBoard().name;
	}
	if(currentBoard != previousBoard) currentTTH.clear();
	refreshing = false;
	refreshEntries();
	updateTitle();
}

void BBSFrame::refreshEntries() {
	vector<BBSEntry> snapshot;
	if(!currentBoard.empty()) snapshot = BBSManager::getInstance()->getEntries(url, currentBoard);
	snapshot.erase(std::remove_if(snapshot.begin(), snapshot.end(), [this](const BBSEntry& entry) { return !entryMatchesFilter(entry); }), snapshot.end());
	const auto ordered = orderEntries(snapshot);
	const auto previousTTH = currentTTH;
	refreshing = true;
	HoldRedraw hold { entries };
	entries->clear();
	int selected = -1;
	for(const auto& item: ordered) {
		const auto document = BBSManager::getInstance()->getDocument(item.entry.tth);
		const auto pending = BBSManager::getInstance()->isDocumentPending(url, currentBoard, item.entry.tth);
		const auto index = entries->insert(new EntryInfo(item.entry, document, pending, item.depth));
		if(item.entry.tth == previousTTH) selected = index;
	}
	currentTTH.clear();
	if(selected >= 0) {
		entries->select(selected);
		entries->ensureVisible(selected);
		currentTTH = entries->getData(selected)->getEntry().tth;
	}
	refreshing = false;
	refreshDocument();
	updateStatusCounts();
}

void BBSFrame::refreshDocument() {
	auto entry = getSelectedEntry();
	if(!entry) {
		showBoardDetails();
		updateActions();
		return;
	}
	auto document = BBSManager::getInstance()->getDocument(entry->tth);
	const auto subject = BBSManager::sanitizeDisplayText(document && !document->subject.empty() ? document->subject : entry->subject);
	const auto nick = BBSManager::sanitizeDisplayText(entry->nick.empty() ? entry->authorId : entry->nick, 128);
	documentTitle->setText(Text::toT(subject.empty() ? _("(no subject)") : subject));
	tstring meta = Text::toT(nick) + _T(" | ") + formatIndexedTime(entry->timestamp) + _T(" | TTH: ") + Text::toT(entry->tth);
	if(document) {
		meta += document->authorId == entry->authorId ? T_(" | Verified author claim") : T_(" | Warning: document author differs from hub submitter");
	} else {
		meta += BBSManager::getInstance()->isDocumentPending(url, currentBoard, entry->tth) ? T_(" | Fetching") : T_(" | Unverified index metadata");
	}
	documentMeta->setText(meta);
	documentView->setText(Util::emptyStringT);
	if(!document) {
		documentView->setText(T_("Post body is not cached."));
		updateActions();
		return;
	}

	bool rendered = false;
	const auto richTextLimit = static_cast<size_t>(std::max(1024, SETTING(RICH_TEXT_MAX_SIZE)));
	if(document->richText == 1 && SETTING(ENABLE_RICH_TEXT) && document->body.size() <= richTextLimit) {
		const auto parsed = RichText::parse(document->body, static_cast<size_t>(std::max(1, SETTING(CHAT_LINK_MAX_LENGTH))));
		if(parsed.valid) {
			const auto rtf = _T("{\\urtf1\n") + HtmlToRtf::convert(parsed.html, documentView, url) + _T("}\n");
			documentView->addTextSteady(rtf);
			rendered = true;
		}
	}
	if(!rendered) documentView->setText(Text::toT(document->body));
	documentView->setSelection(0, 0);
	documentView->sendMessage(WM_VSCROLL, SB_TOP);
	updateActions();
}

void BBSFrame::showBoardDetails() {
	auto board = getSelectedBoard();
	if(!board) {
		documentTitle->setText(T_("No bulletin boards"));
		documentMeta->setText(Util::emptyStringT);
		documentView->setText(Util::emptyStringT);
		return;
	}
	const auto title = BBSManager::sanitizeDisplayText(board->title.empty() ? board->name : board->title);
	documentTitle->setText(Text::toT(title));
	tstring meta = board->subscribed ? T_("Subscribed") : board->canSubscribe() ? T_("Available") : T_("Read only");
	if(board->gap) meta += T_(" | History gap");
	if(board->postCount >= 0) meta += Text::toT(str(F_(" | %1% posts") % board->postCount));
	documentMeta->setText(meta);
	documentView->setText(Text::toT(BBSManager::sanitizeDisplayText(board->description, 4096)));
}

vector<BBSFrame::OrderedEntry> BBSFrame::orderEntries(const vector<BBSEntry>& source) const {
	struct Group {
		string key;
		uint64_t updated = 0;
		vector<const BBSEntry*> entries;
	};
	vector<Group> groups;
	std::unordered_map<string, size_t> groupIndexes;
	for(const auto& entry: source) {
		const auto key = getThreadKey(entry);
		auto inserted = groupIndexes.emplace(key, groups.size());
		if(inserted.second) groups.push_back({ key, 0, {} });
		auto& group = groups[inserted.first->second];
		group.updated = std::max(group.updated, entry.timestamp);
		group.entries.push_back(&entry);
	}
	std::sort(groups.begin(), groups.end(), [](const Group& lhs, const Group& rhs) { return lhs.updated != rhs.updated ? lhs.updated > rhs.updated : lhs.key < rhs.key; });

	vector<OrderedEntry> result;
	result.reserve(source.size());
	for(auto& group: groups) {
		std::sort(group.entries.begin(), group.entries.end(), [](const BBSEntry* lhs, const BBSEntry* rhs) { return lhs->timestamp != rhs->timestamp ? lhs->timestamp < rhs->timestamp : lhs->tth < rhs->tth; });
		std::unordered_set<string> members;
		std::unordered_map<string, vector<const BBSEntry*>> children;
		vector<const BBSEntry*> roots;
		for(const auto entry: group.entries) members.insert(entry->tth);
		for(const auto entry: group.entries) {
			if(entry->parent.empty() || entry->parent == entry->tth || members.find(entry->parent) == members.end()) roots.push_back(entry);
			else children[entry->parent].push_back(entry);
		}
		for(auto& childList: children) std::sort(childList.second.begin(), childList.second.end(), [](const BBSEntry* lhs, const BBSEntry* rhs) { return lhs->timestamp != rhs->timestamp ? lhs->timestamp < rhs->timestamp : lhs->tth < rhs->tth; });
		std::unordered_set<string> visited;
		auto append = [&](const BBSEntry* root, size_t rootDepth) {
			vector<std::pair<const BBSEntry*, size_t>> stack { { root, rootDepth } };
			while(!stack.empty()) {
				auto current = stack.back();
				stack.pop_back();
				if(!visited.insert(current.first->tth).second) continue;
				result.emplace_back(*current.first, current.second);
				auto childList = children.find(current.first->tth);
				if(childList == children.end()) continue;
				for(auto child = childList->second.rbegin(); child != childList->second.rend(); ++child) stack.emplace_back(*child, current.second + 1);
			}
		};
		for(const auto root: roots) append(root, root->parent.empty() ? 0 : 1);
		for(const auto entry: group.entries) if(visited.find(entry->tth) == visited.end()) append(entry, 1);
	}
	return result;
}

bool BBSFrame::entryMatchesFilter(const BBSEntry& entry) const {
	string pattern;
	try { pattern = Text::fromT(filter->getText()); } catch(...) { return false; }
	Util::trim(pattern);
	if(pattern.empty()) return true;
	string text = entry.subject + '\n' + entry.nick + '\n' + entry.authorId + '\n' + entry.tth;
	auto document = BBSManager::getInstance()->getDocument(entry.tth);
	if(document) text += '\n' + document->subject;
	return Util::findSubString(text, pattern) != string::npos;
}

dcpp::AdcHub* BBSFrame::getAdcHub() const {
	if(!hubFrame || !hubFrame->client || !hubHintsEqual(hubFrame->url, url)) return nullptr;
	return dynamic_cast<AdcHub*>(hubFrame->client);
}

bool BBSFrame::isOnline() const {
	auto hub = getAdcHub();
	return hub && hub->supportsBBS() && hub->isConnected();
}

string BBSFrame::getMyCID() const {
	auto hub = getAdcHub();
	if(!hub || !hub->getMyIdentity().getUser()) return Util::emptyString;
	return hub->getMyIdentity().getUser()->getCID().toBase32();
}

std::optional<BBSBoard> BBSFrame::getSelectedBoard() const {
	if(currentBoard.empty()) return std::nullopt;
	return BBSManager::getInstance()->getBoard(url, currentBoard);
}

std::optional<BBSEntry> BBSFrame::getSelectedEntry() const {
	if(currentBoard.empty() || currentTTH.empty()) return std::nullopt;
	return BBSManager::getInstance()->getEntry(url, currentBoard, currentTTH);
}

void BBSFrame::handleBoardSelection() {
	if(refreshing) return;
	auto selected = boards->getSelectedData();
	const auto nextBoard = selected ? selected->getBoard().name : Util::emptyString;
	if(nextBoard == currentBoard) return;
	currentBoard = nextBoard;
	currentTTH.clear();
	refreshEntries();
}

void BBSFrame::handleEntrySelection() {
	if(refreshing) return;
	auto selected = entries->getSelectedData();
	currentTTH = selected ? selected->getEntry().tth : Util::emptyString;
	refreshDocument();
}

void BBSFrame::handleSubscribe() {
	auto hub = getAdcHub();
	auto board = getSelectedBoard();
	if(!hub || !hub->supportsBBS() || !board) { setStatus(T_("BBS0 is not available on the connected hub.")); return; }
	string error;
	if(hub->subscribeBBS(board->name, BBSManager::getInstance()->getResumeTimestamp(url, board->name), error)) setStatus(T_("Subscribed to the board."));
	if(!error.empty()) setStatus(Text::toT(error));
}

void BBSFrame::handleLeave() {
	auto hub = getAdcHub();
	auto board = getSelectedBoard();
	if(!hub || !hub->supportsBBS() || !board) { setStatus(T_("BBS0 is not available on the connected hub.")); return; }
	string error;
	if(hub->unsubscribeBBS(board->name, error)) setStatus(T_("The live board subscription was removed."));
	if(!error.empty()) setStatus(Text::toT(error));
}

void BBSFrame::handleNewThread() {
	auto hub = getAdcHub();
	auto board = getSelectedBoard();
	if(!hub || !hub->supportsBBS() || !board || !board->canPost()) { setStatus(T_("You cannot start a thread on this board.")); return; }
	BBSPostDlg dialog(this, url, board->name, Util::emptyString, Util::emptyStringT, board->maxSize);
	if(dialog.run() != IDOK) return;
	string error;
	if(hub->postBBS(board->name, Util::emptyString, dialog.getSubject(), dialog.getBody(), dialog.getRichText(), error)) setStatus(T_("Post submitted; publication is pending hub acceptance."));
	if(!error.empty()) setStatus(Text::toT(error));
}

void BBSFrame::handleReply() {
	auto hub = getAdcHub();
	auto board = getSelectedBoard();
	auto entry = getSelectedEntry();
	if(!hub || !hub->supportsBBS() || !board || !entry || !board->canReply()) { setStatus(T_("You cannot reply to this post.")); return; }
	auto document = BBSManager::getInstance()->getDocument(entry->tth);
	auto subjectText = BBSManager::sanitizeDisplayText(document && !document->subject.empty() ? document->subject : entry->subject);
	if(subjectText.size() < 3 || Util::strnicmp(subjectText.c_str(), "Re:", 3) != 0) subjectText = "Re: " + subjectText;
	BBSPostDlg dialog(this, url, board->name, entry->tth, Text::toT(subjectText), board->maxSize);
	if(dialog.run() != IDOK) return;
	string error;
	if(hub->postBBS(board->name, entry->tth, dialog.getSubject(), dialog.getBody(), dialog.getRichText(), error)) setStatus(T_("Reply submitted; publication is pending hub acceptance."));
	if(!error.empty()) setStatus(Text::toT(error));
}

void BBSFrame::handleFetch() {
	auto entry = getSelectedEntry();
	if(!entry) return;
	if(BBSManager::getInstance()->getDocument(entry->tth)) { refreshDocument(); return; }
	string error;
	bool started = false;
	if(auto hub = getAdcHub(); hub && hub->supportsBBS() && hub->isConnected()) started = hub->fetchBBS(currentBoard, entry->tth, error);
	else started = BBSManager::getInstance()->loadCachedDocument(url, currentBoard, entry->tth, error);
	if(started) setStatus(BBSManager::getInstance()->getDocument(entry->tth) ? T_("The cached post was verified.") : T_("Searching for the post by exact TTH."));
	if(!error.empty()) setStatus(Text::toT(error));
	refreshEntries();
}

void BBSFrame::handleWithdraw() {
	auto hub = getAdcHub();
	auto board = getSelectedBoard();
	auto entry = getSelectedEntry();
	if(!hub || !hub->supportsBBS() || !board || !entry) return;
	const auto myCID = getMyCID();
	if(!board->canWithdrawAny() && (myCID.empty() || entry->authorId != myCID || !board->canWithdrawOwn())) { setStatus(T_("You cannot withdraw this post.")); return; }
	const auto title = BBSManager::sanitizeDisplayText(entry->subject.empty() ? entry->tth : entry->subject);
	if(dwt::MessageBox(this).show(Text::toT(title) + T_("\n\nWithdraw this post from the board? Existing peer copies cannot be deleted."), T_("Withdraw BBS post"), dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) != IDYES) return;
	string error;
	if(hub->withdrawBBS(board->name, entry->tth, error)) setStatus(T_("Withdrawal requested."));
	if(!error.empty()) setStatus(Text::toT(error));
}

void BBSFrame::handleCopyTTH() {
	auto entry = getSelectedEntry();
	if(entry) WinUtil::setClipboard(Text::toT(entry->tth));
}

void BBSFrame::handleOpenHub() {
	if(hubFrame) HubFrame::activateWindow(url); else HubFrame::openWindow(getParent(), url);
}

bool BBSFrame::handleBoardContextMenu(dwt::ScreenCoordinate point) {
	auto board = getSelectedBoard();
	if(!board) return false;
	auto menu = addChild(WinUtil::Seeds::menu);
	menu->setTitle(Text::toT(BBSManager::sanitizeDisplayText(board->title.empty() ? board->name : board->title)), WinUtil::menuIcon(IDI_CHAT));
	menu->appendItem(T_("&Subscribe"), [this] { handleSubscribe(); }, WinUtil::menuIcon(IDI_PLAY), isOnline() && board->canSubscribe() && !board->subscribed);
	menu->appendItem(T_("&Leave board"), [this] { handleLeave(); }, WinUtil::menuIcon(IDI_PAUSE), isOnline() && board->subscribed);
	menu->appendSeparator();
	menu->appendItem(T_("&New thread...\tCtrl+N"), [this] { handleNewThread(); }, WinUtil::menuIcon(IDI_NOTEPAD), isOnline() && board->canPost());
	menu->open(point);
	return true;
}

bool BBSFrame::handleEntryContextMenu(dwt::ScreenCoordinate point) {
	auto entry = getSelectedEntry();
	if(!entry) return false;
	auto board = getSelectedBoard();
	auto menu = addChild(WinUtil::Seeds::menu);
	menu->setTitle(Text::toT(BBSManager::sanitizeDisplayText(entry->subject.empty() ? entry->tth : entry->subject)), WinUtil::menuIcon(IDI_CHAT));
	menu->appendItem(T_("&Fetch and verify"), [this] { handleFetch(); }, WinUtil::menuIcon(IDI_DOWNLOAD), !BBSManager::getInstance()->getDocument(entry->tth) && !BBSManager::getInstance()->isDocumentPending(url, currentBoard, entry->tth));
	menu->appendItem(T_("&Reply...\tCtrl+R"), [this] { handleReply(); }, WinUtil::menuIcon(IDI_CHAT), board && isOnline() && board->canReply());
	const auto myCID = getMyCID();
	const auto canWithdraw = board && isOnline() && (board->canWithdrawAny() || (!myCID.empty() && entry->authorId == myCID && board->canWithdrawOwn()));
	menu->appendItem(T_("&Withdraw"), [this] { handleWithdraw(); }, WinUtil::menuIcon(IDI_DELETE), canWithdraw);
	menu->appendSeparator();
	menu->appendItem(T_("Copy &TTH"), [this] { handleCopyTTH(); }, WinUtil::menuIcon(IDI_MAGNET));
	menu->open(point);
	return true;
}

bool BBSFrame::handleEntriesKeyDown(int key) {
	if(key == VK_RETURN && !WinUtil::isCtrl() && !WinUtil::isShift() && !WinUtil::isAlt()) { handleFetch(); return true; }
	if(key == VK_DELETE) { handleWithdraw(); return true; }
	if(key == 'C' && WinUtil::isCtrl()) { handleCopyTTH(); return true; }
	return false;
}

void BBSFrame::on(BBSManagerListener::BoardUpdated, const string& hubUrl, const string&) noexcept {
	if(hubHintsEqual(hubUrl, url)) queueRefresh(REFRESH_BOARDS);
}

void BBSFrame::on(BBSManagerListener::EntryUpdated, const string& hubUrl, const string&, const string&) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	queueRefresh(REFRESH_ENTRIES, true);
}

void BBSFrame::on(BBSManagerListener::DocumentUpdated, const string& hubUrl, const string&, const string&) noexcept {
	if(hubHintsEqual(hubUrl, url)) queueRefresh(REFRESH_ENTRIES | REFRESH_DOCUMENT);
}

void BBSFrame::on(BBSManagerListener::Status, const string& hubUrl, const string& line) noexcept {
	if(!hubHintsEqual(hubUrl, url)) return;
	auto guard = alive;
	callAsync([this, guard, line] { if(guard->load()) setStatus(Text::toT(line)); });
}

void BBSFrame::on(BBSManagerListener::SupportUpdated, const string& hubUrl, bool) noexcept {
	if(hubHintsEqual(hubUrl, url)) queueRefresh(REFRESH_CONNECTION | REFRESH_BOARDS | REFRESH_ENTRIES | REFRESH_DOCUMENT);
}
