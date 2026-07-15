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
	/** Open a SQLite database immediately, using the same secure configuration as open(). */
	explicit SQLiteDB(const string& fileName, bool readOnly = false);
	~SQLiteDB();

	SQLiteDB(const SQLiteDB&) = delete;
	SQLiteDB& operator=(const SQLiteDB&) = delete;

	SQLiteDB(SQLiteDB&& rhs) noexcept;
	SQLiteDB& operator=(SQLiteDB&& rhs) noexcept;

	/** Open a database file and apply the project's restricted SQLite runtime settings. */
	void open(const string& fileName, bool readOnly = false);
	/** Close the database handle; pending prepared statements must have been finalized first. */
	void close() noexcept;

	bool isOpen() const noexcept { return db != nullptr; }
	/** Execute one or more SQL statements that do not need bound parameters or returned rows. */
	void execute(const char* sql);
	/** Compile a SQL statement for bound parameters, repeated execution, or row inspection. */
	SQLiteStatement prepare(const char* sql);

	/** Start an IMMEDIATE transaction so hash-store writes either fully commit or fully roll back. */
	void beginImmediate();
	/** Commit the active transaction. */
	void commit();
	/** Roll back the active transaction; used from noexcept cleanup paths. */
	void rollback() noexcept;

	sqlite3* getHandle() const noexcept { return db; }
	/** Return SQLite's latest error message for this connection. */
	string getError() const;
	/** Apply a SQLite runtime limit and return SQLite's previous value. */
	int setLimit(int id, int value) noexcept;

	/** Return the linked SQLite library version for diagnostics and the About dialog. */
	static string getLibraryVersion();

private:
	sqlite3* db = nullptr;

	/** Apply PRAGMAs and SQLite limits that harden the connection for local hash/share storage. */
	void configure();
	[[noreturn]] void throwLastError(const string& context) const;
};

class SQLiteStatement {
public:
	SQLiteStatement() = default;
	/** Own an already prepared SQLite statement handle. */
	SQLiteStatement(sqlite3* db, sqlite3_stmt* stmt) noexcept;
	~SQLiteStatement();

	SQLiteStatement(const SQLiteStatement&) = delete;
	SQLiteStatement& operator=(const SQLiteStatement&) = delete;

	SQLiteStatement(SQLiteStatement&& rhs) noexcept;
	SQLiteStatement& operator=(SQLiteStatement&& rhs) noexcept;

	/** Advance the statement and return true only when a row is available. */
	bool step();
	/** Execute a statement that must finish without returning rows. */
	void stepDone();
	/** Reset the prepared statement so it can be rebound and executed again. */
	void reset();
	/** Clear all previously bound values without throwing. */
	void clearBindings() noexcept;
	bool isOpen() const noexcept { return stmt != nullptr; }

	/** Bind UTF-8 text to a one-based SQLite parameter index. */
	void bind(int index, const string& value);
	/** Bind a binary blob to a one-based SQLite parameter index. */
	void bind(int index, const void* data, size_t size);
	/** Bind a 32-bit integer to a one-based SQLite parameter index. */
	void bind(int index, int value);
	/** Bind a 64-bit integer to a one-based SQLite parameter index. */
	void bind(int index, int64_t value);
	/** Bind SQL NULL to a one-based SQLite parameter index. */
	void bindNull(int index);

	/** Read the current row column as text. */
	string columnText(int column) const;
	/** Read the current row column as a 32-bit integer. */
	int columnInt(int column) const;
	/** Read the current row column as a 64-bit integer. */
	int64_t columnInt64(int column) const;
	/** Read the current row column as a blob pointer valid until the next statement step/reset. */
	const void* columnBlob(int column) const;
	/** Return the byte length of the current row column. */
	size_t columnBytes(int column) const;
	/** Return true when the current row column is SQL NULL. */
	bool columnIsNull(int column) const;

private:
	sqlite3* db = nullptr;
	sqlite3_stmt* stmt = nullptr;

	void check(int rc, const char* context) const;
};

class SQLiteTransaction {
public:
	/** Begin an IMMEDIATE transaction and roll it back automatically unless commit() is called. */
	explicit SQLiteTransaction(SQLiteDB& db);
	~SQLiteTransaction();

	SQLiteTransaction(const SQLiteTransaction&) = delete;
	SQLiteTransaction& operator=(const SQLiteTransaction&) = delete;

	/** Commit the transaction and disable destructor rollback. */
	void commit();

private:
	SQLiteDB& db;
	bool active = true;
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_SQLITE_DB_H
