/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_THEME_MANAGER_H
#define DCPLUSPLUS_WIN32_THEME_MANAGER_H

#include <string>
#include <vector>

#include <dwt/Appearance.h>
#include <dwt/tstring.h>

/** Loads DC++ application-chrome palettes without adding application policy to DWT. */
class ThemeManager {
public:
	struct Theme {
		Theme(const dwt::tstring& name_, const dwt::Appearance::Palette& palette_, const std::string& path_ = std::string()) : name(name_), palette(palette_), path(path_) { }

		dwt::tstring name;
		dwt::Appearance::Palette palette;
		std::string path;
	};

	static std::string getDirectory();
	static std::vector<Theme> getThemes();
	static Theme load(const std::string& path);
	static std::string getImportPath(const std::string& source);
	static std::string importTheme(const std::string& source);
	static std::string saveTheme(const std::string& target, const dwt::Appearance::Palette& palette);
};

#endif
