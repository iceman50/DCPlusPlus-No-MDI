/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H
#define DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H

#include <memory>
#include <vector>

#include "PropPage.h"
#include "ThemeManager.h"

/** Experimental sharing, hashing, MCN, RTF0 and defensive protocol controls. */
class ExperimentalPage : public PropPage
{
public:
	explicit ExperimentalPage(dwt::Widget* parent);
	virtual ~ExperimentalPage();

	virtual void layout();
	virtual void write();

private:
	struct ScaledIntItem {
		TextBoxPtr box;
		int setting;
		int multiplier;
	};
	struct ThemeColorItem {
		LabelPtr preview;
		int setting;
		COLORREF color;
	};
	struct TempShareRow {
		string path;
		string route;
		string tth;
	};

	ItemList items;
	std::vector<ScaledIntItem> scaledIntItems;
	std::vector<ThemeColorItem> themeColors;
	std::vector<ThemeManager::Theme> themePresets;
	ComboBoxPtr themeMode;
	ComboBoxPtr themePreset;
	TablePtr tempShares;
	std::vector<std::unique_ptr<TempShareRow>> tempShareRows;
	LabelPtr tempSummary;
	ButtonPtr removeTemp;
	ButtonPtr clearTemps;
	LabelPtr hashDbStatus;
	std::shared_ptr<int> hashDbCallbackToken;
	bool hashDbMaintenanceRunning;

	void layoutTempShares();
	void addIntItem(GridPtr target, const tstring& text, int setting, unsigned helpId,
		const tstring& unit, int minimum, int maximum, int multiplier = 1);
	void readScaledIntItems();
	void writeScaledIntItems();
	void addThemeColor(GridPtr target, const tstring& text, int setting);
	void chooseThemeColor(size_t index);
	void resetThemeColors();
	void updateThemeColor(size_t index);
	void reloadThemePresets(const std::string& selectedPath = std::string());
	void applyThemePreset();
	void applyThemePalette(const dwt::Appearance::Palette& palette);
	dwt::Appearance::Palette currentThemePalette() const;
	void updateThemePresetSelection();
	void saveCustomTheme();
	void importTheme();
	void openThemeDirectory();
	void fillTempShares();
	TempShareRow* getTempShareRow(int row);
	void handleTempSelectionChanged();
	bool handleTempContextMenu(dwt::ScreenCoordinate pt);
	void handleCopyTempMagnet();
	void handleRemoveTemps();
	void handleClearTemps();
	void handleVerifyHashDb(bool fullCheck);
	void handleOptimizeHashDb();
	void handleCompactHashDb();
	void setHashDbMaintenanceRunning(bool running, const tstring& status = tstring());
	function<void (bool, const string&)> hashDbCompletion(
		const tstring& successMessage, const tstring& failureMessage = tstring());
};

#endif // !defined(DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H)
