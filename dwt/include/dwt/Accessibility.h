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

#ifndef DWT_ACCESSIBILITY_H
#define DWT_ACCESSIBILITY_H

#include <dwt/Rectangle.h>
#include <dwt/tstring.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace dwt { namespace accessibility {

enum ControlType : long {
	Button = 50000,
	Calendar = 50001,
	CheckBox = 50002,
	ComboBox = 50003,
	Edit = 50004,
	Hyperlink = 50005,
	Image = 50006,
	ListItem = 50007,
	List = 50008,
	Menu = 50009,
	MenuBar = 50010,
	MenuItem = 50011,
	ProgressBar = 50012,
	RadioButton = 50013,
	ScrollBar = 50014,
	Slider = 50015,
	Spinner = 50016,
	StatusBar = 50017,
	Tab = 50018,
	TabItem = 50019,
	Text = 50020,
	ToolBar = 50021,
	ToolTip = 50022,
	Tree = 50023,
	TreeItem = 50024,
	Custom = 50025,
	Group = 50026,
	Thumb = 50027,
	DataGrid = 50028,
	DataItem = 50029,
	Document = 50030,
	SplitButton = 50031,
	Window = 50032,
	Pane = 50033,
	Header = 50034,
	HeaderItem = 50035,
	Table = 50036,
	TitleBar = 50037,
	Separator = 50038
};

struct RangeValueProvider {
	std::function<double ()> getValue;
	std::function<void (double)> setValue;
	double minimum = 0;
	double maximum = 100;
	double smallChange = 1;
	double largeChange = 10;
	bool readOnly = false;
};

enum ScrollAmount {
	LargeDecrement = 0,
	SmallDecrement = 1,
	NoAmount = 2,
	LargeIncrement = 3,
	SmallIncrement = 4
};

struct ScrollProvider {
	std::function<void (ScrollAmount, ScrollAmount)> scroll;
	std::function<void (double, double)> setPercent;
	std::function<double ()> horizontalPercent;
	std::function<double ()> verticalPercent;
	std::function<double ()> horizontalViewSize;
	std::function<double ()> verticalViewSize;
	std::function<bool ()> horizontallyScrollable;
	std::function<bool ()> verticallyScrollable;
};

using ItemId = std::uintptr_t;

enum ExpandState {
	Collapsed = 0,
	Expanded = 1,
	PartiallyExpanded = 2,
	LeafNode = 3
};

struct ItemProvider {
	std::function<bool (ItemId)> exists;
	std::function<std::vector<ItemId> (ItemId)> children;
	std::function<ItemId (ItemId)> parent;
	std::function<tstring (ItemId)> name;
	std::function<Rectangle (ItemId)> bounds;
	std::function<long (ItemId)> controlType;

	std::function<std::vector<ItemId> ()> selection;
	std::function<bool (ItemId)> selected;
	std::function<void (ItemId)> select;
	std::function<void (ItemId)> addToSelection;
	std::function<void (ItemId)> removeFromSelection;
	bool canSelectMultiple = false;
	bool selectionRequired = false;

	std::function<ExpandState (ItemId)> expandState;
	std::function<void (ItemId)> expand;
	std::function<void (ItemId)> collapse;
	std::function<void (ItemId)> invoke;

	std::function<ItemId ()> focused;
	std::function<void (ItemId)> setFocus;
};

} }

#endif
