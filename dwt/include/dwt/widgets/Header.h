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

#ifndef DWT_HEADER_H_
#define DWT_HEADER_H_

#include "Control.h"

#include "../aspects/Collection.h"
#include "../aspects/CustomDraw.h"
#include "../aspects/Data.h"
#include "../resources/ImageList.h"

#include <vector>

namespace dwt {

/** Header control like the one used for Table */
class Header :
	public CommonControl,
	public aspects::Collection<Header, int>,
	public aspects::CustomDraw<Header, NMCUSTOMDRAW>,
	public aspects::Data<Header, int>
{
	typedef CommonControl BaseType;

	friend class WidgetCreator<Header>;
	friend class aspects::Collection<Header, int>;
	friend class aspects::Data<Header, int>;
public:
	/// Class type
	typedef Header ThisType;

	/// Object type
	typedef ThisType* ObjectType;

	/// Seed class
	/** This class contains all of the values needed to create the widget. It also
	  * knows the type of the class whose seed values it contains. Every widget
	  * should define one of these.
	  */
	struct Seed : public BaseType::Seed {
		typedef ThisType WidgetType;

		FontPtr font;

		/// Fills with default parameters
		Seed();
	};

	/// Actually creates the Header
	void create( const Seed & cs = Seed() );

	int insert(const tstring& header, int width, LPARAM lParam = 0, int after = -1);

	bool getItem(int index, HDITEM& item) const;
	bool setItem(int index, const HDITEM& item);
	tstring getText(int index) const;
	bool setText(int index, const tstring& text);
	int getWidth(int i) const;
	bool setWidth(int index, int width);
	int getFormat(int index) const;
	bool setFormat(int index, int format);
	bool addRemoveFormat(int index, int format, bool add);
	void setCheckBoxes(bool value = true);
	bool setItemCheckBox(int index, bool value = true);
	bool setItemChecked(int index, bool value = true);
	bool getItemChecked(int index) const;
	bool setItemDropDown(int index, bool value = true);
	bool setItemSplitButton(int index, bool value = true);
	bool setSortArrow(int index, int direction);
	void clearSortArrows();
	void setImageList(const ImageListPtr& images);
	HIMAGELIST getImageListHandle() const;
	ImageListPtr getImageList() const { return imageList; }
	ImageListPtr createDragImage(int index) const;
	int getBitmapMargin() const;
	int setBitmapMargin(int margin);
	bool getItemRect(int index, Rectangle& rect) const;
	bool getItemDropDownRect(int index, Rectangle& rect) const;
	bool getOverflowRect(Rectangle& rect) const;
	int hitTest(HDHITTESTINFO& info) const;
	std::vector<int> getOrder() const;
	bool setOrder(const std::vector<int>& order);
	int orderToIndex(int order) const;
	int getFocusedItem() const;
	bool setFocusedItem(int index);
	int setHotDividerIndex(int divider);
	int setHotDividerPosition(int position);
	void setFilterBar(bool value = true);
	void setFilterChangeTimeout(unsigned milliseconds);
	bool editFilter(int index, bool discardChanges = false);
	bool clearFilter(int index);
	bool clearAllFilters();
	bool getUnicodeFormat() const;
	bool setUnicodeFormat(bool unicode);
	void onItemClicked(std::function<void (const NMHEADER&)> f);
	void onItemDoubleClicked(std::function<void (const NMHEADER&)> f);
	void onDividerDoubleClicked(std::function<void (const NMHEADER&)> f);
	void onTrack(std::function<void (const NMHEADER&)> f);
	void onEndTrack(std::function<void (const NMHEADER&)> f);
	void onEndDrag(std::function<void (const NMHEADER&)> f);
	void onFilterChanged(std::function<void (const NMHEADER&)> f);
	void onFilterButtonClicked(std::function<void (const NMHDFILTERBTNCLICK&)> f);
	void onDropDown(std::function<void (const NMHEADER&)> f);
	void onOverflowClicked(std::function<void (const NMHEADER&)> f);
	/** Return true to allow tracking or false to veto it. */
	void onBeginTrack(std::function<bool (const NMHEADER&)> f);
	/** Return true to allow dragging or false to veto it. */
	void onBeginDrag(std::function<bool (const NMHEADER&)> f);

	virtual Point getPreferredSize();
	virtual bool handleMessage(const MSG& msg, LRESULT& retVal);

protected:
	/// Constructor Taking pointer to parent
	explicit Header( Widget * parent );

	// Protected to avoid direct instantiation, you can inherit and use
	// WidgetFactory class which is friend
	virtual ~Header()
	{}

private:
	friend class ChainingDispatcher;
	static const TCHAR windowClass[];
	ImageListPtr imageList;

	void applyFilterStyles(bool clearColors = false);
	void setFontImpl();
	virtual void setColorImpl(COLORREF text, COLORREF background);
	virtual void clearColorImpl();

	// aspects::Collection
	void eraseImpl(int row);
	void clearImpl();
	size_t sizeImpl() const;

	// aspects::Data
	int findDataImpl(LPARAM data, int start = -1);
	LPARAM getDataImpl(int idx);
	void setDataImpl(int i, LPARAM data);

};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Implementation of class
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline Header::Header( Widget * parent )
	: BaseType(parent, ChainingDispatcher::superClass<Header>())
{
}

inline void Header::eraseImpl( int row ) {
	Header_DeleteItem(handle(), row);
}

inline size_t Header::sizeImpl() const {
	return Header_GetItemCount(handle());
}

inline void Header::clearImpl() {
	for(size_t i = 0, iend = size(); i < iend; ++i) {
		erase(0);
	}
}

inline bool Header::getItem(int index, HDITEM& item) const {
	return Header_GetItem(handle(), index, &item) == TRUE;
}

inline bool Header::setItem(int index, const HDITEM& item) {
	auto value = item;
	return Header_SetItem(handle(), index, &value) == TRUE;
}

inline bool Header::setWidth(int index, int width) {
	HDITEM item = { HDI_WIDTH };
	item.cxy = width;
	return setItem(index, item);
}

inline bool Header::setText(int index, const tstring& text) {
	HDITEM item = { HDI_TEXT };
	item.pszText = const_cast<LPTSTR>(text.c_str());
	return setItem(index, item);
}

inline bool Header::setFormat(int index, int format) {
	HDITEM item = { HDI_FORMAT };
	item.fmt = format;
	return setItem(index, item);
}

inline bool Header::addRemoveFormat(int index, int format, bool add) {
	HDITEM item = { HDI_FORMAT };
	if(!getItem(index, item)) {
		return false;
	}
	if(add) {
		item.fmt |= format;
	} else {
		item.fmt &= ~format;
	}
	return setItem(index, item);
}

inline void Header::setCheckBoxes(bool value) {
	addRemoveStyle(HDS_CHECKBOXES, value);
}

inline bool Header::setItemCheckBox(int index, bool value) {
	return addRemoveFormat(index, HDF_CHECKBOX, value);
}

inline bool Header::setItemChecked(int index, bool value) {
	return setItemCheckBox(index, true) && addRemoveFormat(index, HDF_CHECKED, value);
}

inline bool Header::getItemChecked(int index) const {
	HDITEM item = { HDI_FORMAT };
	return getItem(index, item) && (item.fmt & HDF_CHECKED) == HDF_CHECKED;
}

inline bool Header::setItemDropDown(int index, bool value) {
	return setItemSplitButton(index, value);
}

inline bool Header::setItemSplitButton(int index, bool value) {
	return addRemoveFormat(index, HDF_SPLITBUTTON, value);
}

inline bool Header::setSortArrow(int index, int direction) {
	HDITEM item = { HDI_FORMAT };
	if(!getItem(index, item)) {
		return false;
	}
	item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
	if(direction < 0) {
		item.fmt |= HDF_SORTDOWN;
	} else if(direction > 0) {
		item.fmt |= HDF_SORTUP;
	}
	return setItem(index, item);
}

inline void Header::setImageList(const ImageListPtr& images) {
	imageList = images;
	Header_SetImageList(handle(), imageList ? imageList->handle() : nullptr);
}

inline HIMAGELIST Header::getImageListHandle() const {
	return Header_GetImageList(handle());
}

inline ImageListPtr Header::createDragImage(int index) const {
	HIMAGELIST image = Header_CreateDragImage(handle(), index);
	return image ? ImageListPtr(new ImageList(image, true)) : ImageListPtr();
}

inline int Header::getBitmapMargin() const {
	return static_cast<int>(sendMessage(HDM_GETBITMAPMARGIN));
}

inline int Header::setBitmapMargin(int margin) {
	return static_cast<int>(sendMessage(HDM_SETBITMAPMARGIN, margin));
}

inline bool Header::getItemRect(int index, Rectangle& rect) const {
	RECT value = { 0 };
	if(!Header_GetItemRect(handle(), index, &value)) {
		return false;
	}
	rect = Rectangle(value);
	return true;
}

inline bool Header::getItemDropDownRect(int index, Rectangle& rect) const {
	RECT value = { 0 };
	if(!sendMessage(HDM_GETITEMDROPDOWNRECT, index,
		reinterpret_cast<LPARAM>(&value))) {
		return false;
	}
	rect = Rectangle(value);
	return true;
}

inline bool Header::getOverflowRect(Rectangle& rect) const {
	RECT value = { 0 };
	if(!sendMessage(HDM_GETOVERFLOWRECT, 0, reinterpret_cast<LPARAM>(&value))) {
		return false;
	}
	rect = Rectangle(value);
	return true;
}

inline int Header::hitTest(HDHITTESTINFO& info) const {
	return static_cast<int>(sendMessage(HDM_HITTEST, 0,
		reinterpret_cast<LPARAM>(&info)));
}

inline int Header::orderToIndex(int order) const {
	return Header_OrderToIndex(handle(), order);
}

inline int Header::getFocusedItem() const {
	return static_cast<int>(sendMessage(HDM_GETFOCUSEDITEM));
}

inline bool Header::setFocusedItem(int index) {
	return sendMessage(HDM_SETFOCUSEDITEM, 0, index) != FALSE;
}

inline int Header::setHotDividerIndex(int divider) {
	return static_cast<int>(sendMessage(HDM_SETHOTDIVIDER, FALSE, divider));
}

inline int Header::setHotDividerPosition(int position) {
	return static_cast<int>(sendMessage(HDM_SETHOTDIVIDER, TRUE, position));
}

inline void Header::setFilterChangeTimeout(unsigned milliseconds) {
	sendMessage(HDM_SETFILTERCHANGETIMEOUT, 0, milliseconds);
}

inline bool Header::editFilter(int index, bool discardChanges) {
	auto result = sendMessage(HDM_EDITFILTER, index, discardChanges ? TRUE : FALSE) != FALSE;
	applyFilterStyles();
	return result;
}

inline bool Header::clearFilter(int index) {
	return sendMessage(HDM_CLEARFILTER, index) != FALSE;
}

inline bool Header::clearAllFilters() {
	return sendMessage(HDM_CLEARFILTER, static_cast<WPARAM>(-1)) != FALSE;
}

inline bool Header::getUnicodeFormat() const {
	return sendMessage(HDM_GETUNICODEFORMAT) != 0;
}

inline bool Header::setUnicodeFormat(bool unicode) {
	return sendMessage(HDM_SETUNICODEFORMAT, unicode ? TRUE : FALSE) != 0;
}

}

#endif /* HEADER_H_ */
