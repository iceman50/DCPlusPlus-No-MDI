/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdafx.h"
#include "ThemeManager.h"

#include <algorithm>
#include <cstdint>

#include <dcpp/File.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/Util.h>

namespace {

constexpr int64_t MAX_THEME_FILE_SIZE = 64 * 1024;

int hexDigit(char value) {
	if(value >= '0' && value <= '9') return value - '0';
	if(value >= 'a' && value <= 'f') return value - 'a' + 10;
	if(value >= 'A' && value <= 'F') return value - 'A' + 10;
	return -1;
}

COLORREF parseColor(std::string value, const std::string& attribute, const std::string& path) {
	Util::trim(value);
	if(!value.empty() && value.front() == '#') value.erase(value.begin());
	if(value.size() != 6) throw Exception("Invalid " + attribute + " color in " + path + "; expected #RRGGBB");

	unsigned rgb = 0;
	for(const auto valuePart: value) {
		const auto digit = hexDigit(valuePart);
		if(digit < 0) throw Exception("Invalid " + attribute + " color in " + path + "; expected #RRGGBB");
		rgb = rgb * 16 + static_cast<unsigned>(digit);
	}
	return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

COLORREF readColor(SimpleXML& xml, const std::string& attribute, const std::string& path) {
	const auto value = xml.getChildAttrib(attribute);
	if(value.empty()) throw Exception("Missing " + attribute + " palette attribute in " + path);
	return parseColor(value, attribute, path);
}

dwt::tstring fileThemeName(const std::string& path) {
	auto name = Util::getFileName(path);
	const auto extension = Util::getFileExt(name);
	if(!extension.empty()) name.erase(name.size() - extension.size());
	return Text::toT(name);
}

std::string writeColor(COLORREF color) {
	return "#" + Util::cssColor(color);
}

}

std::string ThemeManager::getDirectory() {
	return Util::getPath(Util::PATH_USER_CONFIG) + "Themes" PATH_SEPARATOR_STR;
}

std::vector<ThemeManager::Theme> ThemeManager::getThemes() {
	std::vector<Theme> themes = {
		{ T_("Default dark"), dwt::Appearance::defaultPalette() },
		{ T_("Midnight blue"), { RGB(17, 24, 39), RGB(31, 41, 55), RGB(229, 231, 235), RGB(156, 163, 175), RGB(55, 65, 81), RGB(59, 130, 246), RGB(255, 255, 255) } },
		{ T_("Graphite"), { RGB(24, 24, 24), RGB(38, 38, 38), RGB(235, 235, 235), RGB(155, 155, 155), RGB(70, 70, 70), RGB(58, 150, 221), RGB(255, 255, 255) } },
		{ T_("Dark slate"), { RGB(24, 28, 32), RGB(38, 44, 50), RGB(232, 235, 238), RGB(151, 158, 164), RGB(68, 76, 84), RGB(38, 166, 154), RGB(255, 255, 255) } }
	};

	const auto directory = getDirectory();
	File::ensureDirectory(directory);
	std::vector<Theme> files;
	for(const auto& path: File::findFiles(directory, "*.xml")) {
		try {
			files.push_back(load(path));
		} catch(const Exception&) {
		}
	}
	std::sort(files.begin(), files.end(), [](const Theme& lhs, const Theme& rhs) { return lhs.name < rhs.name; });
	themes.insert(themes.end(), files.begin(), files.end());
	return themes;
}

ThemeManager::Theme ThemeManager::load(const std::string& path) {
	const auto size = File::getSize(path);
	if(size < 0) throw Exception("Theme file does not exist: " + path);
	if(size > MAX_THEME_FILE_SIZE) throw Exception("Theme file is larger than 64 KiB: " + path);

	SimpleXML xml;
	xml.fromXML(File(path, File::READ, File::OPEN).read());
	if(!xml.findChild("DCPlusPlusTheme")) throw Exception("Invalid theme root in " + path + "; expected DCPlusPlusTheme");
	auto name = Text::toT(xml.getChildAttrib("Name"));
	if(name.empty()) name = fileThemeName(path);
	xml.stepIn();
	if(!xml.findChild("Palette")) throw Exception("Missing Palette element in " + path);

	dwt::Appearance::Palette palette = {
		readColor(xml, "Background", path),
		readColor(xml, "Surface", path),
		readColor(xml, "Text", path),
		readColor(xml, "DisabledText", path),
		readColor(xml, "Border", path),
		readColor(xml, "Accent", path),
		readColor(xml, "HighlightText", path)
	};
	return Theme(name, palette, path);
}

std::string ThemeManager::getImportPath(const std::string& source) {
	auto name = Util::getFileName(source);
	if(Text::toLower(Util::getFileExt(name)) != ".xml") name += ".xml";
	return getDirectory() + name;
}

std::string ThemeManager::importTheme(const std::string& source) {
	load(source);
	const auto target = getImportPath(source);
	File::ensureDirectory(target);
	if(Util::stricmp(source, target) != 0) File::copyFile(source, target);
	return target;
}

std::string ThemeManager::saveTheme(const std::string& target, const dwt::Appearance::Palette& palette) {
	auto path = target;
	if(Text::toLower(Util::getFileExt(path)) != ".xml") path += ".xml";
	File::ensureDirectory(path);

	SimpleXML xml;
	xml.addTag("DCPlusPlusTheme");
	xml.addChildAttrib("Name", Text::fromT(fileThemeName(path)));
	xml.stepIn();
	xml.addTag("Palette");
	xml.addChildAttrib("Background", writeColor(palette.background));
	xml.addChildAttrib("Surface", writeColor(palette.surface));
	xml.addChildAttrib("Text", writeColor(palette.text));
	xml.addChildAttrib("DisabledText", writeColor(palette.disabledText));
	xml.addChildAttrib("Border", writeColor(palette.border));
	xml.addChildAttrib("Accent", writeColor(palette.accent));
	xml.addChildAttrib("HighlightText", writeColor(palette.highlightText));
	xml.stepOut();

	const auto temporary = path + ".tmp";
	try {
		File output(temporary, File::WRITE, File::CREATE | File::TRUNCATE);
		output.write(SimpleXML::utf8Header);
		output.write(xml.toXML());
		output.close();
		File::renameFile(temporary, path);
	} catch(...) {
		File::deleteFile(temporary);
		throw;
	}
	return path;
}
