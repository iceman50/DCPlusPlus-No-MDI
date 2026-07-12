/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_SQLITE_DB_H
#define DCPLUSPLUS_DCPP_SQLITE_DB_H

#include "Exception.h"

#include <cstdint>

#include <sqlite3.h>

namespace dcpp {

STANDARD_EXCEPTION(SQLiteException);

class SQLiteStatement;

class SQLiteDB {
public:
	SQLiteDB() = default;
	explicit SQLiteDB(const string& fileName, bool readOnly = false);
	~SQLiteDB();

	SQLiteDB(const SQLiteDB&) = delete;
	SQLiteDB& operator=(const SQLiteDB&) = delete;

	SQLiteDB(SQLiteDB&& rhs) noexcept;
	SQLiteDB& operator=(SQLiteDB&& rhs) noexcept;

	void open(const string& fileName, bool readOnly = false);
	void close() noexcept;

	bool isOpen() const noexcept { return db != nullptr; }
	void execute(const char* sql);
	SQLiteStatement prepare(const char* sql);

	void beginImmediate();
	void commit();
	void rollback() noexcept;

	sqlite3* getHandle() const noexcept { return db; }
	string getError() const;

	static string getLibraryVersion();

private:
	sqlite3* db = nullptr;

	void configure();
	void setLimit(int id, int value) noexcept;
	[[noreturn]] void throwLastError(const string& context) const;
};

class SQLiteStatement {
public:
	SQLiteStatement() = default;
	SQLiteStatement(sqlite3* db, sqlite3_stmt* stmt) noexcept;
	~SQLiteStatement();

	SQLiteStatement(const SQLiteStatement&) = delete;
	SQLiteStatement& operator=(const SQLiteStatement&) = delete;

	SQLiteStatement(SQLiteStatement&& rhs) noexcept;
	SQLiteStatement& operator=(SQLiteStatement&& rhs) noexcept;

	bool step();
	void stepDone();
	void reset();
	void clearBindings() noexcept;

	void bind(int index, const string& value);
	void bind(int index, const void* data, size_t size);
	void bind(int index, int value);
	void bind(int index, int64_t value);
	void bindNull(int index);

	string columnText(int column) const;
	int columnInt(int column) const;
	int64_t columnInt64(int column) const;
	const void* columnBlob(int column) const;
	size_t columnBytes(int column) const;
	bool columnIsNull(int column) const;

private:
	sqlite3* db = nullptr;
	sqlite3_stmt* stmt = nullptr;

	void check(int rc, const char* context) const;
};

class SQLiteTransaction {
public:
	explicit SQLiteTransaction(SQLiteDB& db);
	~SQLiteTransaction();

	SQLiteTransaction(const SQLiteTransaction&) = delete;
	SQLiteTransaction& operator=(const SQLiteTransaction&) = delete;

	void commit();

private:
	SQLiteDB& db;
	bool active = true;
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_SQLITE_DB_H
