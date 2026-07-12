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

#include "stdinc.h"
#include "SQLiteDB.h"

namespace dcpp {

namespace {
string errorMessage(sqlite3* db) {
	const auto message = db ? sqlite3_errmsg(db) : nullptr;
	return message ? string(message) : string("Unknown SQLite error");
}
}

SQLiteDB::SQLiteDB(const string& fileName, bool readOnly) {
	open(fileName, readOnly);
}

SQLiteDB::~SQLiteDB() {
	close();
}

SQLiteDB::SQLiteDB(SQLiteDB&& rhs) noexcept : db(rhs.db) {
	rhs.db = nullptr;
}

SQLiteDB& SQLiteDB::operator=(SQLiteDB&& rhs) noexcept {
	if (this != &rhs) {
		close();
		db = rhs.db;
		rhs.db = nullptr;
	}
	return *this;
}

void SQLiteDB::open(const string& fileName, bool readOnly) {
	close();

	const auto flags = (readOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) |
		SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_PRIVATECACHE;

	sqlite3* handle = nullptr;
	const auto rc = sqlite3_open_v2(fileName.c_str(), &handle, flags, nullptr);
	db = handle;
	if (rc != SQLITE_OK) {
		throwLastError("opening " + fileName);
	}

	configure();
}

void SQLiteDB::close() noexcept {
	if (db) {
		sqlite3_close(db);
		db = nullptr;
	}
}

void SQLiteDB::configure() {
	sqlite3_busy_timeout(db, 30000);

	int oldValue = 0;
	sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, &oldValue);
#ifdef SQLITE_DBCONFIG_TRUSTED_SCHEMA
	sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, &oldValue);
#endif
#ifdef SQLITE_DBCONFIG_ENABLE_TRIGGER
	sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_TRIGGER, 0, &oldValue);
#endif
#ifdef SQLITE_DBCONFIG_ENABLE_VIEW
	sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_VIEW, 0, &oldValue);
#endif

	setLimit(SQLITE_LIMIT_LENGTH, 16 * 1024 * 1024);
	setLimit(SQLITE_LIMIT_SQL_LENGTH, 64 * 1024);
	setLimit(SQLITE_LIMIT_COLUMN, 64);
	setLimit(SQLITE_LIMIT_COMPOUND_SELECT, 4);
	setLimit(SQLITE_LIMIT_ATTACHED, 0);
	setLimit(SQLITE_LIMIT_LIKE_PATTERN_LENGTH, 1024);
	setLimit(SQLITE_LIMIT_VARIABLE_NUMBER, 128);

	execute(
		"PRAGMA trusted_schema=OFF;"
		"PRAGMA foreign_keys=ON;"
		"PRAGMA journal_mode=WAL;"
		"PRAGMA synchronous=NORMAL;"
		"PRAGMA temp_store=MEMORY;"
		"PRAGMA mmap_size=0;"
		"PRAGMA cell_size_check=ON;"
	);
}

void SQLiteDB::setLimit(int id, int value) noexcept {
	sqlite3_limit(db, id, value);
}

void SQLiteDB::execute(const char* sql) {
	char* error = nullptr;
	const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
	if (rc != SQLITE_OK) {
		string message = error ? string(error) : getError();
		sqlite3_free(error);
		throw SQLiteException(message);
	}
}

SQLiteStatement SQLiteDB::prepare(const char* sql) {
	sqlite3_stmt* stmt = nullptr;
	const auto rc = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &stmt, nullptr);
	if (rc != SQLITE_OK) {
		throwLastError("preparing SQL statement");
	}
	return SQLiteStatement(db, stmt);
}

void SQLiteDB::beginImmediate() {
	execute("BEGIN IMMEDIATE");
}

void SQLiteDB::commit() {
	execute("COMMIT");
}

void SQLiteDB::rollback() noexcept {
	try {
		execute("ROLLBACK");
	} catch (const SQLiteException&) {
	}
}

string SQLiteDB::getError() const {
	return errorMessage(db);
}

string SQLiteDB::getLibraryVersion() {
	return sqlite3_libversion();
}

void SQLiteDB::throwLastError(const string& context) const {
	throw SQLiteException(context + ": " + getError());
}

SQLiteStatement::SQLiteStatement(sqlite3* aDb, sqlite3_stmt* aStmt) noexcept : db(aDb), stmt(aStmt) {
}

SQLiteStatement::~SQLiteStatement() {
	if (stmt) {
		sqlite3_finalize(stmt);
	}
}

SQLiteStatement::SQLiteStatement(SQLiteStatement&& rhs) noexcept : db(rhs.db), stmt(rhs.stmt) {
	rhs.db = nullptr;
	rhs.stmt = nullptr;
}

SQLiteStatement& SQLiteStatement::operator=(SQLiteStatement&& rhs) noexcept {
	if (this != &rhs) {
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		db = rhs.db;
		stmt = rhs.stmt;
		rhs.db = nullptr;
		rhs.stmt = nullptr;
	}
	return *this;
}

bool SQLiteStatement::step() {
	const auto rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		return true;
	}
	if (rc == SQLITE_DONE) {
		return false;
	}
	check(rc, "stepping SQLite statement");
	return false;
}

void SQLiteStatement::stepDone() {
	if (step()) {
		throw SQLiteException("SQLite statement unexpectedly returned a row");
	}
}

void SQLiteStatement::reset() {
	check(sqlite3_reset(stmt), "resetting SQLite statement");
}

void SQLiteStatement::clearBindings() noexcept {
	sqlite3_clear_bindings(stmt);
}

void SQLiteStatement::bind(int index, const string& value) {
	check(sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT), "binding text");
}

void SQLiteStatement::bind(int index, const void* data, size_t size) {
	check(sqlite3_bind_blob(stmt, index, data, static_cast<int>(size), SQLITE_TRANSIENT), "binding blob");
}

void SQLiteStatement::bind(int index, int value) {
	check(sqlite3_bind_int(stmt, index, value), "binding int");
}

void SQLiteStatement::bind(int index, int64_t value) {
	check(sqlite3_bind_int64(stmt, index, value), "binding int64");
}

void SQLiteStatement::bindNull(int index) {
	check(sqlite3_bind_null(stmt, index), "binding null");
}

string SQLiteStatement::columnText(int column) const {
	const auto text = sqlite3_column_text(stmt, column);
	const auto bytes = sqlite3_column_bytes(stmt, column);
	return text ? string(reinterpret_cast<const char*>(text), bytes) : string();
}

int SQLiteStatement::columnInt(int column) const {
	return sqlite3_column_int(stmt, column);
}

int64_t SQLiteStatement::columnInt64(int column) const {
	return sqlite3_column_int64(stmt, column);
}

const void* SQLiteStatement::columnBlob(int column) const {
	return sqlite3_column_blob(stmt, column);
}

size_t SQLiteStatement::columnBytes(int column) const {
	return static_cast<size_t>(sqlite3_column_bytes(stmt, column));
}

bool SQLiteStatement::columnIsNull(int column) const {
	return sqlite3_column_type(stmt, column) == SQLITE_NULL;
}

void SQLiteStatement::check(int rc, const char* context) const {
	if (rc != SQLITE_OK) {
		throw SQLiteException(string(context) + ": " + errorMessage(db));
	}
}

SQLiteTransaction::SQLiteTransaction(SQLiteDB& aDb) : db(aDb) {
	db.beginImmediate();
}

SQLiteTransaction::~SQLiteTransaction() {
	if (active) {
		db.rollback();
	}
}

void SQLiteTransaction::commit() {
	db.commit();
	active = false;
}

} // namespace dcpp
