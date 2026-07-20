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

#ifndef DCPLUSPLUS_WIN32_STATS_FRAME_H
#define DCPLUSPLUS_WIN32_STATS_FRAME_H

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "StaticFrame.h"

using std::deque;
using std::string;
using std::vector;

class StatsFrame : public StaticFrame<StatsFrame>
{
	typedef StaticFrame<StatsFrame> BaseType;
public:
	static const string id;
	const string& getId() const;

private:
	friend class StaticFrame<StatsFrame>;
	friend class MDIChildFrame<StatsFrame>;

	enum { PIX_PER_SEC = 2 }; // Pixels per second
	enum { AVG_SIZE = 5 };
	enum { PANEL_PADDING = 14 };
	enum { MIN_PANE_WIDTH = 200 };
	enum { SPLITTER_HIT_RADIUS = 5 };

	StatsFrame(TabViewPtr parent);
	virtual ~StatsFrame();

	dwt::FontPtr headingFont;
	dwt::PenPtr borderPen;
	dwt::PenPtr gridPen;
	dwt::PenPtr upPen;
	dwt::PenPtr downPen;
	dwt::PenPtr upGlowPen;
	dwt::PenPtr downGlowPen;
	dwt::BrushPtr backgroundBrush;
	dwt::BrushPtr cardBrush;
	dwt::BrushPtr graphBrush;
	COLORREF secondaryTextColor;

	struct Stat {
		Stat() : scroll(0), speed(0) { }
		Stat(uint32_t aScroll, int64_t aSpeed) : scroll(aScroll), speed(aSpeed) { }
		uint32_t scroll;
		int64_t speed;
	};
	typedef deque<Stat> StatList;
	typedef deque<int64_t> AvgList;
	StatList up;
	StatList down;
	AvgList upAvg;
	AvgList downAvg;

	struct InfoSection {
		tstring title;
		vector<std::pair<tstring, tstring>> rows;
	};
	vector<InfoSection> infoSections;

	double splitRatio;
	bool movingSplitter;
	long width;
	uint32_t lastTick;
	uint32_t scrollTick;
	uint64_t lastUp;
	uint64_t lastDown;
	int64_t currentUp;
	int64_t currentDown;
	int64_t peakUp;
	int64_t peakDown;
	int64_t max;

	void draw(dwt::Canvas& canvas, const dwt::Rectangle& rect);
	void drawInfo(dwt::Canvas& canvas, const dwt::Rectangle& rect);
	void drawGraph(dwt::Canvas& canvas, const dwt::Rectangle& rect);
	void drawSeries(dwt::Canvas& canvas, const StatList& stats, const dwt::Rectangle& plot);
	long getSplitterPosition(long clientWidth) const;
	void moveSplitter(long x);

	void layout();
	bool eachSecond();
	void updateColors();
	void updateStats(uint64_t totalDown, uint64_t totalUp);

	void addTick(int64_t bdiff, int64_t tdiff, StatList& lst, AvgList& avg, int scroll);
};

#endif
