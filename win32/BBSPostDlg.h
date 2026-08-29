/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_BBS_POST_DLG_H
#define DCPLUSPLUS_WIN32_BBS_POST_DLG_H

#include <dcpp/typedefs.h>

#include "GridDialog.h"

class BBSPostDlg : public GridDialog {
public:
	BBSPostDlg(dwt::Widget* parent, const string& hubUrl, const string& board, const string& parentTTH, const tstring& initialSubject, int64_t maxSize);

	const string& getSubject() const { return resultSubject; }
	const string& getBody() const { return resultBody; }
	bool getRichText() const { return resultRichText; }

private:
	string hubUrl;
	string board;
	string parentTTH;
	tstring initialSubject;
	int64_t maxSize;

	TextBoxPtr subject;
	TextBoxPtr body;
	CheckBoxPtr richText;
	ButtonPtr editRichText;
	LabelPtr validation;
	ButtonPtr postButton;
	bool ready;

	string resultSubject;
	string resultBody;
	bool resultRichText;

	bool handleInitDialog();
	void updateState();
	void handleEditRichText();
	void handlePost();
};

#endif // DCPLUSPLUS_WIN32_BBS_POST_DLG_H
