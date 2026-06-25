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

#ifndef DCPLUSPLUS_WIN32_SETTINGS_DIALOG_H
#define DCPLUSPLUS_WIN32_SETTINGS_DIALOG_H

#include <functional>
#include <typeindex>
#include <utility>

#include <dcpp/debug.h>

#include <dwt/widgets/ModalDialog.h>
#include <dwt/widgets/Tree.h>

#include "forward.h"
#include "PropPage.h"

using dwt::TreePtr;

class SettingsDialog : public dwt::ModalDialog
{
public:
	SettingsDialog(dwt::Widget* parent);

	int run();

	virtual ~SettingsDialog();

	template<typename T> T* getPage() {
		for(auto& i: pages) {
			if(i.type == std::type_index(typeid(T))) {
				return dynamic_cast<T*>(ensurePage(i));
			}
		}
		dcassert(0);
		return nullptr;
	}

	template<typename T> void activatePage() {
		for(auto& i: pages) {
			if(i.type == std::type_index(typeid(T))) {
				tree->setSelected(i.item);
				break;
			}
		}
	}

	static const int pluginPagePos;

private:
	friend class PropPage;

	struct PageInfo {
		PageInfo(const std::type_info& type_, HTREEITEM item_, unsigned icon_, std::function<PropPage* ()> create_) :
			type(type_),
			item(item_),
			icon(icon_),
			create(std::move(create_)),
			page(nullptr)
		{
		}

		std::type_index type;
		HTREEITEM item;
		unsigned icon;
		std::function<PropPage* ()> create;
		PropPage* page;
	};

	typedef std::vector<PageInfo> PageList;
	PageList pages;
	PropPage* currentPage;

	GridPtr grid;
	TreePtr tree;
	RichTextBoxPtr help;
	ToolTipPtr tip;

	void updateTitle();
	void write();

	void layout();

	PropPage* ensurePage(PageInfo& info);
	void registerHelp(HWND root);

	bool initDialog();
	static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam);
	void handleChildHelp(dwt::Control* widget);
	bool handleClosing();
	void handleSelectionChanged();
	void handleOKClicked();
	void handleCtrlTab(bool shift);

	// aspects::Help
	void helpImpl(unsigned& id);
};

#endif
