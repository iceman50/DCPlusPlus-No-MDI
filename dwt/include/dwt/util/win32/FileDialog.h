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

#ifndef DWT_UTIL_WIN32_FILEDIALOG_H
#define DWT_UTIL_WIN32_FILEDIALOG_H

#include "../../WindowsHeaders.h"
#include "../../tstring.h"

#include <utility>
#include <vector>

namespace dwt { namespace util { namespace win32 {

struct FileDialogOptions {
	HWND owner = nullptr;
	bool save = false;
	bool pickFolders = false;
	bool allowMultiple = false;
	DWORD legacyFlags = 0;
	FILEOPENDIALOGOPTIONS options = 0;
	unsigned activeFilter = 0;
	tstring title;
	tstring initialDirectory;
	tstring defaultExtension;
	tstring initialFileName;
	std::vector<std::pair<tstring, tstring>> filters;
	std::vector<std::pair<tstring, FDAP>> places;
	PCIDLIST_ABSOLUTE initialItem = nullptr;
	const GUID* clientGuid = nullptr;
};

bool showFileDialog(const FileDialogOptions& options, std::vector<tstring>& paths);

} } }

#endif
