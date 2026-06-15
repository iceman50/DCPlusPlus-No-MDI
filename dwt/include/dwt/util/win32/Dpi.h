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

#ifndef DWT_UTIL_WIN32_DPI_H
#define DWT_UTIL_WIN32_DPI_H

#include "../../WindowsHeaders.h"
#include "../../Point.h"
#include "../../Rectangle.h"

#include <cstdint>

namespace dwt { namespace util { namespace win32 {

static const unsigned defaultDpi = 96;

/**
 * Request the best DPI-awareness mode supported by the running Windows version.
 * Application manifests remain the preferred way to configure awareness.
 */
bool enablePerMonitorDpiAwareness();

enum class ThreadDpiAwareness : std::intptr_t {
	Unaware = -1,
	SystemAware = -2,
	PerMonitorAware = -3,
	PerMonitorAwareV2 = -4,
	UnawareGdiScaled = -5
};

class ScopedThreadDpiAwareness {
public:
	explicit ScopedThreadDpiAwareness(ThreadDpiAwareness awareness);
	~ScopedThreadDpiAwareness();

	ScopedThreadDpiAwareness(const ScopedThreadDpiAwareness&) = delete;
	ScopedThreadDpiAwareness& operator=(const ScopedThreadDpiAwareness&) = delete;

	bool changed() const { return previous != nullptr; }

private:
	HANDLE previous;
};

unsigned getDpi(HWND window);

int scale(int value, unsigned dpi, unsigned sourceDpi = defaultDpi);
Point scale(const Point& value, unsigned dpi, unsigned sourceDpi = defaultDpi);
Rectangle scale(const Rectangle& value, unsigned dpi, unsigned sourceDpi = defaultDpi);

int getSystemMetricsForDpi(int index, unsigned dpi);
bool systemParametersInfoForDpi(UINT action, UINT param, void* value,
	UINT flags, unsigned dpi);

bool adjustWindowRectForDpi(RECT& rect, DWORD style, bool hasMenu, DWORD exStyle, unsigned dpi);

} } }

#endif
