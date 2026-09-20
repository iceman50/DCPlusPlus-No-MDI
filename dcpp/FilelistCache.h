/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_FILELIST_CACHE_H
#define DCPLUSPLUS_DCPP_FILELIST_CACHE_H

#include "DirectoryListing.h"

struct sqlite3;
namespace dcpp {

// A disposable local index. Each row contains a separately checksummed Zstd frame.
// The source XML/BZip2/Zstd list remains authoritative.
class FilelistCache {
public:
	FilelistCache(const string& path, bool create, const string& source = string());
	~FilelistCache();
	FilelistCache(const FilelistCache&) = delete;
	FilelistCache& operator=(const FilelistCache&) = delete;
	bool load(DirectoryListing& list, const string& source);
	void addFile(DirectoryListing::Directory* dir, const DirectoryListing::File& file);
	void finish(DirectoryListing& list, const string& source);
	void loadFiles(DirectoryListing::Directory* dir) const;
	void publish(const string& path);
	bool isBuilding() const { return building; }
private:
	void flushChunk();
	void exec(const char* sql) const;
	sqlite3* db = nullptr;
	bool building;
	string temporaryPath;
	string initialStamp;
	int64_t nextId = 1;
	DirectoryListing::Directory* chunkDir = nullptr;
	string chunk;
};
}
#endif
