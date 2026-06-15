/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, Jacek Sieka

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
*/

#ifndef DWT_TASKDIALOG_H
#define DWT_TASKDIALOG_H

#include "../Widget.h"
#include "../resources/Icon.h"

#include <functional>
#include <vector>

namespace dwt {

class TaskDialog {
public:
	struct Button {
		Button(int id_, const tstring& text_) : id(id_), text(text_) { }
		int id;
		tstring text;
	};

	struct Result {
		int button = 0;
		int radioButton = 0;
		bool verificationChecked = false;
	};

	typedef std::function<HRESULT (HWND, UINT, WPARAM, LPARAM)> Callback;

	explicit TaskDialog(Widget* parent = nullptr);

	TaskDialog& setTitle(const tstring& value);
	TaskDialog& setMainInstruction(const tstring& value);
	TaskDialog& setContent(const tstring& value);
	TaskDialog& setFooter(const tstring& value);
	TaskDialog& setExpandedInformation(const tstring& value);
	TaskDialog& setVerificationText(const tstring& value, bool checked = false);
	TaskDialog& setExpandedControlText(const tstring& value);
	TaskDialog& setCollapsedControlText(const tstring& value);

	TaskDialog& setCommonButtons(TASKDIALOG_COMMON_BUTTON_FLAGS value);
	TaskDialog& addButton(int id, const tstring& text);
	TaskDialog& addRadioButton(int id, const tstring& text);
	TaskDialog& setDefaultButton(int id);
	TaskDialog& setDefaultRadioButton(int id);

	TaskDialog& setFlags(TASKDIALOG_FLAGS value);
	TaskDialog& addFlags(TASKDIALOG_FLAGS value);
	TaskDialog& setCommandLinks(bool useCommandLinks = true, bool showIcons = true);
	TaskDialog& setProgressBar(bool enabled = true, bool marquee = false);
	TaskDialog& setWidth(unsigned dialogUnits);

	TaskDialog& setMainIcon(PCWSTR icon);
	TaskDialog& setMainIcon(const IconPtr& icon);
	TaskDialog& setFooterIcon(PCWSTR icon);
	TaskDialog& setFooterIcon(const IconPtr& icon);

	TaskDialog& onEvent(const Callback& callback);

	Result show();

private:
	static HRESULT CALLBACK callbackProc(HWND window, UINT notification,
		WPARAM wParam, LPARAM lParam, LONG_PTR data);

	static PCWSTR textOrNull(const tstring& value);

	Widget* parent;
	tstring title;
	tstring mainInstruction;
	tstring content;
	tstring footer;
	tstring expandedInformation;
	tstring verificationText;
	tstring expandedControlText;
	tstring collapsedControlText;
	std::vector<Button> buttons;
	std::vector<Button> radioButtons;
	TASKDIALOG_COMMON_BUTTON_FLAGS commonButtons;
	TASKDIALOG_FLAGS flags;
	int defaultButton;
	int defaultRadioButton;
	unsigned width;
	PCWSTR mainIcon;
	PCWSTR footerIcon;
	IconPtr mainIconHandle;
	IconPtr footerIconHandle;
	Callback callback;
	bool verificationChecked;
};

}

#endif
