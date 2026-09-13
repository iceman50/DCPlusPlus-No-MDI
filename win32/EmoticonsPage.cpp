/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "EmoticonsPage.h"

#include <dcpp/EmoticonManager.h>
#include <dcpp/File.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/format.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/CheckBox.h>
#include <dwt/widgets/ComboBox.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/MessageBox.h>

#include "EmoticonPackDlg.h"
#include "Emoticons.h"
#include "RichTextBox.h"
#include "WinUtil.h"
#include "resource.h"

using dwt::Button;
using dwt::CheckBox;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using namespace dcpp;

EmoticonsPage::EmoticonsPage(dwt::Widget* parent) : PropPage(parent, 4, 1), packageBox(nullptr), sizeBox(nullptr),
	bitDepthBox(nullptr), preview(nullptr), previewStatus(nullptr) {
	setHelpId(IDH_APPEARANCEPAGE);
	grid->column(0).mode = GridInfo::FILL;
	grid->row(2).mode = GridInfo::FILL;
	grid->row(2).align = GridInfo::STRETCH;

	auto enabled = grid->addChild(CheckBox::Seed(T_("Enable emoticons in chat")));
	items.emplace_back(enabled, SettingsManager::ENABLE_EMOTICONS, PropPage::T_BOOL);

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Emoticon package")));
		auto content = group->addChild(Grid::Seed(3, 1));
		content->column(0).mode = GridInfo::FILL;
		auto row = content->addChild(Grid::Seed(1, 6));
		row->column(0).mode = GridInfo::FILL;

		packageBox = row->addChild(WinUtil::Seeds::Dialog::comboBox);
		packageBox->onSelectionChanged([this] { updatePreview(); });
		row->addChild(Button::Seed(T_("Browse...")))->onClicked([this] {
			const auto current = selectedPackagePath();
			auto path = Text::toT(current.empty() ? EmoticonManager::getDirectory() : Util::getFilePath(current));
			if(dwt::LoadDialog(this).addFilter(T_("DC++ emoticon packages"), _T("*.dcemo"))
				.addFilter(T_("All files"), _T("*.*")).open(path)) {
				selectPackage(Text::fromT(path));
				updatePreview();
			}
		});
		row->addChild(Button::Seed(T_("Build...")))->onClicked([this] {
			EmoticonPackDlg dialog(this);
			if(dialog.run() == IDOK) {
				selectPackage(Text::fromT(dialog.getExportedPath()));
				updatePreview();
			}
		});
		row->addChild(Button::Seed(T_("Import XML...")))->onClicked([this] {
			tstring source;
			if(!dwt::LoadDialog(this).addFilter(T_("XML emoticon packages"), _T("*.xml"))
				.addFilter(T_("All files"), _T("*.*")).open(source)) return;
			EmoticonPackDlg dialog(this, std::move(source));
			if(dialog.run() == IDOK) {
				selectPackage(Text::fromT(dialog.getExportedPath()));
				updatePreview();
			}
		});
		row->addChild(Button::Seed(T_("Reload")))->onClicked([this] {
			reloadPackages(selectedPackagePath());
			updatePreview();
		});
		row->addChild(Button::Seed(T_("Preview")))->onClicked([this] { updatePreview(); });

		auto sizeRow = content->addChild(Grid::Seed(1, 2));
		sizeRow->column(1).mode = GridInfo::FILL;
		sizeRow->addChild(Label::Seed(T_("Display size")));
		sizeBox = sizeRow->addChild(WinUtil::Seeds::Dialog::comboBox);
		static const int sizes[] = { 20, 24, 28, 32 };
		int selected = 1;
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

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Loaded emoticons")));
		auto content = group->addChild(Grid::Seed(2, 1));
		content->column(0).mode = GridInfo::FILL;
		content->row(0).mode = GridInfo::FILL;
		content->row(0).align = GridInfo::STRETCH;
		RichTextBox::Seed seed = WinUtil::Seeds::Dialog::richTextBox;
		seed.style |= ES_READONLY | WS_VSCROLL;
		preview = dwt::WidgetCreator<RichTextBox>::create(content, seed);
		WinUtil::setColor(preview);
		previewStatus = content->addChild(Label::Seed());
	}

	grid->addChild(Label::Seed(T_("Packages are .dcemo ZIP files containing XML shortcut rules and BMP, ICO, or PNG images.")));
	PropPage::read(items);
	reloadPackages(SETTING(EMOTICON_PACK));
	updatePreview();
}

std::string EmoticonsPage::selectedPackagePath() const {
	const auto selected = packageBox ? packageBox->getSelected() : 0;
	return selected > 0 && static_cast<size_t>(selected) <= packages.size() ? packages[selected - 1].path : std::string();
}

void EmoticonsPage::selectPackage(const std::string& path) {
	if(path.empty()) {
		packageBox->setSelected(0);
		return;
	}
	for(size_t index = 0; index < packages.size(); ++index) {
		if(Util::stricmp(packages[index].path, path) == 0) {
			packageBox->setSelected(static_cast<int>(index + 1));
			return;
		}
	}
	if(File::getSize(path) < 0) {
		for(size_t index = 0; index < packages.size(); ++index) {
			if(Util::stricmp(Util::getFileName(packages[index].path), Util::getFileName(path)) == 0) {
				packageBox->setSelected(static_cast<int>(index + 1));
				return;
			}
		}
	}
	try {
		packages.push_back(EmoticonManager::inspectPackage(path));
		packageBox->addValue(Text::toT(packages.back().name) + T_(" (external DCEMO)"));
		packageBox->setSelected(static_cast<int>(packages.size()));
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(T_("Unable to load the emoticon package:\r\n") + Text::toT(e.getError()),
			T_("Emoticon package"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void EmoticonsPage::reloadPackages(const std::string& selectedPath) {
	packages = EmoticonManager::getPackages();
	packageBox->clear();
	packageBox->addValue(T_("(No package)"));
	for(const auto& package: packages) packageBox->addValue(Text::toT(package.name) + T_(" (DCEMO)"));
	packageBox->setSelected(0);
	selectPackage(selectedPath);
}

void EmoticonsPage::updatePreview() {
	preview->setText(Util::emptyStringT);
	const auto path = selectedPackagePath();
	if(path.empty()) {
		previewStatus->setText(T_("No emoticon package selected."));
		return;
	}

	try {
		const auto package = EmoticonManager::previewPackage(path);
		constexpr size_t maxPreviewItems = 128;
		const auto previewItems = std::min(package.items.size(), maxPreviewItems);
		std::vector<tstring> documents;
		documents.reserve(previewItems);
		size_t ruleCount = 0;
		for(const auto& item: package.items) ruleCount += item.rules.size();
		size_t failedImages = 0;
		for(size_t index = 0; index < previewItems; ++index) {
			const auto& item = package.items[index];
			tstring rules;
			for(const auto& rule: item.rules) {
				if(!rules.empty()) rules += _T("   ");
				rules += Text::toT(rule);
			}
			tstring document = _T("{\\urtf1\\viewkind4\\uc1 ");
			const auto image = Emoticons::fileRtf(item.iconPath, 40);
			if(image.empty()) {
				document += dwt::RichTextBox::rtfEscape(T_("[image unavailable]"));
				++failedImages;
			} else {
				document += image;
			}
			document += _T("\\tab\\b ") + dwt::RichTextBox::rtfEscape(Text::toT(item.name));
			document += _T("\\b0\\tab ") + dwt::RichTextBox::rtfEscape(rules);
			if(index + 1 < previewItems) document += _T("\\line ");
			document += _T("}");
			documents.push_back(std::move(document));
		}
		preview->addTextSteadyBatch(documents);
		auto status = str(TF_("%1% version %2%: %3% emoticons, %4% shortcuts") %
			Text::toT(package.name) % Text::toT(package.version) % package.items.size() % ruleCount);
		if(previewItems < package.items.size()) status += str(TF_("; showing the first %1%") % previewItems);
		if(failedImages) status += str(TF_("; %1% images unavailable") % failedImages);
		previewStatus->setText(status);
	} catch(const Exception& e) {
		previewStatus->setText(T_("Unable to preview package: ") + Text::toT(e.getError()));
	}
}

void EmoticonsPage::write() {
	PropPage::write(items);
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_PACK, selectedPackagePath());
	static const int sizes[] = { 20, 24, 28, 32 };
	const auto selected = sizeBox ? sizeBox->getSelected() : 1;
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_SIZE,
		sizes[selected >= 0 && selected < static_cast<int>(std::size(sizes)) ? selected : 1]);
	static const int bitDepths[] = { 16, 24, 32 };
	const auto selectedDepth = bitDepthBox ? bitDepthBox->getSelected() : 0;
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_BIT_DEPTH,
		bitDepths[selectedDepth >= 0 && selectedDepth < static_cast<int>(std::size(bitDepths)) ? selectedDepth : 0]);
	EmoticonManager::reload();
}
