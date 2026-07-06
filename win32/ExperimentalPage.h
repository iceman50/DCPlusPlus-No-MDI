/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H
#define DCPLUSPLUS_WIN32_EXPERIMENTAL_PAGE_H

#include "PropPage.h"

/** Runtime controls for defensive limits that would otherwise require a rebuild.
 * Defaults retain the original constexpr values; deliberately low values may reject
 * legitimate traffic and high values weaken resource-exhaustion protection. */
class ExperimentalPage : public PropPage
{
public:
	explicit ExperimentalPage(dwt::Widget* parent);
	virtual ~ExperimentalPage();

	virtual void write();

private:
	void addItem(const tstring& text, int setting, unsigned helpId, const tstring& unit);

	ItemList items;
};

#endif
