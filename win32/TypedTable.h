/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef DCPLUSPLUS_WIN32_TYPED_TABLE_H
#define DCPLUSPLUS_WIN32_TYPED_TABLE_H

#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "forward.h"
#include "WinUtil.h"

/** Table with an object associated to each item.

@tparam ContentType Type of the objects associated to each item.

@tparam managed Whether this class should handle deleting associated objects.

@note Support for texts:
The ContentType class must provide a const tstring& getText(int col) [const] function.

@note Support for images:
The ContentType class must provide a int getImage(int col) [const] function.

@note Support for item sorting:
The ContentType class must provide a
static int compareItems([const] ContentType* a, [const] ContentType* b, int col) function.

@note Support for custom styles per item (whole row) or per sub-item (each cell):
The ContentType class must provide a
int getStyle(HFONT& font, COLORREF& textColor, COLORREF& bgColor, int col) [const] function. It is
called a first time with col=-1 to set the style of the whole item. It can return:
- CDRF_DODEFAULT to keep the default style for the item.
- CDRF_NEWFONT to change the style of the item.
- CDRF_NOTIFYSUBITEMDRAW to request custom styles for each sub-item (getStyle will then be called
for each sub-item).

@note Support for tooltips:
The ContentType class must provide a tstring getTooltip() [const] function. Note that tooltips are
only for the first column. */
template<typename ContentType, bool managed, typename TableType>
class TypedTable : public TableType
{
	typedef TableType BaseType;
	typedef TypedTable<ContentType, managed, TableType> ThisType;

	template<typename T>
	using text_expr_t = decltype(std::declval<T&>().getText(0));

	template<typename T>
	using image_expr_t = decltype(std::declval<T&>().getImage(0));

	template<typename T>
	using sort_expr_t = decltype(T::compareItems(std::declval<T*>(), std::declval<T*>(), 0));

	template<typename T>
	using style_expr_t = decltype(std::declval<T&>().getStyle(
		std::declval<HFONT&>(),
		std::declval<COLORREF&>(),
		std::declval<COLORREF&>(),
		0));

	template<typename T>
	using tooltip_expr_t = decltype(std::declval<T&>().getTooltip());

	template<typename T, typename = void>
	struct HasText : std::false_type { };

	template<typename T>
	struct HasText<T, std::void_t<text_expr_t<T>>> :
		std::integral_constant<bool, std::is_convertible_v<text_expr_t<T>, const tstring&>> { };

	template<typename T, typename = void>
	struct HasImage : std::false_type { };

	template<typename T>
	struct HasImage<T, std::void_t<image_expr_t<T>>> :
		std::integral_constant<bool, std::is_convertible_v<image_expr_t<T>, int>> { };

	template<typename T, typename = void>
	struct HasSort : std::false_type { };

	template<typename T>
	struct HasSort<T, std::void_t<sort_expr_t<T>>> :
		std::integral_constant<bool, std::is_convertible_v<sort_expr_t<T>, int>> { };

	template<typename T, typename = void>
	struct HasStyle : std::false_type { };

	template<typename T>
	struct HasStyle<T, std::void_t<style_expr_t<T>>> :
		std::integral_constant<bool, std::is_convertible_v<style_expr_t<T>, int>> { };

	template<typename T, typename = void>
	struct HasTooltip : std::false_type { };

	template<typename T>
	struct HasTooltip<T, std::void_t<tooltip_expr_t<T>>> :
		std::integral_constant<bool, std::is_convertible_v<tooltip_expr_t<T>, tstring>> { };

	static constexpr bool hasText = HasText<ContentType>::value;
	static constexpr bool hasImage = HasImage<ContentType>::value;
	static constexpr bool hasSort = HasSort<ContentType>::value;
	static constexpr bool hasStyle = HasStyle<ContentType>::value;
	static constexpr bool hasTooltip = HasTooltip<ContentType>::value;

public:
	typedef ThisType* ObjectType;

	explicit TypedTable(dwt::Widget* parent) : BaseType(parent) { }

	struct Seed : public BaseType::Seed {
		typedef ThisType WidgetType;

		Seed(const typename BaseType::Seed& seed) : BaseType::Seed(seed) {
		}
	};

	virtual ~TypedTable() = default;

	using BaseType::find;
	using BaseType::insert;

	void create(const Seed& seed) {
#ifdef _DEBUG
		writeDebugInfo();
#endif
		BaseType::create(seed);

		addDisplayEvent();
		addSortEvent();
		addStyleEvent();
		addTooltipEvent();

		if constexpr (managed) {
			this->onDestroy([this] { this->clear(); });
		}
	}

	int insert(ContentType* item) {
		return insert(getSortPos(item), item);
	}

	int insert(int i, ContentType* item) {
		return BaseType::insert(LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE, i,
			LPSTR_TEXTCALLBACK, 0, 0, I_IMAGECALLBACK, reinterpret_cast<LPARAM>(item));
	}

	ContentType* getData(int iItem) {
		return reinterpret_cast<ContentType*>(BaseType::getData(iItem));
	}

	void setData(int iItem, ContentType* lparam) {
		if constexpr (managed) {
			auto old = getData(iItem);
			BaseType::setData(iItem, reinterpret_cast<LPARAM>(lparam));
			if(old != lparam) {
				delete old;
			}
		} else {
			BaseType::setData(iItem, reinterpret_cast<LPARAM>(lparam));
		}
	}

	ContentType* getSelectedData() {
		int item = this->getSelected();
		return item == -1 ? nullptr : getData(item);
	}

	int find(ContentType* item) {
		return this->findData(reinterpret_cast<LPARAM>(item));
	}

	struct CompFirst {
		bool operator()(const ContentType& a, const tstring& b) const {
			return Util::stricmp(a.getText(0), b) < 0;
		}
	};

	void forEach(void (ContentType::*func)()) {
		for(int i = 0, n = static_cast<int>(this->size()); i < n; ++i) {
			(getData(i)->*func)();
		}
	}
	void forEachSelected(void (ContentType::*func)(), bool removing = false) {
		int i = -1;
		while((i = ListView_GetNextItem(this->handle(), removing ? -1 : i, LVNI_SELECTED)) != -1) {
			(getData(i)->*func)();
		}
	}
	template<class Function>
	Function forEachT(Function pred) {
		for(int i = 0, n = static_cast<int>(this->size()); i < n; ++i)
			pred(getData(i));
		return pred;
	}
	template<class Function>
	Function forEachSelectedT(Function pred, bool removing = false) {
		int i = -1;
		while((i = ListView_GetNextItem(this->handle(), removing ? -1 : i, LVNI_SELECTED)) != -1) {
			pred(getData(i));
		}
		return pred;
	}

	void update(int i) {
		this->redraw(i, i);
	}

	void update(ContentType* item) { int i = find(item); if(i != -1) update(i); }

	void clear() {
		if constexpr (managed) {
			const int n = static_cast<int>(this->size());
			std::vector<ContentType*> toDelete;
			toDelete.reserve(static_cast<size_t>(n));

			for(int i = 0; i < n; ++i) {
				toDelete.push_back(getData(i));
			}

			this->BaseType::clear();

			for(auto p : toDelete) {
				delete p;
			}
		} else {
			this->BaseType::clear();
		}
	}

	void erase(int i) {
		if constexpr (managed) {
			auto data = getData(i);
			this->BaseType::erase(i);
			delete data;
		} else {
			this->BaseType::erase(i);
		}
	}

	void erase(ContentType* item) {
		int i = find(item);
		if(i != -1) {
			this->erase(i);
		}
	}

	void setSort(int col = -1, bool ascending = true) {
		BaseType::setSort(col, BaseType::SORT_CALLBACK, ascending);
	}

private:
	void addDisplayEvent() {
		if constexpr (hasText || hasImage) {
			this->onRaw([this](WPARAM, LPARAM lParam) -> LRESULT {
				auto& data = *reinterpret_cast<NMLVDISPINFO*>(lParam);
				handleDisplay(data);
				return 0;
			}, dwt::Message(WM_NOTIFY, LVN_GETDISPINFO));
		}
	}

	void addSortEvent() {
		if constexpr (hasSort) {
			this->onSortItems([this](LPARAM lhs, LPARAM rhs) { return this->handleSort(lhs, rhs); });
			this->onColumnClick([this](int column) { this->handleColumnClick(column); });
		}
	}

	void addStyleEvent() {
		if constexpr (hasStyle) {
			this->onCustomDraw([this](NMLVCUSTOMDRAW& data) { return this->handleCustomDraw(data); });
		}
	}

	void addTooltipEvent() {
		if constexpr (hasTooltip) {
			this->setTooltips([this](int i) -> tstring {
				auto data = this->getData(i);
				return data ? data->getTooltip() : tstring();
			});
		}
	}

#ifdef _DEBUG
	void writeDebugInfo() {
		typedef ContentType T;
		printf("Creating a TypedTable<%s>; text: %d, images: %d, sorting: %d, custom styles: %d, tooltips: %d\n",
			typeid(T).name(), hasText, hasImage, hasSort, hasStyle, hasTooltip);
	}
#endif

	void handleDisplay(NMLVDISPINFO& data) {
		if((data.item.mask & (LVIF_TEXT | LVIF_IMAGE)) == 0) {
			return;
		}

		auto content = reinterpret_cast<ContentType*>(data.item.lParam);
		if(!content) {
			return;
		}

		if constexpr (hasText) {
			if(data.item.mask & LVIF_TEXT) {
				handleText(*content, data);
			}
		}

		if constexpr (hasImage) {
			if(data.item.mask & LVIF_IMAGE) {
				data.item.iImage = content->getImage(data.item.iSubItem);
			}
		}
	}

	int getSortPos(ContentType* a) {
		if constexpr (!hasSort) {
			return static_cast<int>(this->size());
		} else {
			int sortCol = this->getSortColumn();
			int low = 0;
			int high = static_cast<int>(this->size());

			if(sortCol == -1 || high == 0) {
				return high;
			}

			while(low < high) {
				int mid = low + ((high - low) / 2);
				int comp = ContentType::compareItems(a, getData(mid), sortCol);

				if(!this->isAscending()) {
					comp = -comp;
				}

				if(comp > 0) {
					low = mid + 1;
				} else {
					high = mid;
				}
			}

			return low;
		}
	}

	void handleText(ContentType& content, NMLVDISPINFO& data) {
		if(!data.item.pszText || data.item.cchTextMax <= 0) {
			return;
		}

		const tstring& text = content.getText(data.item.iSubItem);
		_tcsncpy_s(data.item.pszText, static_cast<size_t>(data.item.cchTextMax), text.c_str(), _TRUNCATE);
	}

	void handleColumnClick(int column) {
		if(column != this->getSortColumn()) {
			this->setSort(column, true);
		} else if(this->isAscending()) {
			this->setSort(this->getSortColumn(), false);
		} else {
			this->setSort(-1, true);
		}
	}

	int handleSort(LPARAM lhs, LPARAM rhs) {
		return ContentType::compareItems(reinterpret_cast<ContentType*>(lhs), reinterpret_cast<ContentType*>(rhs), this->getSortColumn());
	}

	LRESULT handleCustomDraw(NMLVCUSTOMDRAW& data) {
		if(data.nmcd.dwDrawStage == CDDS_PREPAINT) {
			return CDRF_NOTIFYITEMDRAW;
		}

		if((data.nmcd.dwDrawStage & CDDS_ITEMPREPAINT) == CDDS_ITEMPREPAINT && data.dwItemType == LVCDI_ITEM && data.nmcd.lItemlParam) {
			HFONT font = nullptr;
			auto ret = reinterpret_cast<ContentType*>(data.nmcd.lItemlParam)->getStyle(font, data.clrText, data.clrTextBk,
				((data.nmcd.dwDrawStage & CDDS_SUBITEM) == CDDS_SUBITEM) ? data.iSubItem : -1);
			if(ret == CDRF_NEWFONT && font) {
				::SelectObject(data.nmcd.hdc, font);
			}
			return ret;
		}

		return CDRF_DODEFAULT;
	}
};

#endif
