/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_EMOTICON_MANAGER_H
#define DCPLUSPLUS_DCPP_EMOTICON_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

namespace dcpp {

using std::string;
using std::vector;

/** Loads rules and image assets from a .dcemo ZIP package.
 *
 * A package contains an info.xml file and local BMP, ICO, or PNG files. Paths are relative to the
 * archive root. The manifest follows the same root-metadata style as .dcext packages:
 *
 * <dcemo>
 *   <Name>Example set</Name>
 *   <Version>1.0</Version>
 *   <Emoticons>
 *     <Emoticon Name="smile" Icon="icons/smile.png">
 *       <Rule>:)</Rule>
 *       <Rule>:-)</Rule>
 *     </Emoticon>
 *   </Emoticons>
 * </dcemo>
 *
 * Rules are literal, case-sensitive strings. Recognition uses chat-token boundaries so rules do
 * not activate in URLs or in the middle of words. Invalid packages simply leave the source text
 * untouched; callers may invoke reload() after changing the configured package.
 */
class EmoticonManager {
public:
	struct Rule {
		string text;
		string name;
	};
	struct ExportItem {
		string name;
		vector<string> rules;
		string iconPath;
	};
	struct ImportPackage {
		string name;
		string version;
		vector<ExportItem> items;
		size_t skipped = 0;
	};

	/** Return a snapshot of all rules in the configured package. */
	static vector<Rule> getRules();

	/** Return the extracted local image path for an emoticon name, or an empty string. */
	static string getIconPath(const string& name);
	/** Monotonic package revision used by image-rendering caches. */
	static uint64_t getRevision();

	/** Create a .dcemo ZIP containing info.xml and copies of all referenced local icons. */
	static void exportPackage(const string& path, const string& name, const vector<ExportItem>& items);

	/** Import an XML emoticon manifest and resolve its sibling image folder. */
	static ImportPackage importEmoticonPackage(const string& manifestPath);

	/** Forget the current package so it is re-read on the next request. */
	static void reload();
};

} // namespace dcpp

#endif
