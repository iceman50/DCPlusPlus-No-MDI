/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H
#define DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H

#include <memory>

#include "PropPage.h"

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

	ItemList items;
	std::vector<ScaledIntItem> scaledIntItems;
	TablePtr tempShares;
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
	void fillTempShares();
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
