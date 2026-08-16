/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "ExperimentalPage.h"

#include <limits>

#include <dcpp/File.h>
#include <dcpp/HashManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/ShareManager.h>
#include <dcpp/version.h>

#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/Spinner.h>
#include <dwt/widgets/TabView.h>

#include "resource.h"
#include "WinUtil.h"

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::Spinner;

namespace {

constexpr int BYTES_PER_KIB = 1024;
constexpr int MAX_KIB_SETTING = static_cast<int>(
	(static_cast<int64_t>(std::numeric_limits<int>::max()) + BYTES_PER_KIB - 1) / BYTES_PER_KIB);

const ColumnInfo tempColumns[] = {
	{ N_("File"), 130, false },
	{ N_("Size"), 75, true },
	{ N_("Route"), 140, false },
	{ N_("TTH"), 250, false },
	{ N_("Path"), 220, false }
};

/** An opaque settings tab page that uses the same default background as its child grid. */
class ExperimentalTabPage : public dwt::Container
{
public:
	ExperimentalTabPage(dwt::Widget* parent, const tstring& title, size_t rows) :
		dwt::Container(parent),
		grid(nullptr)
	{
		dwt::Container::Seed seed;
		seed.caption = title;
		seed.style &= ~WS_VISIBLE;
		seed.exStyle |= WS_EX_CONTROLPARENT;
		create(seed);
		setHelpId(IDH_EXPERIMENTALPAGE);

		grid = addChild(Grid::Seed(rows, 1));
		grid->setSpacing(10);
		grid->column(0).mode = GridInfo::FILL;
	}

	GridPtr content() const { return grid; }

	dwt::Point getPreferredSize() override {
		return grid->getPreferredSize() + dwt::Point(14, 12);
	}

	void layout() override {
		const auto size = getClientSize();
		grid->resize(dwt::Rectangle(7, 4,
			std::max<LONG>(0, size.x - 14), std::max<LONG>(0, size.y - 12)));
	}

private:
	GridPtr grid;
};

/** TabView normally has no preferred size because it is used to fill application frames.
 * Settings pages need one so that their shared ScrolledContainer remains a small-window fallback. */
class ExperimentalTabView : public dwt::TabView
{
public:
	typedef ExperimentalTabView* ObjectType;
	typedef dwt::TabView::Seed Seed;

	explicit ExperimentalTabView(dwt::Widget* parent) : dwt::TabView(parent) { }

	ExperimentalTabPage* addPage(const tstring& title, size_t rows) {
		auto page = new ExperimentalTabPage(this, title, rows);
		pages.push_back(page);
		add(page);
		return page;
	}

	dwt::Point getPreferredSize() override {
		dwt::Point contentSize;
		for(auto page: pages) {
			const auto size = page->getPreferredSize();
			contentSize.x = std::max(contentSize.x, size.x);
			contentSize.y = std::max(contentSize.y, size.y);
		}

		// Ensure that all single-line tab labels fit without scroll buttons at the preferred width.
		LONG tabsRight = 0;
		for(size_t i = 0; i < size(); ++i) {
			RECT rect = { 0 };
			if(TabCtrl_GetItemRect(handle(), static_cast<int>(i), &rect)) {
				tabsRight = std::max(tabsRight, rect.right);
			}
		}
		contentSize.x = std::max(contentSize.x, tabsRight);

		RECT rect = { 0, 0, contentSize.x, contentSize.y };
		TabCtrl_AdjustRect(handle(), TRUE, &rect);
		return dwt::Point(rect.right - rect.left, rect.bottom - rect.top);
	}

private:
	std::vector<ExperimentalTabPage*> pages;
};

}

ExperimentalPage::ExperimentalPage(dwt::Widget* parent) :
	PropPage(parent, 1, 1),
	tempShares(nullptr),
	tempSummary(nullptr),
	removeTemp(nullptr),
	clearTemps(nullptr),
	hashDbStatus(nullptr),
	hashDbCallbackToken(std::make_shared<int>(0)),
	hashDbMaintenanceRunning(false)
{
	setHelpId(IDH_EXPERIMENTALPAGE);
	grid->row(0).mode = GridInfo::FILL;
	grid->row(0).align = GridInfo::STRETCH;
	grid->column(0).mode = GridInfo::FILL;

	auto tabSeed = WinUtil::Seeds::tabs;
	tabSeed.style &= ~(TCS_OWNERDRAWFIXED | TCS_MULTILINE | TCS_RAGGEDRIGHT | TCS_TOOLTIPS);
	tabSeed.exStyle |= WS_EX_CONTROLPARENT;
	tabSeed.widthConfig = 0;
	tabSeed.closeable = false;
	auto tabs = dwt::WidgetCreator<ExperimentalTabView>::create(grid, tabSeed);
	tabs->setHelpId(IDH_EXPERIMENTALPAGE);
	grid->setWidget(tabs, 0, 0);

	auto chatGrid = tabs->addPage(T_("Chat and sharing"), 2)->content();
	chatGrid->row(1).mode = GridInfo::FILL;
	chatGrid->row(1).align = GridInfo::STRETCH;

	{
		auto group = chatGrid->addChild(GroupBox::Seed(T_("Rich text and temporary shares")));
		group->setHelpId(IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARES);
		auto cur = group->addChild(Grid::Seed(4, 1));
		cur->column(0).mode = GridInfo::FILL;

		auto checks = cur->addChild(Grid::Seed(3, 1));
		checks->column(0).mode = GridInfo::FILL;
		auto richText = checks->addChild(CheckBox::Seed(T_("Enable ADC RTF0 CommonMark chat")));
		richText->setHelpId(IDH_SETTINGS_EXPERIMENTAL_ENABLE_RICH_TEXT);
		items.emplace_back(richText, SettingsManager::ENABLE_RICH_TEXT, PropPage::T_BOOL);
		auto temp = checks->addChild(CheckBox::Seed(T_("Enable temporary file sharing in chats")));
		temp->setHelpId(IDH_SETTINGS_EXPERIMENTAL_ENABLE_TEMP_SHARES);
		items.emplace_back(temp, SettingsManager::ENABLE_RTF_TEMP_SHARES, PropPage::T_BOOL);
		auto inlineImages = checks->addChild(CheckBox::Seed(T_("Insert dropped image files as inline media")));
		inlineImages->setHelpId(IDH_SETTINGS_EXPERIMENTAL_DROPPED_IMAGES_INLINE);
		items.emplace_back(inlineImages, SettingsManager::RTF_DROPPED_IMAGES_INLINE, PropPage::T_BOOL);

		addIntItem(cur, T_("Rich text message size limit"), SettingsManager::RICH_TEXT_MAX_SIZE,
			IDH_SETTINGS_EXPERIMENTAL_RICH_TEXT_MAX_SIZE, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
		addIntItem(cur, T_("Clickable chat link length limit"), SettingsManager::CHAT_LINK_MAX_LENGTH,
			IDH_SETTINGS_EXPERIMENTAL_CHAT_LINK_MAX_LENGTH, T_("characters"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Maximum temporary shares"), SettingsManager::RTF_TEMP_SHARE_LIMIT,
			IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARE_LIMIT, T_("files"), 1, 10000);
	}

	{
		auto group = chatGrid->addChild(GroupBox::Seed(T_("Active temporary shares")));
		group->setHelpId(IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARES);
		auto cur = group->addChild(Grid::Seed(3, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->row(0).mode = GridInfo::FILL;
		cur->row(0).size = cur->scale(110);
		cur->row(0).align = GridInfo::STRETCH;
		tempShares = cur->addChild(WinUtil::Seeds::Dialog::table);
		tempShares->setHelpId(IDH_SETTINGS_EXPERIMENTAL_TEMP_SHARES);
		WinUtil::makeColumns(tempShares, tempColumns, 5);
		tempShares->onGetEmptyText([] { return T_("No temporary attachment shares are active"); });
		tempShares->onSelectionChanged([this] { handleTempSelectionChanged(); });

		auto buttons = cur->addChild(Grid::Seed(1, 4));
		buttons->column(3).mode = GridInfo::FILL;
		removeTemp = buttons->addChild(Button::Seed(T_("&Remove selected")));
		removeTemp->onClicked([this] { handleRemoveTemps(); });
		clearTemps = buttons->addChild(Button::Seed(T_("&Clear all")));
		clearTemps->onClicked([this] { handleClearTemps(); });
		buttons->addChild(Button::Seed(T_("Re&fresh")))->onClicked([this] { fillTempShares(); });
		tempSummary = cur->addChild(Label::Seed(tstring()));

		// Preserve the group's preferred height while allowing the table to consume extra tab space.
		chatGrid->row(1).size = group->getPreferredSize().y;
		tempShares->onWindowPosChanged([this](const dwt::Rectangle&) { layoutTempShares(); });
	}

	auto transferGrid = tabs->addPage(T_("Transfers and hashing"), 2)->content();

	{
		auto group = transferGrid->addChild(GroupBox::Seed(T_("Multi-connection transfers (MCN)")));
		auto cur = group->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;
		addIntItem(cur, T_("Maximum download connections per user"), SettingsManager::MAX_MCN_DOWNLOADS,
			IDH_SETTINGS_EXPERIMENTAL_MCN_DOWNLOADS, T_("connections"), 1, 100);
		addIntItem(cur, T_("Maximum upload connections per user"), SettingsManager::MAX_MCN_UPLOADS,
			IDH_SETTINGS_EXPERIMENTAL_MCN_UPLOADS, T_("connections"), 1, 100);
	}

	{
		auto group = transferGrid->addChild(GroupBox::Seed(T_("Hashing and share database")));
		auto cur = group->addChild(Grid::Seed(5, 1));
		cur->column(0).mode = GridInfo::FILL;
		addIntItem(cur, T_("Maximum hash speed"), SettingsManager::MAX_HASH_SPEED,
			IDH_SETTINGS_EXPERIMENTAL_MAX_HASH_SPEED, T_("MiB/s (0 = unlimited)"), 0, UD_MAXVAL);
		addIntItem(cur, T_("Hash database write batch size"), SettingsManager::HASH_DB_WRITE_BATCH_SIZE,
			IDH_SETTINGS_EXPERIMENTAL_HASH_BATCH_SIZE, T_("statements"), 1, UD_MAXVAL);

		auto checks = cur->addChild(Grid::Seed(1, 2));
		checks->column(0).mode = GridInfo::FILL;
		auto verify = checks->addChild(CheckBox::Seed(T_("Verify hash database on startup")));
		verify->setHelpId(IDH_SETTINGS_EXPERIMENTAL_HASH_VERIFY_STARTUP);
		items.emplace_back(verify, SettingsManager::HASH_DB_VERIFY_STARTUP, PropPage::T_BOOL);
		auto compact = checks->addChild(CheckBox::Seed(T_("Compact hash database after rebuild")));
		compact->setHelpId(IDH_SETTINGS_EXPERIMENTAL_HASH_COMPACT_REBUILD);
		items.emplace_back(compact, SettingsManager::HASH_DB_COMPACT_ON_REBUILD, PropPage::T_BOOL);

		auto cache = cur->addChild(CheckBox::Seed(T_("Use cached share tree on startup")));
		cache->setHelpId(IDH_SETTINGS_EXPERIMENTAL_SHARE_CACHE);
		items.emplace_back(cache, SettingsManager::SHARE_CACHE, PropPage::T_BOOL);

		auto buttons = cur->addChild(Grid::Seed(2, 4));
		buttons->column(3).mode = GridInfo::FILL;
		buttons->addChild(Button::Seed(T_("&Verify")))->onClicked([this] { handleVerifyHashDb(false); });
		buttons->addChild(Button::Seed(T_("Full &check")))->onClicked([this] { handleVerifyHashDb(true); });
		buttons->addChild(Button::Seed(T_("&Optimize")))->onClicked([this] { handleOptimizeHashDb(); });
		buttons->addChild(Button::Seed(T_("&Compact")))->onClicked([this] { handleCompactHashDb(); });
		hashDbStatus = buttons->addChild(Label::Seed(_T(" ")));
		buttons->setWidget(hashDbStatus, 1, 0, 1, 4);
	}

	auto protocolGrid = tabs->addPage(T_("Protocol limits"), 1)->content();

	{
		auto group = protocolGrid->addChild(GroupBox::Seed(T_("Protocol resource limits")));
		auto cur = group->addChild(Grid::Seed(11, 1));
		cur->column(0).mode = GridInfo::FILL;
		addIntItem(cur, T_("Queued protocol data limit"), SettingsManager::MAX_QUEUED_PROTOCOL_DATA,
			IDH_SETTINGS_EXPERIMENTAL_MAX_QUEUED_PROTOCOL_DATA, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
		addIntItem(cur, T_("Concurrent peer connection limit"), SettingsManager::MAX_CONCURRENT_CONNECTIONS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_CONCURRENT_CONNECTIONS, T_("connections"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Incoming connection flood window"), SettingsManager::FLOOD_WINDOW,
			IDH_SETTINGS_EXPERIMENTAL_FLOOD_WINDOW, T_("milliseconds"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Global UDP rate window"), SettingsManager::GLOBAL_WINDOW,
			IDH_SETTINGS_EXPERIMENTAL_GLOBAL_WINDOW, T_("milliseconds"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Global UDP packet limit"), SettingsManager::GLOBAL_LIMIT,
			IDH_SETTINGS_EXPERIMENTAL_GLOBAL_LIMIT, T_("packets per window"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Per-peer UDP packet limit"), SettingsManager::PEER_LIMIT,
			IDH_SETTINGS_EXPERIMENTAL_PEER_LIMIT, T_("packets per window"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Per-peer UDP rate window"), SettingsManager::PEER_WINDOW,
			IDH_SETTINGS_EXPERIMENTAL_PEER_WINDOW, T_("milliseconds"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Tracked UDP peer limit"), SettingsManager::MAX_TRACKED_PEERS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_TRACKED_PEERS, T_("peers"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Encrypted UDP packet limit"), SettingsManager::MAX_SUDP_PACKET,
			IDH_SETTINGS_EXPERIMENTAL_MAX_SUDP_PACKET, T_("bytes"), 32, UD_MAXVAL);
		addIntItem(cur, T_("Stored encrypted UDP key limit"), SettingsManager::MAX_SUDP_KEYS,
			IDH_SETTINGS_EXPERIMENTAL_MAX_SUDP_KEYS, T_("keys"), 1, UD_MAXVAL);
		addIntItem(cur, T_("Generated partial file list limit"), SettingsManager::MAX_PARTIAL_LIST_BYTES,
			IDH_SETTINGS_EXPERIMENTAL_MAX_PARTIAL_LIST_BYTES, T_("KiB"), 1, MAX_KIB_SETTING, BYTES_PER_KIB);
	}

	PropPage::read(items);
	readScaledIntItems();
	fillTempShares();
}

ExperimentalPage::~ExperimentalPage() {
	hashDbCallbackToken.reset();
}

void ExperimentalPage::layout() {
	PropPage::layout();
	layoutTempShares();
}

void ExperimentalPage::write() {
	PropPage::write(items);
	writeScaledIntItems();

	auto settings = SettingsManager::getInstance();
	auto clamp = [settings](SettingsManager::IntSetting setting, int minimum) {
		if(settings->get(setting) < minimum) settings->set(setting, minimum);
	};
	clamp(SettingsManager::MAX_MCN_DOWNLOADS, 1);
	clamp(SettingsManager::MAX_MCN_UPLOADS, 1);
	clamp(SettingsManager::MAX_HASH_SPEED, 0);
	clamp(SettingsManager::HASH_DB_WRITE_BATCH_SIZE, 1);
	clamp(SettingsManager::RTF_TEMP_SHARE_LIMIT, 1);
	clamp(SettingsManager::MAX_QUEUED_PROTOCOL_DATA, 1024);
	clamp(SettingsManager::MAX_CONCURRENT_CONNECTIONS, 1);
	clamp(SettingsManager::FLOOD_WINDOW, 1);
	clamp(SettingsManager::GLOBAL_WINDOW, 1);
	clamp(SettingsManager::GLOBAL_LIMIT, 1);
	clamp(SettingsManager::PEER_LIMIT, 1);
	clamp(SettingsManager::PEER_WINDOW, 1);
	clamp(SettingsManager::MAX_TRACKED_PEERS, 1);
	clamp(SettingsManager::MAX_SUDP_PACKET, 32);
	clamp(SettingsManager::MAX_SUDP_KEYS, 1);
	clamp(SettingsManager::MAX_PARTIAL_LIST_BYTES, 1024);
	clamp(SettingsManager::CHAT_LINK_MAX_LENGTH, 1);
	clamp(SettingsManager::RICH_TEXT_MAX_SIZE, 1024);
}

void ExperimentalPage::layoutTempShares() {
	if(!tempShares) return;
	const auto width = tempShares->getWindowSize().x;
	tempShares->setColumnWidth(4, std::max<LONG>(180, width - 610));
}

void ExperimentalPage::addIntItem(GridPtr target, const tstring& text, int setting,
	unsigned helpId, const tstring& unit, int minimum, int maximum, int multiplier)
{
	auto row = target->addChild(Grid::Seed(1, 4));
	row->column(0).mode = GridInfo::FILL;
	row->column(1).size = 72;
	row->column(1).mode = GridInfo::STATIC;
	row->addChild(Label::Seed(text))->setHelpId(helpId);
	auto box = row->addChild(WinUtil::Seeds::Dialog::intTextBox);
	box->setHelpId(helpId);
	if(multiplier == 1) {
		items.emplace_back(box, setting, PropPage::T_INT_WITH_SPIN);
	} else {
		scaledIntItems.push_back({ box, setting, multiplier });
	}
	auto spin = row->addChild(Spinner::Seed(minimum, maximum, box));
	row->setWidget(spin);
	spin->setHelpId(helpId);
	row->addChild(Label::Seed(unit))->setHelpId(helpId);
}

void ExperimentalPage::readScaledIntItems() {
	auto settings = SettingsManager::getInstance();
	for(const auto& item: scaledIntItems) {
		const auto storedValue = static_cast<int64_t>(settings->get(
			static_cast<SettingsManager::IntSetting>(item.setting)));
		// Round upward so opening and applying Settings can never reduce an existing byte limit.
		const auto displayedValue = storedValue > 0 ?
			(storedValue + item.multiplier - 1) / item.multiplier : 0;
		item.box->setText(Text::toT(std::to_string(displayedValue)));
	}
}

void ExperimentalPage::writeScaledIntItems() {
	auto settings = SettingsManager::getInstance();
	for(const auto& item: scaledIntItems) {
		const auto setting = static_cast<SettingsManager::IntSetting>(item.setting);
		const auto text = Text::fromT(item.box->getText());
		if(text.empty()) {
			settings->unset(setting);
			continue;
		}

		const auto maximumStoredValue = static_cast<int64_t>(std::numeric_limits<int>::max());
		const auto maximum = (maximumStoredValue + item.multiplier - 1) / item.multiplier;
		const auto displayedValue = std::max<int64_t>(0, std::min(Util::toInt64(text), maximum));
		settings->set(setting, static_cast<int>(std::min(displayedValue * item.multiplier, maximumStoredValue)));
	}
}

void ExperimentalPage::fillTempShares() {
	tempShares->clear();
	const auto shares = ShareManager::getInstance()->getTempShares();
	for(const auto& share: shares) {
		bool available = false;
		try {
			File file(share.realPath, File::READ, File::OPEN | File::SHARED);
			available = file.getSize() == share.size && file.getLastModified() == share.timestamp;
		} catch(...) {
		}
		auto name = Text::toT(Util::getFileName(share.realPath));
		if(!available) name += T_(" (missing or changed)");
		tempShares->insert({ name, Text::toT(Util::formatBytes(share.size)), Text::toT(share.hubUrl),
			Text::toT(share.tth.toBase32()), Text::toT(share.realPath) });
	}
	tempSummary->setText(T_("Active temporary shares: ") + Text::toT(std::to_string(shares.size())));
	handleTempSelectionChanged();
}

void ExperimentalPage::handleTempSelectionChanged() {
	const auto any = tempShares->hasSelected();
	removeTemp->setEnabled(any);
	clearTemps->setEnabled(tempShares->size() != 0);
}

void ExperimentalPage::handleRemoveTemps() {
	for(int row = tempShares->getNext(-1, LVNI_SELECTED); row != -1;
		row = tempShares->getNext(row, LVNI_SELECTED))
	{
		try {
			ShareManager::getInstance()->removeTempShare(Text::fromT(tempShares->getText(row, 4)),
				TTHValue(Text::fromT(tempShares->getText(row, 3))), Text::fromT(tempShares->getText(row, 2)));
		} catch(...) {
		}
	}
	fillTempShares();
}

void ExperimentalPage::handleClearTemps() {
	if(dwt::MessageBox(this).show(T_("Remove all active temporary attachment shares and cancel pending preparations?"),
		_T(APPNAME) _T(" ") _T(VERSIONSTRING), dwt::MessageBox::BOX_YESNO,
		dwt::MessageBox::BOX_ICONQUESTION) != IDYES) return;
	ShareManager::getInstance()->clearTempShares();
	fillTempShares();
}

void ExperimentalPage::handleVerifyHashDb(bool fullCheck) {
	if(hashDbMaintenanceRunning) return;
	setHashDbMaintenanceRunning(true, fullCheck ?
		T_("Checking the hash database...") : T_("Verifying the hash database..."));
	HashManager::getInstance()->verifyHashStoreAsync(fullCheck,
		hashDbCompletion(T_("Hash database check passed"),
			T_("Hash database check failed; see the system log for details")));
}

void ExperimentalPage::handleOptimizeHashDb() {
	if(hashDbMaintenanceRunning) return;
	setHashDbMaintenanceRunning(true, T_("Optimizing the hash database..."));
	HashManager::getInstance()->optimizeHashStoreAsync(hashDbCompletion(T_("Hash database optimized")));
}

void ExperimentalPage::handleCompactHashDb() {
	if(hashDbMaintenanceRunning) return;
	if(dwt::MessageBox(this).show(T_("Compact the hash database now?"), _T(APPNAME) _T(" ") _T(VERSIONSTRING),
		dwt::MessageBox::BOX_YESNO, dwt::MessageBox::BOX_ICONQUESTION) != IDYES) return;
	setHashDbMaintenanceRunning(true, T_("Compacting the hash database..."));
	HashManager::getInstance()->compactHashStoreAsync(hashDbCompletion(T_("Hash database compacted")));
}

void ExperimentalPage::setHashDbMaintenanceRunning(bool running, const tstring& status) {
	// Do not disable the clicked button while it owns focus. SettingsDialog's IsDialogMessage
	// loop can otherwise spin in WM_GETDLGCODE; the flag guards duplicate operations instead.
	hashDbMaintenanceRunning = running;
	hashDbStatus->setText(status.empty() ? _T(" ") : status);
}

function<void (bool, const string&)> ExperimentalPage::hashDbCompletion(
	const tstring& successMessage, const tstring& failureMessage)
{
	const auto pageHandle = handle();
	std::weak_ptr<int> callbackToken(hashDbCallbackToken);
	return [pageHandle, callbackToken, successMessage, failureMessage](bool success, const string& error) {
		dwt::Application::instance().callAsync([pageHandle, callbackToken, successMessage, failureMessage, success, error] {
			if(callbackToken.expired() || !::IsWindow(pageHandle)) return;
			auto page = dwt::hwnd_cast<ExperimentalPage*>(pageHandle);
			if(!page) return;

			page->setHashDbMaintenanceRunning(false);
			const auto failed = !error.empty() || !success;
			const auto message = !error.empty() ? Text::toT(error) :
				(success ? successMessage : failureMessage);
			dwt::MessageBox(page).show(message, _T(APPNAME) _T(" ") _T(VERSIONSTRING),
				dwt::MessageBox::BOX_OK, !error.empty() ? dwt::MessageBox::BOX_ICONSTOP :
					(failed ? dwt::MessageBox::BOX_ICONEXCLAMATION : dwt::MessageBox::BOX_ICONINFORMATION));
		});
	};
}
