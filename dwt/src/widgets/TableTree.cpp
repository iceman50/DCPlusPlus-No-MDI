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

#include <dwt/widgets/TableTree.h>

#include <vsstyle.h>

#include <algorithm>

#include <dwt/CanvasClasses.h>
#include <dwt/resources/ImageList.h>
#include <dwt/resources/Pen.h>
#include <dwt/util/check.h>
#include <dwt/util/HoldRedraw.h>

namespace dwt {

TableTree::Seed::Seed(const BaseType::Seed& seed) :
	BaseType::Seed(seed)
{
}

TableTree::TableTree(Widget* parent) :
	BaseType(parent),
	indent(0)
{
}

void TableTree::create(const Seed& seed) {
	dwtassert((seed.style & LVS_REPORT) == LVS_REPORT, "TableTree requires LVS_REPORT");

	BaseType::create(seed);
	if(indent <= 0) {
		indent = std::max<long>(::GetSystemMetrics(SM_CXSMICON), 16);
	}

	theme.load(VSCLASS_TREEVIEW, this, false);

	onCustomDraw([this](NMLVCUSTOMDRAW& data) { return handleCustomDraw(data); });
	onKeyDown([this](int c) { return handleKeyDown(c); });
	onLeftMouseDown([this](const MouseEvent& me) { return handleLeftMouseDown(me); });
	configureAccessibility();
}

void TableTree::appearanceChanged() {
	if(isManualAppearance()) {
		theme.unload();
	} else {
		theme.reload();
	}
	BaseType::appearanceChanged();
}

bool TableTree::handleMessage(const MSG& msg, LRESULT& retVal) {
	switch(msg.message) {
	case LVM_DELETEITEM:
		{
			handleDelete(static_cast<int>(msg.wParam));
			raiseAccessibleStructureChanged();
			break;
		}
	case LVM_DELETEALLITEMS:
		{
			items.clear();
			children.clear();
			storedRows.clear();
			raiseAccessibleStructureChanged();
			break;
		}
	case LVM_INSERTITEM:
		{
			handleInsert(*reinterpret_cast<LVITEM*>(msg.lParam));
			raiseAccessibleStructureChanged();
			break;
		}
	case LVM_SETIMAGELIST:
		{
			if(msg.wParam == LVSIL_SMALL && msg.lParam) {
				indent = ImageList(reinterpret_cast<HIMAGELIST>(msg.lParam), false).getImageSize().x;
			}
			break;
		}
	case LVM_SETITEM:
		{
			handleSetItem(*reinterpret_cast<LVITEM*>(msg.lParam));
			break;
		}
	case LVM_SETITEMTEXT:
		{
			auto item = *reinterpret_cast<LVITEM*>(msg.lParam);
			item.mask = LVIF_TEXT;
			handleSetItem(item, static_cast<int>(msg.wParam));
			break;
		}
	}
	return BaseType::handleMessage(msg, retVal);
}

void TableTree::insertChild(LPARAM parentParam, LPARAM child) {
	if(!parentParam || !child || parentParam == child) {
		dwtDebugFail("TableTree hierarchy requires distinct non-null item data");
		return;
	}

	auto existingParent = children.find(child);
	if(existingParent != children.end()) {
		if(existingParent->second == parentParam) {
			return;
		}
		auto& oldSiblings = items[existingParent->second].children;
		oldSiblings.erase(std::remove(oldSiblings.begin(), oldSiblings.end(), child),
			oldSiblings.end());
		if(oldSiblings.empty()) {
			auto& oldParent = items[existingParent->second];
			oldParent.expanded = false;
			oldParent.glyphRect = Rectangle();
			oldParent.redrawGlyph(*this);
		}
		children.erase(existingParent);
	}

	auto& parent = items[parentParam];
	if(std::find(parent.children.begin(), parent.children.end(), child) ==
		parent.children.end()) {
		parent.children.push_back(child);
	}
	children[child] = parentParam;

	auto childRow = findData(child);
	if(parent.expanded) {
		if(childRow >= 0) {
			sendMsg(LVM_DELETEITEM, childRow, 0);
		}
		insertStoredRow(child, findData(parentParam) + 1, 2);
		resort();
	} else if(childRow >= 0) {
		// A collapsed parent owns the row, but the native list must not display it.
		sendMsg(LVM_DELETEITEM, childRow, 0);
	}
	raiseAccessibleStructureChanged();
}

void TableTree::eraseChild(LPARAM child) {
	auto i = children.find(child);
	if(i != children.end()) {
		eraseChild(i, false);
		raiseAccessibleStructureChanged();
	}
}

void TableTree::collapse(LPARAM parentParam) {
	util::HoldRedraw hold { this };

	auto parent = items.find(parentParam);
	if(parent == items.end() || !parent->second.expanded) {
		return;
	}
	auto pos = findData(parentParam);
	if(pos < 0) {
		return;
	}
	auto n = parent->second.children.size();

	// assume children are all at the right pos.
	for(size_t i = 0; i < n; ++i) {
		sendMsg(LVM_DELETEITEM, pos + 1, 0);
	}

	parent->second.expanded = false;
	parent->second.redrawGlyph(*this);
	raiseAccessibleStructureChanged();

	// special case, see TableTreeTest
	if(n == 1 && pos == static_cast<int>(size()) - 1) {
		Control::redraw(false);
	}
}

void TableTree::expand(LPARAM parentParam) {
	util::HoldRedraw hold { this };

	auto parent = items.find(parentParam);
	if(parent == items.end() || parent->second.expanded) {
		return;
	}
	auto position = findData(parentParam);
	if(position < 0) {
		return;
	}
	for(auto child: parent->second.children) {
		if(findData(child) < 0) {
			insertStoredRow(child, ++position, 2);
		}
	}

	resort();

	parent->second.expanded = true;
	parent->second.redrawGlyph(*this);
	raiseAccessibleStructureChanged();
}

TableTree::Item::Item() : expanded(false)
{
}

TableTree::StoredRow::StoredRow() :
	image(I_IMAGENONE),
	hasImage(false),
	imageCallback(false)
{
}

void TableTree::Item::redrawGlyph(TableTree& w) {
	::RECT rect = glyphRect;
	::InvalidateRect(w.handle(), &rect, FALSE);
}

LRESULT TableTree::handleCustomDraw(NMLVCUSTOMDRAW& data) {
	if(data.nmcd.dwDrawStage == CDDS_PREPAINT) {
		return CDRF_NOTIFYITEMDRAW;
	}

	if(data.nmcd.dwDrawStage == CDDS_ITEMPREPAINT && data.dwItemType == LVCDI_ITEM) {
		return CDRF_NOTIFYSUBITEMDRAW;
	}

	if(data.nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM) && data.dwItemType == LVCDI_ITEM && data.iSubItem == 0) {
		FreeCanvas canvas { data.nmcd.hdc };
		const long drawIndent = std::max<long>(indent, 16);
		const auto manual = isManualAppearance();
		const auto& palette = getAppearance().getPalette();

		auto rect = getRect(static_cast<int>(data.nmcd.dwItemSpec), 0, LVIR_BOUNDS);

		{
			// draw tree lines.
			LOGBRUSH lb { BS_SOLID, manual ? palette.border :
				Color::predefined(COLOR_GRAYTEXT) };
			Pen pen { ::ExtCreatePen(PS_COSMETIC | PS_ALTERNATE, 1, &lb, 0, nullptr) };
			auto selectPen(canvas.select(pen));

			Point mid { rect.left() + drawIndent / 2, rect.top() + drawIndent / 2 };

			auto lastChild = false;
			if(children.find(data.nmcd.lItemlParam) != children.end()) {
				// this is a child item; draw a second vertical line to link surrounding parents.
				canvas.line(mid.x, rect.top(), mid.x, rect.bottom()); // vertical
				rect.pos.x += drawIndent;
				mid.x += drawIndent;
				lastChild = children.find(getData(static_cast<int>(data.nmcd.dwItemSpec) + 1)) == children.end();
			}

			canvas.line(mid, Point(rect.left() + drawIndent, mid.y)); // horizontal
			canvas.line(mid.x, rect.top(), mid.x, lastChild ? mid.y : rect.bottom()); // vertical
		}

		auto parentItem = items.find(data.nmcd.lItemlParam);
		if(parentItem != items.end() && !parentItem->second.children.empty()) {
			// this is a parent item; draw the +/- glyph.

			if(theme && !manual) {
				int part = TVP_GLYPH, state = parentItem->second.expanded ? GLPS_OPENED : GLPS_CLOSED;
				theme.getPartSize(canvas, part, state, rect.size);
				rect.pos.x += std::max(drawIndent - rect.size.x, 0L) / 2;
				rect.pos.y += std::max(drawIndent - rect.size.y, 0L) / 2;
				theme.drawBackground(canvas, part, state, rect);
				parentItem->second.glyphRect = rect;

			} else {
				const long glyphSize = 9, padding = 2;
				rect.pos.x += std::max(drawIndent - glyphSize, 0L) / 2;
				rect.pos.y += std::max(drawIndent - glyphSize, 0L) / 2;
				rect.size.x = rect.size.y = glyphSize;

				if(manual) {
					Brush background(palette.surface);
					Pen border(palette.border);
					auto selectBackground(canvas.select(background));
					auto selectBorder(canvas.select(border));
					canvas.rectangle(rect);
				} else {
					::RECT rc = rect;
					::DrawEdge(canvas.handle(), &rc, EDGE_BUMP,
						BF_RECT | BF_MIDDLE | BF_FLAT);
				}

				Point mid { rect.left() + glyphSize / 2, rect.top() + glyphSize / 2 };

				Pen pen { manual ? palette.text :
					Color::predefined(COLOR_GRAYTEXT), Pen::Solid };
				auto selectPen(canvas.select(pen));

				canvas.line(rect.left() + padding, mid.y, rect.right() - padding, mid.y); // horizontal
				if(!parentItem->second.expanded) {
					canvas.line(mid.x, rect.top() + padding, mid.x, rect.bottom() - padding); // vertical
				}

				parentItem->second.glyphRect = rect;
			}
		}
	}

	return CDRF_DODEFAULT;
}

bool TableTree::handleKeyDown(int c) {
	bool handled = false;
	switch(c) {
	case VK_LEFT:
		{
			for(auto sel: getSelection()) {
				auto item = items.find(getData(sel));
				if(item != items.end()) {
					// a parent is selected; collapse it.
					handled = true;
					if(item->second.expanded) {
						collapse(item->first);
						continue;
					}
				}
				auto child = children.find(getData(sel));
				if(child != children.end()) {
					// a child is selected; select its parent.
					handled = true;
					ListView_SetItemState(handle(), sel, 0, LVIS_SELECTED | LVIS_FOCUSED);
					setSelected(findData(child->second));
				}
			}
			break;
		}
	case VK_RIGHT:
		{
			for(auto sel: getSelection()) {
				auto item = items.find(getData(sel));
				if(item != items.end()) {
					// a parent is selected; either expand it or select its first child.
					handled = true;
					if(item->second.expanded) {
						if(!item->second.children.empty()) {
							ListView_SetItemState(handle(), sel, 0, LVIS_SELECTED | LVIS_FOCUSED);
							setSelected(sel + 1);
						}
					} else {
						expand(item->first);
					}
				}
			}
			break;
		}
	}
	return handled;
}

bool TableTree::handleLeftMouseDown(const MouseEvent& me) {
	auto hit = hitTest(me.pos);
	if(hit.second == 0) { // first column
		auto it = items.find(getData(hit.first));
		if(it != items.end() && it->second.glyphRect.contains(ClientCoordinate(me.pos, this).getPoint())) {
			it->second.expanded ? collapse(it->first) : expand(it->first);
			return true;
		}
	}
	return false;
}

void TableTree::handleDelete(int pos) {
	auto param = getData(pos);

	auto parentItem = items.find(param);
	if(parentItem != items.end()) {
		if(parentItem->second.expanded) {
			collapse(param);
		}
		for(auto child: parentItem->second.children) {
			children.erase(child);
			storedRows.erase(child);
		}
		items.erase(parentItem);
	}

	auto child = children.find(param);
	if(child != children.end()) {
		eraseChild(child, true);
	}
	storedRows.erase(param);
}

void TableTree::handleInsert(LVITEM& lv) {
	if((lv.mask & LVIF_PARAM) == LVIF_PARAM && lv.lParam) {
		auto& row = storedRows[lv.lParam];
		if((lv.mask & LVIF_TEXT) == LVIF_TEXT) {
			row.texts.resize(1);
			row.textCallbacks.resize(1);
			row.textCallbacks[0] = lv.pszText == LPSTR_TEXTCALLBACK;
			if(!row.textCallbacks[0] && lv.pszText) {
				row.texts[0] = lv.pszText;
			}
		}
		if((lv.mask & LVIF_IMAGE) == LVIF_IMAGE) {
			row.image = lv.iImage;
			row.hasImage = true;
			row.imageCallback = lv.iImage == I_IMAGECALLBACK;
		}
	}

	// add indentation to draw tree lines.
	if((lv.mask & LVIF_INDENT) != LVIF_INDENT) {
		lv.mask |= LVIF_INDENT;
	}
	lv.iIndent = 1;
}

void TableTree::handleSetItem(LVITEM& lv, int itemIndex) {
	const auto rowIndex = itemIndex >= 0 ? itemIndex : lv.iItem;
	if(rowIndex < 0 || rowIndex >= static_cast<int>(size())) {
		return;
	}

	auto oldData = getData(rowIndex);
	auto newData = ((lv.mask & LVIF_PARAM) == LVIF_PARAM) ? lv.lParam : oldData;
	if((lv.mask & LVIF_PARAM) == LVIF_PARAM && newData != oldData) {
		const bool hierarchyNeedsIdentity = !newData && oldData &&
			(items.find(oldData) != items.end() || children.find(oldData) != children.end());
		const bool duplicate = newData && (findData(newData) >= 0 ||
			storedRows.find(newData) != storedRows.end() ||
			items.find(newData) != items.end() || children.find(newData) != children.end());
		if(hierarchyNeedsIdentity || duplicate) {
			dwtDebugFail("TableTree hierarchy item data must be unique and non-null");
			lv.lParam = oldData;
			newData = oldData;
		} else {
			remapData(oldData, newData);
		}
	}

	if(newData) {
		auto& row = storedRows[newData];
		if((lv.mask & LVIF_TEXT) == LVIF_TEXT && lv.iSubItem >= 0) {
			auto column = static_cast<size_t>(lv.iSubItem);
			if(row.texts.size() <= column) {
				row.texts.resize(column + 1);
				row.textCallbacks.resize(column + 1);
			}
			row.textCallbacks[column] = lv.pszText == LPSTR_TEXTCALLBACK;
			if(!row.textCallbacks[column]) {
				row.texts[column] = lv.pszText ? lv.pszText : _T("");
			}
		}
		if((lv.mask & LVIF_IMAGE) == LVIF_IMAGE && lv.iSubItem == 0) {
			row.image = lv.iImage;
			row.hasImage = true;
			row.imageCallback = lv.iImage == I_IMAGECALLBACK;
		}
	}

	if((lv.mask & LVIF_INDENT) == LVIF_INDENT) {
		lv.iIndent = children.find(newData) == children.end() ? 1 : 2;
	}
}

void TableTree::insertStoredRow(LPARAM data, int index, int itemIndent) {
	auto found = storedRows.find(data);
	LVITEM item = { LVIF_PARAM | LVIF_INDENT, index };
	item.lParam = data;
	item.iIndent = itemIndent;

	if(found == storedRows.end()) {
		// Preserve the historical callback-backed mode for data-only child rows.
		item.mask |= LVIF_TEXT | LVIF_IMAGE;
		item.pszText = LPSTR_TEXTCALLBACK;
		item.iImage = I_IMAGECALLBACK;
	} else {
		auto& row = found->second;
		if(!row.texts.empty() || !row.textCallbacks.empty()) {
			item.mask |= LVIF_TEXT;
			item.pszText = !row.textCallbacks.empty() && row.textCallbacks[0] ?
				LPSTR_TEXTCALLBACK : const_cast<LPTSTR>(row.texts[0].c_str());
		}
		if(row.hasImage) {
			item.mask |= LVIF_IMAGE;
			item.iImage = row.imageCallback ? I_IMAGECALLBACK : row.image;
		}
	}

	auto inserted = static_cast<int>(sendMsg(LVM_INSERTITEM, 0,
		reinterpret_cast<LPARAM>(&item)));
	if(inserted < 0 || found == storedRows.end()) {
		return;
	}

	auto& row = found->second;
	for(size_t column = 1; column < row.texts.size(); ++column) {
		LVITEM subItem = { LVIF_TEXT, inserted, static_cast<int>(column) };
		subItem.pszText = column < row.textCallbacks.size() &&
			row.textCallbacks[column] ? LPSTR_TEXTCALLBACK :
			const_cast<LPTSTR>(row.texts[column].c_str());
		sendMsg(LVM_SETITEM, 0, reinterpret_cast<LPARAM>(&subItem));
	}
}

void TableTree::remapData(LPARAM oldData, LPARAM newData) {
	if(oldData == newData) {
		return;
	}

	if(oldData) {
		auto row = storedRows.find(oldData);
		if(row != storedRows.end()) {
			auto value = std::move(row->second);
			storedRows.erase(row);
			if(newData) {
				storedRows.emplace(newData, std::move(value));
			}
		}

		auto parent = items.find(oldData);
		if(parent != items.end()) {
			auto value = std::move(parent->second);
			items.erase(parent);
			if(newData) {
				auto inserted = items.emplace(newData, std::move(value));
				for(auto childData : inserted.first->second.children) {
					children[childData] = newData;
				}
			}
		}

		auto child = children.find(oldData);
		if(child != children.end()) {
			auto parentData = child->second;
			auto& siblings = items[parentData].children;
			std::replace(siblings.begin(), siblings.end(), oldData, newData);
			children.erase(child);
			if(newData) {
				children[newData] = parentData;
			} else {
				siblings.erase(std::remove(siblings.begin(), siblings.end(), LPARAM()),
					siblings.end());
			}
		}
	}
	raiseAccessibleStructureChanged();
}

int TableTree::handleSort(LPARAM& lhs, LPARAM& rhs) {
	/* return 1 or -1 when the 2 items have a direct parency relationship; otherwise, return 0 and
	let the host proceed with comparing. */

	auto c1 = children.find(lhs), c2 = children.find(rhs);

	if(c1 != children.end() && c2 != children.end() && c1->second == c2->second) {
		return 0; // rhs & lhs have the same parent
	}

	if(c1 != children.end()) {
		if(c1->second == rhs) { return isAscending() ? 1 : -1; } // rhs is lhs' parent
		lhs = c1->second; // lhs now points to its parent
	}

	if(c2 != children.end()) {
		if(c2->second == lhs) { return isAscending() ? -1 : 1; } // lhs is rhs' parent
		rhs = c2->second; // rhs now points to its parent
	}

	return 0;
}

void TableTree::eraseChild(decltype(children)::iterator& child, bool deleting) {
	auto& item = items[child->second];
	auto& cont = item.children;
	if(!deleting && item.expanded) {
		sendMsg(LVM_DELETEITEM, findData(child->first), 0);
	}
	cont.erase(std::remove(cont.begin(), cont.end(), child->first), cont.end());
	if(cont.empty()) {
		item.expanded = false;
		item.glyphRect = Rectangle();
		item.redrawGlyph(*this);
	}
	children.erase(child);
}

void TableTree::configureAccessibility() {
	setAccessibleControlType(accessibility::Tree);
	if(getAccessibleName().empty()) {
		setAccessibleName(_T("Table tree"));
	}

	accessibility::ItemProvider provider;
	provider.exists = [this](accessibility::ItemId id) {
		auto value = static_cast<LPARAM>(id);
		return findData(value) >= 0 || children.find(value) != children.end() ||
			items.find(value) != items.end();
	};
	provider.children = [this](accessibility::ItemId parent) {
		std::vector<accessibility::ItemId> result;
		if(parent) {
			auto found = items.find(static_cast<LPARAM>(parent));
			if(found != items.end() && found->second.expanded) {
				for(auto child: found->second.children) {
					result.push_back(static_cast<accessibility::ItemId>(child));
				}
			}
			return result;
		}
		for(size_t row = 0; row < size(); ++row) {
			auto value = getData(static_cast<int>(row));
			if(children.find(value) == children.end()) {
				result.push_back(static_cast<accessibility::ItemId>(value));
			}
		}
		return result;
	};
	provider.parent = [this](accessibility::ItemId id) {
		auto found = children.find(static_cast<LPARAM>(id));
		return found == children.end() ? accessibility::ItemId() :
			static_cast<accessibility::ItemId>(found->second);
	};
	provider.name = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		return row >= 0 ? getText(static_cast<unsigned>(row), 0) : tstring();
	};
	provider.bounds = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		return row >= 0 ? getRect(row, LVIR_BOUNDS) : Rectangle();
	};
	provider.controlType = [](accessibility::ItemId) {
		return static_cast<long>(accessibility::TreeItem);
	};
	provider.selection = [this] {
		std::vector<accessibility::ItemId> result;
		for(auto row: getSelection()) {
			result.push_back(static_cast<accessibility::ItemId>(getData(row)));
		}
		return result;
	};
	provider.selected = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		return row >= 0 &&
			(ListView_GetItemState(handle(), row, LVIS_SELECTED) & LVIS_SELECTED);
	};
	provider.select = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		if(row >= 0) {
			clearSelection();
			setSelected(row);
		}
	};
	provider.addToSelection = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		if(row >= 0) {
			setSelected(row);
		}
	};
	provider.removeFromSelection = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		if(row >= 0) {
			ListView_SetItemState(handle(), row, 0, LVIS_SELECTED | LVIS_FOCUSED);
		}
	};
	provider.canSelectMultiple = !hasStyle(LVS_SINGLESEL);
	provider.expandState = [this](accessibility::ItemId id) {
		auto found = items.find(static_cast<LPARAM>(id));
		if(found == items.end() || found->second.children.empty()) {
			return accessibility::LeafNode;
		}
		return found->second.expanded ?
			accessibility::Expanded : accessibility::Collapsed;
	};
	provider.expand = [this](accessibility::ItemId id) {
		expand(static_cast<LPARAM>(id));
	};
	provider.collapse = [this](accessibility::ItemId id) {
		collapse(static_cast<LPARAM>(id));
	};
	provider.invoke = [this](accessibility::ItemId id) {
		auto found = items.find(static_cast<LPARAM>(id));
		if(found != items.end() && !found->second.children.empty()) {
			found->second.expanded ? collapse(found->first) : expand(found->first);
		}
	};
	provider.focused = [this] {
		auto row = ListView_GetNextItem(handle(), -1, LVNI_FOCUSED);
		return row >= 0 ?
			static_cast<accessibility::ItemId>(getData(row)) :
			accessibility::ItemId();
	};
	provider.setFocus = [this](accessibility::ItemId id) {
		auto row = findData(static_cast<LPARAM>(id));
		if(row >= 0) {
			::SetFocus(handle());
			setSelected(row);
		}
	};
	setAccessibleItems(provider);
	onSelectionChanged([this] {
		auto row = ListView_GetNextItem(handle(), -1, LVNI_FOCUSED);
		if(row >= 0) {
			raiseAccessibleItemEvent(
				static_cast<accessibility::ItemId>(getData(row)), 20012);
		}
	});
}

LRESULT TableTree::sendMsg(UINT msg, WPARAM wParam, LPARAM lParam) {
	// send with a direct dispatcher call to avoid loops (since we catch messages in handleMessage).
	MSG directMsg { handle(), msg, wParam, lParam };
	return getDispatcher().chain(directMsg);
}

}
