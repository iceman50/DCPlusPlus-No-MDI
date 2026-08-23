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

#ifndef DWT_APPEARANCE_H
#define DWT_APPEARANCE_H

#include "WindowsHeaders.h"
#include "forward.h"

#include <functional>
#include <list>
#include <vector>

namespace dwt {

class Widget;

/** \ingroup Appearance
 * Determines how a widget participates in application appearance changes.
 */
enum class AppearancePolicy {
	/** Use the policy inherited from the application and the widget type. */
	Inherit,
	/** Paint framework-owned chrome while preserving application content colors. */
	FrameworkChrome,
	/** Preserve colors and formatting explicitly supplied by the application. */
	ApplicationContent,
	/** Leave the widget entirely under operating-system control. */
	Native
};

/** \ingroup Appearance
 * Application-wide visual appearance service.
 *
 * DWT uses native controls for behavior, input, metrics and accessibility. In
 * manual dark mode it paints their visual chrome with this palette so the same
 * implementation works on Windows 7 and later. High contrast always overrides
 * the configured mode and palette.
 */
class Appearance {
public:
	enum class Mode {
		System,
		Light,
		Dark
	};

	/** Semantic colors used by framework renderers. */
	struct Palette {
		COLORREF background;
		COLORREF surface;
		COLORREF text;
		COLORREF disabledText;
		COLORREF border;
		COLORREF accent;
		COLORREF highlightText;
	};

	typedef std::function<void ()> Callback;
	typedef std::list<Callback> CallbackList;
	typedef CallbackList::iterator CallbackIter;

	Appearance();
	~Appearance();

	Appearance(const Appearance&) = delete;
	Appearance& operator=(const Appearance&) = delete;

	/** Return the neutral dark palette supplied by DWT. */
	static Palette defaultPalette();

	/**
	 * Set the requested mode and manual palette, then refresh every DWT widget.
	 * Call this on the application's GUI thread.
	 */
	void configure(Mode mode, const Palette& palette);

	Mode getMode() const { return mode; }
	bool isDark() const { return dark; }
	bool isHighContrast() const { return highContrast; }
	bool isManual() const { return dark && !highContrast; }
	unsigned getGeneration() const { return generation; }

	const Palette& getPalette() const { return active; }
	COLORREF getContentBackground() const;
	COLORREF getContentText() const;
	const BrushPtr& getBackgroundBrush() const { return backgroundBrush; }
	const BrushPtr& getSurfaceBrush() const { return surfaceBrush; }

	/** Refresh system preference and high-contrast state. */
	bool refreshSystemState();

	/** Return true for messages which may change colors or contrast policy. */
	bool isColorSchemeMessage(const MSG& msg) const;

	/** Reapply the current appearance to all completely constructed widgets. */
	void applyAll();

	/** Observe appearance changes. The iterator remains valid until removed. */
	CallbackIter onChanged(const Callback& callback);
	void removeChanged(const CallbackIter& callback);

	/** Blend overlay into base; weight is the overlay contribution in 0..255. */
	static COLORREF blend(COLORREF base, COLORREF overlay, BYTE weight);

private:
	friend class Widget;

	void addWidget(Widget* widget);
	void removeWidget(Widget* widget);
	void apply(Widget* widget);
	bool handleMessage(Widget* widget, const MSG& msg, LRESULT& retVal);
	void updateActivePalette();
	void changed();
	void scheduleSystemRefresh(bool forceApply);

	static Palette sanitize(Palette palette);
	static bool queryHighContrast();
	static bool querySystemDarkPreference();

	Mode mode;
	Palette configured;
	Palette active;
	bool highContrast;
	bool systemDark;
	bool dark;
	bool applying;
	bool systemRefreshPending;
	bool systemRefreshRequiresApply;
	unsigned generation;
	std::vector<Widget*> widgets;
	CallbackList callbacks;
	BrushPtr backgroundBrush;
	BrushPtr surfaceBrush;
};

}

#endif
