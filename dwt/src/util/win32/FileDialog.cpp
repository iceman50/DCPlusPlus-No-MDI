/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, Jacek Sieka

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

#include <dwt/DWTException.h>

#include <sstream>

namespace dwt { namespace util { namespace win32 {

namespace {

template<typename T>
class ComPtr {
public:
	ComPtr() : value(nullptr) { }
	~ComPtr() { if(value) value->Release(); }

	T* get() const { return value; }
	T** put() { return &value; }
	T* operator->() const { return value; }

private:
	T* value;
};

void throwDialogError(const char* operation, HRESULT result) {
	std::ostringstream message;
	message << operation << " failed (HRESULT 0x" << std::hex
		<< static_cast<unsigned long>(result) << ")";
	throw DWTException(message.str());
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

void setFolder(IFileDialog* dialog, const FileDialogOptions& options) {
	ComPtr<IShellItem> item;
	HRESULT result = E_FAIL;
	if(options.initialItem) {
		result = ::SHCreateItemFromIDList(options.initialItem, IID_PPV_ARGS(item.put()));
	} else if(!options.initialDirectory.empty()) {
		result = ::SHCreateItemFromParsingName(options.initialDirectory.c_str(), nullptr,
			IID_PPV_ARGS(item.put()));
	}
	if(SUCCEEDED(result)) {
		dialog->SetFolder(item.get());
	}
}

}

bool showFileDialog(const FileDialogOptions& options, std::vector<tstring>& paths) {
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

	FILEOPENDIALOGOPTIONS dialogOptions = 0;
	result = dialog->GetOptions(&dialogOptions);
	if(FAILED(result)) {
		throwDialogError("IFileDialog::GetOptions", result);
	}
	dialogOptions |= FOS_FORCEFILESYSTEM | options.options | mapLegacyFlags(options.legacyFlags);
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
		dialog->SetFileTypeIndex(options.activeFilter + 1);
	}

	if(!options.title.empty()) dialog->SetTitle(options.title.c_str());
	if(!options.defaultExtension.empty()) dialog->SetDefaultExtension(options.defaultExtension.c_str());
	if(!options.initialFileName.empty()) dialog->SetFileName(options.initialFileName.c_str());
	if(options.clientGuid) dialog->SetClientGuid(*options.clientGuid);
	setFolder(dialog.get(), options);

	for(const auto& place: options.places) {
		ComPtr<IShellItem> item;
		if(SUCCEEDED(::SHCreateItemFromParsingName(place.first.c_str(), nullptr,
			IID_PPV_ARGS(item.put())))) {
			dialog->AddPlace(item.get(), place.second);
		}
	}

	result = dialog->Show(options.owner);
	if(result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		return false;
	}
	if(FAILED(result)) {
		throwDialogError("IFileDialog::Show", result);
	}

	paths.clear();
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
		items->GetCount(&count);
		for(DWORD i = 0; i < count; ++i) {
			ComPtr<IShellItem> item;
			if(SUCCEEDED(items->GetItemAt(i, item.put()))) {
				tstring path;
				if(getPath(item.get(), path)) {
					paths.push_back(path);
				}
			}
		}
	} else {
		ComPtr<IShellItem> item;
		result = dialog->GetResult(item.put());
		if(FAILED(result)) {
			throwDialogError("IFileDialog::GetResult", result);
		}
		tstring path;
		if(getPath(item.get(), path)) {
			paths.push_back(path);
		}
	}

	return !paths.empty();
}

} } }
