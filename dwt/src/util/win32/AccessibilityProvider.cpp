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

#include <dwt/util/win32/AccessibilityProvider.h>

#include <dwt/Widget.h>

#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace dwt {

namespace {

static const PROPERTYID controlTypeProperty = 30003;
static const PROPERTYID nameProperty = 30005;
static const PROPERTYID hasKeyboardFocusProperty = 30008;
static const PROPERTYID isKeyboardFocusableProperty = 30009;
static const PROPERTYID isEnabledProperty = 30010;
static const PROPERTYID helpTextProperty = 30013;
static const PROPERTYID isControlElementProperty = 30016;
static const PROPERTYID isContentElementProperty = 30017;
static const PROPERTYID nativeWindowHandleProperty = 30020;
static const PROPERTYID isOffscreenProperty = 30022;

static const PATTERNID rangeValuePattern = 10003;
static const PATTERNID scrollPattern = 10004;
static const PATTERNID expandCollapsePattern = 10005;
static const PATTERNID invokePattern = 10000;
static const PATTERNID selectionPattern = 10001;
static const PATTERNID selectionItemPattern = 10010;
static const double noScroll = -1.0;

template<typename T>
T getAutomationProc(const char* name) {
	static HMODULE module = ::LoadLibrary(_T("uiautomationcore.dll"));
	T function = nullptr;
	auto address = module ? ::GetProcAddress(module, name) : nullptr;
	static_assert(sizeof(function) == sizeof(address), "Function pointer size mismatch");
	std::memcpy(&function, &address, sizeof(function));
	return function;
}

void setStringVariant(VARIANT* value, const tstring& text) {
	value->vt = VT_BSTR;
	value->bstrVal = ::SysAllocString(text.c_str());
}

void setBoolVariant(VARIANT* value, bool state) {
	value->vt = VT_BOOL;
	value->boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
}

HRESULT hostProvider(HWND window, IRawElementProviderSimple** value) {
	if(!value) {
		return E_POINTER;
	}
	*value = nullptr;
	typedef HRESULT (WINAPI *Function)(HWND, IRawElementProviderSimple**);
	auto function = getAutomationProc<Function>("UiaHostProviderFromHwnd");
	return function ? function(window, value) : E_NOTIMPL;
}

HRESULT hostFragment(HWND window, IRawElementProviderFragment** value) {
	if(!value) {
		return E_POINTER;
	}
	*value = nullptr;
	IRawElementProviderSimple* simple = nullptr;
	auto result = hostProvider(window, &simple);
	if(SUCCEEDED(result) && simple) {
		result = simple->QueryInterface(IID_IRawElementProviderFragment,
			reinterpret_cast<void**>(value));
		simple->Release();
	}
	return result;
}

}

class AccessibilityProvider :
	public IRawElementProviderSimple,
	public IRawElementProviderFragment,
	public IRawElementProviderFragmentRoot,
	public IRangeValueProvider,
	public IScrollProvider,
	public ISelectionProvider,
	public ISelectionItemProvider,
	public IExpandCollapseProvider,
	public IInvokeProvider
{
public:
	explicit AccessibilityProvider(Widget* widget_) :
		refs(1), widget(widget_), owner(nullptr), item(0) { }
	virtual ~AccessibilityProvider() {
		if(owner) {
			owner->Release();
		}
	}

	void detach() {
		widget = nullptr;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
		if(!object) {
			return E_POINTER;
		}
		if(::IsEqualIID(iid, IID_IUnknown) ||
			::IsEqualIID(iid, IID_IRawElementProviderSimple)) {
			*object = static_cast<IRawElementProviderSimple*>(this);
		} else if(::IsEqualIID(iid, IID_IRawElementProviderFragment)) {
			*object = static_cast<IRawElementProviderFragment*>(this);
		} else if(::IsEqualIID(iid, IID_IRawElementProviderFragmentRoot) &&
			isFragmentRoot()) {
			*object = static_cast<IRawElementProviderFragmentRoot*>(this);
		} else if(::IsEqualIID(iid, IID_IRangeValueProvider) &&
			!isItem() && widget && widget->getAccessibleRangeValue()) {
			*object = static_cast<IRangeValueProvider*>(this);
		} else if(::IsEqualIID(iid, IID_IScrollProvider) &&
			!isItem() && widget && widget->getAccessibleScroll()) {
			*object = static_cast<IScrollProvider*>(this);
		} else if(::IsEqualIID(iid, IID_ISelectionProvider) &&
			!isItem() && itemProvider() && itemProvider()->selection) {
			*object = static_cast<ISelectionProvider*>(this);
		} else if(::IsEqualIID(iid, IID_ISelectionItemProvider) &&
			isItem() && itemProvider() && itemProvider()->selected &&
			itemProvider()->select) {
			*object = static_cast<ISelectionItemProvider*>(this);
		} else if(::IsEqualIID(iid, IID_IExpandCollapseProvider) &&
			isItem() && itemProvider() && itemProvider()->expandState) {
			*object = static_cast<IExpandCollapseProvider*>(this);
		} else if(::IsEqualIID(iid, IID_IInvokeProvider) &&
			isItem() && itemProvider() && itemProvider()->invoke) {
			*object = static_cast<IInvokeProvider*>(this);
		} else {
			*object = nullptr;
			return E_NOINTERFACE;
		}
		AddRef();
		return S_OK;
	}

	ULONG STDMETHODCALLTYPE AddRef() override {
		return ++refs;
	}

	ULONG STDMETHODCALLTYPE Release() override {
		auto value = --refs;
		if(!value) {
			delete this;
		}
		return value;
	}

	HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
		if(!value) {
			return E_POINTER;
		}
		*value = ProviderOptions_ServerSideProvider;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
		IUnknown** value) override
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(pattern == rangeValuePattern && !isItem() &&
			widget->getAccessibleRangeValue()) {
			*value = static_cast<IRangeValueProvider*>(this);
		} else if(pattern == scrollPattern && !isItem() &&
			widget->getAccessibleScroll()) {
			*value = static_cast<IScrollProvider*>(this);
		} else if(pattern == selectionPattern && !isItem() &&
			itemProvider() && itemProvider()->selection) {
			*value = static_cast<ISelectionProvider*>(this);
		} else if(pattern == selectionItemPattern && isItem() &&
			itemProvider() && itemProvider()->selected && itemProvider()->select) {
			*value = static_cast<ISelectionItemProvider*>(this);
		} else if(pattern == expandCollapsePattern && isItem() &&
			itemProvider() && itemProvider()->expandState) {
			*value = static_cast<IExpandCollapseProvider*>(this);
		} else if(pattern == invokePattern && isItem() &&
			itemProvider() && itemProvider()->invoke) {
			*value = static_cast<IInvokeProvider*>(this);
		}
		if(*value) {
			AddRef();
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,
		VARIANT* value) override
	{
		if(!value) {
			return E_POINTER;
		}
		::VariantInit(value);
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}

		switch(property) {
		case nameProperty:
			setStringVariant(value, isItem() && itemProvider()->name ?
				itemProvider()->name(item) : widget->getAccessibleName());
			break;
		case controlTypeProperty:
			value->vt = VT_I4;
			value->lVal = isItem() && itemProvider()->controlType ?
				itemProvider()->controlType(item) :
				widget->getAccessibleControlType();
			break;
		case hasKeyboardFocusProperty:
			setBoolVariant(value, isItem() ? itemHasFocus() :
				::GetFocus() == widget->handle());
			break;
		case isKeyboardFocusableProperty:
			setBoolVariant(value, isItem() ?
				static_cast<bool>(itemProvider()->setFocus) :
				widget->getAccessibleKeyboardFocusable());
			break;
		case isEnabledProperty:
			setBoolVariant(value, widget->getEnabled());
			break;
		case helpTextProperty:
			setStringVariant(value, widget->getAccessibleHelpText());
			break;
		case isControlElementProperty:
		case isContentElementProperty:
			setBoolVariant(value, true);
			break;
		case nativeWindowHandleProperty:
			if(!isItem()) {
				value->vt = VT_I4;
				value->lVal = static_cast<LONG>(
					reinterpret_cast<LONG_PTR>(widget->handle()));
			}
			break;
		case isOffscreenProperty:
			setBoolVariant(value, !::IsWindowVisible(widget->handle()) ||
				(isItem() && (itemBounds().width() <= 0 ||
					itemBounds().height() <= 0)));
			break;
		default:
			break;
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
		IRawElementProviderSimple** value) override
	{
		if(!available()) {
			if(value) {
				*value = nullptr;
			}
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(isItem()) {
			if(value) {
				*value = nullptr;
			}
			return S_OK;
		}
		return hostProvider(widget->handle(), value);
	}

	HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
		IRawElementProviderFragment** value) override
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}

		switch(direction) {
		case NavigateDirection_Parent:
			if(isItem()) {
				auto parent = itemProvider()->parent ?
					itemProvider()->parent(item) : 0;
				return parent ? itemFragment(parent, value) : rootFragment(value);
			}
			if(isFragmentRoot()) {
				return S_OK;
			}
			return fragmentForWindow(::GetParent(widget->handle()), value);

		case NavigateDirection_FirstChild:
			if(itemProvider()) {
				auto children = itemProvider()->children ?
					itemProvider()->children(item) :
					std::vector<accessibility::ItemId>();
				if(!children.empty()) {
					return itemFragment(children.front(), value);
				}
				if(isItem()) {
					return S_OK;
				}
			}
			return childFragment(false, value);

		case NavigateDirection_LastChild:
			if(itemProvider()) {
				auto children = itemProvider()->children ?
					itemProvider()->children(item) :
					std::vector<accessibility::ItemId>();
				if(!children.empty()) {
					return itemFragment(children.back(), value);
				}
				if(isItem()) {
					return S_OK;
				}
			}
			return childFragment(true, value);

		case NavigateDirection_NextSibling:
			if(isItem()) {
				return itemSibling(false, value);
			}
			return siblingFragment(false, value);

		case NavigateDirection_PreviousSibling:
			if(isItem()) {
				return itemSibling(true, value);
			}
			return siblingFragment(true, value);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		auto array = ::SafeArrayCreateVector(VT_I4, 0, isItem() ? 4 : 2);
		if(!array) {
			return E_OUTOFMEMORY;
		}
		LONG index = 0;
		int part = UiaAppendRuntimeId;
		::SafeArrayPutElement(array, &index, &part);
		index = 1;
		part = static_cast<int>(reinterpret_cast<INT_PTR>(widget->handle()));
		::SafeArrayPutElement(array, &index, &part);
		if(isItem()) {
			index = 2;
			part = static_cast<int>(item & 0xffffffffu);
			::SafeArrayPutElement(array, &index, &part);
			index = 3;
			part = static_cast<int>(
				static_cast<std::uint64_t>(item) >> 32);
			::SafeArrayPutElement(array, &index, &part);
		}
		*value = array;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
		if(!value) {
			return E_POINTER;
		}
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		RECT rect = isItem() ? static_cast<RECT>(itemBounds()) : RECT { 0 };
		if(isItem()) {
			::MapWindowPoints(widget->handle(), nullptr,
				reinterpret_cast<POINT*>(&rect), 2);
		} else {
			::GetWindowRect(widget->handle(), &rect);
		}
		value->left = rect.left;
		value->top = rect.top;
		value->width = rect.right - rect.left;
		value->height = rect.bottom - rect.top;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetFocus() override {
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(isItem()) {
			if(!itemProvider()->setFocus) {
				return UIA_E_NOTSUPPORTED;
			}
			itemProvider()->setFocus(item);
			return itemHasFocus() ? S_OK : E_FAIL;
		}
		::SetFocus(widget->handle());
		return ::GetFocus() == widget->handle() ? S_OK : E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE get_FragmentRoot(
		IRawElementProviderFragmentRoot** value) override
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		auto root = isItem() ? owner->fragmentRoot() : fragmentRoot();
		return root ? root->QueryInterface(IID_IRawElementProviderFragmentRoot,
			reinterpret_cast<void**>(value)) : UIA_E_ELEMENTNOTAVAILABLE;
	}

	HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
		IRawElementProviderFragment** value) override
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		POINT point = { static_cast<LONG>(x), static_cast<LONG>(y) };
		if(itemProvider()) {
			auto found = itemFromPoint(0, point);
			if(found) {
				return itemFragment(found, value);
			}
		}
		auto target = ::WindowFromPoint(point);
		if(target && (target == widget->handle() ||
			::IsChild(widget->handle(), target))) {
			for(auto ancestor = target; ancestor;
				ancestor = ::GetParent(ancestor)) {
				auto candidate = hwnd_cast<Widget*>(ancestor);
				if(candidate && candidate->accessibilityProvider &&
					candidate->getAccessibleItems()) {
					auto provider = candidate->accessibilityProvider;
					auto found = provider->itemFromPoint(0, point);
					if(found) {
						return provider->itemFragment(found, value);
					}
				}
				if(ancestor == widget->handle()) {
					break;
				}
			}
			while(target && target != widget->handle()) {
				auto candidate = hwnd_cast<Widget*>(target);
				if(candidate && candidate->accessibilityProvider) {
					auto provider = candidate->accessibilityProvider;
					auto found = provider->itemFromPoint(0, point);
					if(found) {
						return provider->itemFragment(found, value);
					}
				}
				auto result = fragmentForWindow(target, value);
				if(SUCCEEDED(result) && *value) {
					return S_OK;
				}
				target = ::GetParent(target);
			}
		}
		*value = static_cast<IRawElementProviderFragment*>(this);
		AddRef();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetFocus(
		IRawElementProviderFragment** value) override
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!available()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		auto focus = ::GetFocus();
		if(itemProvider() && itemProvider()->focused &&
			(focus == widget->handle() || ::IsChild(widget->handle(), focus))) {
			auto focused = itemProvider()->focused();
			if(focused) {
				return itemFragment(focused, value);
			}
		}
		for(auto target = focus; target &&
			(target == widget->handle() || ::IsChild(widget->handle(), target));
			target = ::GetParent(target)) {
			auto candidate = hwnd_cast<Widget*>(target);
			if(!candidate || !candidate->accessibilityProvider) {
				continue;
			}
			auto provider = candidate->accessibilityProvider;
			auto items = provider->itemProvider();
			if(items && items->focused) {
				auto focused = items->focused();
				if(focused) {
					return provider->itemFragment(focused, value);
				}
			}
		}
		if(focus && (focus == widget->handle() ||
			::IsChild(widget->handle(), focus))) {
			return fragmentForWindow(focus, value);
		}
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetValue(double value) override {
		auto range = available() ? widget->getAccessibleRangeValue() : nullptr;
		if(!range) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(range->readOnly || !range->setValue) {
			return UIA_E_NOTSUPPORTED;
		}
		if(value < range->minimum || value > range->maximum) {
			return E_INVALIDARG;
		}
		range->setValue(value);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_Value(double* value) override {
		auto range = available() ? widget->getAccessibleRangeValue() : nullptr;
		if(!value) {
			return E_POINTER;
		}
		if(!range || !range->getValue) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = range->getValue();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* value) override {
		auto range = available() ? widget->getAccessibleRangeValue() : nullptr;
		if(!value) {
			return E_POINTER;
		}
		if(!range) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = range->readOnly ? TRUE : FALSE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_Maximum(double* value) override {
		return rangeConstant(value, &accessibility::RangeValueProvider::maximum);
	}

	HRESULT STDMETHODCALLTYPE get_Minimum(double* value) override {
		return rangeConstant(value, &accessibility::RangeValueProvider::minimum);
	}

	HRESULT STDMETHODCALLTYPE get_LargeChange(double* value) override {
		return rangeConstant(value, &accessibility::RangeValueProvider::largeChange);
	}

	HRESULT STDMETHODCALLTYPE get_SmallChange(double* value) override {
		return rangeConstant(value, &accessibility::RangeValueProvider::smallChange);
	}

	HRESULT STDMETHODCALLTYPE Scroll(::ScrollAmount horizontal,
		::ScrollAmount vertical) override
	{
		auto scroll = available() ? widget->getAccessibleScroll() : nullptr;
		if(!scroll || !scroll->scroll) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		scroll->scroll(
			static_cast<accessibility::ScrollAmount>(horizontal),
			static_cast<accessibility::ScrollAmount>(vertical));
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal,
		double vertical) override
	{
		auto scroll = available() ? widget->getAccessibleScroll() : nullptr;
		if(!scroll || !scroll->setPercent) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if((horizontal != noScroll && (horizontal < 0 || horizontal > 100)) ||
			(vertical != noScroll && (vertical < 0 || vertical > 100))) {
			return E_INVALIDARG;
		}
		scroll->setPercent(horizontal, vertical);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* value) override {
		return scrollValue(value, &accessibility::ScrollProvider::horizontalPercent,
			noScroll);
	}

	HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* value) override {
		return scrollValue(value, &accessibility::ScrollProvider::verticalPercent,
			noScroll);
	}

	HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* value) override {
		return scrollValue(value, &accessibility::ScrollProvider::horizontalViewSize,
			100);
	}

	HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* value) override {
		return scrollValue(value, &accessibility::ScrollProvider::verticalViewSize,
			100);
	}

	HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* value) override {
		return scrollBool(value,
			&accessibility::ScrollProvider::horizontallyScrollable);
	}

	HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* value) override {
		return scrollBool(value,
			&accessibility::ScrollProvider::verticallyScrollable);
	}

	HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** value) override {
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		auto items = available() ? itemProvider() : nullptr;
		if(isItem() || !items || !items->selection) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		auto selected = items->selection();
		auto array = ::SafeArrayCreateVector(VT_UNKNOWN, 0,
			static_cast<ULONG>(selected.size()));
		if(!array) {
			return E_OUTOFMEMORY;
		}
		for(LONG index = 0; index < static_cast<LONG>(selected.size()); ++index) {
			IRawElementProviderFragment* provider = nullptr;
			auto result = itemFragment(selected[index], &provider);
			if(FAILED(result)) {
				::SafeArrayDestroy(array);
				return result;
			}
			IUnknown* unknown = provider;
			::SafeArrayPutElement(array, &index, unknown);
			if(provider) {
				provider->Release();
			}
		}
		*value = array;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* value) override {
		if(!value) {
			return E_POINTER;
		}
		auto items = available() ? itemProvider() : nullptr;
		if(isItem() || !items) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = items->canSelectMultiple ? TRUE : FALSE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* value) override {
		if(!value) {
			return E_POINTER;
		}
		auto items = available() ? itemProvider() : nullptr;
		if(isItem() || !items) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = items->selectionRequired ? TRUE : FALSE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Select() override {
		return selectItem(&accessibility::ItemProvider::select);
	}

	HRESULT STDMETHODCALLTYPE AddToSelection() override {
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(!items->canSelectMultiple) {
			return UIA_E_INVALIDOPERATION;
		}
		return selectItem(&accessibility::ItemProvider::addToSelection);
	}

	HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(items->selectionRequired && items->selection &&
			items->selection().size() <= 1 && items->selected &&
			items->selected(item)) {
			return UIA_E_INVALIDOPERATION;
		}
		return selectItem(&accessibility::ItemProvider::removeFromSelection);
	}

	HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override {
		if(!value) {
			return E_POINTER;
		}
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem() || !items->selected) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = items->selected(item) ? TRUE : FALSE;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE get_SelectionContainer(
		IRawElementProviderSimple** value) override
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!available() || !isItem() || !owner) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		return owner->QueryInterface(IID_IRawElementProviderSimple,
			reinterpret_cast<void**>(value));
	}

	HRESULT STDMETHODCALLTYPE Expand() override {
		return expandItem(&accessibility::ItemProvider::expand);
	}

	HRESULT STDMETHODCALLTYPE Collapse() override {
		return expandItem(&accessibility::ItemProvider::collapse);
	}

	HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(
		::ExpandCollapseState* value) override
	{
		if(!value) {
			return E_POINTER;
		}
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem() || !items->expandState) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = static_cast<::ExpandCollapseState>(items->expandState(item));
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Invoke() override {
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem() || !items->invoke) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		items->invoke(item);
		raiseSelfEvent(20009);
		return S_OK;
	}

	void raiseItemEvent(accessibility::ItemId id, long eventId) {
		IRawElementProviderFragment* fragment = nullptr;
		if(SUCCEEDED(itemFragment(id, &fragment)) && fragment) {
			IRawElementProviderSimple* simple = nullptr;
			if(SUCCEEDED(fragment->QueryInterface(
				IID_IRawElementProviderSimple,
				reinterpret_cast<void**>(&simple))) && simple) {
				typedef HRESULT (WINAPI *Function)(
					IRawElementProviderSimple*, EVENTID);
				auto function =
					getAutomationProc<Function>("UiaRaiseAutomationEvent");
				if(function) {
					function(simple, eventId);
				}
				simple->Release();
			}
			fragment->Release();
		}
	}

private:
	AccessibilityProvider(AccessibilityProvider* owner_,
		accessibility::ItemId item_) :
		refs(1), widget(owner_->widget), owner(owner_), item(item_)
	{
		owner->AddRef();
	}

	bool available() const {
		auto target = isItem() ? owner->widget : widget;
		if(!target || !target->handle() || !::IsWindow(target->handle())) {
			return false;
		}
		auto items = itemProvider();
		return !isItem() || (items && (!items->exists || items->exists(item)));
	}

	bool isItem() const {
		return owner != nullptr;
	}

	const accessibility::ItemProvider* itemProvider() const {
		auto target = isItem() ? owner->widget : widget;
		return target ? target->getAccessibleItems() : nullptr;
	}

	Rectangle itemBounds() const {
		auto items = itemProvider();
		return items && items->bounds ? items->bounds(item) : Rectangle();
	}

	bool itemHasFocus() const {
		auto items = itemProvider();
		return items && items->focused && items->focused() == item &&
			(::GetFocus() == widget->handle() ||
				::IsChild(widget->handle(), ::GetFocus()));
	}

	HRESULT rootFragment(IRawElementProviderFragment** value) {
		if(!owner) {
			return E_FAIL;
		}
		return owner->QueryInterface(IID_IRawElementProviderFragment,
			reinterpret_cast<void**>(value));
	}

	HRESULT itemFragment(accessibility::ItemId id,
		IRawElementProviderFragment** value)
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		auto items = itemProvider();
		if(!id || !items || (items->exists && !items->exists(id))) {
			return S_OK;
		}
		auto provider = new AccessibilityProvider(isItem() ? owner : this, id);
		*value = static_cast<IRawElementProviderFragment*>(provider);
		return S_OK;
	}

	HRESULT itemSibling(bool previous,
		IRawElementProviderFragment** value)
	{
		auto items = itemProvider();
		if(!items || !items->children) {
			return S_OK;
		}
		auto parent = items->parent ? items->parent(item) : 0;
		auto siblings = items->children(parent);
		auto current = std::find(siblings.begin(), siblings.end(), item);
		if(current == siblings.end()) {
			return S_OK;
		}
		if(previous) {
			if(current == siblings.begin()) {
				return S_OK;
			}
			--current;
		} else {
			++current;
			if(current == siblings.end()) {
				return S_OK;
			}
		}
		return itemFragment(*current, value);
	}

	accessibility::ItemId itemFromPoint(accessibility::ItemId parent,
		const POINT& screenPoint)
	{
		auto items = itemProvider();
		if(!items || !items->children || !items->bounds) {
			return 0;
		}
		auto point = screenPoint;
		::ScreenToClient(widget->handle(), &point);
		for(auto id: items->children(parent)) {
			auto child = itemFromPoint(id, screenPoint);
			if(child) {
				return child;
			}
			if(items->bounds(id).contains(Point(point))) {
				return id;
			}
		}
		return 0;
	}

	HRESULT selectItem(
		std::function<void (accessibility::ItemId)>
			accessibility::ItemProvider::* member)
	{
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem()) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		auto& callback = items->*member;
		if(!callback) {
			return UIA_E_NOTSUPPORTED;
		}
		callback(item);
		if(member == &accessibility::ItemProvider::addToSelection) {
			raiseSelfEvent(20010);
		} else if(member == &accessibility::ItemProvider::removeFromSelection) {
			raiseSelfEvent(20011);
		}
		return S_OK;
	}

	void raiseSelfEvent(long eventId) {
		if(owner) {
			owner->raiseItemEvent(item, eventId);
		}
	}

	HRESULT expandItem(
		std::function<void (accessibility::ItemId)>
			accessibility::ItemProvider::* member)
	{
		auto items = available() ? itemProvider() : nullptr;
		if(!items || !isItem() || !items->expandState) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		if(items->expandState(item) == accessibility::LeafNode) {
			return UIA_E_INVALIDOPERATION;
		}
		auto& callback = items->*member;
		if(!callback) {
			return UIA_E_NOTSUPPORTED;
		}
		callback(item);
		return S_OK;
	}

	bool isFragmentRoot() const {
		if(isItem()) {
			return false;
		}
		if(!available()) {
			return false;
		}
		for(auto parent = widget->getParent(); parent; parent = parent->getParent()) {
			if(parent->accessibilityProvider) {
				return false;
			}
		}
		return true;
	}

	AccessibilityProvider* fragmentRoot() {
		if(!available()) {
			return nullptr;
		}
		auto root = this;
		for(auto parent = widget->getParent(); parent; parent = parent->getParent()) {
			if(parent->accessibilityProvider) {
				root = parent->accessibilityProvider;
			}
		}
		return root;
	}

	static HRESULT fragmentForWindow(HWND window,
		IRawElementProviderFragment** value)
	{
		if(!value) {
			return E_POINTER;
		}
		*value = nullptr;
		if(!window) {
			return S_OK;
		}
		auto candidate = hwnd_cast<Widget*>(window);
		if(candidate && candidate->accessibilityProvider) {
			return candidate->accessibilityProvider->QueryInterface(
				IID_IRawElementProviderFragment, reinterpret_cast<void**>(value));
		}
		auto result = hostFragment(window, value);
		return result == E_NOINTERFACE ? S_OK : result;
	}

	HRESULT childFragment(bool reverse,
		IRawElementProviderFragment** value) const
	{
		HWND child = nullptr;
		while((child = ::FindWindowEx(widget->handle(), child, nullptr, nullptr))) {
			IRawElementProviderFragment* candidate = nullptr;
			auto result = fragmentForWindow(child, &candidate);
			if(FAILED(result)) {
				return result;
			}
			if(candidate) {
				if(!reverse) {
					*value = candidate;
					return S_OK;
				}
				if(*value) {
					(*value)->Release();
				}
				*value = candidate;
			}
		}
		return S_OK;
	}

	HRESULT siblingFragment(bool previous,
		IRawElementProviderFragment** value) const
	{
		auto parent = ::GetParent(widget->handle());
		if(!parent) {
			return S_OK;
		}
		HWND child = nullptr;
		bool found = false;
		IRawElementProviderFragment* prior = nullptr;
		while((child = ::FindWindowEx(parent, child, nullptr, nullptr))) {
			IRawElementProviderFragment* candidate = nullptr;
			auto result = fragmentForWindow(child, &candidate);
			if(FAILED(result)) {
				if(prior) {
					prior->Release();
				}
				return result;
			}
			if(!candidate) {
				continue;
			}
			if(child == widget->handle()) {
				candidate->Release();
				if(previous) {
					*value = prior;
					return S_OK;
				}
				if(prior) {
					prior->Release();
				}
				prior = nullptr;
				found = true;
				continue;
			}
			if(found && !previous) {
				*value = candidate;
				return S_OK;
			}
			if(!found) {
				if(prior) {
					prior->Release();
				}
				prior = candidate;
			} else {
				candidate->Release();
			}
		}
		if(prior) {
			prior->Release();
		}
		return S_OK;
	}

	HRESULT rangeConstant(double* value,
		double accessibility::RangeValueProvider::* member) const
	{
		if(!value) {
			return E_POINTER;
		}
		auto range = available() ? widget->getAccessibleRangeValue() : nullptr;
		if(!range) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		*value = range->*member;
		return S_OK;
	}

	HRESULT scrollValue(double* value,
		std::function<double ()> accessibility::ScrollProvider::* member,
		double fallback) const
	{
		if(!value) {
			return E_POINTER;
		}
		auto scroll = available() ? widget->getAccessibleScroll() : nullptr;
		if(!scroll) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		auto& callback = scroll->*member;
		*value = callback ? callback() : fallback;
		return S_OK;
	}

	HRESULT scrollBool(BOOL* value,
		std::function<bool ()> accessibility::ScrollProvider::* member) const
	{
		if(!value) {
			return E_POINTER;
		}
		auto scroll = available() ? widget->getAccessibleScroll() : nullptr;
		if(!scroll) {
			return UIA_E_ELEMENTNOTAVAILABLE;
		}
		auto& callback = scroll->*member;
		*value = callback && callback() ? TRUE : FALSE;
		return S_OK;
	}

	std::atomic<ULONG> refs;
	Widget* widget;
	AccessibilityProvider* owner;
	accessibility::ItemId item;
};

namespace util { namespace win32 {

AccessibilityProvider* createAccessibilityProvider(Widget* widget) {
	return new AccessibilityProvider(widget);
}

void detachAccessibilityProvider(AccessibilityProvider* provider) {
	if(provider) {
		provider->detach();
	}
}

void releaseAccessibilityProvider(AccessibilityProvider* provider) {
	if(provider) {
		provider->Release();
	}
}

LRESULT returnAccessibilityProvider(AccessibilityProvider* provider,
	HWND window, WPARAM wParam, LPARAM lParam)
{
	typedef LRESULT (WINAPI *Function)(HWND, WPARAM, LPARAM,
		IRawElementProviderSimple*);
	auto function = getAutomationProc<Function>("UiaReturnRawElementProvider");
	return function ? function(window, wParam, lParam,
		static_cast<IRawElementProviderSimple*>(provider)) : 0;
}

void raiseAccessibilityEvent(AccessibilityProvider* provider, long eventId) {
	if(!provider) {
		return;
	}
	typedef HRESULT (WINAPI *Function)(IRawElementProviderSimple*, EVENTID);
	auto function = getAutomationProc<Function>("UiaRaiseAutomationEvent");
	if(function) {
		function(static_cast<IRawElementProviderSimple*>(provider), eventId);
	}
}

void raiseAccessibilityItemEvent(AccessibilityProvider* provider,
	std::uintptr_t item, long eventId)
{
	if(provider && item) {
		provider->raiseItemEvent(item, eventId);
	}
}

void raiseAccessibilityStructureChanged(AccessibilityProvider* provider) {
	if(!provider) {
		return;
	}
	typedef HRESULT (WINAPI *Function)(IRawElementProviderSimple*,
		StructureChangeType, int*, int);
	auto function = getAutomationProc<Function>("UiaRaiseStructureChangedEvent");
	if(function) {
		function(static_cast<IRawElementProviderSimple*>(provider),
			StructureChangeType_ChildrenInvalidated, nullptr, 0);
	}
}

} }

}
