/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "IconManager.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <dcpp/Archive.h>
#include <dcpp/File.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/StringTokenizer.h>
#include <dcpp/Text.h>
#include <dcpp/Util.h>

#include <dwt/DWTException.h>
#include <dwt/resources/Icon.h>

#include "resource.h"

using namespace dcpp;

namespace {

constexpr int64_t MAX_ICON_ARCHIVE_SIZE = 32 * 1024 * 1024;
constexpr size_t MAX_ICON_FILE_SIZE = 4 * 1024 * 1024;
constexpr size_t MAX_MANIFEST_SIZE = 256 * 1024;
constexpr size_t MAX_PACKAGE_ICONS = 512;
constexpr size_t MAX_METADATA_LENGTH = 128;
constexpr size_t MAX_ARCHIVE_PATH_LENGTH = 512;
constexpr const char* DARK_PACK_FILE = "Dark.dcico";

struct IconDefinition {
	unsigned id;
	const char* fileName;
};

constexpr IconDefinition ICONS[] = {
	{ IDI_DCPP, "DCPlusPlus.ico" }, { IDI_PUBLICHUBS, "PublicHubs.ico" },
	{ IDI_SEARCH, "Search.ico" }, { IDI_FAVORITE_HUBS, "FavoriteHubs.ico" },
	{ IDI_PRIVATE, "UserOn.ico" }, { IDI_DIRECTORY, "Directory.ico" },
	{ IDI_HUB, "HubOn.ico" }, { IDI_NOTEPAD, "Notepad.ico" },
	{ IDI_QUEUE, "Queue.ico" }, { IDI_FINISHED_DL, "FinishedDL.ico" },
	{ IDI_FINISHED_UL, "FinishedUL.ico" }, { IDI_ADLSEARCH, "ADLSearch.ico" },
	{ IDI_USERS, "Users.ico" }, { IDI_NET_STATS, "NetStats.ico" },
	{ IDI_MAGNET, "Magnet.ico" }, { IDI_HUB_OFF, "HubOff.ico" },
	{ IDI_PRIVATE_OFF, "UserOff.ico" }, { IDI_GROUPED_BY_FILES, "GroupedByFiles.ico" },
	{ IDI_GROUPED_BY_USERS, "GroupedByUsers.ico" }, { IDI_EXIT, "Exit.ico" },
	{ IDI_CHAT, "Chat.ico" }, { IDI_HELP, "Help.ico" },
	{ IDI_OPEN_DL_DIR, "OpenDLDir.ico" }, { IDI_OPEN_FILE_LIST, "OpenFileList.ico" },
	{ IDI_RECONNECT, "Reconnect.ico" }, { IDI_SETTINGS, "Settings.ico" },
	{ IDI_TRAY_PM, "TrayPM.ico" }, { IDI_TRUSTED, "Trusted.ico" },
	{ IDI_SECURE, "Secure.ico" }, { IDI_RECENTS, "Recents.ico" },
	{ IDI_WHATS_THIS, "WhatsThis.ico" }, { IDI_DOWNLOAD, "Download.ico" },
	{ IDI_UPLOAD, "Upload.ico" }, { IDI_CHANGELOG, "Changelog.ico" },
	{ IDI_DONATE, "Donate.ico" }, { IDI_GET_STARTED, "GetStarted.ico" },
	{ IDI_INDEXING, "Indexing.ico" }, { IDI_LINKS, "Links.ico" },
	{ IDI_REFRESH, "Refresh.ico" }, { IDI_SLOTS, "Slots.ico" },
	{ IDI_OK, "OK.ico" }, { IDI_CANCEL, "Cancel.ico" },
	{ IDI_LEFT, "Left.ico" }, { IDI_RIGHT, "Right.ico" },
	{ IDI_USER, "User.ico" }, { IDI_USER_AWAY, "UserAway.ico" },
	{ IDI_USER_BOT, "UserBot.ico" }, { IDI_USER_NOCON, "UserNoCon.ico" },
	{ IDI_USER_NOSLOT, "UserNoSlot.ico" }, { IDI_USER_OP, "UserOp.ico" },
	{ IDI_UP, "Up.ico" }, { IDI_FILE, "File.ico" },
	{ IDI_EXEC, "Exec.ico" }, { IDI_FAVORITE_USER_ON, "FavoriteUserOn.ico" },
	{ IDI_FAVORITE_USER_OFF, "FavoriteUserOff.ico" }, { IDI_GREEN_BALL, "BallGreen.ico" },
	{ IDI_RED_BALL, "BallRed.ico" }, { IDI_SLOTS_FULL, "SlotsFull.ico" },
	{ IDI_ADVANCED, "Advanced.ico" }, { IDI_CLOCK, "Clock.ico" },
	{ IDI_STYLES, "Styles.ico" }, { IDI_BW_LIMITER, "BandwidthLimiter.ico" },
	{ IDI_CONN_GREY, "ConnGrey.ico" }, { IDI_CONN_BLUE, "ConnBlue.ico" },
	{ IDI_EXPERT, "Expert.ico" }, { IDI_FAVORITE_DIRS, "FavoriteDirs.ico" },
	{ IDI_LOGS, "Logs.ico" }, { IDI_NOTIFICATIONS, "Notifications.ico" },
	{ IDI_PROXY, "Proxy.ico" }, { IDI_TABS, "Tabs.ico" },
	{ IDI_WINDOWS, "Windows.ico" }, { IDI_BALLOON, "Balloon.ico" },
	{ IDI_SOUND, "Sound.ico" }, { IDI_ULIMIT, "ULimit.ico" },
	{ IDI_DLIMIT, "DLimit.ico" }, { IDI_OPEN_OWN_FILE_LIST, "OpenOwnFileList.ico" },
	{ IDI_PLUGINS, "Plugins.ico" }, { IDI_UPLOAD_FILTERING, "UploadFiltering.ico" },
	{ IDI_USER_REG, "UserReg.ico" }, { IDI_DELETE, "Remove.ico" },
	{ IDI_PAUSE, "Pause.ico" }, { IDI_PLAY, "Play.ico" },
	{ IDI_INCREMENT, "Increment.ico" }, { IDI_DECREMENT, "Decrement.ico" },
	{ IDI_REMOVEQUEUE, "RemoveQueue.ico" }, { IDI_DCPP_WARNING, "Warning.ico" }
};

constexpr size_t MAX_CACHED_ICONS = sizeof(ICONS) / sizeof(ICONS[0]) * 8;

struct State {
	std::mutex mutex;
	std::unordered_map<uint64_t, dwt::IconPtr> cache;
	std::unordered_map<string, string> entries;
	string activePack;
	bool initialized = false;
	bool darkMode = false;
	bool packResolved = false;
};

State& state() {
	static State value;
	return value;
}

uint64_t cacheKey(unsigned resourceId, long size) noexcept {
	return (static_cast<uint64_t>(resourceId) << 32) | static_cast<uint32_t>(size);
}

bool safeRelativePath(string path) {
	if(path.empty() || path.front() == '/' || path.front() == '\\' || path.find(':') != string::npos) return false;
	std::replace(path.begin(), path.end(), '\\', '/');
	StringTokenizer<string> parts(path, '/');
	for(const auto& part: parts.getTokens()) {
		if(part.empty() || part == "." || part == "..") return false;
	}
	return true;
}

string packDirectory(Util::Paths path) {
	return Util::getPath(path) + "IconPacks" PATH_SEPARATOR_STR;
}

IconManager::Package readManifest(const string& path, std::unordered_map<string, string>* entries) {
	if(Text::toLower(Util::getFileExt(path)) != ".dcico") throw Exception(_("Invalid icon package extension"));
	const auto archiveSize = File::getSize(path);
	if(archiveSize <= 0 || archiveSize > MAX_ICON_ARCHIVE_SIZE) throw Exception(_("Invalid icon package size"));

	SimpleXML xml;
	xml.fromXML(Archive(path).readFile("info.xml", MAX_MANIFEST_SIZE));
	if(!xml.findChild("dcico")) throw Exception(_("Invalid icon package"));
	xml.stepIn();
	if(!xml.findChild("Name") || xml.getChildData().empty() || xml.getChildData().size() > MAX_METADATA_LENGTH) throw Exception(_("Invalid icon package"));
	const auto name = xml.getChildData();
	xml.resetCurrentChild();
	if(!xml.findChild("Version") || xml.getChildData().size() > MAX_METADATA_LENGTH || Util::toDouble(xml.getChildData()) <= 0) throw Exception(_("Invalid icon package"));
	const auto version = xml.getChildData();
	xml.resetCurrentChild();
	string scheme = "any";
	if(xml.findChild("Scheme") && !xml.getChildData().empty()) {
		if(xml.getChildData().size() > MAX_METADATA_LENGTH) throw Exception(_("Invalid icon package"));
		scheme = Text::toLower(xml.getChildData());
	}
	xml.resetCurrentChild();
	if(!xml.findChild("Icons")) throw Exception(_("Invalid icon package"));
	xml.stepIn();

	std::unordered_set<string> names;
	size_t iconCount = 0;
	while(xml.findChild("Icon")) {
		auto nameValue = xml.getChildAttrib("Name");
		auto fileValue = xml.getChildAttrib("File");
		if(nameValue.empty() || nameValue.size() > MAX_METADATA_LENGTH || fileValue.size() > MAX_ARCHIVE_PATH_LENGTH ||
			!safeRelativePath(fileValue) || Text::toLower(Util::getFileExt(fileValue)) != ".ico") continue;
		const auto normalizedName = Text::toLower(nameValue);
		if(!names.insert(normalizedName).second) throw Exception(_("Duplicate icon package entry"));
		if(++iconCount > MAX_PACKAGE_ICONS) throw Exception(_("Too many icon package entries"));
		if(entries) (*entries)[normalizedName] = fileValue;
	}
	if(!iconCount) throw Exception(_("Icon package contains no usable icons"));
	return { name, version, scheme, path, iconCount };
}

vector<IconManager::Package> discoverPackages() {
	vector<IconManager::Package> packages;
	auto loadDirectory = [&packages](const string& directory) {
		for(const auto& path: File::findFiles(directory, "*.dcico")) {
			try {
				auto package = readManifest(path, nullptr);
				auto existing = std::find_if(packages.begin(), packages.end(), [&path](const auto& item) {
					return Util::stricmp(Util::getFileName(item.path), Util::getFileName(path)) == 0;
				});
				if(existing == packages.end()) packages.push_back(std::move(package));
				else *existing = std::move(package);
			} catch(const Exception&) {
			}
		}
	};

	const auto applicationDirectory = packDirectory(Util::PATH_GLOBAL_CONFIG);
	const auto userDirectory = packDirectory(Util::PATH_USER_CONFIG);
	File::ensureDirectory(userDirectory);
	loadDirectory(applicationDirectory);
	if(Util::stricmp(applicationDirectory, userDirectory) != 0) loadDirectory(userDirectory);
	std::sort(packages.begin(), packages.end(), [](const auto& lhs, const auto& rhs) {
		const auto nameOrder = Util::stricmp(lhs.name, rhs.name);
		return nameOrder == 0 ? Util::stricmp(lhs.path, rhs.path) < 0 : nameOrder < 0;
	});
	return packages;
}

string resolvePack(bool darkMode) {
	const auto configured = SETTING(ICON_PACK);
	if(configured == IconManager::EMBEDDED_PACK) return string();
	if(!configured.empty() && File::getSize(configured) > 0) return configured;
	const auto wanted = configured.empty() ? (darkMode ? DARK_PACK_FILE : "") : Util::getFileName(configured);
	if(wanted.empty()) return string();
	for(const auto& package: discoverPackages()) {
		if(Util::stricmp(Util::getFileName(package.path), wanted) == 0) return package.path;
	}
	return string();
}

void resolveActivePack(State& current) {
	if(current.packResolved) return;
	current.packResolved = true;
	current.activePack = resolvePack(current.darkMode);
	current.entries.clear();
	if(current.activePack.empty()) return;
	try {
		readManifest(current.activePack, &current.entries);
	} catch(const Exception&) {
		current.activePack.clear();
		current.entries.clear();
	}
}

dwt::IconPtr loadPackIcon(const string& archivePath, const string& entry, long size) {
	try {
		const auto contents = Archive(archivePath).readFile(entry, MAX_ICON_FILE_SIZE);
		if(contents.empty()) return dwt::IconPtr();
		const auto directory = Util::getTempPath() + "dcico-" + std::to_string(std::hash<string>{}(archivePath)) + PATH_SEPARATOR_STR;
		const auto target = directory + std::to_string(std::hash<string>{}(entry)) + ".ico";
		File::ensureDirectory(target);
		File(target, File::WRITE, File::CREATE | File::TRUNCATE).write(contents);
		return new dwt::Icon(Text::toT(target), dwt::Point(size, size));
	} catch(const Exception&) {
		return dwt::IconPtr();
	} catch(const dwt::DWTException&) {
		return dwt::IconPtr();
	}
}

}

void IconManager::initialize() noexcept {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	current.initialized = true;
	current.cache.clear();
	current.entries.clear();
	current.activePack.clear();
	current.packResolved = false;
}

void IconManager::setDarkMode(bool enabled) noexcept {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	if(current.darkMode != enabled) {
		current.darkMode = enabled;
		current.cache.clear();
		current.entries.clear();
		current.activePack.clear();
		current.packResolved = false;
	}
}

void IconManager::reload() noexcept {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	current.cache.clear();
	current.entries.clear();
	current.activePack.clear();
	current.packResolved = false;
}

dwt::IconPtr IconManager::load(unsigned resourceId, long size) {
	auto& current = state();
	std::lock_guard<std::mutex> lock(current.mutex);
	// SplashWindow loads the application icon before startup() constructs SettingsManager.
	// Do not consult package settings or cache that fallback: once initialize() runs, the
	// same resource may resolve to the user's selected package.
	if(!current.initialized) return new dwt::Icon(resourceId, dwt::Point(size, size));
	const auto key = cacheKey(resourceId, size);
	const auto cached = current.cache.find(key);
	if(cached != current.cache.end()) return cached->second;

	resolveActivePack(current);
	dwt::IconPtr icon;
	const auto fileName = getFileName(resourceId);
	if(!current.activePack.empty() && fileName) {
		const auto entry = current.entries.find(Text::toLower(fileName));
		if(entry != current.entries.end()) icon = loadPackIcon(current.activePack, entry->second, size);
	}
	if(!icon) icon = new dwt::Icon(resourceId, dwt::Point(size, size));
	if(current.cache.size() < MAX_CACHED_ICONS) current.cache.emplace(key, icon);
	return icon;
}

const char* IconManager::getFileName(unsigned resourceId) noexcept {
	for(const auto& icon: ICONS) {
		if(icon.id == resourceId) return icon.fileName;
	}
	return nullptr;
}

IconManager::Package IconManager::inspectPackage(const string& path) {
	return readManifest(path, nullptr);
}

vector<IconManager::Package> IconManager::getPackages() {
	return discoverPackages();
}

string IconManager::getDirectory() {
	const auto directory = packDirectory(Util::PATH_USER_CONFIG);
	File::ensureDirectory(directory);
	return directory;
}
