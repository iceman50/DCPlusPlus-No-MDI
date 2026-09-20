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
#include <dwt/widgets/TabView.h>

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

namespace {

/** A settings tab page that sizes itself from its content grid. */
class IconsTabPage : public dwt::Container {
public:
	IconsTabPage(dwt::Widget* parent, const tstring& title, size_t rows) : dwt::Container(parent), grid(nullptr) {
		dwt::Container::Seed seed;
		seed.caption = title;
		seed.style &= ~WS_VISIBLE;
		seed.exStyle |= WS_EX_CONTROLPARENT;
		create(seed);
		setHelpId(IDH_APPEARANCEPAGE);
		grid = addChild(Grid::Seed(rows, 1));
		grid->setSpacing(10);
		grid->column(0).mode = GridInfo::FILL;
	}

	GridPtr content() const { return grid; }
	dwt::Point getPreferredSize() override { return grid->getPreferredSize() + dwt::Point(14, 12); }
	void layout() override {
		const auto size = getClientSize();
		grid->resize(dwt::Rectangle(7, 4, std::max<LONG>(0, size.x - 14), std::max<LONG>(0, size.y - 12)));
	}

private:
	GridPtr grid;
};

/** Gives settings tabs a preferred size while retaining TabView behavior. */
class IconsTabView : public dwt::TabView {
public:
	typedef IconsTabView* ObjectType;
	typedef dwt::TabView::Seed Seed;
	explicit IconsTabView(dwt::Widget* parent) : dwt::TabView(parent) { }

	IconsTabPage* addPage(const tstring& title, size_t rows) {
		auto page = new IconsTabPage(this, title, rows);
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
		LONG tabsRight = 0;
		for(size_t index = 0; index < size(); ++index) {
			RECT rect = { 0 };
			if(TabCtrl_GetItemRect(handle(), static_cast<int>(index), &rect)) tabsRight = std::max(tabsRight, rect.right);
		}
		contentSize.x = std::max(contentSize.x, tabsRight);
		RECT rect = { 0, 0, contentSize.x, contentSize.y };
		TabCtrl_AdjustRect(handle(), TRUE, &rect);
		return dwt::Point(rect.right - rect.left, rect.bottom - rect.top);
	}

private:
	std::vector<IconsTabPage*> pages;
};

}

EmoticonsPage::EmoticonsPage(dwt::Widget* parent) : PropPage(parent, 1, 1), packageBox(nullptr),
	changingPackageSelection(false), sizeBox(nullptr), bitDepthBox(nullptr), preview(nullptr),
	previewStatus(nullptr), iconPackageBox(nullptr), iconPackageStatus(nullptr) {
	setHelpId(IDH_APPEARANCEPAGE);
	grid->column(0).mode = GridInfo::FILL;
	grid->row(0).mode = GridInfo::FILL;
	grid->row(0).align = GridInfo::STRETCH;

	auto tabSeed = WinUtil::Seeds::tabs;
	tabSeed.style &= ~(TCS_OWNERDRAWFIXED | TCS_MULTILINE | TCS_RAGGEDRIGHT | TCS_TOOLTIPS);
	tabSeed.exStyle |= WS_EX_CONTROLPARENT;
	tabSeed.widthConfig = 0;
	tabSeed.closeable = false;
	auto tabs = dwt::WidgetCreator<IconsTabView>::create(grid, tabSeed);
	grid->setWidget(tabs, 0, 0);

	auto emoticonGrid = tabs->addPage(T_("Emoticons"), 4)->content();
	emoticonGrid->row(2).mode = GridInfo::FILL;
	emoticonGrid->row(2).align = GridInfo::STRETCH;
	auto enabled = emoticonGrid->addChild(CheckBox::Seed(T_("Enable emoticons in chat")));
	items.emplace_back(enabled, SettingsManager::ENABLE_EMOTICONS, PropPage::T_BOOL);

	{
		auto group = emoticonGrid->addChild(GroupBox::Seed(T_("Emoticon package")));
		auto content = group->addChild(Grid::Seed(3, 1));
		content->column(0).mode = GridInfo::FILL;
		auto row = content->addChild(Grid::Seed(1, 5));
		row->column(0).mode = GridInfo::FILL;

		packageBox = row->addChild(WinUtil::Seeds::Dialog::comboBox);
		packageBox->onSelectionChanged([this] { handlePackageSelection(); });
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
			EmoticonPackDlg dialog(this, tstring(), Text::toT(EmoticonManager::getDirectory() + "Custom.dcemo"));
			if(dialog.run() == IDOK) {
				reloadPackages(Text::fromT(dialog.getExportedPath()));
				updatePreview();
			}
		});
		row->addChild(Button::Seed(T_("Import XML...")))->onClicked([this] {
			tstring source;
			if(!dwt::LoadDialog(this).addFilter(T_("XML emoticon packages"), _T("*.xml"))
				.addFilter(T_("All files"), _T("*.*")).open(source)) return;
			EmoticonPackDlg dialog(this, std::move(source));
			if(dialog.run() == IDOK) {
				reloadPackages(Text::fromT(dialog.getExportedPath()));
				updatePreview();
			}
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
		depthRow->addChild(Label::Seed(T_("Preferred emoticon bit depth")));
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
		auto group = emoticonGrid->addChild(GroupBox::Seed(T_("Loaded emoticons")));
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

	emoticonGrid->addChild(Label::Seed(T_("Choose Custom / edit selected from the package list to add, update, or remove emoticons.")));

	auto iconGrid = tabs->addPage(T_("Application icons"), 3)->content();
	{
		auto group = iconGrid->addChild(GroupBox::Seed(T_("Icon package")));
		auto content = group->addChild(Grid::Seed(2, 1));
		content->column(0).mode = GridInfo::FILL;
		auto row = content->addChild(Grid::Seed(1, 2));
		row->column(0).mode = GridInfo::FILL;
		iconPackageBox = row->addChild(WinUtil::Seeds::Dialog::comboBox);
		iconPackageBox->onSelectionChanged([this] { updateIconPackageStatus(); });
		row->addChild(Button::Seed(T_("Browse...")))->onClicked([this] {
			const auto selected = selectedIconPackagePath();
			auto path = Text::toT(selected.empty() || selected == IconManager::EMBEDDED_PACK ? IconManager::getDirectory() : Util::getFilePath(selected));
			if(dwt::LoadDialog(this).addFilter(T_("DC++ icon packages"), _T("*.dcico"))
				.addFilter(T_("All files"), _T("*.*")).open(path)) selectIconPackage(Text::fromT(path));
		});
		iconPackageStatus = content->addChild(Label::Seed());
	}
	iconGrid->addChild(Label::Seed(T_("Automatic uses Dark.dcico for dark appearance and compiled icons otherwise.")));
	iconGrid->addChild(Label::Seed(T_("Packages are .dcico ZIP files containing info.xml and multi-resolution ICO files.\r\nRestart DC++ to refresh every existing control.")));

	PropPage::read(items);
	reloadPackages(SETTING(EMOTICON_PACK));
	updatePreview();
	iconPackages = IconManager::getPackages();
	iconPackageBox->addValue(T_("Automatic (appearance-aware)"));
	iconPackageBox->addValue(T_("Embedded icons (EXE)"));
	for(const auto& package: iconPackages) iconPackageBox->addValue(Text::toT(package.name) + T_(" (DCICO)"));
	selectIconPackage(SETTING(ICON_PACK));
}

std::string EmoticonsPage::selectedPackagePath() const {
	const auto selected = packageBox ? packageBox->getSelected() : 0;
	return selected > 0 && static_cast<size_t>(selected) <= packages.size() ? packages[selected - 1].path : std::string();
}

void EmoticonsPage::selectPackage(const std::string& path) {
	changingPackageSelection = true;
	if(path.empty()) {
		packageBox->setSelected(0);
		retainedPackagePath.clear();
		changingPackageSelection = false;
		return;
	}
	for(size_t index = 0; index < packages.size(); ++index) {
		if(Util::stricmp(packages[index].path, path) == 0) {
			packageBox->setSelected(static_cast<int>(index + 1));
			retainedPackagePath = packages[index].path;
			changingPackageSelection = false;
			return;
		}
	}
	if(File::getSize(path) < 0) {
		for(size_t index = 0; index < packages.size(); ++index) {
			if(Util::stricmp(Util::getFileName(packages[index].path), Util::getFileName(path)) == 0) {
				packageBox->setSelected(static_cast<int>(index + 1));
				retainedPackagePath = packages[index].path;
				changingPackageSelection = false;
				return;
			}
		}
	}
	try {
		packages.push_back(EmoticonManager::inspectPackage(path));
		packageBox->insertValue(static_cast<int>(packages.size()), Text::toT(packages.back().name) + T_(" (external DCEMO)"));
		packageBox->setSelected(static_cast<int>(packages.size()));
		retainedPackagePath = packages.back().path;
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(T_("Unable to load the emoticon package:\r\n") + Text::toT(e.getError()),
			T_("Emoticon package"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
	changingPackageSelection = false;
}

void EmoticonsPage::reloadPackages(const std::string& selectedPath) {
	packages = EmoticonManager::getPackages();
	packageBox->clear();
	packageBox->addValue(T_("(No package)"));
	for(const auto& package: packages) packageBox->addValue(Text::toT(package.name) + T_(" (DCEMO)"));
	packageBox->addValue(T_("Custom / edit selected..."));
	selectPackage(selectedPath);
}

void EmoticonsPage::handlePackageSelection() {
	if(changingPackageSelection) return;
	if(packageBox->getSelected() == static_cast<int>(packages.size() + 1)) {
		editSelectedPackage();
		return;
	}
	retainedPackagePath = selectedPackagePath();
	updatePreview();
}

void EmoticonsPage::editSelectedPackage() {
	const auto source = retainedPackagePath;
	changingPackageSelection = true;
	selectPackage(source);
	changingPackageSelection = false;
	const auto fileName = source.empty() ? string("Custom.dcemo") : Util::getFileName(source);
	const auto target = EmoticonManager::getDirectory() + fileName;
	EmoticonPackDlg dialog(this, Text::toT(source), Text::toT(target));
	if(dialog.run() == IDOK) {
		reloadPackages(Text::fromT(dialog.getExportedPath()));
		updatePreview();
	}
}

std::string EmoticonsPage::selectedIconPackagePath() const {
	const auto selected = iconPackageBox ? iconPackageBox->getSelected() : 0;
	if(selected == 1) return IconManager::EMBEDDED_PACK;
	return selected >= 2 && static_cast<size_t>(selected - 2) < iconPackages.size() ? iconPackages[selected - 2].path : string();
}

void EmoticonsPage::selectIconPackage(const std::string& path) {
	if(path.empty() || path == IconManager::EMBEDDED_PACK) {
		iconPackageBox->setSelected(path.empty() ? 0 : 1);
		updateIconPackageStatus();
		return;
	}
	for(size_t index = 0; index < iconPackages.size(); ++index) {
		if(Util::stricmp(iconPackages[index].path, path) == 0 ||
			(File::getSize(path) < 0 && Util::stricmp(Util::getFileName(iconPackages[index].path), Util::getFileName(path)) == 0)) {
			iconPackageBox->setSelected(static_cast<int>(index + 2));
			updateIconPackageStatus();
			return;
		}
	}
	try {
		iconPackages.push_back(IconManager::inspectPackage(path));
		iconPackageBox->addValue(Text::toT(iconPackages.back().name) + T_(" (external DCICO)"));
		iconPackageBox->setSelected(static_cast<int>(iconPackages.size() + 1));
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(T_("Unable to load the icon package:\r\n") + Text::toT(e.getError()),
			T_("Icon package"), dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
		iconPackageBox->setSelected(0);
	}
	updateIconPackageStatus();
}

void EmoticonsPage::updateIconPackageStatus() {
	if(!iconPackageStatus) return;
	const auto selected = iconPackageBox ? iconPackageBox->getSelected() : 0;
	if(selected == 1) {
		iconPackageStatus->setText(T_("Use the icons embedded in DCPlusPlus.exe in both light and dark appearance."));
		return;
	}
	if(selected < 2 || static_cast<size_t>(selected - 2) >= iconPackages.size()) {
		iconPackageStatus->setText(T_("Automatic selection is enabled."));
		return;
	}
	const auto& package = iconPackages[selected - 2];
	iconPackageStatus->setText(str(TF_("%1% version %2%: %3% application icons; scheme %4%") %
		Text::toT(package.name) % Text::toT(package.version) % package.iconCount % Text::toT(package.scheme)));
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
	SettingsManager::getInstance()->set(SettingsManager::ICON_PACK, selectedIconPackagePath());
	static const int sizes[] = { 20, 24, 28, 32 };
	const auto selected = sizeBox ? sizeBox->getSelected() : 1;
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_SIZE,
						sizes[selected >= 0 && selected < static_cast<int>(std::size(sizes)) ? selected : 1]);
	static const int bitDepths[] = { 16, 24, 32 };
	const auto selectedDepth = bitDepthBox ? bitDepthBox->getSelected() : 0;
	SettingsManager::getInstance()->set(SettingsManager::EMOTICON_BIT_DEPTH,
						bitDepths[selectedDepth >= 0 && selectedDepth < static_cast<int>(std::size(bitDepths)) ? selectedDepth : 0]);
	EmoticonManager::reload();
	IconManager::reload();
}
