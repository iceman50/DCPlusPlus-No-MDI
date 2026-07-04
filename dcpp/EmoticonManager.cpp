/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "EmoticonManager.h"

#include "Archive.h"
#include "File.h"
#include "SettingsManager.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "Util.h"
#include "version.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace dcpp {

namespace {
	struct PackageState {
		std::mutex mutex;
		string loadedPath;
		vector<EmoticonManager::Rule> rules;
		std::unordered_map<string, string> icons;
		uint64_t revision = 0;
	};

	PackageState& state() {
		static PackageState value;
		return value;
	}

	struct ZipEntry {
		string name;
		ByteVector data;
		uint32_t crc;
		uint32_t offset;
	};

	uint32_t crc32(const ByteVector& data) {
		uint32_t crc = 0xffffffff;
		for(auto byte: data) {
			crc ^= byte;
			for(int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320 & (0 - (crc & 1)));
		}
		return ~crc;
	}

	void append16(ByteVector& out, uint16_t value) {
		out.push_back(static_cast<uint8_t>(value));
		out.push_back(static_cast<uint8_t>(value >> 8));
	}

	void append32(ByteVector& out, uint32_t value) {
		for(int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (i * 8)));
	}

	void append(ByteVector& out, const string& value) {
		out.insert(out.end(), value.begin(), value.end());
	}

	void writeZip(const string& path, vector<ZipEntry> entries) {
		uint64_t requiredSize = 22;
		for(const auto& entry: entries) {
			if(entry.name.size() > UINT16_MAX) throw Exception(_("Emoticon package path is too long"));
			requiredSize += 30 + entry.name.size() + entry.data.size();
			requiredSize += 46 + entry.name.size();
		}
		if(requiredSize > UINT32_MAX) throw Exception(_("Emoticon package is too large"));

		ByteVector archive;
		archive.reserve(static_cast<size_t>(requiredSize));
		for(auto& entry: entries) {
			entry.offset = static_cast<uint32_t>(archive.size());
			append32(archive, 0x04034b50); append16(archive, 20); append16(archive, 0); append16(archive, 0);
			append16(archive, 0); append16(archive, 0); append32(archive, entry.crc);
			append32(archive, static_cast<uint32_t>(entry.data.size())); append32(archive, static_cast<uint32_t>(entry.data.size()));
			append16(archive, static_cast<uint16_t>(entry.name.size())); append16(archive, 0); append(archive, entry.name);
			archive.insert(archive.end(), entry.data.begin(), entry.data.end());
		}

		const auto centralOffset = static_cast<uint32_t>(archive.size());
		for(const auto& entry: entries) {
			append32(archive, 0x02014b50); append16(archive, 20); append16(archive, 20); append16(archive, 0); append16(archive, 0);
			append16(archive, 0); append16(archive, 0); append32(archive, entry.crc);
			append32(archive, static_cast<uint32_t>(entry.data.size())); append32(archive, static_cast<uint32_t>(entry.data.size()));
			append16(archive, static_cast<uint16_t>(entry.name.size())); append16(archive, 0); append16(archive, 0);
			append16(archive, 0); append16(archive, 0); append32(archive, 0); append32(archive, entry.offset); append(archive, entry.name);
		}

		const auto centralSize = static_cast<uint32_t>(archive.size()) - centralOffset;
		append32(archive, 0x06054b50); append16(archive, 0); append16(archive, 0);
		append16(archive, static_cast<uint16_t>(entries.size())); append16(archive, static_cast<uint16_t>(entries.size()));
		append32(archive, centralSize); append32(archive, centralOffset); append16(archive, 0);
		File::ensureDirectory(path);
		File(path, File::WRITE, File::CREATE | File::TRUNCATE).write(archive.data(), archive.size());
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

	void loadPackage(PackageState& data, const string& path) {
		data.rules.clear();
		data.icons.clear();
		if(path.empty()) return;
		auto packageExtension = Util::getFileExt(path);
		std::transform(packageExtension.begin(), packageExtension.end(), packageExtension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if(packageExtension != ".dcemo") throw Exception(_("Invalid emoticon package extension"));

		const auto directory = Util::getTempPath() + "dcemo-" + std::to_string(std::hash<string>{}(path)) + PATH_SEPARATOR_STR;
		Archive(path).extract(directory);

		SimpleXML xml;
		xml.fromXML(File(directory + "info.xml", File::READ, File::OPEN).read());
		if(!xml.findChild("dcemo")) throw Exception(_("Invalid emoticon package"));
		xml.stepIn();

		xml.resetCurrentChild();
		if(!xml.findChild("Name") || xml.getChildData().empty()) throw Exception(_("Invalid emoticon package"));
		xml.resetCurrentChild();
		if(!xml.findChild("Version") || Util::toDouble(xml.getChildData()) <= 0) throw Exception(_("Invalid emoticon package"));

		xml.resetCurrentChild();
		if(!xml.findChild("Emoticons")) throw Exception(_("Invalid emoticon package"));
		xml.stepIn();

		std::unordered_set<string> knownRules;
		while(xml.findChild("Emoticon")) {
			const auto name = xml.getChildAttrib("Name");
			auto icon = xml.getChildAttrib("Icon");
			if(name.empty() || !safeRelativePath(icon)) continue;

			auto extension = Util::getFileExt(icon);
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if(extension != ".bmp" && extension != ".ico" && extension != ".png") continue;
			std::replace(icon.begin(), icon.end(), '/', PATH_SEPARATOR);
			std::replace(icon.begin(), icon.end(), '\\', PATH_SEPARATOR);
			data.icons[name] = directory + icon;

			xml.stepIn();
			auto readRules = [&](const string& element) {
				xml.resetCurrentChild();
				while(xml.findChild(element)) {
					auto text = xml.getChildAttrib("Text");
					if(text.empty()) text = xml.getChildData();
					if(!text.empty() && knownRules.insert(text).second) data.rules.push_back({ text, name });
				}
			};
			readRules("Rule");
			readRules("Shortcut"); // accepted for compatibility with older emoticon manifests
			xml.stepOut();
		}

		std::stable_sort(data.rules.begin(), data.rules.end(), [](const auto& a, const auto& b) {
			return a.text.size() > b.text.size();
		});
	}

	void ensureLoaded(PackageState& data) {
		const auto path = SETTING(EMOTICON_PACK);
		if(path == data.loadedPath) return;
		data.loadedPath = path;
		++data.revision;
		try {
			loadPackage(data, path);
		} catch(const Exception&) {
			data.rules.clear();
			data.icons.clear();
		}
	}
}

vector<EmoticonManager::Rule> EmoticonManager::getRules() {
	auto& data = state();
	std::lock_guard<std::mutex> lock(data.mutex);
	ensureLoaded(data);
	return data.rules;
}

string EmoticonManager::getIconPath(const string& name) {
	auto& data = state();
	std::lock_guard<std::mutex> lock(data.mutex);
	ensureLoaded(data);
	const auto i = data.icons.find(name);
	return i == data.icons.end() ? Util::emptyString : i->second;
}

uint64_t EmoticonManager::getRevision() {
	auto& data = state();
	std::lock_guard<std::mutex> lock(data.mutex);
	ensureLoaded(data);
	return data.revision;
}

void EmoticonManager::exportPackage(const string& path, const string& name, const vector<ExportItem>& items) {
	if(name.empty() || items.empty()) throw Exception(_("Incomplete emoticon package"));
	auto extension = Util::getFileExt(path);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if(extension != ".dcemo") throw Exception(_("Invalid emoticon package extension"));
	if(items.size() > 65534) throw Exception(_("Too many emoticons"));

	string tmp;
	string manifest = SimpleXML::utf8Header + "<dcemo><Name>" + SimpleXML::escape(name, tmp, false) +
		"</Name><Version>" VERSIONSTRING "</Version><Emoticons>";
	vector<ZipEntry> entries;
	std::unordered_set<string> names;
	std::unordered_set<string> rules;
	for(size_t index = 0; index < items.size(); ++index) {
		const auto& item = items[index];
		if(item.name.empty() || item.rules.empty() || !names.insert(item.name).second) throw Exception(_("Invalid or duplicate emoticon name"));
		auto iconExtension = Util::getFileExt(item.iconPath);
		std::transform(iconExtension.begin(), iconExtension.end(), iconExtension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if(iconExtension != ".bmp" && iconExtension != ".ico" && iconExtension != ".png") throw Exception(_("Unsupported emoticon image format"));

		const auto asset = "icons/" + std::to_string(index) + iconExtension;
		const auto contents = File(item.iconPath, File::READ, File::OPEN).read();
		if(contents.size() > UINT32_MAX) throw Exception(_("Emoticon image is too large"));
		ByteVector bytes(contents.begin(), contents.end());
		entries.push_back({ asset, std::move(bytes), 0, 0 });
		entries.back().crc = crc32(entries.back().data);

		manifest += "<Emoticon Name=\"" + SimpleXML::escape(item.name, tmp, true) + "\" Icon=\"" + asset + "\">";
		for(const auto& rule: item.rules) {
			if(rule.empty() || !rules.insert(rule).second) throw Exception(_("Invalid or duplicate emoticon rule"));
			manifest += "<Rule>" + SimpleXML::escape(rule, tmp, false) + "</Rule>";
		}
		manifest += "</Emoticon>";
	}
	manifest += "</Emoticons></dcemo>";
	ByteVector manifestBytes(manifest.begin(), manifest.end());
	entries.insert(entries.begin(), { "info.xml", std::move(manifestBytes), 0, 0 });
	entries.front().crc = crc32(entries.front().data);
	writeZip(path, std::move(entries));
}

EmoticonManager::ImportPackage EmoticonManager::importEmoticonPackage(const string& manifestPath) {
	auto extension = Util::getFileExt(manifestPath);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if(extension != ".xml") throw Exception(_("Select an emoticon package XML file"));

	ImportPackage result;
	result.name = Util::getFileName(manifestPath);
	result.name.erase(result.name.size() - extension.size());
	result.version = "1.0";

	const auto contents = File(manifestPath, File::READ, File::OPEN).read();
	const auto rootPosition = contents.find("<Emoticons");
	if(rootPosition != string::npos) {
		const auto versionMarker = contents.rfind("v.", rootPosition);
		if(versionMarker != string::npos) {
			auto end = versionMarker + 2;
			while(end < rootPosition && (std::isdigit(static_cast<unsigned char>(contents[end])) || contents[end] == '.')) ++end;
			const auto importedVersion = contents.substr(versionMarker + 2, end - versionMarker - 2);
			if(Util::toDouble(importedVersion) > 0) result.version = importedVersion;
		}
	}

	SimpleXML xml;
	xml.fromXML(contents);
	if(!xml.findChild("Emoticons")) throw Exception(_("Invalid XML emoticon package"));
	xml.stepIn();

	const auto basePath = Util::getFilePath(manifestPath);
	std::unordered_map<string, size_t> imageIndexes;
	std::unordered_set<string> usedNames;
	std::unordered_set<string> usedRules;
	while(xml.findChild("Emoticon")) {
		auto rule = xml.getChildAttrib("PasteText");
		if(rule.empty()) rule = xml.getChildAttrib("Expression");
		rule.erase(std::remove(rule.begin(), rule.end(), ' '), rule.end()); // XML manifests may pad shortcuts with spaces
		auto bitmap = xml.getChildAttrib("Bitmap");
		if(rule.empty() || !safeRelativePath(bitmap)) {
			++result.skipped;
			continue;
		}

		auto imageExtension = Util::getFileExt(bitmap);
		std::transform(imageExtension.begin(), imageExtension.end(), imageExtension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if((imageExtension != ".bmp" && imageExtension != ".ico" && imageExtension != ".png") || !usedRules.insert(rule).second) {
			++result.skipped;
			continue;
		}
		std::replace(bitmap.begin(), bitmap.end(), '/', PATH_SEPARATOR);
		std::replace(bitmap.begin(), bitmap.end(), '\\', PATH_SEPARATOR);
		const auto imagePath = basePath + bitmap;
		if(File::getSize(imagePath) < 0) {
			++result.skipped;
			continue;
		}

		auto existing = imageIndexes.find(imagePath);
		if(existing != imageIndexes.end()) {
			result.items[existing->second].rules.push_back(rule);
			continue;
		}

		auto itemName = Util::getFileName(bitmap);
		itemName.erase(itemName.size() - imageExtension.size());
		const auto baseName = itemName.empty() ? "emoticon" : itemName;
		itemName = baseName;
		for(size_t suffix = 2; !usedNames.insert(itemName).second; ++suffix) itemName = baseName + '-' + std::to_string(suffix);
		imageIndexes.emplace(imagePath, result.items.size());
		result.items.push_back({ itemName, { rule }, imagePath });
	}

	if(result.items.empty()) throw Exception(_("The XML emoticon package contains no usable BMP, ICO, or PNG entries"));
	return result;
}

void EmoticonManager::reload() {
	auto& data = state();
	std::lock_guard<std::mutex> lock(data.mutex);
	data.loadedPath.clear();
	data.rules.clear();
	data.icons.clear();
	++data.revision;
}

} // namespace dcpp
