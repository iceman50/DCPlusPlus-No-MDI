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

#include <dwt/util/win32/Dpi.h>

#include <cstring>

namespace dwt { namespace util { namespace win32 {

namespace {

template<typename T>
T getProc(HMODULE module, const char* name) {
	T function = nullptr;
	auto address = module ? ::GetProcAddress(module, name) : nullptr;
	static_assert(sizeof(function) == sizeof(address), "Function pointer size mismatch");
	std::memcpy(&function, &address, sizeof(function));
	return function;
}

HMODULE user32() {
	static HMODULE module = ::GetModuleHandle(_T("user32.dll"));
	return module;
}

HMODULE shcore() {
	static HMODULE module = ::LoadLibrary(_T("shcore.dll"));
	return module;
}

}

bool enablePerMonitorDpiAwareness() {
	typedef BOOL (WINAPI *SetProcessDpiAwarenessContextFunction)(HANDLE);
	auto setContext = getProc<SetProcessDpiAwarenessContextFunction>(
		user32(), "SetProcessDpiAwarenessContext");
	if(setContext) {
		// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
		if(setContext(reinterpret_cast<HANDLE>(-4)) || ::GetLastError() == ERROR_ACCESS_DENIED) {
			return true;
		}
	}

	typedef HRESULT (WINAPI *SetProcessDpiAwarenessFunction)(int);
	auto setAwareness = getProc<SetProcessDpiAwarenessFunction>(
		shcore(), "SetProcessDpiAwareness");
	if(setAwareness) {
		// PROCESS_PER_MONITOR_DPI_AWARE
		auto result = setAwareness(2);
		if(SUCCEEDED(result) || result == E_ACCESSDENIED) {
			return true;
		}
	}

	return ::SetProcessDPIAware() != FALSE || ::GetLastError() == ERROR_ACCESS_DENIED;
}

ScopedThreadDpiAwareness::ScopedThreadDpiAwareness(
	ThreadDpiAwareness awareness) :
	previous(nullptr)
{
	typedef HANDLE (WINAPI *Function)(HANDLE);
	auto function = getProc<Function>(
		user32(), "SetThreadDpiAwarenessContext");
	if(function) {
		previous = function(reinterpret_cast<HANDLE>(
			static_cast<std::intptr_t>(awareness)));
	}
}

ScopedThreadDpiAwareness::~ScopedThreadDpiAwareness() {
	if(!previous) {
		return;
	}
	typedef HANDLE (WINAPI *Function)(HANDLE);
	auto function = getProc<Function>(
		user32(), "SetThreadDpiAwarenessContext");
	if(function) {
		function(previous);
	}
}

unsigned getDpi(HWND window) {
	typedef UINT (WINAPI *GetDpiForWindowFunction)(HWND);
	auto getWindowDpi = getProc<GetDpiForWindowFunction>(user32(), "GetDpiForWindow");
	if(getWindowDpi && window) {
		auto dpi = getWindowDpi(window);
		if(dpi) {
			return dpi;
		}
	}

	HDC dc = ::GetDC(window);
	if(!dc) {
		return defaultDpi;
	}
	auto dpi = static_cast<unsigned>(::GetDeviceCaps(dc, LOGPIXELSX));
	::ReleaseDC(window, dc);
	return dpi ? dpi : defaultDpi;
}

int scale(int value, unsigned dpi, unsigned sourceDpi) {
	return ::MulDiv(value, static_cast<int>(dpi), static_cast<int>(sourceDpi));
}

Point scale(const Point& value, unsigned dpi, unsigned sourceDpi) {
	return Point(scale(static_cast<int>(value.x), dpi, sourceDpi),
		scale(static_cast<int>(value.y), dpi, sourceDpi));
}

Rectangle scale(const Rectangle& value, unsigned dpi, unsigned sourceDpi) {
	return Rectangle(scale(value.pos, dpi, sourceDpi), scale(value.size, dpi, sourceDpi));
}

int getSystemMetricsForDpi(int index, unsigned dpi) {
	typedef int (WINAPI *GetSystemMetricsForDpiFunction)(int, UINT);
	auto getMetric = getProc<GetSystemMetricsForDpiFunction>(
		user32(), "GetSystemMetricsForDpi");
	if(getMetric) {
		return getMetric(index, dpi);
	}
	return scale(::GetSystemMetrics(index), dpi);
}

bool systemParametersInfoForDpi(UINT action, UINT param, void* value,
	UINT flags, unsigned dpi)
{
	typedef BOOL (WINAPI *Function)(UINT, UINT, PVOID, UINT, UINT);
	auto function = getProc<Function>(
		user32(), "SystemParametersInfoForDpi");
	if(function) {
		return function(action, param, value, flags, dpi) != FALSE;
	}
	return ::SystemParametersInfo(action, param, value, flags) != FALSE;
}

bool adjustWindowRectForDpi(RECT& rect, DWORD style, bool hasMenu, DWORD exStyle, unsigned dpi) {
	typedef BOOL (WINAPI *AdjustWindowRectExForDpiFunction)(LPRECT, DWORD, BOOL, DWORD, UINT);
	auto adjust = getProc<AdjustWindowRectExForDpiFunction>(
		user32(), "AdjustWindowRectExForDpi");
	if(adjust) {
		return adjust(&rect, style, hasMenu ? TRUE : FALSE, exStyle, dpi) != FALSE;
	}
	return ::AdjustWindowRectEx(&rect, style, hasMenu ? TRUE : FALSE, exStyle) != FALSE;
}

} } }
