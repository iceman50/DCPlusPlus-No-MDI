/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, iceman50

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
*/

#include <dwt/util/win32/FileDialog.h>

#include <dwt/Application.h>
#include <dwt/DWTException.h>
#include <dwt/util/win32/NativeDialogAppearance.h>

#include <sstream>

namespace dwt { namespace util { namespace win32 {

namespace {

class FileDialogEventSink : public IFileDialogEvents {
public:
	explicit FileDialogEventSink(const FileDialogEvents& events) :
		refCount(1), events(events) { }

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
		if(!object) {
			return E_POINTER;
		}
		if(::IsEqualIID(riid, IID_IUnknown) || ::IsEqualIID(riid, IID_IFileDialogEvents)) {
			*object = static_cast<IFileDialogEvents*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override {
		return ::InterlockedIncrement(&refCount);
	}

	ULONG STDMETHODCALLTYPE Release() override {
		auto result = ::InterlockedDecrement(&refCount);
		if(!result) {
			delete this;
		}
		return result;
	}

	HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog* dialog) override {
		return events.fileOk ? events.fileOk(dialog) : S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog* dialog, IShellItem* folder) override {
		return events.folderChanging ? events.folderChanging(dialog, folder) : S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog* dialog) override {
		if(events.folderChanged) {
			events.folderChanged(dialog);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog* dialog) override {
		if(events.selectionChanged) {
			events.selectionChanged(dialog);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog* dialog, IShellItem* item,
		FDE_SHAREVIOLATION_RESPONSE* response) override
	{
		if(events.shareViolation && response) {
			events.shareViolation(dialog, item, *response);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog* dialog) override {
		if(events.typeChanged) {
			events.typeChanged(dialog);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog* dialog, IShellItem* item,
		FDE_OVERWRITE_RESPONSE* response) override
	{
		if(events.overwrite && response) {
			events.overwrite(dialog, item, *response);
		}
		return S_OK;
	}

private:
	volatile LONG refCount;
	const FileDialogEvents& events;
};

void throwDialogError(const char* operation, HRESULT result) {
	std::ostringstream message;
	message << operation << " failed (HRESULT 0x" << std::hex
		<< static_cast<unsigned long>(result) << ")";
	throw DWTException(message.str());
}

void checkDialogResult(const char* operation, HRESULT result) {
	if(FAILED(result)) {
		throwDialogError(operation, result);
	}
}

FILEOPENDIALOGOPTIONS mapLegacyFlags(DWORD flags) {
	FILEOPENDIALOGOPTIONS options = 0;
	if(flags & OFN_OVERWRITEPROMPT) options |= FOS_OVERWRITEPROMPT;
	if(flags & OFN_NOCHANGEDIR) options |= FOS_NOCHANGEDIR;
	if(flags & OFN_NOVALIDATE) options |= FOS_NOVALIDATE;
	if(flags & OFN_ALLOWMULTISELECT) options |= FOS_ALLOWMULTISELECT;
	if(flags & OFN_PATHMUSTEXIST) options |= FOS_PATHMUSTEXIST;
	if(flags & OFN_FILEMUSTEXIST) options |= FOS_FILEMUSTEXIST;
	if(flags & OFN_CREATEPROMPT) options |= FOS_CREATEPROMPT;
	if(flags & OFN_NODEREFERENCELINKS) options |= FOS_NODEREFERENCELINKS;
	if(flags & OFN_DONTADDTORECENT) options |= FOS_DONTADDTORECENT;
	if(flags & OFN_FORCESHOWHIDDEN) options |= FOS_FORCESHOWHIDDEN;
	return options;
}

bool getPath(IShellItem* item, tstring& path) {
	PWSTR value = nullptr;
	auto result = item->GetDisplayName(SIGDN_FILESYSPATH, &value);
	if(FAILED(result)) {
		return false;
	}
	path = value;
	::CoTaskMemFree(value);
	return true;
}

FileDialogResult makeResult(IShellItem* item) {
	FileDialogResult result;
	result.item.reset(item);
	getPath(item, result.path);
	return result;
}

void setFolder(IFileDialog* dialog, const FileDialogOptions& options) {
	ComPtr<IShellItem> item;
	HRESULT result = E_FAIL;
	if(options.initialFolder) {
		item = options.initialFolder;
		result = S_OK;
	} else if(options.initialItem) {
		result = ::SHCreateItemFromIDList(options.initialItem, IID_PPV_ARGS(item.put()));
	} else if(!options.initialDirectory.empty()) {
		result = ::SHCreateItemFromParsingName(options.initialDirectory.c_str(), nullptr,
			IID_PPV_ARGS(item.put()));
	}
	if(SUCCEEDED(result)) {
		dialog->SetFolder(item.get());
	}
}

void configureDialog(const FileDialogOptions& options, IFileDialog* dialog) {
	HRESULT result;
	FILEOPENDIALOGOPTIONS dialogOptions = 0;
	result = dialog->GetOptions(&dialogOptions);
	if(FAILED(result)) {
		throwDialogError("IFileDialog::GetOptions", result);
	}
	dialogOptions |= options.options | mapLegacyFlags(options.legacyFlags);
	if(options.forceFilesystem) {
		dialogOptions |= FOS_FORCEFILESYSTEM;
	}
	if(options.pickFolders) dialogOptions |= FOS_PICKFOLDERS | FOS_PATHMUSTEXIST;
	if(options.allowMultiple) dialogOptions |= FOS_ALLOWMULTISELECT;
	if(!options.save && !options.pickFolders) {
		dialogOptions |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST;
	}
	result = dialog->SetOptions(dialogOptions);
	if(FAILED(result)) {
		throwDialogError("IFileDialog::SetOptions", result);
	}

	std::vector<COMDLG_FILTERSPEC> filters;
	filters.reserve(options.filters.size());
	for(const auto& filter: options.filters) {
		COMDLG_FILTERSPEC spec = { filter.first.c_str(), filter.second.c_str() };
		filters.push_back(spec);
	}
	if(!filters.empty()) {
		result = dialog->SetFileTypes(static_cast<UINT>(filters.size()), filters.data());
		if(FAILED(result)) {
			throwDialogError("IFileDialog::SetFileTypes", result);
		}
		result = dialog->SetFileTypeIndex(options.activeFilter + 1);
		if(FAILED(result)) {
			throwDialogError("IFileDialog::SetFileTypeIndex", result);
		}
	}

	if(!options.title.empty()) dialog->SetTitle(options.title.c_str());
	if(!options.defaultExtension.empty()) dialog->SetDefaultExtension(options.defaultExtension.c_str());
	if(!options.initialFileName.empty()) dialog->SetFileName(options.initialFileName.c_str());
	if(options.clientGuid) dialog->SetClientGuid(*options.clientGuid);
	setFolder(dialog, options);

	for(const auto& place: options.places) {
		ComPtr<IShellItem> item;
		if(SUCCEEDED(::SHCreateItemFromParsingName(place.first.c_str(), nullptr,
			IID_PPV_ARGS(item.put())))) {
			dialog->AddPlace(item.get(), place.second);
		}
	}

	for(const auto& place: options.shellPlaces) {
		if(place.first) {
			dialog->AddPlace(place.first.get(), place.second);
		}
	}

	if(options.controls || options.customize) {
		ComPtr<IFileDialogCustomize> customize;
		result = dialog->QueryInterface(IID_PPV_ARGS(customize.put()));
		if(FAILED(result)) {
			throwDialogError("IFileDialogCustomize::QueryInterface", result);
		}
		if(options.controls) {
			FileDialogControls controls(*customize.get());
			options.controls(controls);
		}
		if(options.customize) {
			options.customize(*customize.get());
		}
	}
}

bool collectDialogResults(const FileDialogOptions& options, IFileDialog* dialog,
	std::vector<FileDialogResult>& results)
{
	results.clear();
	HRESULT result;
	if(options.allowMultiple && !options.save) {
		ComPtr<IFileOpenDialog> openDialog;
		result = dialog->QueryInterface(IID_PPV_ARGS(openDialog.put()));
		if(FAILED(result)) {
			throwDialogError("IFileOpenDialog::QueryInterface", result);
		}
		ComPtr<IShellItemArray> items;
		result = openDialog->GetResults(items.put());
		if(FAILED(result)) {
			throwDialogError("IFileOpenDialog::GetResults", result);
		}
		DWORD count = 0;
		result = items->GetCount(&count);
		if(FAILED(result)) {
			throwDialogError("IShellItemArray::GetCount", result);
		}
		for(DWORD i = 0; i < count; ++i) {
			ComPtr<IShellItem> item;
			if(SUCCEEDED(items->GetItemAt(i, item.put()))) {
				results.push_back(makeResult(item.get()));
			}
		}
	} else {
		ComPtr<IShellItem> item;
		result = dialog->GetResult(item.put());
		if(FAILED(result)) {
			throwDialogError("IFileDialog::GetResult", result);
		}
		results.push_back(makeResult(item.get()));
	}

	return !results.empty();
}

}

bool FileDialogResult::getDisplayName(SIGDN sigdn, tstring& value) const {
	value.clear();
	if(!item) {
		return false;
	}

	PWSTR displayName = nullptr;
	auto result = item->GetDisplayName(sigdn, &displayName);
	if(FAILED(result)) {
		return false;
	}
	value = displayName;
	::CoTaskMemFree(displayName);
	return true;
}

FileDialogControls::FileDialogControls(IFileDialogCustomize& value) :
	customize(&value)
{
}

FileDialogControls::FileDialogControls(IFileDialog& dialog) {
	auto result = dialog.QueryInterface(IID_PPV_ARGS(customize.put()));
	checkDialogResult("IFileDialogCustomize::QueryInterface", result);
}

IFileDialogCustomize& FileDialogControls::raw() const {
	return *customize.get();
}

void FileDialogControls::startVisualGroup(DWORD id, const tstring& label) {
	checkDialogResult("IFileDialogCustomize::StartVisualGroup",
		customize->StartVisualGroup(id, label.c_str()));
}

void FileDialogControls::endVisualGroup() {
	checkDialogResult("IFileDialogCustomize::EndVisualGroup",
		customize->EndVisualGroup());
}

void FileDialogControls::makeProminent(DWORD id) {
	checkDialogResult("IFileDialogCustomize::MakeProminent",
		customize->MakeProminent(id));
}

void FileDialogControls::addLabel(DWORD id, const tstring& text) {
	addText(id, text);
}

void FileDialogControls::addText(DWORD id, const tstring& text) {
	checkDialogResult("IFileDialogCustomize::AddText",
		customize->AddText(id, text.c_str()));
}

void FileDialogControls::addPushButton(DWORD id, const tstring& label) {
	checkDialogResult("IFileDialogCustomize::AddPushButton",
		customize->AddPushButton(id, label.c_str()));
}

void FileDialogControls::addCheckButton(DWORD id, const tstring& label,
	bool checked)
{
	checkDialogResult("IFileDialogCustomize::AddCheckButton",
		customize->AddCheckButton(id, label.c_str(), checked ? TRUE : FALSE));
}

void FileDialogControls::addRadioButtonList(DWORD id,
	const std::vector<FileDialogControlItem>& items)
{
	checkDialogResult("IFileDialogCustomize::AddRadioButtonList",
		customize->AddRadioButtonList(id));
	for(const auto& item: items) {
		addControlItem(id, item.id, item.label);
	}
}

void FileDialogControls::addComboBox(DWORD id,
	const std::vector<FileDialogControlItem>& items)
{
	checkDialogResult("IFileDialogCustomize::AddComboBox",
		customize->AddComboBox(id));
	for(const auto& item: items) {
		addControlItem(id, item.id, item.label);
	}
}

void FileDialogControls::addMenu(DWORD id, const tstring& label) {
	checkDialogResult("IFileDialogCustomize::AddMenu",
		customize->AddMenu(id, label.c_str()));
}

void FileDialogControls::addControlItem(DWORD controlId, DWORD itemId,
	const tstring& label)
{
	checkDialogResult("IFileDialogCustomize::AddControlItem",
		customize->AddControlItem(controlId, itemId, label.c_str()));
}

void FileDialogControls::removeControlItem(DWORD controlId, DWORD itemId) {
	checkDialogResult("IFileDialogCustomize::RemoveControlItem",
		customize->RemoveControlItem(controlId, itemId));
}

void FileDialogControls::removeAllControlItems(DWORD controlId) {
	checkDialogResult("IFileDialogCustomize::RemoveAllControlItems",
		customize->RemoveAllControlItems(controlId));
}

void FileDialogControls::setControlLabel(DWORD id, const tstring& label) {
	checkDialogResult("IFileDialogCustomize::SetControlLabel",
		customize->SetControlLabel(id, label.c_str()));
}

CDCONTROLSTATEF FileDialogControls::getControlState(DWORD id) const {
	CDCONTROLSTATEF state = CDCS_INACTIVE;
	checkDialogResult("IFileDialogCustomize::GetControlState",
		customize->GetControlState(id, &state));
	return state;
}

void FileDialogControls::setControlState(DWORD id, CDCONTROLSTATEF state) {
	checkDialogResult("IFileDialogCustomize::SetControlState",
		customize->SetControlState(id, state));
}

bool FileDialogControls::getControlVisible(DWORD id) const {
	return (getControlState(id) & CDCS_VISIBLE) != 0;
}

bool FileDialogControls::getControlEnabled(DWORD id) const {
	return (getControlState(id) & CDCS_ENABLED) != 0;
}

void FileDialogControls::setControlVisible(DWORD id, bool visible) {
	auto state = getControlState(id);
	state = static_cast<CDCONTROLSTATEF>(
		visible ? state | CDCS_VISIBLE : state & ~CDCS_VISIBLE);
	setControlState(id, state);
}

void FileDialogControls::setControlEnabled(DWORD id, bool enabled) {
	auto state = getControlState(id);
	state = static_cast<CDCONTROLSTATEF>(
		enabled ? state | CDCS_ENABLED : state & ~CDCS_ENABLED);
	setControlState(id, state);
}

bool FileDialogControls::getCheckButtonChecked(DWORD id) const {
	BOOL checked = FALSE;
	checkDialogResult("IFileDialogCustomize::GetCheckButtonState",
		customize->GetCheckButtonState(id, &checked));
	return checked != FALSE;
}

void FileDialogControls::setCheckButtonChecked(DWORD id, bool checked) {
	checkDialogResult("IFileDialogCustomize::SetCheckButtonState",
		customize->SetCheckButtonState(id, checked ? TRUE : FALSE));
}

DWORD FileDialogControls::getSelectedControlItem(DWORD controlId) const {
	DWORD itemId = 0;
	checkDialogResult("IFileDialogCustomize::GetSelectedControlItem",
		customize->GetSelectedControlItem(controlId, &itemId));
	return itemId;
}

void FileDialogControls::setSelectedControlItem(DWORD controlId, DWORD itemId) {
	checkDialogResult("IFileDialogCustomize::SetSelectedControlItem",
		customize->SetSelectedControlItem(controlId, itemId));
}

CDCONTROLSTATEF FileDialogControls::getControlItemState(DWORD controlId,
	DWORD itemId) const
{
	CDCONTROLSTATEF state = CDCS_INACTIVE;
	checkDialogResult("IFileDialogCustomize::GetControlItemState",
		customize->GetControlItemState(controlId, itemId, &state));
	return state;
}

void FileDialogControls::setControlItemState(DWORD controlId, DWORD itemId,
	CDCONTROLSTATEF state)
{
	checkDialogResult("IFileDialogCustomize::SetControlItemState",
		customize->SetControlItemState(controlId, itemId, state));
}

bool FileDialogControls::getControlItemVisible(DWORD controlId,
	DWORD itemId) const
{
	return (getControlItemState(controlId, itemId) & CDCS_VISIBLE) != 0;
}

bool FileDialogControls::getControlItemEnabled(DWORD controlId,
	DWORD itemId) const
{
	return (getControlItemState(controlId, itemId) & CDCS_ENABLED) != 0;
}

void FileDialogControls::setControlItemVisible(DWORD controlId, DWORD itemId,
	bool visible)
{
	auto state = getControlItemState(controlId, itemId);
	state = static_cast<CDCONTROLSTATEF>(
		visible ? state | CDCS_VISIBLE : state & ~CDCS_VISIBLE);
	setControlItemState(controlId, itemId, state);
}

void FileDialogControls::setControlItemEnabled(DWORD controlId, DWORD itemId,
	bool enabled)
{
	auto state = getControlItemState(controlId, itemId);
	state = static_cast<CDCONTROLSTATEF>(
		enabled ? state | CDCS_ENABLED : state & ~CDCS_ENABLED);
	setControlItemState(controlId, itemId, state);
}

bool showFileDialogItems(const FileDialogOptions& options, std::vector<FileDialogResult>& results) {
	ComPtr<IFileDialog> dialog;
	HRESULT result;
	if(options.save) {
		result = ::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(dialog.put()));
	} else {
		result = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(dialog.put()));
	}
	if(FAILED(result)) {
		throwDialogError("CoCreateInstance(IFileDialog)", result);
	}

	configureDialog(options, dialog.get());

	DWORD adviseCookie = 0;
	ComPtr<IFileDialogEvents> sink;
	if(options.events && !options.events->empty()) {
		sink.reset(new FileDialogEventSink(*options.events), false);
		result = dialog->Advise(sink.get(), &adviseCookie);
		if(FAILED(result)) {
			throwDialogError("IFileDialog::Advise", result);
		}
	}

	/* IFileDialog owns a DirectUI client surface. Preserve that composition while
	 * NativeDialogAppearance themes the conventional frame and child controls. */
	NativeDialogAppearance appearance(Application::instance().getAppearance(), true);
	result = dialog->Show(options.owner);
	if(adviseCookie) {
		dialog->Unadvise(adviseCookie);
	}
	if(result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		return false;
	}
	if(FAILED(result)) {
		throwDialogError("IFileDialog::Show", result);
	}

	return collectDialogResults(options, dialog.get(), results);
}

bool showFileDialog(const FileDialogOptions& options, std::vector<tstring>& paths) {
	std::vector<FileDialogResult> results;
	if(!showFileDialogItems(options, results)) {
		return false;
	}

	paths.clear();
	for(const auto& item: results) {
		if(item.hasPath()) {
			paths.push_back(item.path);
		}
	}
	return !paths.empty();
}

} } }
