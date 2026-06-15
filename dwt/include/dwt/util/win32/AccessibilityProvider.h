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

#ifndef DWT_UTIL_WIN32_ACCESSIBILITYPROVIDER_H
#define DWT_UTIL_WIN32_ACCESSIBILITYPROVIDER_H

#include "../../WindowsHeaders.h"

#include <cstdint>

namespace dwt {

class Widget;
class AccessibilityProvider;

namespace util { namespace win32 {

AccessibilityProvider* createAccessibilityProvider(Widget* widget);
void detachAccessibilityProvider(AccessibilityProvider* provider);
void releaseAccessibilityProvider(AccessibilityProvider* provider);
LRESULT returnAccessibilityProvider(AccessibilityProvider* provider,
	HWND window, WPARAM wParam, LPARAM lParam);
void raiseAccessibilityEvent(AccessibilityProvider* provider, long eventId);
void raiseAccessibilityItemEvent(AccessibilityProvider* provider,
	std::uintptr_t item, long eventId);
void raiseAccessibilityStructureChanged(AccessibilityProvider* provider);

} }

}

#endif
