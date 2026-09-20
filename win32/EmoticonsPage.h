/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EMOTICONS_PAGE_H
#define DCPLUSPLUS_WIN32_EMOTICONS_PAGE_H

#include <dcpp/EmoticonManager.h>

#include "IconManager.h"
#include "PropPage.h"

/** Appearance sub-page for selecting and editing .dcemo and .dcico packages. */
class EmoticonsPage : public PropPage {
public:
	explicit EmoticonsPage(dwt::Widget* parent);
	void write() override;

private:
	void updatePreview();
	void reloadPackages(const std::string& selectedPath = std::string());
	void selectPackage(const std::string& path);
	void handlePackageSelection();
	void editSelectedPackage();
	std::string selectedPackagePath() const;
	void updateIconPackageStatus();
	void selectIconPackage(const std::string& path);
	std::string selectedIconPackagePath() const;

	ItemList items;
	ComboBoxPtr packageBox;
	std::vector<dcpp::EmoticonManager::Package> packages;
	std::string retainedPackagePath;
	bool changingPackageSelection;
	ComboBoxPtr sizeBox;
	ComboBoxPtr bitDepthBox;
	RichTextBoxPtr preview;
	LabelPtr previewStatus;
	ComboBoxPtr iconPackageBox;
	std::vector<IconManager::Package> iconPackages;
	LabelPtr iconPackageStatus;
};

#endif
