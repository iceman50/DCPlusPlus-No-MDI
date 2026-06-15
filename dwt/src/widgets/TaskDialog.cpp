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

#include <dwt/widgets/TaskDialog.h>

#include <dwt/DWTException.h>

#include <sstream>

namespace dwt {

TaskDialog::TaskDialog(Widget* parent_) :
	parent(parent_),
	commonButtons(0),
	flags(TDF_ALLOW_DIALOG_CANCELLATION),
	defaultButton(0),
	defaultRadioButton(0),
	width(0),
	mainIcon(nullptr),
	footerIcon(nullptr),
	verificationChecked(false)
{
}

TaskDialog& TaskDialog::setTitle(const tstring& value) {
	title = value;
	return *this;
}

TaskDialog& TaskDialog::setMainInstruction(const tstring& value) {
	mainInstruction = value;
	return *this;
}

TaskDialog& TaskDialog::setContent(const tstring& value) {
	content = value;
	return *this;
}

TaskDialog& TaskDialog::setFooter(const tstring& value) {
	footer = value;
	return *this;
}

TaskDialog& TaskDialog::setExpandedInformation(const tstring& value) {
	expandedInformation = value;
	return *this;
}

TaskDialog& TaskDialog::setVerificationText(const tstring& value, bool checked) {
	verificationText = value;
	verificationChecked = checked;
	return *this;
}

TaskDialog& TaskDialog::setExpandedControlText(const tstring& value) {
	expandedControlText = value;
	return *this;
}

TaskDialog& TaskDialog::setCollapsedControlText(const tstring& value) {
	collapsedControlText = value;
	return *this;
}

TaskDialog& TaskDialog::setCommonButtons(TASKDIALOG_COMMON_BUTTON_FLAGS value) {
	commonButtons = value;
	return *this;
}

TaskDialog& TaskDialog::addButton(int id, const tstring& text) {
	buttons.emplace_back(id, text);
	return *this;
}

TaskDialog& TaskDialog::addRadioButton(int id, const tstring& text) {
	radioButtons.emplace_back(id, text);
	return *this;
}

TaskDialog& TaskDialog::setDefaultButton(int id) {
	defaultButton = id;
	return *this;
}

TaskDialog& TaskDialog::setDefaultRadioButton(int id) {
	defaultRadioButton = id;
	return *this;
}

TaskDialog& TaskDialog::setFlags(TASKDIALOG_FLAGS value) {
	flags = value;
	return *this;
}

TaskDialog& TaskDialog::addFlags(TASKDIALOG_FLAGS value) {
	flags = static_cast<TASKDIALOG_FLAGS>(flags | value);
	return *this;
}

TaskDialog& TaskDialog::setCommandLinks(bool useCommandLinks, bool showIcons) {
	flags = static_cast<TASKDIALOG_FLAGS>(flags &
		~(TDF_USE_COMMAND_LINKS | TDF_USE_COMMAND_LINKS_NO_ICON));
	if(useCommandLinks) {
		flags = static_cast<TASKDIALOG_FLAGS>(flags |
			(showIcons ? TDF_USE_COMMAND_LINKS : TDF_USE_COMMAND_LINKS_NO_ICON));
	}
	return *this;
}

TaskDialog& TaskDialog::setProgressBar(bool enabled, bool marquee) {
	flags = static_cast<TASKDIALOG_FLAGS>(flags &
		~(TDF_SHOW_PROGRESS_BAR | TDF_SHOW_MARQUEE_PROGRESS_BAR));
	if(enabled) {
		flags = static_cast<TASKDIALOG_FLAGS>(flags |
			(marquee ? TDF_SHOW_MARQUEE_PROGRESS_BAR : TDF_SHOW_PROGRESS_BAR));
	}
	return *this;
}

TaskDialog& TaskDialog::setWidth(unsigned dialogUnits) {
	width = dialogUnits;
	return *this;
}

TaskDialog& TaskDialog::setMainIcon(PCWSTR icon) {
	mainIconHandle.reset();
	mainIcon = icon;
	flags = static_cast<TASKDIALOG_FLAGS>(flags & ~TDF_USE_HICON_MAIN);
	return *this;
}

TaskDialog& TaskDialog::setMainIcon(const IconPtr& icon) {
	mainIconHandle = icon;
	mainIcon = nullptr;
	flags = static_cast<TASKDIALOG_FLAGS>(flags | TDF_USE_HICON_MAIN);
	return *this;
}

TaskDialog& TaskDialog::setFooterIcon(PCWSTR icon) {
	footerIconHandle.reset();
	footerIcon = icon;
	flags = static_cast<TASKDIALOG_FLAGS>(flags & ~TDF_USE_HICON_FOOTER);
	return *this;
}

TaskDialog& TaskDialog::setFooterIcon(const IconPtr& icon) {
	footerIconHandle = icon;
	footerIcon = nullptr;
	flags = static_cast<TASKDIALOG_FLAGS>(flags | TDF_USE_HICON_FOOTER);
	return *this;
}

TaskDialog& TaskDialog::onEvent(const Callback& value) {
	callback = value;
	return *this;
}

PCWSTR TaskDialog::textOrNull(const tstring& value) {
	return value.empty() ? nullptr : value.c_str();
}

HRESULT CALLBACK TaskDialog::callbackProc(HWND window, UINT notification,
	WPARAM wParam, LPARAM lParam, LONG_PTR data)
{
	auto dialog = reinterpret_cast<TaskDialog*>(data);
	return dialog->callback ? dialog->callback(window, notification, wParam, lParam) : S_OK;
}

TaskDialog::Result TaskDialog::show() {
	std::vector<TASKDIALOG_BUTTON> nativeButtons;
	nativeButtons.reserve(buttons.size());
	for(const auto& button: buttons) {
		TASKDIALOG_BUTTON value = { button.id, button.text.c_str() };
		nativeButtons.push_back(value);
	}

	std::vector<TASKDIALOG_BUTTON> nativeRadioButtons;
	nativeRadioButtons.reserve(radioButtons.size());
	for(const auto& button: radioButtons) {
		TASKDIALOG_BUTTON value = { button.id, button.text.c_str() };
		nativeRadioButtons.push_back(value);
	}

	TASKDIALOGCONFIG config = { sizeof(TASKDIALOGCONFIG) };
	config.hwndParent = parent ? parent->handle() : nullptr;
	config.hInstance = ::GetModuleHandle(nullptr);
	config.dwFlags = flags;
	config.dwCommonButtons = commonButtons;
	config.pszWindowTitle = textOrNull(title);
	if(flags & TDF_USE_HICON_MAIN) {
		config.hMainIcon = mainIconHandle ? mainIconHandle->handle() : nullptr;
	} else {
		config.pszMainIcon = mainIcon;
	}
	config.pszMainInstruction = textOrNull(mainInstruction);
	config.pszContent = textOrNull(content);
	config.cButtons = static_cast<UINT>(nativeButtons.size());
	config.pButtons = nativeButtons.empty() ? nullptr : nativeButtons.data();
	config.nDefaultButton = defaultButton;
	config.cRadioButtons = static_cast<UINT>(nativeRadioButtons.size());
	config.pRadioButtons = nativeRadioButtons.empty() ? nullptr : nativeRadioButtons.data();
	config.nDefaultRadioButton = defaultRadioButton;
	config.pszVerificationText = textOrNull(verificationText);
	config.pszExpandedInformation = textOrNull(expandedInformation);
	config.pszExpandedControlText = textOrNull(expandedControlText);
	config.pszCollapsedControlText = textOrNull(collapsedControlText);
	if(flags & TDF_USE_HICON_FOOTER) {
		config.hFooterIcon = footerIconHandle ? footerIconHandle->handle() : nullptr;
	} else {
		config.pszFooterIcon = footerIcon;
	}
	config.pszFooter = textOrNull(footer);
	config.pfCallback = &callbackProc;
	config.lpCallbackData = reinterpret_cast<LONG_PTR>(this);
	config.cxWidth = width;
	if(verificationChecked) {
		config.dwFlags = static_cast<TASKDIALOG_FLAGS>(
			config.dwFlags | TDF_VERIFICATION_FLAG_CHECKED);
	}

	Result result;
	BOOL checked = FALSE;
	auto status = ::TaskDialogIndirect(&config, &result.button, &result.radioButton, &checked);
	if(FAILED(status)) {
		std::ostringstream message;
		message << "TaskDialogIndirect failed (HRESULT 0x" << std::hex
			<< static_cast<unsigned long>(status) << ")";
		throw DWTException(message.str());
	}
	result.verificationChecked = checked != FALSE;
	return result;
}

}
