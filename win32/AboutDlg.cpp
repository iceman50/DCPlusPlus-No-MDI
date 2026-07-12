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

#include "stdafx.h"

#include "AboutDlg.h"

#include <dcpp/format.h>
#include <dcpp/HttpManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/SQLiteDB.h>
#include <dcpp/Streams.h>
#include <dcpp/version.h>
#include <GeoIP.h>
#include <bzlib.h>
#include <zlib.h>
#include <dwt/Version.h>
#include <miniupnpc/miniupnpc.h>
#include <natpmp/natpmp.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>

#include <cstring>

#include <dwt/widgets/Grid.h>
#include <dwt/widgets/Label.h>
#include <dwt/widgets/Link.h>

#if defined(__GNUC__)
#include <libdwarf.h>
#endif

#include "resource.h"
#include "WinUtil.h"

using dwt::Grid;
using dwt::GridInfo;
using dwt::Label;
using dwt::Link;

namespace {

string getCompilerVersion() {
#ifdef __MINGW64_VERSION_MAJOR
	return string("MinGW-w64 GCC ") + __VERSION__;
#elif defined(__GNUC__)
	return string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
	return string("MSVC ") + Util::toString(_MSC_VER) + " (" + Util::toString(_MSC_FULL_VER) + ")";
#else
	return "Unknown";
#endif
}

string getBuildArchitecture() {
#ifdef _WIN64
	return "x64";
#elif defined(_WIN32)
	return "x86";
#else
	return "Unknown";
#endif
}

string getNativeArchitecture() {
	SYSTEM_INFO si = {};
	::GetNativeSystemInfo(&si);

	switch(si.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
	case PROCESSOR_ARCHITECTURE_ARM64: return "ARM64";
	case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
	case PROCESSOR_ARCHITECTURE_ARM: return "ARM";
	default: return "Unknown";
	}
}

string getWindowsVersion() {
	OSVERSIONINFOEXW info = {};
	info.dwOSVersionInfoSize = sizeof(info);

	typedef LONG (WINAPI *RtlGetVersionFunc)(PRTL_OSVERSIONINFOW);
	RtlGetVersionFunc rtlGetVersion = nullptr;
	auto ntdll = ::GetModuleHandle(_T("ntdll.dll"));
	auto proc = ntdll ? ::GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
	if(proc) {
		static_assert(sizeof(rtlGetVersion) == sizeof(proc), "Unexpected function pointer size");
		std::memcpy(&rtlGetVersion, &proc, sizeof(rtlGetVersion));
	}

	if(!rtlGetVersion || rtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&info)) != 0) {
		return "Unknown";
	}

	const bool workstation = info.wProductType == VER_NT_WORKSTATION;
	string name;
	if(info.dwMajorVersion == 10 && info.dwMinorVersion == 0) {
		name = workstation ? (info.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10") : "Windows Server";
	} else if(info.dwMajorVersion == 6 && info.dwMinorVersion == 3) {
		name = workstation ? "Windows 8.1" : "Windows Server 2012 R2";
	} else if(info.dwMajorVersion == 6 && info.dwMinorVersion == 2) {
		name = workstation ? "Windows 8" : "Windows Server 2012";
	} else if(info.dwMajorVersion == 6 && info.dwMinorVersion == 1) {
		name = workstation ? "Windows 7" : "Windows Server 2008 R2";
	} else {
		name = workstation ? "Windows" : "Windows Server";
	}

	return str(F_("%1% %2%.%3%.%4%") % name % info.dwMajorVersion % info.dwMinorVersion % info.dwBuildNumber);
}

string getMemoryInfo() {
	MEMORYSTATUSEX memory = {};
	memory.dwLength = sizeof(memory);
	if(!::GlobalMemoryStatusEx(&memory)) {
		return "Unknown";
	}

	return str(F_("%1% total, %2% available") %
		Util::formatBytes(static_cast<int64_t>(memory.ullTotalPhys)) %
		Util::formatBytes(static_cast<int64_t>(memory.ullAvailPhys)));
}

string getCppStandard() {
	return Util::toString(static_cast<long long>(__cplusplus));
}

string getLibintlVersion() {
	return str(F_("%1%.%2%.%3%") %
		((LIBINTL_VERSION >> 16) & 0xff) %
		((LIBINTL_VERSION >> 8) & 0xff) %
		(LIBINTL_VERSION & 0xff));
}

string getVersionString(const char* version) {
	return (version && *version) ? version : "Bundled";
}

string getOpenSSLVersion() {
	const auto runtime = getVersionString(OpenSSL_version(OPENSSL_VERSION));
	if(runtime == OPENSSL_VERSION_TEXT) {
		return runtime;
	}
	return runtime + " (headers: " OPENSSL_VERSION_TEXT ")";
}

void addInfoLine(string& info, const string& name, const string& value) {
	info += name + ": " + value + "\r\n";
}

string getAboutInfo() {
	string info;

	info += "Application\r\n";
	addInfoLine(info, "Full name", FULL_APPNAME);
	addInfoLine(info, "Core identity", APPNAME);
	addInfoLine(info, "Version", VERSIONSTRING);
	addInfoLine(info, "Build type",
#ifdef _DEBUG
		"Debug"
#else
		"Release"
#endif
	);
	addInfoLine(info, "Build architecture", getBuildArchitecture());
	info += "\r\n";

	info += "System\r\n";
	addInfoLine(info, "Windows", getWindowsVersion());
	addInfoLine(info, "Native architecture", getNativeArchitecture());
	addInfoLine(info, "Physical memory", getMemoryInfo());
	info += "\r\n";

	info += "Toolchain\r\n";
	addInfoLine(info, "Compiler", getCompilerVersion());
	addInfoLine(info, "C++ standard", getCppStandard());
	info += "\r\n";

	info += "Libraries\r\n";
	addInfoLine(info, "OpenSSL", getOpenSSLVersion());
	addInfoLine(info, "SQLite", SQLiteDB::getLibraryVersion());
	addInfoLine(info, "zlib", string(zlibVersion()) + " (headers: " ZLIB_VERSION ")");
	addInfoLine(info, "bzip2", BZ2_bzlibVersion());
	addInfoLine(info, "DWT", DWT_VERSION_STRING);
	addInfoLine(info, "MiniUPnPc", string(MINIUPNPC_VERSION) + " (API " + Util::toString(MINIUPNPC_API_VERSION) + ")");
	addInfoLine(info, "libnatpmp", "Bundled");
	addInfoLine(info, "GeoIP C API", getVersionString(GeoIP_lib_version()));
#if defined(__GNUC__)
	addInfoLine(info, "libdwarf", DW_LIBDWARF_VERSION);
#else
	addInfoLine(info, "DbgHelp", "Windows");
#endif
	addInfoLine(info, "GNU gettext/libintl", getLibintlVersion());
#ifdef HAVE_HTMLHELP_H
	addInfoLine(info, "HTML Help", "Enabled");
#else
	addInfoLine(info, "HTML Help", "Disabled");
#endif

	return info;
}

} // namespace

AboutDlg::AboutDlg(dwt::Widget* parent) :
dwt::ModalDialog(parent),
grid(0),
version(0),
c(nullptr)
{
	onInitDialog([this] { return handleInitDialog(); });
}

AboutDlg::~AboutDlg() {
}

int AboutDlg::run() {
	create(dwt::Point(400, 600));
	return show();
}

bool AboutDlg::handleInitDialog() {
	grid = addChild(Grid::Seed(6, 1));
	grid->column(0).mode = GridInfo::FILL;
	grid->row(1).mode = GridInfo::FILL;
	grid->row(1).align = GridInfo::STRETCH;

	// horizontally centered seeds
	GroupBox::Seed gs;
	gs.style |= BS_CENTER;
	gs.padding.y = 2;
	Label::Seed ls;
	ls.style |= SS_CENTER;

	{
		auto cur = grid->addChild(gs)->addChild(Grid::Seed(4, 1));
		cur->column(0).mode = GridInfo::FILL;
		cur->column(0).align = GridInfo::CENTER;

		cur->addChild(Label::Seed(WinUtil::createIcon(IDI_DCPP, 48)));

		ls.caption = _T(FULL_APPNAME) _T(" v") _T(VERSIONSTRING) _T("\n(c) Copyright 2001-2025 Jacek Sieka\n");
		ls.caption += T_("Ex-main project contributors: Todd Pederzani, poy\nEx-codeveloper: Per Lind\303\251n\nOriginal DC++ logo design: Martin Skogevall\nGraphics: Radox and various GPL and CC authors\n\nDC++ is licenced under GPL.");
		cur->addChild(ls);

		cur->addChild(Link::Seed(_T("https://dcplusplus.sourceforge.io/"), true));

		auto ts = WinUtil::Seeds::Dialog::textBox;
		ts.style |= ES_READONLY;
		ts.exStyle &= ~WS_EX_CLIENTEDGE;

		gs.caption = T_("TTH");
		ts.caption = WinUtil::tth;
		cur->addChild(gs)->addChild(ts);
	}

	{
		gs.caption = T_("Build and Runtime Information");
		auto seed = WinUtil::Seeds::Dialog::textBox;
		seed.style &= ~ES_AUTOHSCROLL;
		seed.style |= ES_MULTILINE | WS_VSCROLL | ES_READONLY;
		seed.caption = Text::toT(getAboutInfo());
		grid->addChild(gs)->addChild(seed);
	}

	{
		gs.caption = T_("Totals");
		auto cur = grid->addChild(gs)->addChild(Grid::Seed(2, 1));
		cur->column(0).mode = GridInfo::FILL;

		ls.caption = str(TF_("Upload: %1%, Download: %2%") % Text::toT(Util::formatBytes(SETTING(TOTAL_UPLOAD))) % Text::toT(Util::formatBytes(SETTING(TOTAL_DOWNLOAD))));
		cur->addChild(ls);

		ls.caption = (SETTING(TOTAL_DOWNLOAD) > 0)
			? str(TF_("Ratio (up/down): %1$0.2f") % (((double)SETTING(TOTAL_UPLOAD)) / ((double)SETTING(TOTAL_DOWNLOAD))))
			: T_("No transfers yet");
		cur->addChild(ls);
	}

	gs.caption = T_("Latest stable version");
	ls.caption = T_("Downloading...");
	version = grid->addChild(gs)->addChild(ls);

	auto buttons = WinUtil::addDlgButtons(grid,
		[this] { endDialog(IDOK); },
		[this] { endDialog(IDCANCEL); });
	buttons.first->setFocus();
	buttons.second->setVisible(false);

	setText(T_("About DC++"));
	setSmallIcon(WinUtil::createIcon(IDI_DCPP, 16));
	setLargeIcon(WinUtil::createIcon(IDI_DCPP, 32));

	layout();
	centerWindow();

	HttpManager::getInstance()->addListener(this);
	onDestroy([this] { HttpManager::getInstance()->removeListener(this); });
	c = HttpManager::getInstance()->download("https://dcplusplus.sourceforge.io/version.xml");

	return false;
}

void AboutDlg::layout() {
	dwt::Point sz = getClientSize();
	grid->resize(dwt::Rectangle(3, 3, sz.x - 6, sz.y - 6));
}

void AboutDlg::completeDownload(bool success, const string& result) {
	tstring str;

	if(success && !result.empty()) {
		try {
			SimpleXML xml;
			xml.fromXML(result);
			if(xml.findChild("DCUpdate")) {
				xml.stepIn();
				if(xml.findChild("Version")) {
					const auto& ver = xml.getChildData();
					if(!ver.empty()) {
						str = Text::toT(ver);
					}
				}
			}
		} catch(const SimpleXMLException&) {
			str = T_("Error processing version information");
		}
	}

	version->setText(str.empty() ? Text::toT(result) : str);
}

void AboutDlg::on(HttpManagerListener::Failed, HttpConnection* c, const string& str) noexcept {
	if(c != this->c) { return; }
	c = nullptr;

	callAsync([str, this] { completeDownload(false, str); });
}

void AboutDlg::on(HttpManagerListener::Complete, HttpConnection* c, OutputStream* stream) noexcept {
	if(c != this->c) { return; }
	c = nullptr;

	auto str = static_cast<StringOutputStream*>(stream)->getString();
	callAsync([str, this] { completeDownload(true, str); });
}
