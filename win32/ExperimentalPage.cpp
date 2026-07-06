/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "ExperimentalPage.h"

#include <dcpp/SettingsManager.h>

#include <dwt/widgets/Grid.h>
#include <dwt/widgets/GroupBox.h>
#include <dwt/widgets/Label.h>

#include "resource.h"
#include "WinUtil.h"

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;

ExperimentalPage::ExperimentalPage(dwt::Widget* parent) : PropPage(parent, 6, 2) {
	setHelpId(IDH_EXPERIMENTALPAGE);

	grid->column(0).mode = GridInfo::FILL;
	grid->column(1).mode = GridInfo::FILL;

	addItem(T_("Queued protocol data limit"), SettingsManager::MAX_QUEUED_PROTOCOL_DATA,
		IDH_SETTINGS_EXPERIMENTAL_MAX_QUEUED_PROTOCOL_DATA, T_("bytes"));
	addItem(T_("Concurrent peer connection limit"), SettingsManager::MAX_CONCURRENT_CONNECTIONS,
		IDH_SETTINGS_EXPERIMENTAL_MAX_CONCURRENT_CONNECTIONS, T_("connections"));
	addItem(T_("Incoming connection flood window"), SettingsManager::FLOOD_WINDOW,
		IDH_SETTINGS_EXPERIMENTAL_FLOOD_WINDOW, T_("milliseconds"));
	addItem(T_("Global UDP rate window"), SettingsManager::GLOBAL_WINDOW,
		IDH_SETTINGS_EXPERIMENTAL_GLOBAL_WINDOW, T_("milliseconds"));
	addItem(T_("Global UDP packet limit"), SettingsManager::GLOBAL_LIMIT,
		IDH_SETTINGS_EXPERIMENTAL_GLOBAL_LIMIT, T_("packets per window"));
	addItem(T_("Per-peer UDP packet limit"), SettingsManager::PEER_LIMIT,
		IDH_SETTINGS_EXPERIMENTAL_PEER_LIMIT, T_("packets per window"));
	addItem(T_("Per-peer UDP rate window"), SettingsManager::PEER_WINDOW,
		IDH_SETTINGS_EXPERIMENTAL_PEER_WINDOW, T_("milliseconds"));
	addItem(T_("Tracked UDP peer limit"), SettingsManager::MAX_TRACKED_PEERS,
		IDH_SETTINGS_EXPERIMENTAL_MAX_TRACKED_PEERS, T_("peers"));
	addItem(T_("Encrypted UDP packet limit"), SettingsManager::MAX_SUDP_PACKET,
		IDH_SETTINGS_EXPERIMENTAL_MAX_SUDP_PACKET, T_("bytes"));
	addItem(T_("Stored encrypted UDP key limit"), SettingsManager::MAX_SUDP_KEYS,
		IDH_SETTINGS_EXPERIMENTAL_MAX_SUDP_KEYS, T_("keys"));
	addItem(T_("Generated partial file list limit"), SettingsManager::MAX_PARTIAL_LIST_BYTES,
		IDH_SETTINGS_EXPERIMENTAL_MAX_PARTIAL_LIST_BYTES, T_("bytes"));

	PropPage::read(items);
}

ExperimentalPage::~ExperimentalPage() {
}

void ExperimentalPage::write() {
	PropPage::write(items);

	// Defend against hand-edited and UI-entered values that would disable a limit or
	// violate the minimum packet/header sizes assumed by the protocol implementations.
	auto settings = SettingsManager::getInstance();
	auto clamp = [settings](SettingsManager::IntSetting setting, int minimum) {
		if(settings->get(setting) < minimum) {
			settings->set(setting, minimum);
		}
	};
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
}

void ExperimentalPage::addItem(const tstring& text, int setting, unsigned helpId, const tstring& unit) {
	auto group = grid->addChild(GroupBox::Seed(text));
	group->setHelpId(helpId);

	auto row = group->addChild(Grid::Seed(1, 2));
	row->column(0).mode = GridInfo::FILL;
	auto box = row->addChild(WinUtil::Seeds::Dialog::intTextBox);
	// PropPage normally leaves default-valued integer settings blank. These limits
	// are easier and safer to tune when their effective values are always visible.
	box->setText(Text::toT(std::to_string(SettingsManager::getInstance()->get(
		static_cast<SettingsManager::IntSetting>(setting)))));
	items.emplace_back(box, setting, PropPage::T_INT);
	row->addChild(Label::Seed(unit));
}
