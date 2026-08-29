/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_BBS_FRAME_H
#define DCPLUSPLUS_WIN32_BBS_FRAME_H

#include <atomic>
#include <memory>

#include <dcpp/BBSManager.h>
#include <dcpp/WindowInfo.h>

#include "MDIChildFrame.h"

namespace dcpp { class AdcHub; }

class HubFrame;

class BBSFrame : public MDIChildFrame<BBSFrame>, private BBSManagerListener {
	typedef MDIChildFrame<BBSFrame> BaseType;

	friend class MDIChildFrame<BBSFrame>;

public:
	enum Status {
		STATUS_STATUS,
		STATUS_CONNECTION,
		STATUS_BOARD,
		STATUS_ENTRIES,
		STATUS_LAST
	};

	static const string id;
	const string& getId() const;
	WindowParams getWindowParams() const;

	static void openWindow(TabViewPtr parent, string url, HubFrame* hubFrame = nullptr, bool activate = true);
	static void parseWindowParams(TabViewPtr parent, const WindowParams& params);
	static void attachHub(HubFrame* hubFrame);
	static void detachHub(HubFrame* hubFrame);
	static void hubStateChanged(HubFrame* hubFrame);

private:
	enum BoardColumn {
		BOARD_COLUMN_TITLE,
		BOARD_COLUMN_NAME,
		BOARD_COLUMN_STATE,
		BOARD_COLUMN_POSTS,
		BOARD_COLUMN_LAST
	};

	enum EntryColumn {
		ENTRY_COLUMN_SUBJECT,
		ENTRY_COLUMN_AUTHOR,
		ENTRY_COLUMN_TIME,
		ENTRY_COLUMN_SIZE,
		ENTRY_COLUMN_STATE,
		ENTRY_COLUMN_LAST
	};

	class BoardInfo {
	public:
		explicit BoardInfo(const BBSBoard& board);
		const tstring& getText(int column) const { return columns[column]; }
		const BBSBoard& getBoard() const { return board; }

	private:
		BBSBoard board;
		tstring columns[BOARD_COLUMN_LAST];
	};

	class EntryInfo {
	public:
		EntryInfo(const BBSEntry& entry, const std::optional<BBSDocument>& document, bool pending, size_t depth);
		const tstring& getText(int column) const { return columns[column]; }
		const BBSEntry& getEntry() const { return entry; }

	private:
		BBSEntry entry;
		tstring columns[ENTRY_COLUMN_LAST];
	};

	struct OrderedEntry {
		OrderedEntry(const BBSEntry& entry, size_t depth) : entry(entry), depth(depth) { }
		BBSEntry entry;
		size_t depth;
	};

	typedef TypedTable<BoardInfo> WidgetBoards;
	typedef WidgetBoards* WidgetBoardsPtr;
	typedef TypedTable<EntryInfo> WidgetEntries;
	typedef WidgetEntries* WidgetEntriesPtr;

	static vector<BBSFrame*> frames;

	HubFrame* hubFrame;
	string url;
	string currentBoard;
	string currentTTH;

	RebarPtr rebar;
	ToolBarPtr toolbar;
	TextBoxPtr filter;
	SplitterContainerPtr paned;
	SplitterContainerPtr contentPaned;
	WidgetBoardsPtr boards;
	WidgetEntriesPtr entries;
	GridPtr documentGrid;
	LabelPtr documentTitle;
	LabelPtr documentMeta;
	RichTextBoxPtr documentView;

	bool refreshing;
	std::atomic_uint refreshFlags;
	std::shared_ptr<std::atomic<bool>> alive;

	BBSFrame(TabViewPtr parent, string&& url, HubFrame* hubFrame);
	virtual ~BBSFrame();

	void layout();
	bool preClosing();
	void postClosing();
	void tabMenuImpl(dwt::Menu* menu);

	void updateTitle();
	void updateActions();
	void updateStatusCounts();
	void setStatus(const tstring& text);
	void queueRefresh(unsigned flags, bool markDirty = false) noexcept;
	void refreshBoards();
	void refreshEntries();
	void refreshDocument();
	void showBoardDetails();
	vector<OrderedEntry> orderEntries(const vector<BBSEntry>& source) const;
	bool entryMatchesFilter(const BBSEntry& entry) const;

	dcpp::AdcHub* getAdcHub() const;
	bool isOnline() const;
	string getMyCID() const;
	std::optional<BBSBoard> getSelectedBoard() const;
	std::optional<BBSEntry> getSelectedEntry() const;

	void handleBoardSelection();
	void handleEntrySelection();
	void handleSubscribe();
	void handleLeave();
	void handleNewThread();
	void handleReply();
	void handleFetch();
	void handleWithdraw();
	void handleCopyTTH();
	void handleOpenHub();
	bool handleBoardContextMenu(dwt::ScreenCoordinate pt);
	bool handleEntryContextMenu(dwt::ScreenCoordinate pt);
	bool handleEntriesKeyDown(int key);

	void on(BBSManagerListener::BoardUpdated, const string& hubUrl, const string& board) noexcept override;
	void on(BBSManagerListener::EntryUpdated, const string& hubUrl, const string& board, const string& tth) noexcept override;
	void on(BBSManagerListener::DocumentUpdated, const string& hubUrl, const string& board, const string& tth) noexcept override;
	void on(BBSManagerListener::Status, const string& hubUrl, const string& line) noexcept override;
	void on(BBSManagerListener::SupportUpdated, const string& hubUrl, bool supported) noexcept override;
};

#endif // DCPLUSPLUS_WIN32_BBS_FRAME_H
