/*
  DC++ Widget Toolkit

  Copyright (c) 2026, iceman50

  All rights reserved.
*/

#ifndef DWT_UTIL_WIN32_PATH_H
#define DWT_UTIL_WIN32_PATH_H

#include "../../WindowsHeaders.h"
#include "../../tstring.h"

#include <algorithm>

namespace dwt { namespace util { namespace win32 {

/** Convert an absolute path to Win32 extended-length syntax when legacy file
 * APIs would otherwise apply MAX_PATH. Relative paths keep their semantics. */
inline tstring toNativePath(tstring path) {
	if(path.size() < MAX_PATH - 12 || path.compare(0, 4, _T("\\\\?\\")) == 0 ||
		path.compare(0, 4, _T("\\\\.\\")) == 0)
	{
		return path;
	}

	std::replace(path.begin(), path.end(), _T('/'), _T('\\'));
	if(path.size() >= 3 && path[1] == _T(':') && path[2] == _T('\\')) {
		return _T("\\\\?\\") + path;
	}
	if(path.compare(0, 2, _T("\\\\")) == 0) {
		return _T("\\\\?\\UNC\\") + path.substr(2);
	}
	return path;
}

} } }

#endif
