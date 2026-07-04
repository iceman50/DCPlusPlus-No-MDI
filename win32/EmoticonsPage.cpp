/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "EmoticonsPage.h"

#include <dcpp/EmoticonManager.h>
#include <dcpp/SettingsManager.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>

#include "EmoticonPackDlg.h"
#include "WinUtil.h"
#include "resource.h"

using dwt::Button;
using dwt::CheckBox;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using namespace dcpp;

EmoticonsPage::EmoticonsPage(dwt::Widget* parent) : PropPage(parent, 3, 1), sizeBox(nullptr), bitDepthBox(nullptr) {
	setHelpId(IDH_APPEARANCEPAGE);
	grid->column(0).mode = GridInfo::FILL;

	auto enabled = grid->addChild(CheckBox::Seed(T_("Enable emoticons in chat")));
	items.emplace_back(enabled, SettingsManager::ENABLE_EMOTICONS, PropPage::T_BOOL);

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Emoticon package")));
		auto content = group->addChild(Grid::Seed(3, 1));
		content->column(0).mode = GridInfo::FILL;
		auto row = content->addChild(Grid::Seed(1, 4));
		row->column(0).mode = GridInfo::FILL;

		auto box = row->addChild(WinUtil::Seeds::Dialog::textBox);
		items.emplace_back(box, SettingsManager::EMOTICON_PACK, PropPage::T_STR);
		row->addChild(Button::Seed(T_("Browse...")))->onClicked([this, box] {
			auto path = box->getText();
			if(dwt::LoadDialog(this).addFilter(T_("DC++ emoticon packages"), _T("*.dcemo"))
				.addFilter(T_("All files"), _T("*.*")).open(path)) box->setText(path);
		});
		row->addChild(Button::Seed(T_("Build...")))->onClicked([this, box] {
			EmoticonPackDlg dialog(this);
			if(dialog.run() == IDOK) box->setText(dialog.getExportedPath());
		});
		row->addChild(Button::Seed(T_("Import XML...")))->onClicked([this, box] {
			tstring source;
			if(!dwt::LoadDialog(this).addFilter(T_("XML emoticon packages"), _T("*.xml"))
				.addFilter(T_("All files"), _T("*.*")).open(source)) return;
			EmoticonPackDlg dialog(this, std::move(source));
			if(dialog.run() == IDOK) box->setText(dialog.getExportedPath());
		});

		auto sizeRow = content->addChild(Grid::Seed(1, 2));
		sizeRow->column(1).mode = GridInfo::FILL;
		sizeRow->addChild(Label::Seed(T_("Display size")));
		sizeBox = sizeRow->addChild(WinUtil::Seeds::Dialog::comboBox);
		static const int sizes[] = { 16, 20, 22, 24 };
		int selected = 0;
		for(size_t i = 0; i < std::size(sizes); ++i) {
			sizeBox->addValue(Text::toT(std::to_string(sizes[i]) + " x " + std::to_string(sizes[i])));
			if(SETTING(EMOTICON_SIZE) == sizes[i]) selected = static_cast<int>(i);
		}
		sizeBox->setSelected(selected);

		auto depthRow = content->addChild(Grid::Seed(1, 2));
		depthRow->column(1).mode = GridInfo::FILL;
		depthRow->addChild(Label::Seed(T_("Preferred icon bit depth")));
		bitDepthBox = depthRow->addChild(WinUtil::Seeds::Dialog::comboBox);
		static const int bitDepths[] = { 16, 24, 32 };
		selected = 0;
		for(size_t i = 0; i < std::size(bitDepths); ++i) {
			bitDepthBox->addValue(Text::toT(std::to_string(bitDepths[i]) + " bpp"));
			if(SETTING(EMOTICON_BIT_DEPTH) == bitDepths[i]) selected = static_cast<int>(i);
		}
		bitDepthBox->setSelected(selected);
	}

	grid->addChild(Label::Seed(T_("Packages are .dcemo ZIP files containing XML shortcut rules and BMP, ICO, or PNG images.")));
	PropPage::read(items);
}

void EmoticonsPage::write() {
	PropPage::write(items);
	static const int sizes[] = { 16, 20, 22, 24 };
	const auto selected = sizeBox ? sizeBox->getSelected() : 0;
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_SIZE,
		sizes[selected >= 0 && selected < static_cast<int>(std::size(sizes)) ? selected : 0]);
	static const int bitDepths[] = { 16, 24, 32 };
	const auto selectedDepth = bitDepthBox ? bitDepthBox->getSelected() : 0;
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_BIT_DEPTH,
		bitDepths[selectedDepth >= 0 && selectedDepth < static_cast<int>(std::size(bitDepths)) ? selectedDepth : 0]);
	EmoticonManager::reload();
}
