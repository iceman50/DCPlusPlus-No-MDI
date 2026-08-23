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

#ifndef DWT_Slider_h
#define DWT_Slider_h

#include "../aspects/CustomDraw.h"
#include "../aspects/Scrollable.h"
#include "Control.h"

#include <algorithm>
#include <vector>

namespace dwt {

/// Slider Control class
/** \ingroup WidgetControls
  * \WidgetUsageInfo
  * \image html slider.PNG
  * Class for creating a Slider control Widget. <br>
  * A Slider is a Widget which can be used to give e.g. percentage input. For
  * instance a volume control of a stereo system often has several of these ones, one
  * each for the volume, bass and tone. It contains of a thumbtrack and an axis which
  * you can move the thumbtrack up and down, it is quite similar in functionality to
  * the Spinner control, but have another visual appearance.
  */
class Slider :
	public CommonControl,
	// aspects::s
	public aspects::CustomDraw<Slider, NMCUSTOMDRAW>,
	public aspects::Scrollable< Slider >
{
	typedef CommonControl BaseType;
	friend class WidgetCreator< Slider >;

public:
	/// Class type
	typedef Slider ThisType;

	/// Object type
	typedef ThisType* ObjectType;

	/// Seed class
	/** This class contains all of the values needed to create the widget. It also
	  * knows the type of the class whose seed values it contains. Every widget
	  * should define one of these.
	  */
	struct Seed : public BaseType::Seed {
		typedef ThisType WidgetType;

		/// Fills with default parameters
		Seed();
	};

	/// Sets the Auto Ticks property of the control
	/** Auto ticks means that the control will have a tick note for each increment in
	  * its range of values.
	  */
	void setAutoTicks( bool value = true );

	/// Sets the horizontal property of the control
	/** If you want the slider to be horizontally aligned (default) then call this
	  * function with true, if you call it with false the slider will show up
	  * vertically instead.
	  */
	void setHorizontal( bool value = true );

	/// Sets the placement of the ticks
	/** If true is passed then the ticks of the slider will be displayed to the left
	  * of the slider, else ticks will show up to the right. Only call this function
	  * if you have called the setHorizontal with false.
	  */
	void setShowTicksLeft( bool value = true );

	/// Sets the placement of the ticks
	/** Sets the placement of the ticks to either top (true) or bottom (false). Only
	  * call this function if you have called the setHorizontal with true
	  */
	void setShowTicksTop( bool value = true );

	/// Sets the placement of the ticks to be BOTH (above/bottom or right/left)
	/** If you call this function with true it will show ticks on BOTH sides of the
	  * control depending on if the slider is aligned horizontally or vertically the
	  * ticks will show up either above and below or to the right and the left of the
	  * slider.
	  */
	void setShowTicksBoth( bool value = true );

	/// Removes ticks
	/** If you call this function with true the ticks will be REMOVED from the
	  * control. If you call it with false the ticks will appear again in its former
	  * positions
	  */
	void setShowTicks( bool value = true );

	/// Sets the range of the slider
	/** The range is the unique values of the control, use this function to set the
	  * range of the control.
	  */
	void setRange( int minimum, int maximum, bool redraw = true );
	void setMinValue(int minimum, bool redraw = true);
	void setMaxValue(int maximum, bool redraw = true);

	/// Retrieves the maximum position of the Slider
	/** The return value from this function is the maximum value of the Slider
	  */
	int getMaxValue();

	/// Retrieves the minimum position of the Slider
	/** The return value from this function is the minimum value of the Slider
	  */
	int getMinValue();

	/// Sets the position of the thumb
	/** This is the "value" of the control, pass in the new position/value and the
	  * control will set the thumb to that position.
	  */
	void setPosition( int newPosition );
	void setSelection(int start, int end, bool redraw = true);
	void clearSelection(bool redraw = true);
	int getSelectionStart() const;
	int getSelectionEnd() const;
	int setLineSize(int size);
	int getLineSize() const;
	int setPageSize(int size);
	int getPageSize() const;
	void setThumbLength(int length);
	int getThumbLength() const;
	Rectangle getChannelRect() const;
	Rectangle getThumbRect() const;

	/// Sets tick frequency
	/** Sets the frequency of the ticks, e.g. if five is given every fifth value of
	  * the slider will have a tick, default value is one.
	  */
	void setTickFrequency( unsigned frequency );
	bool setTick(int position);
	void clearTicks(bool redraw = true);
	int getTickCount() const;
	int getTick(int index) const;
	std::vector<int> getTicks() const;
	int getTickPosition(int index) const;
	std::vector<int> getTickPositions() const;

	/// Returns the "value" of the Slider
	/** Returns the "value" of the Slider or the position of the thumb. If you for
	  * instance have defined the minimum/maximum value to be -10 and 10 and the
	  * thumb is in the 3/4 of the max position this function will return 5.
	  */
	int getPosition();
	HWND getToolTip() const;
	void setToolTip(HWND toolTip);
	int setToolTipPosition(int position);
	HWND getBuddy(bool beginning) const;
	bool getUnicodeFormat() const;
	bool setUnicodeFormat(bool unicode);
	void setReversed(bool value = true);
	void setDownIsLeft(bool value = true);
	void setTransparentBackground(bool value = true);
	void setNotifyBeforeMove(bool value = true);
	void setSelectionRangeVisible(bool value = true);
	void setFixedThumbLength(bool value = true);

	/** Called before the thumb position changes when TBS_NOTIFYBEFOREMOVE is enabled.
	 * Return true to allow the move or false to veto it.
	 */
	void onThumbPositionChanging(std::function<bool (const NMTRBTHUMBPOSCHANGING&)> f);

	/// Assigns a buddy control
	/** A "buddy control" may be of any type but most often a TextBox or a
	  * Label is used. <br>
	  * It is normally used for displaying "maximum" and "minimum" text labels. <br>
	  * The buddy control will be positioned either to the left or right of a
	  * vertical Slider or the top or bottom of a horizontal Slider depending on the
	  * boolean value of the first argument. If the first argument is true, the buddy
	  * control will be placed to the left of a vertical Slider or above a horizontal
	  * Slider. <br>
	  * The buddy will be repositioned according to the position of the associated
	  * slider, BUT you MUST set the size of it. Don't burn braincells over the
	  * position setting. Only the SIZE matters. <br>
	  * A Slider may have TWO buddy controls, one with first argument set to true and
	  * one set to false. <br>
	  * If you assign another buddy control with the same first argument, the earlier
	  * instance will no longer be a buddy control, but it will still be there, it
	  * will not cease to exist or be destroyed. See the Slider sample
	  * application for an example of the buddy control in use.
	  */
	void assignBuddy( bool beginning, Widget * buddy );

	virtual bool handleMessage(const MSG& msg, LRESULT& retVal);

protected:
	// Constructor Taking pointer to parent
	explicit Slider( Widget * parent );

	virtual void appearanceChanged();

	// Protected to avoid direct instantiation, you can inherit and use
	// WidgetFactory class which is friend
	virtual ~Slider() {}

private:
	friend class ChainingDispatcher;
	static const TCHAR windowClass[];

	void handlePainting(NMCUSTOMDRAW& data);
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation of class
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline void Slider::setAutoTicks( bool value )
{
	this->addRemoveStyle( TBS_AUTOTICKS, value );
}

inline void Slider::setHorizontal( bool value )
{
	this->addRemoveStyle( TBS_HORZ, value );
	this->addRemoveStyle( TBS_VERT, !value );
}

inline void Slider::setShowTicksLeft( bool value )
{
	// TODO: Add assertion that the TBS_VERT is set!
	this->addRemoveStyle( TBS_LEFT, value );
	this->addRemoveStyle( TBS_RIGHT, !value );
}

inline void Slider::setShowTicksTop( bool value )
{
	// TODO: Add assertion that the TBS_HORZ is set!
	this->addRemoveStyle( TBS_TOP, value );
	this->addRemoveStyle( TBS_BOTTOM, !value );
}

inline void Slider::setShowTicksBoth( bool value )
{
	this->addRemoveStyle( TBS_BOTH, value );
}

inline void Slider::setShowTicks( bool value )
{
	this->addRemoveStyle( TBS_NOTICKS, !value );
}

inline void Slider::setRange( int minimum, int maximum, bool redraw )
{
	if(minimum > maximum) {
		std::swap(minimum, maximum);
	}
	setMinValue(minimum, false);
	setMaxValue(maximum, redraw);
}

inline void Slider::setMinValue(int minimum, bool redraw) {
	this->sendMessage(TBM_SETRANGEMIN, redraw ? TRUE : FALSE,
		static_cast<LPARAM>(minimum));
}

inline void Slider::setMaxValue(int maximum, bool redraw) {
	this->sendMessage(TBM_SETRANGEMAX, redraw ? TRUE : FALSE,
		static_cast<LPARAM>(maximum));
}

inline int Slider::getMaxValue()
{
	return ( int )this->sendMessage(TBM_GETRANGEMAX);
}

inline int Slider::getMinValue()
{
	return ( int )this->sendMessage(TBM_GETRANGEMIN);
}

inline void Slider::setPosition( int newPosition )
{
	this->sendMessage( TBM_SETPOS, static_cast< WPARAM >( TRUE ), static_cast< LPARAM >( newPosition ) );
}

inline void Slider::setSelection(int start, int end, bool redraw) {
	if(start > end) {
		std::swap(start, end);
	}
	this->sendMessage(TBM_SETSELSTART, FALSE, start);
	this->sendMessage(TBM_SETSELEND, redraw ? TRUE : FALSE, end);
}

inline void Slider::clearSelection(bool redraw) {
	this->sendMessage(TBM_CLEARSEL, redraw ? TRUE : FALSE);
}

inline int Slider::getSelectionStart() const {
	return static_cast<int>(this->sendMessage(TBM_GETSELSTART));
}

inline int Slider::getSelectionEnd() const {
	return static_cast<int>(this->sendMessage(TBM_GETSELEND));
}

inline int Slider::setLineSize(int size) {
	return static_cast<int>(this->sendMessage(TBM_SETLINESIZE, 0, size));
}

inline int Slider::getLineSize() const {
	return static_cast<int>(this->sendMessage(TBM_GETLINESIZE));
}

inline int Slider::setPageSize(int size) {
	return static_cast<int>(this->sendMessage(TBM_SETPAGESIZE, 0, size));
}

inline int Slider::getPageSize() const {
	return static_cast<int>(this->sendMessage(TBM_GETPAGESIZE));
}

inline void Slider::setThumbLength(int length) {
	this->sendMessage(TBM_SETTHUMBLENGTH, length);
}

inline int Slider::getThumbLength() const {
	return static_cast<int>(this->sendMessage(TBM_GETTHUMBLENGTH));
}

inline Rectangle Slider::getChannelRect() const {
	RECT rect = { 0 };
	this->sendMessage(TBM_GETCHANNELRECT, 0, reinterpret_cast<LPARAM>(&rect));
	return Rectangle(rect);
}

inline Rectangle Slider::getThumbRect() const {
	RECT rect = { 0 };
	this->sendMessage(TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&rect));
	return Rectangle(rect);
}

inline void Slider::setTickFrequency( unsigned frequency )
{
	this->sendMessage( TBM_SETTICFREQ, static_cast< WPARAM >( frequency ));
}

inline bool Slider::setTick(int position) {
	return this->sendMessage(TBM_SETTIC, 0, position) == TRUE;
}

inline void Slider::clearTicks(bool redraw) {
	this->sendMessage(TBM_CLEARTICS, redraw ? TRUE : FALSE);
}

inline int Slider::getTickCount() const {
	return static_cast<int>(this->sendMessage(TBM_GETNUMTICS));
}

inline int Slider::getTick(int index) const {
	return static_cast<int>(this->sendMessage(TBM_GETTIC, index));
}

inline std::vector<int> Slider::getTicks() const {
	std::vector<int> ticks;
	auto count = getTickCount();
	if(count <= 0) {
		return ticks;
	}
	ticks.reserve(static_cast<size_t>(count));
	for(int i = 0; i < count; ++i) {
		auto tick = getTick(i);
		if(tick >= 0) {
			ticks.push_back(tick);
		}
	}
	return ticks;
}

inline int Slider::getTickPosition(int index) const {
	return static_cast<int>(this->sendMessage(TBM_GETTICPOS, index));
}

inline std::vector<int> Slider::getTickPositions() const {
	std::vector<int> positions;
	auto count = getTickCount();
	if(count <= 0) {
		return positions;
	}
	positions.reserve(static_cast<size_t>(count));
	for(int i = 0; i < count; ++i) {
		auto position = getTickPosition(i);
		if(position >= 0) {
			positions.push_back(position);
		}
	}
	return positions;
}

inline int Slider::getPosition()
{
	return static_cast<int>(this->sendMessage( TBM_GETPOS ));
}

inline HWND Slider::getToolTip() const {
	return reinterpret_cast<HWND>(this->sendMessage(TBM_GETTOOLTIPS));
}

inline void Slider::setToolTip(HWND toolTip) {
	this->sendMessage(TBM_SETTOOLTIPS, reinterpret_cast<WPARAM>(toolTip));
}

inline int Slider::setToolTipPosition(int position) {
	return static_cast<int>(this->sendMessage(TBM_SETTIPSIDE, position));
}

inline HWND Slider::getBuddy(bool beginning) const {
	return reinterpret_cast<HWND>(this->sendMessage(TBM_GETBUDDY,
		beginning ? TRUE : FALSE));
}

inline bool Slider::getUnicodeFormat() const {
	return this->sendMessage(TBM_GETUNICODEFORMAT) != 0;
}

inline bool Slider::setUnicodeFormat(bool unicode) {
	return this->sendMessage(TBM_SETUNICODEFORMAT, unicode ? TRUE : FALSE) != 0;
}

inline void Slider::setReversed(bool value) {
	this->addRemoveStyle(TBS_REVERSED, value);
}

inline void Slider::setDownIsLeft(bool value) {
	this->addRemoveStyle(TBS_DOWNISLEFT, value);
}

inline void Slider::setTransparentBackground(bool value) {
	this->addRemoveStyle(TBS_TRANSPARENTBKGND, value);
}

inline void Slider::setNotifyBeforeMove(bool value) {
	this->addRemoveStyle(TBS_NOTIFYBEFOREMOVE, value);
}

inline void Slider::setSelectionRangeVisible(bool value) {
	this->addRemoveStyle(TBS_ENABLESELRANGE, value);
}

inline void Slider::setFixedThumbLength(bool value) {
	this->addRemoveStyle(TBS_FIXEDLENGTH, value);
}

inline void Slider::onThumbPositionChanging(
	std::function<bool (const NMTRBTHUMBPOSCHANGING&)> f)
{
	addCallback(Message(WM_NOTIFY, TRBN_THUMBPOSCHANGING),
		[f](const MSG& msg, LRESULT& result) -> bool {
			auto data = reinterpret_cast<NMTRBTHUMBPOSCHANGING*>(msg.lParam);
			if(!data) {
				return false;
			}
			result = f(*data) ? FALSE : TRUE;
			return true;
		});
}

inline void Slider::assignBuddy( bool beginning, Widget * buddy )
{
	this->sendMessage( TBM_SETBUDDY, static_cast< WPARAM >( beginning ? TRUE : FALSE ),
		reinterpret_cast< LPARAM >( buddy ? buddy->handle() : nullptr ) );
}

inline Slider::Slider( dwt::Widget * parent )
	: BaseType(parent, ChainingDispatcher::superClass<Slider>())
{
}

}

#endif
