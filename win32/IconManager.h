/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_WIN32_ICON_MANAGER_H
#define DCPLUSPLUS_WIN32_ICON_MANAGER_H

#include <cstddef>
#include <string>
#include <vector>

#include <dwt/forward.h>

/** Discovers .dcico ZIP packages and loads their application icons with a compiled-resource fallback.
 *
 * An empty ICON_PACK setting selects Dark.dcico only for an effective dark appearance. An explicit
 * package applies to either appearance; EMBEDDED_PACK always uses executable resources.
 * Archive members are read with strict size and path limits,
 * materialized in the process temporary directory for Win32 LoadImage, and cached by resource and size.
 */
class IconManager {
public:
	static constexpr const char* EMBEDDED_PACK = "@embedded";

	struct Package {
		std::string name;
		std::string version;
		std::string scheme;
		std::string path;
		std::size_t iconCount;
	};

	struct RuntimeInfo {
		bool initialized = false;
		bool packResolved = false;
		Package package {};
		std::size_t cachedPackageIcons = 0;
		std::size_t cachedEmbeddedIcons = 0;
	};

	/** Snapshot of the current cache, counted by resource and size; does not load icons or packages. */
	static RuntimeInfo getRuntimeInfo();

	/** Enables package-backed icon loading after the core settings singleton has been created. */
	static void initialize() noexcept;
	static void setDarkMode(bool enabled) noexcept;
	static void reload() noexcept;
	static dwt::IconPtr load(unsigned resourceId, long size);
	static const char* getFileName(unsigned resourceId) noexcept;
	static Package inspectPackage(const std::string& path);
	static std::vector<Package> getPackages();
	static std::string getDirectory();
};

#endif
