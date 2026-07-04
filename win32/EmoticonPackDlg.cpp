/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "EmoticonPackDlg.h"

#include <dcpp/EmoticonManager.h>
#include <dcpp/version.h>

#include <dwt/widgets/Button.h>
#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/LoadDialog.h>
#include <dwt/widgets/MessageBox.h>
#include <dwt/widgets/SaveDialog.h>

#include "WinUtil.h"

using dwt::Button;
using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using namespace dcpp;

EmoticonPackDlg::EmoticonPackDlg(dwt::Widget* parent, tstring initialImportPath_) :
	dwt::ModalDialog(parent), grid(nullptr), packageName(nullptr), packageVersion(nullptr),
	emoticonName(nullptr), shortcut(nullptr), iconPath(nullptr), rules(nullptr),
	updateButton(nullptr), removeButton(nullptr), initialImportPath(std::move(initialImportPath_)) {
	onInitDialog([this] { return handleInitDialog(); });
}

int EmoticonPackDlg::run() {
	create(Seed(dwt::Point(680, 500), DS_CONTEXTHELP));
	return show();
}

bool EmoticonPackDlg::handleInitDialog() {
	grid = addChild(Grid::Seed(5, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->row(3).mode = GridInfo::FILL;
	grid->row(3).align = GridInfo::STRETCH;

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Package information")));
		auto fields = group->addChild(Grid::Seed(2, 2));
		fields->column(1).mode = GridInfo::FILL;
		fields->addChild(Label::Seed(T_("Name")));
		packageName = fields->addChild(WinUtil::Seeds::Dialog::textBox);
		fields->addChild(Label::Seed(T_("Version")));
		packageVersion = fields->addChild(WinUtil::Seeds::Dialog::textBox);
		packageVersion->setText(Text::toT(VERSIONSTRING));
		packageVersion->setReadOnly();
	}

	{
		auto group = grid->addChild(GroupBox::Seed(T_("Emoticon rule")));
		auto fields = group->addChild(Grid::Seed(3, 3));
		fields->column(1).mode = GridInfo::FILL;
		fields->addChild(Label::Seed(T_("Emoticon name")));
		emoticonName = fields->addChild(WinUtil::Seeds::Dialog::textBox);
		fields->addChild(Label::Seed());
		fields->addChild(Label::Seed(T_("Shortcut")));
		shortcut = fields->addChild(WinUtil::Seeds::Dialog::textBox);
		fields->addChild(Label::Seed());
		fields->addChild(Label::Seed(T_("Image")));
		iconPath = fields->addChild(WinUtil::Seeds::Dialog::textBox);
		fields->addChild(Button::Seed(T_("Browse...")))->onClicked([this] { browseIcon(); });
	}

	{
		auto buttons = grid->addChild(Grid::Seed(1, 5));
		buttons->column(4).mode = GridInfo::FILL;
		buttons->addChild(Button::Seed(T_("&Add rule")))->onClicked([this] { addRule(); });
		updateButton = buttons->addChild(Button::Seed(T_("&Update selected")));
		updateButton->onClicked([this] { updateRule(); });
		removeButton = buttons->addChild(Button::Seed(T_("&Remove selected")));
		removeButton->onClicked([this] { removeRule(); });
		buttons->addChild(Button::Seed(T_("&Import XML...")))->onClicked([this] { importPackage(); });
		buttons->addChild(Label::Seed());
	}

	{
		Table::Seed seed = WinUtil::Seeds::Dialog::table;
		seed.style |= LVS_SINGLESEL;
		rules = grid->addChild(seed);
		rules->addColumn(T_("Name"), 130);
		rules->addColumn(T_("Shortcut"), 100);
		rules->addColumn(T_("Image file"), 390);
		rules->onSelectionChanged([this] { selectRule(); });
		rules->onDblClicked([this] { selectRule(); });
	}

	{
		auto buttons = grid->addChild(Grid::Seed(1, 2));
		buttons->column(0).mode = GridInfo::FILL;
		buttons->column(0).align = GridInfo::BOTTOM_RIGHT;
		buttons->addChild(Button::Seed(T_("&Export .dcemo...")))->onClicked([this] { exportPackage(); });
		buttons->addChild(Button::Seed(T_("Cancel")))->onClicked([this] { endDialog(IDCANCEL); });
	}

	setText(T_("Build emoticon package"));
	updateButtons();
	grid->resize(dwt::Rectangle(6, 6, getClientSize().x - 12, getClientSize().y - 12));
	centerWindow();
	if(!initialImportPath.empty()) importPackage(std::move(initialImportPath));
	return false;
}

void EmoticonPackDlg::addRule() {
	if(emoticonName->getText().empty() || shortcut->getText().empty() || iconPath->getText().empty()) {
		dwt::MessageBox(this).show(T_("Name, shortcut, and image are required."), T_("Emoticon package"),
			dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONEXCLAMATION);
		return;
	}
	const auto row = rules->insert({ emoticonName->getText(), shortcut->getText(), iconPath->getText() });
	rules->select(row);
}

void EmoticonPackDlg::updateRule() {
	const auto row = rules->getSelected();
	if(row < 0 || emoticonName->getText().empty() || shortcut->getText().empty() || iconPath->getText().empty()) return;
	rules->setText(row, 0, emoticonName->getText());
	rules->setText(row, 1, shortcut->getText());
	rules->setText(row, 2, iconPath->getText());
}

void EmoticonPackDlg::removeRule() {
	const auto row = rules->getSelected();
	if(row >= 0) rules->erase(row);
	updateButtons();
}

void EmoticonPackDlg::selectRule() {
	const auto row = rules->getSelected();
	if(row >= 0) {
		emoticonName->setText(rules->getText(row, 0));
		shortcut->setText(rules->getText(row, 1));
		iconPath->setText(rules->getText(row, 2));
	}
	updateButtons();
}

void EmoticonPackDlg::browseIcon() {
	auto path = iconPath->getText();
	if(dwt::LoadDialog(this).addFilter(T_("Emoticon images"), _T("*.bmp;*.ico;*.png"))
		.addFilter(T_("All files"), _T("*.*")).open(path)) iconPath->setText(path);
}

void EmoticonPackDlg::importPackage(tstring path) {
	if(path.empty() && !dwt::LoadDialog(this)
		.addFilter(T_("XML emoticon packages"), _T("*.xml"))
		.addFilter(T_("All files"), _T("*.*")).open(path)) return;

	try {
		const auto imported = EmoticonManager::importEmoticonPackage(Text::fromT(path));
		packageName->setText(Text::toT(imported.name));
		rules->clear();
		for(const auto& item: imported.items) {
			for(const auto& rule: item.rules) {
				rules->insert({ Text::toT(item.name), Text::toT(rule), Text::toT(item.iconPath) });
			}
		}
		if(rules->size()) rules->select(0);
		if(imported.skipped) {
			dwt::MessageBox(this).show(Text::toT(std::to_string(imported.skipped)) +
				T_(" unsupported, missing, or duplicate rules were skipped."), T_("XML emoticon import"),
				dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONINFORMATION);
		}
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(Text::toT(e.getError()), T_("Cannot import XML emoticons"),
			dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void EmoticonPackDlg::exportPackage() {
	vector<EmoticonManager::ExportItem> items;
	try {
		for(size_t row = 0; row < rules->size(); ++row) {
			const auto name = Text::fromT(rules->getText(static_cast<unsigned>(row), 0));
			const auto rule = Text::fromT(rules->getText(static_cast<unsigned>(row), 1));
			const auto image = Text::fromT(rules->getText(static_cast<unsigned>(row), 2));
			auto existing = std::find_if(items.begin(), items.end(), [&name](const auto& item) { return item.name == name; });
			if(existing == items.end()) {
				items.push_back({ name, { rule }, image });
			} else {
				if(existing->iconPath != image) throw Exception(_("Rules with the same emoticon name must use the same image"));
				existing->rules.push_back(rule);
			}
		}

		tstring target;
		if(!dwt::SaveDialog(this).addFilter(T_("DC++ emoticon packages"), _T("*.dcemo"))
			.setDefaultExtension(_T("dcemo")).open(target)) return;
		EmoticonManager::exportPackage(Text::fromT(target), Text::fromT(packageName->getText()), items);
		exportedPath = target;
		endDialog(IDOK);
	} catch(const Exception& e) {
		dwt::MessageBox(this).show(Text::toT(e.getError()), T_("Cannot export emoticon package"),
			dwt::MessageBox::BOX_OK, dwt::MessageBox::BOX_ICONSTOP);
	}
}

void EmoticonPackDlg::updateButtons() {
	const bool selected = rules && rules->hasSelected();
	if(updateButton) updateButton->setEnabled(selected);
	if(removeButton) removeButton->setEnabled(selected);
}
