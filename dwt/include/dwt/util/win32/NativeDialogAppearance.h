/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, iceman50

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

#ifndef DWT_UTIL_WIN32_NATIVEDIALOGAPPEARANCE_H
#define DWT_UTIL_WIN32_NATIVEDIALOGAPPEARANCE_H

#include "../../WindowsHeaders.h"

#include <memory>

namespace dwt {

class Appearance;

namespace util { namespace win32 {

/**
 * Apply DWT's manual appearance while a native modal dialog is running.
 *
 * Windows creates MessageBox, common-dialog and TaskDialog windows inside their
 * blocking show functions. This scoped object observes those windows on the
 * calling thread and applies the current DWT palette without changing their
 * command, focus, keyboard or accessibility behavior. It is a no-op in light,
 * system and high-contrast modes.
 *
 * Standard dialog controls and conventional #32770 dialog frames are
 * palette-painted on every supported Windows version. The optional
 * preserveDialogClientPainting argument keeps a common dialog's own client
 * painting and background erasure, for content such as ChooseColor's spectrum,
 * swatches, and the Shell file dialog's DirectUI composition.
 * The frame renderer keeps USER32's sizing, system-command, keyboard, tooltip
 * and accessibility contracts while replacing only palette-dependent pixels.
 * Shell-owned DirectUI surfaces and their helper scroll-bar windows remain
 * under operating-system control because they expose no documented Win7
 * palette-painting contract.
 *
 * Keep an instance alive for the complete duration of the native show call.
 * Scopes may be nested on the same UI thread.
 */
class NativeDialogAppearance {
public:
	explicit NativeDialogAppearance(const Appearance& appearance, bool preserveDialogClientPainting = false);
	~NativeDialogAppearance();

	NativeDialogAppearance(const NativeDialogAppearance&) = delete;
	NativeDialogAppearance& operator=(const NativeDialogAppearance&) = delete;

	/** Apply immediately to an already-created native dialog and its children. */
	void apply(HWND dialog) const;

	/** Return whether the supplied appearance requested manual rendering. */
	bool active() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};

} }

}

#endif
