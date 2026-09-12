/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
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

#include "stdafx.h"

#include "StatsFrame.h"

#include <dcpp/Client.h>
#include <dcpp/ClientManager.h>
#include <dcpp/ConnectionManager.h>
#include <dcpp/ConnectivityManager.h>
#include <dcpp/DownloadManager.h>
#include <dcpp/format.h>
#include <dcpp/SearchManager.h>
#include <dcpp/Socket.h>
#include <dcpp/ThrottleManager.h>
#include <dcpp/TimerManager.h>
#include <dcpp/UploadManager.h>

#include <dwt/resources/Brush.h>
#include <dwt/resources/Bitmap.h>
#include <dwt/resources/Font.h>
#include <dwt/resources/Pen.h>

namespace {

tstring formatConnectionMode(int mode) {
	switch(mode) {
	case SettingsManager::INCOMING_ACTIVE: return T_("Active");
	case SettingsManager::INCOMING_ACTIVE_UPNP: return T_("Active (mapped)");
	case SettingsManager::INCOMING_PASSIVE: return T_("Passive");
	default: return T_("Disabled");
	}
}

tstring formatLimit(int limit) {
	return limit > 0 ? Text::toT(Util::formatBytes(static_cast<int64_t>(limit) * 1024) + "/s") : T_("Disabled");
}

int64_t clampByteCount(uint64_t bytes) {
	return static_cast<int64_t>(std::min<uint64_t>(bytes, INT64_MAX));
}

COLORREF blendColor(COLORREF foreground, COLORREF background, unsigned foregroundPercent) {
	auto blend = [foregroundPercent](BYTE foregroundPart, BYTE backgroundPart) {
		return static_cast<BYTE>((foregroundPart * foregroundPercent + backgroundPart * (100 - foregroundPercent)) / 100);
	};
	return RGB(blend(GetRValue(foreground), GetRValue(background)),
		blend(GetGValue(foreground), GetGValue(background)),
		blend(GetBValue(foreground), GetBValue(background)));
}

tstring formatField(const string& value) {
	return value.empty() ? T_("Not set") : Text::toT(value);
}

tstring formatSpeed(int64_t speed) {
	return Text::toT(Util::formatBytes(speed) + "/s");
}

tstring formatTrafficPair(const tstring& down, const tstring& up) {
	return str(TF_("D: %1% | U: %2%") % down % up);
}

tstring formatAddressPair(const tstring& v4, const tstring& v6) {
	return str(TF_("v4: %1% | v6: %2%") % v4 % v6);
}

tstring formatAge(long seconds) {
	if(seconds < 60) {
		return _T("-") + str(TF_("%1% s") % seconds);
	}
	if(seconds % 3600 == 0) {
		return _T("-") + str(TF_("%1% h") % (seconds / 3600));
	}
	return _T("-") + str(TF_("%1% min") % (seconds / 60));
}

int64_t niceScale(int64_t speed) {
	if(speed <= 0) {
		return 1024;
	}

	// Leave some headroom and round the scale to a readable 1/2/5 multiple.
	double target = static_cast<double>(speed) * 1.1;
	double magnitude = pow(10.0, floor(log10(target)));
	double normalized = target / magnitude;
	double rounded = normalized <= 1.0 ? 1.0 : normalized <= 2.0 ? 2.0 : normalized <= 5.0 ? 5.0 : 10.0;
	double result = rounded * magnitude;
	return result >= static_cast<double>(INT64_MAX) ? INT64_MAX : static_cast<int64_t>(result);
}

}

const string StatsFrame::id = "Stats";
const string& StatsFrame::getId() const { return id; }

StatsFrame::StatsFrame(TabViewPtr parent) :
	BaseType(parent, T_("Network Statistics"), IDH_NET_STATS, IDI_NET_STATS),
	smoothPlotWidth(0),
	smoothPlotHeight(0),
	secondaryTextColor(0),
	splitRatio(SETTING(STATS_PANED_POS)),
	movingSplitter(false),
	width(0),
	lastTick(GET_TICK()),
	scrollTick(0),
	lastUp(Socket::getTotalUp()),
	lastDown(Socket::getTotalDown()),
	currentUp(0),
	currentDown(0),
	peakUp(0),
	peakDown(0),
	max(1024)
{
	updateColors();
	onCommand([this] {
		updateColors();
		::InvalidateRect(handle(), nullptr, FALSE);
	}, ID_UPDATECOLOR);

	// The complete frame is painted from a back buffer; erasing first causes a
	// visible flash between WM_ERASEBKGND and the buffered blit.
	noEraseBackground();
	onLeftMouseDown([this](const dwt::MouseEvent& mouseEvent) {
		auto clientWidth = getClientSize().x;
		auto x = dwt::ClientCoordinate(mouseEvent.pos, this).x();
		auto split = getSplitterPosition(clientWidth);
		if(x < split - SPLITTER_HIT_RADIUS || x > split + SPLITTER_HIT_RADIUS) {
			return false;
		}

		movingSplitter = true;
		::SetCapture(handle());
		::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
		return true;
	});
	onMouseMove([this](const dwt::MouseEvent& mouseEvent) {
		auto clientWidth = getClientSize().x;
		auto x = dwt::ClientCoordinate(mouseEvent.pos, this).x();
		if(movingSplitter) {
			if(mouseEvent.ButtonPressed != dwt::MouseEvent::LEFT) {
				movingSplitter = false;
				if(::GetCapture() == handle()) {
					::ReleaseCapture();
				}
				return true;
			}
			moveSplitter(x);
		}

		auto split = getSplitterPosition(clientWidth);
		if(movingSplitter || (x >= split - SPLITTER_HIT_RADIUS && x <= split + SPLITTER_HIT_RADIUS)) {
			::SetCursor(::LoadCursor(nullptr, IDC_SIZEWE));
			return true;
		}
		return false;
	});
	onLeftMouseUp([this](const dwt::MouseEvent&) {
		if(!movingSplitter) {
			return false;
		}

		movingSplitter = false;
		SettingsManager::getInstance()->set(SettingsManager::STATS_PANED_POS, splitRatio);
		if(::GetCapture() == handle()) {
			::ReleaseCapture();
		}
		return true;
	});

	onPainting([this](dwt::PaintCanvas& canvas) {
		dwt::Rectangle rect { canvas.getPaintRect() };
		if(rect.width() == 0 || rect.height() == 0)
			return;
		dwt::BufferedCanvas<dwt::FreeCanvas> buffered(canvas.handle());
		draw(buffered, rect);
		buffered.blast(rect);
	});
	onPrinting([this](dwt::Canvas& canvas) { draw(canvas, dwt::Rectangle(getClientSize())); });

	updateStats(lastDown, lastUp);

	layout();

	setTimer([this] { return eachSecond(); }, 1000);
}

StatsFrame::~StatsFrame() {
	SettingsManager::getInstance()->set(SettingsManager::STATS_PANED_POS, splitRatio);
}

void StatsFrame::updateColors() {
	// Every derived shade uses the current global background as its base so a
	// palette update cannot leave construction-time colors in the custom canvas.
	setColor(WinUtil::textColor, WinUtil::bgColor);
	setFont(WinUtil::font);
	headingFont = WinUtil::font->makeBold();
	backgroundBrush = dwt::BrushPtr(new dwt::Brush(WinUtil::bgColor));
	cardBrush = dwt::BrushPtr(new dwt::Brush(blendColor(WinUtil::textColor, WinUtil::bgColor, 4)));
	graphBrush = dwt::BrushPtr(new dwt::Brush(blendColor(WinUtil::textColor, WinUtil::bgColor, 3)));
	borderPen = dwt::PenPtr(new dwt::Pen(blendColor(WinUtil::textColor, WinUtil::bgColor, 24)));
	upPen = dwt::PenPtr(new dwt::Pen(SETTING(UPLOAD_BG_COLOR), dwt::Pen::Solid, 2));
	downPen = dwt::PenPtr(new dwt::Pen(SETTING(DOWNLOAD_BG_COLOR), dwt::Pen::Solid, 2));
	secondaryTextColor = blendColor(WinUtil::textColor, WinUtil::bgColor, 62);
}

void StatsFrame::draw(dwt::Canvas& canvas, const dwt::Rectangle& rect) {
	{
		auto select(canvas.select(*backgroundBrush));
		::BitBlt(canvas.handle(), rect.x(), rect.y(), rect.width(), rect.height(), NULL, 0, 0, PATCOPY);
	}

	canvas.setTextColor(WinUtil::textColor);
	canvas.setBkColor(WinUtil::bgColor);

	dwt::Rectangle client(getClientSize());
	long split = getSplitterPosition(client.width());
	dwt::Rectangle infoRect(client.left(), client.top(), split, client.height());
	dwt::Rectangle graphRect(split, client.top(), client.width() - split, client.height());

	drawInfo(canvas, infoRect);
	drawGraph(canvas, graphRect);

	{
		auto select(canvas.select(*borderPen));
		canvas.line(split, client.top() + PANEL_PADDING, split, client.bottom() - PANEL_PADDING);
	}
}

void StatsFrame::drawInfo(dwt::Canvas& canvas, const dwt::Rectangle& rect) {
	if(rect.width() <= PANEL_PADDING * 2 || rect.height() <= PANEL_PADDING * 2) {
		return;
	}

	auto transparent(canvas.setBkMode(true));
	auto selectFont(canvas.select(*WinUtil::font));
	long fontHeight = getTextSize(_T("Ag")).y;
	long y = rect.top() + PANEL_PADDING;

	{
		auto selectFont(canvas.select(*headingFont));
		dwt::Rectangle titleRect(rect.left() + PANEL_PADDING, y, rect.width() - PANEL_PADDING * 2, fontHeight + 4);
		canvas.drawText(T_("Network overview"), titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	y += fontHeight + 5;
	canvas.setTextColor(secondaryTextColor);
	dwt::Rectangle subtitleRect(rect.left() + PANEL_PADDING, y, rect.width() - PANEL_PADDING * 2, fontHeight + 3);
	canvas.drawText(T_("Live activity, connectivity and transfer state"), subtitleRect,
		DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	canvas.setTextColor(WinUtil::textColor);
	y += fontHeight + 10;

	size_t totalRows = 0;
	for(const auto& section: infoSections) {
		totalRows += section.rows.size();
	}
	long sectionHeaderHeight = fontHeight + 10;
	long sectionGap = 8;
	long rowHeight = fontHeight + 5;
	long available = std::max(0L, rect.bottom() - PANEL_PADDING - y);
	long fixedHeight = static_cast<long>(infoSections.size()) * (sectionHeaderHeight + sectionGap) - sectionGap;
	if(totalRows > 0 && fixedHeight + rowHeight * static_cast<long>(totalRows) > available) {
		rowHeight = std::max(fontHeight + 1,
			(available - fixedHeight) / static_cast<long>(totalRows));
	}

	for(const auto& section: infoSections) {
		long sectionHeight = sectionHeaderHeight + rowHeight * static_cast<long>(section.rows.size());
		dwt::Rectangle card(rect.left() + PANEL_PADDING, y, rect.width() - PANEL_PADDING * 2, sectionHeight);
		if(card.top() >= rect.bottom() - PANEL_PADDING) {
			break;
		}

		{
			auto selectBrush(canvas.select(*cardBrush));
			auto selectPen(canvas.select(*borderPen));
			::RoundRect(canvas.handle(), card.left(), card.top(), card.right(), card.bottom(), 8, 8);
		}

		long textLeft = card.left() + 10;
		long textRight = card.right() - 10;
		long valueLeft = card.left() + card.width() * 43 / 100;
		{
			auto selectFont(canvas.select(*headingFont));
			dwt::Rectangle headingRect(textLeft, card.top() + 5, textRight - textLeft, fontHeight + 2);
			canvas.drawText(section.title, headingRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
		}

		long rowY = card.top() + sectionHeaderHeight;
		for(const auto& row: section.rows) {
			auto selectFont(canvas.select(*WinUtil::font));
			canvas.setTextColor(secondaryTextColor);
			dwt::Rectangle labelRect(textLeft, rowY, valueLeft - textLeft - 8, rowHeight);
			canvas.drawText(row.first, labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

			canvas.setTextColor(WinUtil::textColor);
			dwt::Rectangle valueRect(valueLeft, rowY, textRight - valueLeft, rowHeight);
			canvas.drawText(row.second, valueRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			rowY += rowHeight;
		}

		y += sectionHeight + sectionGap;
	}
	canvas.setTextColor(WinUtil::textColor);
}

void StatsFrame::drawGraph(dwt::Canvas& canvas, const dwt::Rectangle& rect) {
	if(rect.width() <= PANEL_PADDING * 2 || rect.height() <= PANEL_PADDING * 2) {
		return;
	}

	auto transparent(canvas.setBkMode(true));
	auto selectFont(canvas.select(*WinUtil::font));
	long fontHeight = getTextSize(_T("Ag")).y;
	long titleY = rect.top() + PANEL_PADDING;

	{
		auto selectFont(canvas.select(*headingFont));
		dwt::Rectangle titleRect(rect.left() + PANEL_PADDING, titleY, rect.width() - PANEL_PADDING * 2, fontHeight + 4);
		canvas.drawText(T_("Traffic history"), titleRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	long subtitleY = titleY + fontHeight + 5;
	canvas.setTextColor(secondaryTextColor);
	dwt::Rectangle subtitleRect(rect.left() + PANEL_PADDING, subtitleY, rect.width() - PANEL_PADDING * 2, fontHeight + 3);
	canvas.drawText(str(TF_("All socket traffic, %1%-second moving average") % AVG_SIZE), subtitleRect,
		DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	canvas.setTextColor(WinUtil::textColor);

	long legendY = subtitleY + fontHeight + 7;
	auto downloadLegend = str(TF_("Download %1%") % formatSpeed(currentDown));
	auto uploadLegend = str(TF_("Upload %1%") % formatSpeed(currentUp));
	auto downloadLegendSize = getTextSize(downloadLegend);
	auto uploadLegendSize = getTextSize(uploadLegend);
	long legendX = rect.left() + PANEL_PADDING;
	{
		auto select(canvas.select(*downPen));
		canvas.line(legendX, legendY + fontHeight / 2, legendX + 20, legendY + fontHeight / 2);
	}
	canvas.setTextColor(SETTING(DOWNLOAD_BG_COLOR));
	dwt::Rectangle downloadLegendRect(legendX + 26, legendY, downloadLegendSize.x, fontHeight + 2);
	canvas.drawText(downloadLegend, downloadLegendRect, DT_LEFT | DT_TOP | DT_SINGLELINE);

	legendX += 34 + downloadLegendSize.x;
	if(legendX + 26 + uploadLegendSize.x >= rect.right() - PANEL_PADDING) {
		legendX = rect.left() + PANEL_PADDING;
		legendY += fontHeight + 4;
	}
	{
		auto select(canvas.select(*upPen));
		canvas.line(legendX, legendY + fontHeight / 2, legendX + 20, legendY + fontHeight / 2);
	}
	canvas.setTextColor(SETTING(UPLOAD_BG_COLOR));
	dwt::Rectangle uploadLegendRect(legendX + 26, legendY, uploadLegendSize.x, fontHeight + 2);
	canvas.drawText(uploadLegend, uploadLegendRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	canvas.setTextColor(WinUtil::textColor);

	const int horizontalDivisions = 4;
	long scaleLabelWidth = 0;
	for(int i = 0; i <= horizontalDivisions; ++i) {
		auto label = Text::toT(Util::formatBytes(max * i / horizontalDivisions) + "/s");
		scaleLabelWidth = std::max(scaleLabelWidth, getTextSize(label).x);
	}
	scaleLabelWidth += 6; // Allow for glyph overhang and ClearType edge pixels.
	// Reserve the measured label width plus a fixed gap. The previous estimate
	// left the label rectangle a few pixels narrower than values such as MiB/s.
	long plotLeft = rect.left() + PANEL_PADDING + scaleLabelWidth + 12;
	long plotTop = legendY + fontHeight + 12;
	long plotRight = rect.right() - PANEL_PADDING;
	long plotBottom = rect.bottom() - fontHeight - PANEL_PADDING - 5;
	if(plotRight - plotLeft < 40 || plotBottom - plotTop < 40) {
		return;
	}
	dwt::Rectangle plot(plotLeft, plotTop, plotRight - plotLeft, plotBottom - plotTop);

	for(int i = 0; i <= horizontalDivisions; ++i) {
		long y = plot.bottom() - (plot.height() * i / horizontalDivisions);
		auto label = Text::toT(Util::formatBytes(max * i / horizontalDivisions) + "/s");
		dwt::Rectangle labelRect(rect.left() + PANEL_PADDING, y - fontHeight / 2,
			scaleLabelWidth, fontHeight + 2);
		canvas.setTextColor(secondaryTextColor);
		canvas.drawText(label, labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	}

	long visibleSeconds = plot.width() / PIX_PER_SEC;
	static const int timeSteps[] = { 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
	int timeStep = timeSteps[0];
	for(auto candidate: timeSteps) {
		timeStep = candidate;
		if(candidate * PIX_PER_SEC >= 80) {
			break;
		}
	}

	if(!drawSupersampledPlot(canvas, plot, timeStep)) {
		drawPlot(canvas, plot, timeStep, 1);
	}

	for(long age = timeStep; age < visibleSeconds; age += timeStep) {
		long x = plot.right() - age * PIX_PER_SEC;
		auto label = formatAge(age);
		dwt::Rectangle labelRect(x - 45, plot.bottom() + 4, 90, fontHeight + 2);
		canvas.drawText(label, labelRect, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
	}
	dwt::Rectangle nowRect(plot.right() - 45, plot.bottom() + 4, 45, fontHeight + 2);
	canvas.drawText(T_("Now"), nowRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);
	canvas.setTextColor(WinUtil::textColor);
}

void StatsFrame::drawPlot(dwt::Canvas& canvas, const dwt::Rectangle& plot, int timeStep, int scale) {
	const auto scaled = [scale](long value) { return value * scale; };
	dwt::Pen plotBorder(blendColor(WinUtil::textColor, WinUtil::bgColor, 24), dwt::Pen::Solid, scale);
	dwt::Pen plotGrid(blendColor(WinUtil::textColor, WinUtil::bgColor, 18), dwt::Pen::Solid, scale);
	dwt::Pen downloadGlow(blendColor(SETTING(DOWNLOAD_BG_COLOR), WinUtil::bgColor, 38), dwt::Pen::Solid, scaled(5));
	dwt::Pen uploadGlow(blendColor(SETTING(UPLOAD_BG_COLOR), WinUtil::bgColor, 38), dwt::Pen::Solid, scaled(5));
	dwt::Pen downloadLine(SETTING(DOWNLOAD_BG_COLOR), dwt::Pen::Solid, scaled(2));
	dwt::Pen uploadLine(SETTING(UPLOAD_BG_COLOR), dwt::Pen::Solid, scaled(2));

	canvas.fill(plot, *graphBrush);
	for(int i = 0; i <= 4; ++i) {
		long y = plot.bottom() - (plot.height() * i / 4);
		auto select(canvas.select(plotGrid));
		canvas.line(plot.left(), y, plot.right(), y);
	}

	long visibleSeconds = plot.width() / scaled(PIX_PER_SEC);
	for(long age = timeStep; age < visibleSeconds; age += timeStep) {
		long x = plot.right() - scaled(age * PIX_PER_SEC);
		auto select(canvas.select(plotGrid));
		canvas.line(x, plot.top(), x, plot.bottom());
	}

	int saved = ::SaveDC(canvas.handle());
	if(saved != 0) {
		::IntersectClipRect(canvas.handle(), plot.left() + scale, plot.top() + scale, plot.right() - scale, plot.bottom() - scale);
	}
	{
		auto select(canvas.select(downloadGlow));
		drawSeries(canvas.handle(), down, plot, scale);
	}
	{
		auto select(canvas.select(uploadGlow));
		drawSeries(canvas.handle(), up, plot, scale);
	}
	{
		auto select(canvas.select(downloadLine));
		drawSeries(canvas.handle(), down, plot, scale);
	}
	{
		auto select(canvas.select(uploadLine));
		drawSeries(canvas.handle(), up, plot, scale);
	}
	if(saved != 0) {
		::RestoreDC(canvas.handle(), saved);
	}

	// Highlight the newest sample while using the same bounded coordinate mapping as the curve.
	auto drawCurrentPoint = [&](int64_t speed, COLORREF color, dwt::Pen& seriesPen) {
		long graphTop = plot.top() + scale;
		long graphBottom = plot.bottom() - scale;
		long graphHeight = std::max(0L, graphBottom - graphTop);
		long y = graphBottom - (max > 0 ? static_cast<long>(static_cast<double>(speed) * graphHeight / max) : 0);
		y = std::max(graphTop, std::min(graphBottom, y));
		long radius = scaled(4);
		long x = plot.right() - radius - scale;
		dwt::Brush pointBrush(color);
		auto selectBrush(canvas.select(pointBrush));
		auto selectPen(canvas.select(seriesPen));
		canvas.ellipse(x - radius, y - radius, x + radius, y + radius);
	};
	if(!down.empty()) {
		drawCurrentPoint(currentDown, SETTING(DOWNLOAD_BG_COLOR), downloadLine);
	}
	if(!up.empty()) {
		drawCurrentPoint(currentUp, SETTING(UPLOAD_BG_COLOR), uploadLine);
	}

	{
		auto select(canvas.select(plotBorder));
		canvas.line(plot);
	}
}

bool StatsFrame::drawSupersampledPlot(dwt::Canvas& canvas, const dwt::Rectangle& plot, int timeStep) {
	if(plot.width() <= 0 || plot.height() <= 0) {
		return false;
	}

	// Bound the temporary bitmap to 64 MiB at 32 bits per pixel. Large or
	// unusually tall windows step down to 2x, then to the allocation-free path.
	constexpr uint64_t maxSupersampledPixels = 16ULL * 1024 * 1024;
	uint64_t plotPixels = static_cast<uint64_t>(plot.width()) * static_cast<uint64_t>(plot.height());
	int renderScale = GRAPH_SUPERSAMPLE;
	while(renderScale > 1 && plotPixels > maxSupersampledPixels / static_cast<uint64_t>(renderScale * renderScale)) {
		--renderScale;
	}
	if(renderScale <= 1 || plot.width() > LONG_MAX / renderScale || plot.height() > LONG_MAX / renderScale) {
		smoothPlotBitmap.reset();
		smoothPlotWidth = 0;
		smoothPlotHeight = 0;
		return false;
	}

	long scaledWidth = plot.width() * renderScale;
	long scaledHeight = plot.height() * renderScale;
	if(!smoothPlotBitmap || smoothPlotWidth != scaledWidth || smoothPlotHeight != scaledHeight) {
		// Reuse the surface between one-second updates. It is released before a
		// resize allocation so the old buffer cannot make the replacement fail.
		smoothPlotBitmap.reset();
		smoothPlotWidth = 0;
		smoothPlotHeight = 0;
		BITMAPINFO info { { sizeof(BITMAPINFOHEADER), scaledWidth, scaledHeight, 1, 32, BI_RGB } };
		HBITMAP bitmapHandle = ::CreateDIBSection(canvas.handle(), &info, DIB_RGB_COLORS, nullptr, nullptr, 0);
		if(!bitmapHandle) {
			return false;
		}
		smoothPlotBitmap = dwt::BitmapPtr(new dwt::Bitmap(bitmapHandle));
		smoothPlotWidth = scaledWidth;
		smoothPlotHeight = scaledHeight;
	}

	dwt::CompatibleCanvas smoothCanvas(canvas.handle());
	if(!smoothCanvas.handle()) {
		return false;
	}

	{
		auto selectBitmap(smoothCanvas.select(*smoothPlotBitmap));
		drawPlot(smoothCanvas, dwt::Rectangle(0, 0, scaledWidth, scaledHeight), timeStep, renderScale);

		int oldMode = ::SetStretchBltMode(canvas.handle(), HALFTONE);
		POINT oldOrigin { };
		::SetBrushOrgEx(canvas.handle(), 0, 0, &oldOrigin);
		BOOL result = ::StretchBlt(canvas.handle(), plot.left(), plot.top(), plot.width(), plot.height(),
			smoothCanvas.handle(), 0, 0, scaledWidth, scaledHeight, SRCCOPY);
		::SetBrushOrgEx(canvas.handle(), oldOrigin.x, oldOrigin.y, nullptr);
		if(oldMode != 0) {
			::SetStretchBltMode(canvas.handle(), oldMode);
		}
		return result != FALSE;
	}
}

void StatsFrame::drawSeries(HDC dc, const StatList& stats, const dwt::Rectangle& plot, int scale) {
	if(stats.empty()) {
		return;
	}

	long graphTop = plot.top() + scale;
	long graphBottom = plot.bottom() - scale;
	long graphHeight = std::max(0L, graphBottom - graphTop);
	long x = plot.right() - scale * 5;
	vector<POINT> samples;
	samples.reserve(std::min<size_t>(stats.size(), static_cast<size_t>(plot.width() / std::max(1, scale)) + 2));
	for(const auto& stat: stats) {
		long y = graphBottom - (max > 0 ? static_cast<long>(static_cast<double>(stat.speed) * graphHeight / max) : 0);
		y = std::max(graphTop, std::min(graphBottom, y));
		samples.push_back({ x, y });
		if(x < plot.left()) {
			break;
		}
		x -= static_cast<long>(stat.scroll) * scale;
	}

	if(samples.size() < 2) {
		return;
	}

	// Catmull-Rom tangents converted to cubic Bezier controls retain every real
	// sample. Clamping each control to its segment prevents invented peaks while
	// smoothing the corners that made the one-pixel GDI polyline look stair-stepped.
	vector<POINT> curve;
	curve.reserve(1 + (samples.size() - 1) * 3);
	curve.push_back(samples.front());
	for(size_t i = 0; i + 1 < samples.size(); ++i) {
		const auto& p0 = samples[i == 0 ? i : i - 1];
		const auto& p1 = samples[i];
		const auto& p2 = samples[i + 1];
		const auto& p3 = samples[i + 2 < samples.size() ? i + 2 : i + 1];
		POINT control1 { p1.x + (p2.x - p0.x) / 6, p1.y + (p2.y - p0.y) / 6 };
		POINT control2 { p2.x - (p3.x - p1.x) / 6, p2.y - (p3.y - p1.y) / 6 };
		long minX = std::min(p1.x, p2.x);
		long maxX = std::max(p1.x, p2.x);
		long minY = std::min(p1.y, p2.y);
		long maxY = std::max(p1.y, p2.y);
		control1.x = std::max(minX, std::min(maxX, control1.x));
		control1.y = std::max(minY, std::min(maxY, control1.y));
		control2.x = std::max(minX, std::min(maxX, control2.x));
		control2.y = std::max(minY, std::min(maxY, control2.y));
		curve.push_back(control1);
		curve.push_back(control2);
		curve.push_back(p2);
	}
	::PolyBezier(dc, curve.data(), static_cast<DWORD>(curve.size()));
}

long StatsFrame::getSplitterPosition(long clientWidth) const {
	if(clientWidth <= 0) {
		return 0;
	}

	// Keep both panes usable while allowing either side to take most of a large
	// frame. On small windows the two minimums collapse evenly.
	long minimum = std::min<long>(MIN_PANE_WIDTH, clientWidth / 2);
	long position = static_cast<long>(splitRatio * clientWidth);
	return std::max(minimum, std::min(clientWidth - minimum, position));
}

void StatsFrame::moveSplitter(long x) {
	auto clientWidth = getClientSize().x;
	if(clientWidth <= 0) {
		return;
	}

	long position = getSplitterPosition(clientWidth);
	long minimum = std::min<long>(MIN_PANE_WIDTH, clientWidth / 2);
	long newPosition = std::max(minimum, std::min(clientWidth - minimum, x));
	if(newPosition == position) {
		return;
	}

	splitRatio = static_cast<double>(newPosition) / clientWidth;
	layout();
}

void StatsFrame::layout() {
	dwt::Rectangle r { getClientSize() };

	// Retain enough samples to fill the current graph pane.
	width = std::max(0L, r.width() - getSplitterPosition(r.width()) - 90);

	::InvalidateRect(handle(), nullptr, FALSE);
}

bool StatsFrame::eachSecond() {
	uint64_t tick = GET_TICK();
	uint64_t tdiff = tick - lastTick;
	if(tdiff == 0)
		return true;

	uint64_t scrollms = (tdiff + scrollTick)*PIX_PER_SEC;
	uint64_t scroll = scrollms / 1000;

	if(scroll == 0)
		return true;

	scrollTick = scrollms - (scroll * 1000);

	uint64_t d = Socket::getTotalDown();
	uint64_t ddiff = d >= lastDown ? d - lastDown : 0;
	uint64_t u = Socket::getTotalUp();
	uint64_t udiff = u >= lastUp ? u - lastUp : 0;

	addTick(clampByteCount(ddiff), tdiff, down, downAvg, scroll);
	addTick(clampByteCount(udiff), tdiff, up, upAvg, scroll);

	currentDown = down.empty() ? 0 : down.front().speed;
	currentUp = up.empty() ? 0 : up.front().speed;
	peakDown = std::max(peakDown, currentDown);
	peakUp = std::max(peakUp, currentUp);

	int64_t mspeed = 0;
	auto i = down.begin();
	for(; i != down.end(); ++i) {
		if(mspeed < i->speed)
			mspeed = i->speed;
	}
	for(i = up.begin(); i != up.end(); ++i) {
		if(mspeed < i->speed)
			mspeed = i->speed;
	}
	auto targetMax = niceScale(mspeed);
	if(targetMax > max || targetMax < (max * 3 / 4)) {
		max = targetMax;
	}

	lastTick = tick;
	lastUp = u;
	lastDown = d;
	updateStats(d, u);
	::InvalidateRect(handle(), nullptr, FALSE);
	return true;
}

void StatsFrame::updateStats(uint64_t totalDown, uint64_t totalUp) {
	auto connection4 = formatConnectionMode(CONNSETTING(INCOMING_CONNECTIONS));
	auto connection6 = formatConnectionMode(CONNSETTING(INCOMING_CONNECTIONS6));
	double ratio = totalDown > 0 ? static_cast<double>(totalUp) / static_cast<double>(totalDown) : 0;
	auto downloads = DownloadManager::getInstance()->getDownloadCount();
	auto uploads = UploadManager::getInstance()->getUploadCount();
	auto freeSlots = UploadManager::getInstance()->getFreeSlots();
	auto totalSlots = SETTING(SLOTS);
	auto waitingUsers = UploadManager::getInstance()->getWaitingUsers().size();

	auto sessionTraffic = formatTrafficPair(Text::toT(Util::formatBytes(clampByteCount(totalDown))),
		Text::toT(Util::formatBytes(clampByteCount(totalUp))));
	auto lifetimeTraffic = formatTrafficPair(Text::toT(Util::formatBytes(SETTING(TOTAL_DOWNLOAD))),
		Text::toT(Util::formatBytes(SETTING(TOTAL_UPLOAD))));
	auto transferSpeed = formatTrafficPair(formatSpeed(DownloadManager::getInstance()->getRunningAverage()),
		formatSpeed(UploadManager::getInstance()->getRunningAverage()));
	auto peakSpeed = formatTrafficPair(formatSpeed(peakDown), formatSpeed(peakUp));
	auto limits = formatTrafficPair(formatLimit(ThrottleManager::getDownLimit()), formatLimit(ThrottleManager::getUpLimit()));
	auto externalAddresses = formatAddressPair(formatField(CONNSETTING(EXTERNAL_IP)), formatField(CONNSETTING(EXTERNAL_IP6)));
	auto boundInterfaces = formatAddressPair(formatField(CONNSETTING(BIND_ADDRESS)), formatField(CONNSETTING(BIND_ADDRESS6)));
	auto ports = str(TF_("TCP: %1% | TLS: %2% | UDP: %3%") %
		formatField(ConnectionManager::getInstance()->getPort()) %
		formatField(ConnectionManager::getInstance()->getSecurePort()) %
		formatField(SearchManager::getInstance()->getPort()));

	infoSections = {
		{ T_("Traffic"), {
			{ T_("Download (all sockets)"), formatSpeed(currentDown) },
			{ T_("Upload (all sockets)"), formatSpeed(currentUp) },
			{ T_("Transfer payload"), transferSpeed },
			{ T_("Peak while open"), peakSpeed },
			{ T_("Session total"), sessionTraffic },
			{ T_("Lifetime total"), lifetimeTraffic },
			{ T_("Ratio (up/down)"), str(TF_("%1$0.2f") % ratio) }
		} },
		{ T_("Connectivity"), {
			{ T_("Hubs (normal/reg/op)"), Text::toT(Client::getCounts()) },
			{ T_("Online users"), Text::toT(std::to_string(ClientManager::getInstance()->getUserCount())) },
			{ T_("IPv4 mode"), connection4 },
			{ T_("IPv6 mode"), connection6 },
			{ T_("External addresses"), externalAddresses },
			{ T_("Bound interfaces"), boundInterfaces },
			{ T_("Listening ports"), ports }
		} },
		{ T_("Transfers"), {
			{ T_("Active transfers"), formatTrafficPair(Text::toT(std::to_string(downloads)), Text::toT(std::to_string(uploads))) },
			{ T_("Upload slots"), str(TF_("%1% free of %2%") % freeSlots % totalSlots) },
			{ T_("Users waiting"), Text::toT(std::to_string(waitingUsers)) },
			{ T_("Bandwidth limits"), limits }
		} }
	};
}

void StatsFrame::addTick(int64_t bdiff, int64_t tdiff, StatList& lst, AvgList& avg, int scroll) {
	while((int)lst.size() > ((width / PIX_PER_SEC) + 1) ) {
		lst.pop_back();
	}
	while(avg.size() >= AVG_SIZE ) {
		avg.pop_back();
	}
	int64_t bspeed = bdiff * (int64_t)1000 / tdiff;
	avg.push_front(bspeed);

	bspeed = 0;

	for(auto& ai: avg) {
		bspeed += ai;
	}

	bspeed /= avg.size();
	lst.emplace_front(scroll, bspeed);
}
