/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2013, Jacek Sieka

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
  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <dwt/widgets/FolderDialog.h>
#include <dwt/util/win32/FileDialog.h>

namespace dwt {

FolderDialog::FolderDialog(Widget* parent) :
parent(parent),
pidlRoot(nullptr),
pidlInitialSel(nullptr),
options(0),
hasClientGuid(false)
{
}

FolderDialog::~FolderDialog() {
	if(pidlRoot) {
		::CoTaskMemFree(pidlRoot);
	}
	if(pidlInitialSel) {
		::CoTaskMemFree(pidlInitialSel);
	}
}

FolderDialog& FolderDialog::setRoot(const int csidl) {
	if(pidlRoot) {
		::CoTaskMemFree(pidlRoot);
		pidlRoot = nullptr;
	}
	SHGetSpecialFolderLocation(getParentHandle(), csidl, &pidlRoot);
	return *this;
}

FolderDialog& FolderDialog::setTitle(const tstring& newTitle) {
	this->title = newTitle;
	return *this;
}

FolderDialog& FolderDialog::setInitialSelection(const tstring& sel) {
	if(pidlInitialSel) {
		::CoTaskMemFree(pidlInitialSel);
		pidlInitialSel = nullptr;
	}
	initialSel = sel;
	return *this;
}

FolderDialog& FolderDialog::setInitialSelection(const int csidl) {
	if(pidlInitialSel) {
		::CoTaskMemFree(pidlInitialSel);
		pidlInitialSel = nullptr;
	}
	initialSel.clear();
	SHGetSpecialFolderLocation(getParentHandle(), csidl, &pidlInitialSel);
	return *this;
}

FolderDialog& FolderDialog::setClientGuid(const GUID& guid) {
	clientGuid = guid;
	hasClientGuid = true;
	return *this;
}

FolderDialog& FolderDialog::addPlace(const tstring& path, bool top) {
	places.emplace_back(path, top ? FDAP_TOP : FDAP_BOTTOM);
	return *this;
}

FolderDialog& FolderDialog::addOptions(FILEOPENDIALOGOPTIONS value) {
	options |= value;
	return *this;
}

bool FolderDialog::open(tstring& dir) {
	if(!dir.empty())
		setInitialSelection(dir);

	if(pidlRoot) {
		return openRooted(dir);
	}

	util::win32::FileDialogOptions dialogOptions;
	dialogOptions.owner = getParentHandle();
	dialogOptions.pickFolders = true;
	dialogOptions.title = title;
	dialogOptions.initialDirectory = initialSel;
	if(pidlInitialSel) {
		dialogOptions.initialItem = reinterpret_cast<PCIDLIST_ABSOLUTE>(pidlInitialSel);
	} else if(pidlRoot) {
		dialogOptions.initialItem = reinterpret_cast<PCIDLIST_ABSOLUTE>(pidlRoot);
	}
	dialogOptions.places = places;
	dialogOptions.options = options;
	dialogOptions.clientGuid = hasClientGuid ? &clientGuid : nullptr;

	std::vector<tstring> paths;
	if(!util::win32::showFileDialog(dialogOptions, paths)) {
		return false;
	}
	dir = paths.front();
	if(!dir.empty() && dir.back() != _T('\\')) {
		dir += _T('\\');
	}
	return true;
}

bool FolderDialog::openRooted(tstring& dir) {
	BROWSEINFO info = { getParentHandle(), pidlRoot };
	info.lpszTitle = title.empty() ? nullptr : title.c_str();
	info.ulFlags = BIF_USENEWUI | BIF_RETURNONLYFSDIRS | BIF_EDITBOX;
	if(!initialSel.empty() || pidlInitialSel) {
		info.lpfn = &browseCallbackProc;
		info.lParam = reinterpret_cast<LPARAM>(this);
	}

	auto oldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
	auto selected = ::SHBrowseForFolder(&info);
	::SetErrorMode(oldErrorMode);
	if(!selected) {
		return false;
	}

	TCHAR path[MAX_PATH + 1] = { 0 };
	bool result = ::SHGetPathFromIDList(selected, path) != FALSE;
	::CoTaskMemFree(selected);
	if(!result) {
		return false;
	}

	dir = path;
	if(!dir.empty() && dir.back() != _T('\\')) {
		dir += _T('\\');
	}
	return true;
}

int CALLBACK FolderDialog::browseCallbackProc(HWND window, UINT message, LPARAM, LPARAM data) {
	if(data && message == BFFM_INITIALIZED) {
		auto& dialog = *reinterpret_cast<FolderDialog*>(data);
		auto stringSelection = !dialog.initialSel.empty();
		auto selection = stringSelection ?
			reinterpret_cast<LPARAM>(dialog.initialSel.c_str()) :
			reinterpret_cast<LPARAM>(dialog.pidlInitialSel);
		::SendMessage(window, BFFM_SETSELECTION, stringSelection ? TRUE : FALSE, selection);
		::SendMessage(window, BFFM_SETEXPANDED, stringSelection ? TRUE : FALSE, selection);
	}
	return 0;
}

}
