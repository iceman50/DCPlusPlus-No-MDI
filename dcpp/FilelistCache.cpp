/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "FilelistCache.h"
#include "File.h"
#include "SettingsManager.h"
#include "FilteredFile.h"
#include "SimpleXML.h"
#include "ZstdUtils.h"
#include <sqlite3.h>
#include <filesystem>

namespace dcpp {
namespace {
class Statement {
public:
	Statement(sqlite3* db, const char* sql) {
		if(sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) throw Exception("Invalid file list cache");
	}
	~Statement() { sqlite3_finalize(s); }
	void number(int n, int64_t value) { if(sqlite3_bind_int64(s, n, value) != SQLITE_OK) throw Exception("File list cache binding failed"); }
	void text(int n, const string& value) { if(sqlite3_bind_text(s, n, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw Exception("File list cache binding failed"); }
	bool step() {
		auto result = sqlite3_step(s);
		if(result != SQLITE_ROW && result != SQLITE_DONE) throw Exception("Unable to read or write file list cache (duplicate entry or disk error)");
		return result == SQLITE_ROW;
	}
	int64_t number(int n) const { return sqlite3_column_int64(s, n); }
	string text(int n) const {
		auto p = sqlite3_column_text(s, n);
		return p ? string(reinterpret_cast<const char*>(p), sqlite3_column_bytes(s, n)) : string();
	}
	sqlite3_stmt* s = nullptr;
};

string sourceStamp(const string& source) {
	std::error_code ec;
	const auto path = std::filesystem::u8path(source);
	const auto size = std::filesystem::file_size(path, ec);
	if(ec) throw Exception("Unable to stat file list");
	const auto time = std::filesystem::last_write_time(path, ec);
	if(ec) throw Exception("Unable to stat file list");
	return source + ":" + std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count()) + ":" + std::to_string(SETTING(MAX_FILELIST_SIZE));
}

int collate(void*, int an, const void* a, int bn, const void* b) {
	return compare(string(static_cast<const char*>(a), an), string(static_cast<const char*>(b), bn));
}
}

FilelistCache::FilelistCache(const string& path, bool create, const string& source) : building(create) {
	if(create) initialStamp = sourceStamp(source);
	const auto flags = create ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY;
	if(sqlite3_open_v2(path.c_str(), &db, flags | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
		sqlite3_close(db); db = nullptr;
		throw Exception("Unable to open file list cache");
	}
	try {
		exec("PRAGMA cache_size=-2048; PRAGMA trusted_schema=OFF; PRAGMA temp_store=FILE;");
		if(create) {
			temporaryPath = path;
			sqlite3_create_collation(db, "DCNAME", SQLITE_UTF8, nullptr, collate);
			exec("BEGIN; CREATE TABLE meta(stamp TEXT, base TEXT, version INTEGER);"
				"CREATE TABLE dirs(id INTEGER PRIMARY KEY,parent INTEGER,ordinal INTEGER,name TEXT,date INTEGER,size INTEGER,dirs INTEGER,files INTEGER,children INTEGER,complete INTEGER,count INTEGER,bytes INTEGER);"
				"CREATE TABLE chunks(id INTEGER PRIMARY KEY,dir INTEGER,data BLOB); CREATE INDEX chunk_dir ON chunks(dir,id);"
				"CREATE TEMP TABLE names(dir INTEGER,name TEXT COLLATE DCNAME,PRIMARY KEY(dir,name)) WITHOUT ROWID;");
		}
	} catch(...) {
		sqlite3_close(db); db = nullptr;
		if(!temporaryPath.empty()) {
			File::deleteFile(temporaryPath);
			File::deleteFile(temporaryPath + "-journal");
		}
		throw;
	}
}

FilelistCache::~FilelistCache() {
	sqlite3_close(db);
	if(!temporaryPath.empty()) { File::deleteFile(temporaryPath); File::deleteFile(temporaryPath + "-journal"); }
}

void FilelistCache::publish(const string& path) {
	sqlite3_close(db); db = nullptr;
	string openPath = temporaryPath;
	File::deleteFile(path);
	std::error_code ec;
	std::filesystem::rename(std::filesystem::u8path(temporaryPath), std::filesystem::u8path(path), ec);
	if(!ec) { openPath = path; temporaryPath.clear(); }
	if(sqlite3_open_v2(openPath.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK)
		throw Exception("Unable to reopen file list cache");
	exec("PRAGMA cache_size=-2048; PRAGMA trusted_schema=OFF;");
}

void FilelistCache::exec(const char* sql) const {
	if(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) throw Exception("Unable to update file list cache");
}

bool FilelistCache::load(DirectoryListing& list, const string& source) {
	Statement meta(db, "SELECT stamp,base,version FROM meta");
	if(!meta.step() || meta.number(2) != 1 || meta.text(0) != sourceStamp(source)) return false;
	list.base = meta.text(1);
	Statement rows(db, "SELECT id,parent,name,date,size,dirs,files,children,complete,count,bytes FROM dirs ORDER BY ordinal");
	std::unordered_map<int64_t, DirectoryListing::Directory*> parents;
	while(rows.step()) {
		if(list.getAbort()) throw Exception();
		const auto id = rows.number(0), parent = rows.number(1);
		DirectoryListing::Directory* d;
		if(parent == 0) {
			if(!parents.empty()) throw Exception("Invalid file list cache root");
			d = list.root;
		} else {
			auto p = parents.find(parent);
			if(p == parents.end()) throw Exception("Invalid file list cache parent");
			auto child = std::make_unique<DirectoryListing::Directory>(p->second, rows.text(2), false, rows.number(8) != 0);
			d = child.get();
			if(!p->second->directories.insert(d).second) throw Exception("Duplicate cache directory");
			child.release();
		}
		d->setRemoteDate(static_cast<time_t>(rows.number(3)));
		d->setRemoteSize(rows.number(4));
		d->setRemoteDirectories(static_cast<int>(rows.number(5)));
		d->setRemoteFiles(static_cast<int>(rows.number(6)));
		d->setHasChildren(rows.number(7) != 0);
		d->setComplete(rows.number(8) != 0);
		d->cachedFileCount = static_cast<size_t>(rows.number(9));
		d->cachedSize = rows.number(10);
		d->cacheId = id;
		d->cache = this;
		d->filesLoaded = false;
		if(id <= 0 || !parents.emplace(id, d).second) throw Exception("Invalid cache directory ID");
	}
	return !parents.empty();
}

void FilelistCache::addFile(DirectoryListing::Directory* dir, const DirectoryListing::File& file) {
	if(!dir->cacheId) dir->cacheId = nextId++;
	Statement name(db, "INSERT INTO names VALUES(?,?)");
	name.number(1, dir->cacheId); name.text(2, file.getName()); name.step();
	if(chunkDir != dir) flushChunk();
	chunkDir = dir;
	StringRefOutputStream output(chunk);
	string indent, tmp;
	file.save(output, indent, tmp);
	++dir->cachedFileCount;
	dir->cachedSize += file.getSize();
	if(chunk.size() >= 512 * 1024) flushChunk();
}

void FilelistCache::flushChunk() {
	if(chunk.empty()) return;
	StringOutputStream compressed;
	FilteredOutputStream<ZstdFilter, false> compressor(&compressed);
	compressor.write(chunk); compressor.flush();
	Statement row(db, "INSERT INTO chunks(dir,data) VALUES(?,?)");
	row.number(1, chunkDir->cacheId);
	const auto& data = compressed.getString();
	if(sqlite3_bind_blob(row.s, 2, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw Exception("Unable to cache file list chunk");
	row.step();
	chunk.clear();
}

void FilelistCache::finish(DirectoryListing& list, const string& source) {
	if(sourceStamp(source) != initialStamp) throw Exception("File list changed while indexing");
	flushChunk();
	int64_t ordinal = 0;
	std::function<void(DirectoryListing::Directory*)> save = [&](DirectoryListing::Directory* d) {
		if(!d->cacheId) d->cacheId = nextId++;
		Statement row(db, "INSERT INTO dirs VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
		row.number(1, d->cacheId); row.number(2, d->getParent() ? d->getParent()->cacheId : 0);
		row.number(3, ordinal++); row.text(4, d->getName()); row.number(5, d->getRemoteDate());
		row.number(6, d->getRemoteSize()); row.number(7, d->getRemoteDirectories()); row.number(8, d->getRemoteFiles());
		row.number(9, d->getHasChildren()); row.number(10, d->getComplete());
		row.number(11, d->cachedFileCount); row.number(12, d->cachedSize); row.step();
		d->cache = this; d->filesLoaded = false;
		for(auto child: d->directories) save(child);
	};
	save(list.root);
	Statement meta(db, "INSERT INTO meta VALUES(?,?,1)");
	meta.text(1, sourceStamp(source)); meta.text(2, list.base); meta.step();
	exec("COMMIT; DROP TABLE names;");
	building = false;
}

void FilelistCache::loadFiles(DirectoryListing::Directory* dir) const {
	Statement rows(db, "SELECT data FROM chunks WHERE dir=? ORDER BY id");
	rows.number(1, dir->cacheId);
	// Accumulate off to the side: a corrupt chunk must not expose a half-loaded directory.
	DirectoryListing parsed { HintedUser() };
	while(rows.step()) {
		const auto size = sqlite3_column_bytes(rows.s, 0);
		if(size <= 0 || size > 2 * 1024 * 1024) throw Exception("Invalid file list cache chunk");
		StringOutputStream xml;
		LimitedOutputStream<false> bounded(&xml, 2 * 1024 * 1024);
		FilteredOutputStream<UnZstdFilter, false> decoder(&bounded);
		decoder.write(sqlite3_column_blob(rows.s, 0), static_cast<size_t>(size)); decoder.flush();
		MemoryInputStream input("<FileListing Base=\"/\">" + xml.getString() + "</FileListing>");
		parsed.loadXML(input, false);
	}
	if(parsed.getRoot()->files.size() != dir->cachedFileCount || parsed.getRoot()->getSize() != dir->cachedSize) throw Exception("File list cache count mismatch");
	dir->files.swap(parsed.getRoot()->files);
	for(auto f: dir->files) f->setParent(dir);
}
}
