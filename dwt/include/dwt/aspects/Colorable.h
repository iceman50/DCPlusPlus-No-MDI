/*
  DC++ Widget Toolkit

  Copyright (c) 2007-2026, iceman50

  SmartWin++

  Copyright (c) 2005 Thomas Hansen

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

      * Redistributions of source code must retain the above copyright notice,
        this list of conditions and the following disclaimer.
      * Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.
      * Neither the name of the DWT nor SmartWin++ nor the names of its contributors
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

#ifndef DWT_aspects_Colorable_h
#define DWT_aspects_Colorable_h

#include "../Message.h"
#include "../Widget.h"
#include "../resources/Brush.h"

#include <optional>

namespace dwt { namespace aspects {

/** Aspect class used by Widgets that have the possibility of changing colors. By default, this is
done by catching WM_CTLCOLOR. Widgets that don't support WM_CTLCOLOR can customize this behavior by
providing a void setColorImpl(COLORREF text, COLORREF background) function. */
template<typename WidgetType>
class Colorable {
	WidgetType& W() { return *static_cast<WidgetType*>(this); }

public:
	Colorable() : colorsExplicit(false), textColor(NaC), backgroundColor(NaC) { }

	void setColor(COLORREF text, COLORREF background) {
		colorsExplicit = true;
		textColor = text;
		backgroundColor = background;
		W().setColorImpl(text, background);
	}

	/** Return true when colors were explicitly supplied by the application. */
	bool hasExplicitColors() const { return colorsExplicit; }
	COLORREF getExplicitTextColor() const { return textColor; }
	COLORREF getExplicitBackgroundColor() const { return backgroundColor; }

	/** Return the widget to application appearance or native color inheritance. */
	void clearColor() {
		colorsExplicit = false;
		textColor = backgroundColor = NaC;
		W().clearColorImpl();
	}

protected:
	virtual void setColorImpl(COLORREF text, COLORREF background) {
		if(colorCallback) {
			W().clearInternalCallback(Message(WM_CTLCOLOR), *colorCallback);
		}
		BrushPtr brush { new Brush { background } };
		colorCallback = W().addInternalCallback(Message(WM_CTLCOLOR),
			[text, background, brush](const MSG& msg, LRESULT& ret) -> bool {
			HDC dc = reinterpret_cast<HDC>(msg.wParam);
			::SetTextColor(dc, text);
			::SetBkColor(dc, background);
			ret = reinterpret_cast<LRESULT>(brush->handle());
			return true;
		});
	}

	virtual void clearColorImpl() {
		if(colorCallback) {
			W().clearInternalCallback(Message(WM_CTLCOLOR), *colorCallback);
			colorCallback.reset();
		}
		if(W().handle()) {
			::InvalidateRect(W().handle(), nullptr, TRUE);
		}
	}

private:
	bool colorsExplicit;
	COLORREF textColor;
	COLORREF backgroundColor;
	/* Framework color plumbing uses Widget's internal callback channel so public
	callback replacement cannot invalidate this iterator. */
	std::optional<Widget::CallbackIter> colorCallback;
};

} }

#endif
