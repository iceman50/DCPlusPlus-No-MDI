/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EMOTICONS_PAGE_H
#define DCPLUSPLUS_WIN32_EMOTICONS_PAGE_H

#include "PropPage.h"

/** Appearance sub-page for enabling, selecting, and creating .dcemo packages. */
class EmoticonsPage : public PropPage {
public:
	explicit EmoticonsPage(dwt::Widget* parent);
	void write() override;

private:
	ItemList items;
	ComboBoxPtr sizeBox;
	ComboBoxPtr bitDepthBox;
};

#endif
