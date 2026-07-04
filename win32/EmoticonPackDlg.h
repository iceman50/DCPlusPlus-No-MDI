/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_EMOTICON_PACK_DLG_H
#define DCPLUSPLUS_WIN32_EMOTICON_PACK_DLG_H

#include <dcpp/typedefs.h>
#include <dwt/widgets/ModalDialog.h>

#include "forward.h"

/** Interactive .dcemo package builder used from the Appearance settings page. */
class EmoticonPackDlg : public dwt::ModalDialog {
public:
	explicit EmoticonPackDlg(dwt::Widget* parent, dcpp::tstring initialImportPath = dcpp::tstring());
	int run();
	const dcpp::tstring& getExportedPath() const { return exportedPath; }

private:
	bool handleInitDialog();
	void addRule();
	void updateRule();
	void removeRule();
	void selectRule();
	void browseIcon();
	void importPackage(dcpp::tstring path = dcpp::tstring());
	void exportPackage();
	void updateButtons();

	GridPtr grid;
	TextBoxPtr packageName;
	TextBoxPtr packageVersion;
	TextBoxPtr emoticonName;
	TextBoxPtr shortcut;
	TextBoxPtr iconPath;
	TablePtr rules;
	ButtonPtr updateButton;
	ButtonPtr removeButton;
	dcpp::tstring initialImportPath;
	dcpp::tstring exportedPath;
};

#endif
