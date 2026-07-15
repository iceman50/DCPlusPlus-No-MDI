/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
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
#include "HashManager.h"

#include "File.h"
#include "FileReader.h"
#include "LogManager.h"
#include "ScopedFunctor.h"
#include "SimpleXML.h"
#include "SFVReader.h"
#include "ZUtils.h"

namespace dcpp {

using std::swap;

namespace {
void logHashStoreWarning(const string& message) {
	LogManager::getInstance()->message(message);
}

uint64_t countRows(SQLiteDB& db, const char* sql) {
	auto stmt = db.prepare(sql);
	return stmt.step() ? static_cast<uint64_t>(stmt.columnInt64(0)) : 0;
}
}

/* Version history:
- Version 1: DC++ 0.307 to 0.68.
- Version 2: DC++ 0.670 to DC++ 0.802. Improved efficiency.
- Version 3: from DC++ 0.810 on. Changed the file registry to be case-sensitive. */
#define HASH_FILE_VERSION_STRING "3"
static const uint32_t HASH_FILE_VERSION = 3;
const int64_t HashManager::MIN_BLOCK_SIZE = 64 * 1024;

optional<TTHValue> HashManager::getTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept {
	Lock l(cs);
	auto tth = store.getTTH(aFileName, aSize, aTimeStamp);
	if(!tth) {
		hasher.hashFile(aFileName, aSize);
	}
	return tth;
}

bool HashManager::getTree(const TTHValue& root, TigerTree& tt) {
	Lock l(cs);
	return store.getTree(root, tt);
}

int64_t HashManager::getBlockSize(const TTHValue& root) {
	Lock l(cs);
	return store.getBlockSize(root);
}

void HashManager::hashDone(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, int64_t speed, int64_t size) {
	try {
		Lock l(cs);
		store.addFile(aFileName, aTimeStamp, tth, true);
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Hashing failed: %1%") % e.getError()));
		return;
	}

	fire(HashManagerListener::TTHDone(), aFileName, tth.getRoot());

	if(speed > 0) {
		LogManager::getInstance()->message(str(F_("Finished hashing: %1% (%2% at %3%/s)") % Util::addBrackets(aFileName) %
			Util::formatBytes(size) % Util::formatBytes(speed)));
	} else if(size >= 0) {
		LogManager::getInstance()->message(str(F_("Finished hashing: %1% (%2%)") % Util::addBrackets(aFileName) %
			Util::formatBytes(size)));
	} else {
		LogManager::getInstance()->message(str(F_("Finished hashing: %1%") % Util::addBrackets(aFileName)));
	}
}

bool HashManager::verifyHashStore(bool fullCheck) {
	Lock l(cs);
	return store.verify(fullCheck);
}

void HashManager::optimizeHashStore() {
	Lock l(cs);
	store.optimize();
}

void HashManager::compactHashStore() {
	Lock l(cs);
	store.compact();
}

void HashManager::HashStore::addFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth, bool aUsed) {
	if(!addTree(tth)) {
		throw HashException(_("Unable to save hash data"));
	}

	auto fname = Util::getFileName(aFileName), fpath = Util::getFilePath(aFileName);

	auto& fileList = fileIndex[fpath];

	auto j = find(fileList.begin(), fileList.end(), fname);
	if (j != fileList.end()) {
		fileList.erase(j);
	}

	fileList.emplace_back(fname, tth.getRoot(), aTimeStamp, aUsed);
	saveFile(aFileName, aTimeStamp, tth);
}

bool HashManager::HashStore::addTree(const TigerTree& tt) noexcept {
	if (treeIndex.find(tt.getRoot()) == treeIndex.end()) {
		try {
			saveTree(tt);
		} catch (const Exception& e) {
			LogManager::getInstance()->message(str(F_("Error saving hash data: %1%") % e.getError()));
			return false;
		}
		treeIndex.emplace(tt.getRoot(), TreeInfo(tt.getFileSize(), tt.getLeaves().size() == 1 ? SMALL_TREE : 0, tt.getBlockSize()));
	}

	return true;
}

bool HashManager::HashStore::loadLegacyTree(File& f, const TreeInfo& ti, const TTHValue& root, TigerTree& tt) {
	if (ti.getIndex() == SMALL_TREE) {
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), root);
		return true;
	}
	try {
		f.setPos(ti.getIndex());
		size_t datalen = TigerTree::calcBlocks(ti.getSize(), ti.getBlockSize()) * TTHValue::BYTES;
		std::unique_ptr<uint8_t[]> buf(new uint8_t[datalen]);
		f.read(&buf[0], datalen);
		tt = TigerTree(ti.getSize(), ti.getBlockSize(), &buf[0]);
		if (!(tt.getRoot() == root))
			return false;
	} catch (const Exception&) {
		return false;
	}

	return true;
}

void HashManager::HashStore::openDb() {
	db.open(getDbFile());
	createSchema();
}

void HashManager::HashStore::createSchema() {
	db.execute(
		"CREATE TABLE IF NOT EXISTS trees ("
		"root BLOB PRIMARY KEY NOT NULL CHECK(length(root) = 24),"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"block_size INTEGER NOT NULL CHECK(block_size >= 1024),"
		"leaves BLOB"
		") WITHOUT ROWID;"
		"CREATE TABLE IF NOT EXISTS files ("
		"path TEXT PRIMARY KEY NOT NULL,"
		"size INTEGER NOT NULL CHECK(size >= 0),"
		"timestamp INTEGER NOT NULL CHECK(timestamp > 0),"
		"root BLOB NOT NULL CHECK(length(root) = 24),"
		"FOREIGN KEY(root) REFERENCES trees(root) ON UPDATE CASCADE ON DELETE CASCADE"
		") WITHOUT ROWID;"
		"CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);"
	);

	migrateSchema(getSchemaVersion());
}

int HashManager::HashStore::getSchemaVersion() {
	auto stmt = db.prepare("PRAGMA user_version");
	return stmt.step() ? stmt.columnInt(0) : 0;
}

void HashManager::HashStore::migrateSchema(int version) {
	if(version < 2) {
		SQLiteTransaction transaction(db);
		db.execute(
			"CREATE TABLE IF NOT EXISTS files_v2 ("
			"path TEXT PRIMARY KEY NOT NULL,"
			"size INTEGER NOT NULL CHECK(size >= 0),"
			"timestamp INTEGER NOT NULL CHECK(timestamp > 0),"
			"root BLOB NOT NULL CHECK(length(root) = 24),"
			"FOREIGN KEY(root) REFERENCES trees(root) ON UPDATE CASCADE ON DELETE CASCADE"
			") WITHOUT ROWID;"
			"INSERT OR REPLACE INTO files_v2(path, size, timestamp, root) "
			"SELECT f.path, f.size, f.timestamp, f.root FROM files f "
			"WHERE f.path <> '' AND f.size >= 0 AND f.timestamp > 0 AND length(f.root) = 24 "
			"AND EXISTS(SELECT 1 FROM trees t WHERE t.root = f.root);"
			"DROP TABLE files;"
			"ALTER TABLE files_v2 RENAME TO files;"
			"CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);"
			"PRAGMA user_version = 2;"
		);
		transaction.commit();
	} else {
		db.execute("CREATE INDEX IF NOT EXISTS idx_hash_files_root ON files(root);");
	}
}

bool HashManager::HashStore::hasDbData() {
	auto stmt = db.prepare(
		"SELECT "
		"EXISTS(SELECT 1 FROM trees LIMIT 1) OR "
		"EXISTS(SELECT 1 FROM files LIMIT 1)"
	);
	return stmt.step() && stmt.columnInt(0) != 0;
}

void HashManager::HashStore::ensureDbOpen() {
	if(!db.isOpen()) {
		openDb();
	}
}

bool HashManager::HashStore::saveTree(const TigerTree& tt) {
	beginWrite();
	writeTreeRow(tt);
	recordWrite();
	return true;
}

void HashManager::HashStore::saveFile(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth) {
	beginWrite();
	writeFileRow(aFileName, aTimeStamp, tth);
	recordWrite();
}

void HashManager::HashStore::writeTreeRow(const TigerTree& tt) {
	if(!saveTreeStmt.isOpen()) {
		saveTreeStmt = db.prepare(
			"INSERT INTO trees(root, size, block_size, leaves) VALUES(?1, ?2, ?3, ?4) "
			"ON CONFLICT(root) DO UPDATE SET "
			"size=excluded.size, block_size=excluded.block_size, leaves=excluded.leaves"
		);
	}
	saveTreeStmt.bind(1, tt.getRoot().data, TTHValue::BYTES);
	saveTreeStmt.bind(2, tt.getFileSize());
	saveTreeStmt.bind(3, tt.getBlockSize());
	if(tt.getLeaves().size() == 1) {
		saveTreeStmt.bindNull(4);
	} else {
		saveTreeStmt.bind(4, tt.getLeaves()[0].data, tt.getLeaves().size() * TTHValue::BYTES);
	}
	saveTreeStmt.stepDone();
	saveTreeStmt.reset();
	saveTreeStmt.clearBindings();
}

void HashManager::HashStore::writeFileRow(const string& aFileName, uint32_t aTimeStamp, const TigerTree& tth) {
	if(!saveFileStmt.isOpen()) {
		saveFileStmt = db.prepare(
			"INSERT INTO files(path, size, timestamp, root) VALUES(?1, ?2, ?3, ?4) "
			"ON CONFLICT(path) DO UPDATE SET "
			"size=excluded.size, timestamp=excluded.timestamp, root=excluded.root"
		);
	}
	saveFileStmt.bind(1, aFileName);
	saveFileStmt.bind(2, tth.getFileSize());
	saveFileStmt.bind(3, static_cast<int64_t>(aTimeStamp));
	saveFileStmt.bind(4, tth.getRoot().data, TTHValue::BYTES);
	saveFileStmt.stepDone();
	saveFileStmt.reset();
	saveFileStmt.clearBindings();
}

void HashManager::HashStore::removeFile(const string& aFileName) noexcept {
	try {
		beginWrite();
		if(!removeFileStmt.isOpen()) {
			removeFileStmt = db.prepare("DELETE FROM files WHERE path=?1");
		}
		removeFileStmt.bind(1, aFileName);
		removeFileStmt.stepDone();
		removeFileStmt.reset();
		removeFileStmt.clearBindings();
		recordWrite();
	} catch (const SQLiteException& e) {
		logHashStoreWarning(str(F_("Error removing stale hash database entry for %1%: %2%") % Util::addBrackets(aFileName) % e.getError()));
	}
}

bool HashManager::HashStore::loadTree(const TTHValue& root, TigerTree& tt) {
	try {
		ensureDbOpen();
		if(!loadTreeStmt.isOpen()) {
			loadTreeStmt = db.prepare("SELECT size, block_size, leaves FROM trees WHERE root=?1");
		}
		loadTreeStmt.bind(1, root.data, TTHValue::BYTES);
		if(!loadTreeStmt.step()) {
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}

		const auto size = loadTreeStmt.columnInt64(0);
		const auto blockSize = loadTreeStmt.columnInt64(1);
		if(size < 0 || blockSize < 1024) {
			logHashStoreWarning(str(F_("Invalid hash tree metadata for %1% in hash database") % root.toBase32()));
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}

		const auto expectedLeaves = TigerTree::calcBlocks(size, blockSize);
		const auto expectedBytes = expectedLeaves * TTHValue::BYTES;
		if(loadTreeStmt.columnIsNull(2)) {
			if(expectedLeaves != 1) {
				logHashStoreWarning(str(F_("Missing hash tree leaf data for %1% in hash database") % root.toBase32()));
				loadTreeStmt.reset();
				loadTreeStmt.clearBindings();
				return false;
			}
			tt = TigerTree(size, blockSize, root);
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return true;
		}

		const auto bytes = loadTreeStmt.columnBytes(2);
		const auto blob = static_cast<const uint8_t*>(loadTreeStmt.columnBlob(2));
		if(!blob || bytes != expectedBytes || bytes == 0 || (bytes % TTHValue::BYTES) != 0) {
			logHashStoreWarning(str(F_("Invalid hash tree leaf data for %1% in hash database") % root.toBase32()));
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}

		std::unique_ptr<uint8_t[]> buf(new uint8_t[bytes]);
		memcpy(&buf[0], blob, bytes);
		tt = TigerTree(size, blockSize, &buf[0]);
		if(tt.getRoot() != root) {
			logHashStoreWarning(str(F_("Hash tree root mismatch for %1% in hash database") % root.toBase32()));
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
			return false;
		}
		loadTreeStmt.reset();
		loadTreeStmt.clearBindings();
		return true;
	} catch (const Exception& e) {
		if(loadTreeStmt.isOpen()) {
			loadTreeStmt.reset();
			loadTreeStmt.clearBindings();
		}
		logHashStoreWarning(str(F_("Error loading hash tree %1%: %2%") % root.toBase32() % e.getError()));
		return false;
	}
}

bool HashManager::HashStore::getTree(const TTHValue& root, TigerTree& tt) {
	auto i = treeIndex.find(root);
	if (i == treeIndex.end())
		return false;
	return loadTree(root, tt);
}

int64_t HashManager::HashStore::getBlockSize(const TTHValue& root) const {
	auto i = treeIndex.find(root);
	return i == treeIndex.end() ? 0 : i->second.getBlockSize();
}

optional<TTHValue> HashManager::HashStore::getTTH(const string& aFileName, int64_t aSize, uint32_t aTimeStamp) noexcept {
	auto fname = Util::getFileName(aFileName), fpath = Util::getFilePath(aFileName);

	auto i = fileIndex.find(fpath);
	if (i != fileIndex.end()) {
		auto j = find(i->second.begin(), i->second.end(), fname);
		if (j != i->second.end()) {
			FileInfo& fi = *j;
			const auto& root = fi.getRoot();
			auto ti = treeIndex.find(root);
			if(ti != treeIndex.end() && ti->second.getSize() == aSize && fi.getTimeStamp() == aTimeStamp) {
				fi.setUsed(true);
				return root;
			}

			// the file size or the timestamp has changed
			removeFile(aFileName);
			i->second.erase(j);
		}
	}
	return std::nullopt;
}

void HashManager::HashStore::rebuild() {
	try {
		flushWrites();
		decltype(fileIndex) newFileIndex;
		decltype(treeIndex) newTreeIndex;
		unordered_map<TTHValue, TigerTree> newTrees;

		for (auto& i: fileIndex) {
			for (auto& j: i.second) {
				if (!j.getUsed())
					continue;

				auto k = treeIndex.find(j.getRoot());
				if (k != treeIndex.end()) {
					newTreeIndex[j.getRoot()] = k->second;
				}
			}
		}

		for (auto i = newTreeIndex.begin(); i != newTreeIndex.end();) {
			TigerTree tree;
			if (loadTree(i->first, tree)) {
				i->second.setIndex(tree.getLeaves().size() == 1 ? SMALL_TREE : 0);
				newTrees.emplace(i->first, tree);
				++i;
			} else {
				newTreeIndex.erase(i++);
			}
		}

		for (auto& i: fileIndex) {
			decltype(fileIndex)::mapped_type newFileList;

			for (auto& j: i.second) {
				if (newTreeIndex.find(j.getRoot()) != newTreeIndex.end() && j.getUsed()) {
					newFileList.push_back(j);
				}
			}

			if(!newFileList.empty()) {
				newFileIndex[i.first] = move(newFileList);
			}
		}

		treeIndex = newTreeIndex;
		fileIndex = newFileIndex;
		{
			SQLiteTransaction transaction(db);
			db.execute("DELETE FROM files");
			db.execute("DELETE FROM trees");
			for (auto& i: newTrees) {
				writeTreeRow(i.second);
			}
			for (auto& i: fileIndex) {
				const string& dir = i.first;
				for (auto& fi: i.second) {
					auto tree = newTrees.find(fi.getRoot());
					if(tree != newTrees.end()) {
						writeFileRow(dir + fi.getFileName(), fi.getTimeStamp(), tree->second);
					}
				}
			}
			transaction.commit();
		}
		if(SETTING(HASH_DB_COMPACT_ON_REBUILD)) {
			compact();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Hash data rebuilding failed: %1%") % e.getError()));
	}
}

void HashManager::HashStore::save() {
	if (db.isOpen()) {
		try {
			flushWrites();
			db.execute("PRAGMA optimize");
			db.execute("PRAGMA wal_checkpoint(PASSIVE)");
		} catch (const SQLiteException& e) {
			LogManager::getInstance()->message(str(F_("Error saving hash data: %1%") % e.getError()));
		}
	}
}

bool HashManager::HashStore::verify(bool fullCheck) {
	ensureDbOpen();
	flushWrites();

	const char* sql = fullCheck ? "PRAGMA integrity_check" : "PRAGMA quick_check";
	auto stmt = db.prepare(sql);
	bool ok = true;
	StringList errors;
	while(stmt.step()) {
		const auto result = stmt.columnText(0);
		if(result != "ok") {
			ok = false;
			errors.push_back(result);
		}
	}

	if(ok) {
		LogManager::getInstance()->message(fullCheck ? _("Hash database integrity check passed") : _("Hash database quick check passed"));
	} else {
		LogManager::getInstance()->message(str(F_("Hash database integrity check failed: %1%") % Util::toString(errors)));
	}
	return ok;
}

void HashManager::HashStore::optimize() {
	ensureDbOpen();
	flushWrites();
	db.execute("PRAGMA optimize");
	db.execute("PRAGMA wal_checkpoint(PASSIVE)");
	LogManager::getInstance()->message(_("Hash database optimized"));
}

void HashManager::HashStore::compact() {
	ensureDbOpen();
	flushWrites();
	resetStatements();
	db.execute("PRAGMA wal_checkpoint(TRUNCATE)");
	const auto oldAttachedLimit = db.setLimit(SQLITE_LIMIT_ATTACHED, 1);
	ScopedFunctor restoreAttachedLimit([this, oldAttachedLimit] {
		db.setLimit(SQLITE_LIMIT_ATTACHED, oldAttachedLimit);
	});
	db.execute("VACUUM");
	db.execute("PRAGMA optimize");
	LogManager::getInstance()->message(_("Hash database compacted"));
}

void HashManager::HashStore::beginWrite() {
	ensureDbOpen();
	if(!writeTransaction) {
		writeTransaction.reset(new SQLiteTransaction(db));
		writeTransactionStatements = 0;
	}
}

void HashManager::HashStore::recordWrite() {
	dirty = true;
	writeTransactionStatements++;
	const auto batchSize = max(1, SETTING(HASH_DB_WRITE_BATCH_SIZE));
	if(static_cast<int>(writeTransactionStatements) >= batchSize) {
		flushWrites();
	}
}

void HashManager::HashStore::flushWrites() {
	if(writeTransaction) {
		writeTransaction->commit();
		writeTransaction.reset();
		writeTransactionStatements = 0;
		dirty = false;
	}
}

void HashManager::HashStore::flushWritesNoexcept() noexcept {
	try {
		flushWrites();
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error saving hash data: %1%") % e.getError()));
	}
}

void HashManager::HashStore::resetStatements() noexcept {
	saveTreeStmt = SQLiteStatement();
	saveFileStmt = SQLiteStatement();
	removeFileStmt = SQLiteStatement();
	loadTreeStmt = SQLiteStatement();
}

string HashManager::HashStore::getIndexFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashIndex.xml"; }
string HashManager::HashStore::getDataFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashData.dat"; }
string HashManager::HashStore::getDbFile() { return Util::getPath(Util::PATH_USER_CONFIG) + "HashStore.sqlite3"; }

class HashLoader: public SimpleXMLReader::CallBack {
public:
	HashLoader(HashManager::HashStore& s, const CountedInputStream<false>& countedStream, uint64_t fileSize, function<void (float)> progressF) :
		store(s),
		countedStream(countedStream),
		streamPos(0),
		fileSize(fileSize),
		progressF(progressF),
		version(HASH_FILE_VERSION),
		inTrees(false),
		inFiles(false),
		inHashStore(false)
	{ }
	void startTag(const string& name, StringPairList& attribs, bool simple);

private:
	HashManager::HashStore& store;

	const CountedInputStream<false>& countedStream;
	uint64_t streamPos;
	uint64_t fileSize;
	function<void (float)> progressF;

	int version;
	string file;

	bool inTrees;
	bool inFiles;
	bool inHashStore;
};

void HashManager::HashStore::loadDb(function<void (float)> progressF) {
	const auto treeRows = countRows(db, "SELECT COUNT(*) FROM trees");
	if(treeRows > 0 && treeRows <= static_cast<uint64_t>(treeIndex.max_size())) {
		treeIndex.reserve(static_cast<size_t>(treeRows));
	}

	uint64_t invalidTrees = 0;
	auto trees = db.prepare("SELECT root, size, block_size, leaves IS NULL FROM trees");
	while(trees.step()) {
		if(trees.columnBytes(0) != TTHValue::BYTES) {
			invalidTrees++;
			continue;
		}
		const auto root = TTHValue(static_cast<const uint8_t*>(trees.columnBlob(0)));
		const auto size = trees.columnInt64(1);
		const auto blockSize = trees.columnInt64(2);
		const auto index = trees.columnInt(3) ? SMALL_TREE : 0;
		if(size >= 0 && blockSize >= 1024) {
			treeIndex.emplace(root, TreeInfo(size, index, blockSize));
		} else {
			invalidTrees++;
		}
	}

	uint64_t invalidFiles = 0;
	uint64_t orphanFiles = 0;
	auto files = db.prepare("SELECT path, timestamp, root FROM files ORDER BY path");
	string lastPath;
	vector<FileInfo>* currentFileList = nullptr;
	uint64_t loaded = 0;
	while(files.step()) {
		auto file = files.columnText(0);
		auto timeStamp = static_cast<uint32_t>(files.columnInt64(1));
		if(file.empty() || timeStamp == 0 || files.columnBytes(2) != TTHValue::BYTES) {
			invalidFiles++;
			continue;
		}

		const auto root = TTHValue(static_cast<const uint8_t*>(files.columnBlob(2)));
		if(treeIndex.find(root) == treeIndex.end()) {
			orphanFiles++;
			continue;
		}

		auto fname = Util::getFileName(file), fpath = Util::getFilePath(file);
		if(!currentFileList || fpath != lastPath) {
			auto result = fileIndex.emplace(fpath, decltype(fileIndex)::mapped_type());
			lastPath = std::move(fpath);
			currentFileList = &result.first->second;
		}
		currentFileList->emplace_back(std::move(fname), root, timeStamp, false);
		if((++loaded % 1024) == 0) {
			progressF(0);
		}
	}

	if(invalidTrees > 0 || invalidFiles > 0 || orphanFiles > 0) {
		logHashStoreWarning(str(F_("Hash database loaded with %1% invalid tree records, %2% invalid file records and %3% file records without matching trees") %
			invalidTrees % invalidFiles % orphanFiles));
	}

	progressF(1);
}

void HashManager::HashStore::loadLegacy(function<void (float)> progressF) {
	Util::migrate(getIndexFile());
	if(File::getSize(getIndexFile()) == -1) {
		return;
	}

	try {
		File f(getIndexFile(), File::READ, File::OPEN);
		CountedInputStream<false> countedStream(&f);
		HashLoader l(*this, countedStream, f.getSize(), progressF);
		SimpleXMLReader(&l).parse(countedStream);
	} catch (const Exception& e) {
		logHashStoreWarning(str(F_("Error loading legacy hash database %1%: %2%") % getIndexFile() % e.getError()));
	}
}

void HashManager::HashStore::migrateLegacy() {
	if(treeIndex.empty() && fileIndex.empty()) {
		return;
	}

	try {
		flushWrites();
		unordered_map<TTHValue, TigerTree> validTrees;
		File dataFile(getDataFile(), File::READ, File::OPEN);

		uint64_t invalidTrees = 0;
		for(auto i = treeIndex.begin(); i != treeIndex.end();) {
			TigerTree tree;
			if(loadLegacyTree(dataFile, i->second, i->first, tree)) {
				i->second.setIndex(tree.getLeaves().size() == 1 ? SMALL_TREE : 0);
				validTrees.emplace(i->first, tree);
				++i;
			} else {
				invalidTrees++;
				i = treeIndex.erase(i);
			}
		}

		uint64_t orphanFiles = 0;
		for(auto i = fileIndex.begin(); i != fileIndex.end();) {
			auto& fileList = i->second;
			for(auto j = fileList.begin(); j != fileList.end();) {
				if(validTrees.find(j->getRoot()) == validTrees.end()) {
					orphanFiles++;
					j = fileList.erase(j);
				} else {
					++j;
				}
			}

			if(fileList.empty()) {
				i = fileIndex.erase(i);
			} else {
				++i;
			}
		}

		SQLiteTransaction transaction(db);
		db.execute("DELETE FROM files");
		db.execute("DELETE FROM trees");
		for(auto& i: validTrees) {
			writeTreeRow(i.second);
		}
		for(auto& i: fileIndex) {
			const string& dir = i.first;
			for(auto& fi: i.second) {
				auto tree = validTrees.find(fi.getRoot());
				if(tree != validTrees.end()) {
					writeFileRow(dir + fi.getFileName(), fi.getTimeStamp(), tree->second);
				}
			}
		}
		transaction.commit();
		if(invalidTrees > 0 || orphanFiles > 0) {
			logHashStoreWarning(str(F_("Legacy hash database migration skipped %1% invalid trees and %2% file records without matching trees") %
				invalidTrees % orphanFiles));
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Hash database migration failed: %1%") % e.getError()));
		fileIndex.clear();
		treeIndex.clear();
	}
}

void HashManager::HashStore::load(function<void (float)> progressF) {
	try {
		openDb();
		if(SETTING(HASH_DB_VERIFY_STARTUP) && !verify(false)) {
			LogManager::getInstance()->message(_("Hash database verification failed during startup; DC++ will rehash files as needed"));
		}
		if(hasDbData()) {
			loadDb(progressF);
		} else {
			loadLegacy(progressF);
			migrateLegacy();
		}
	} catch (const Exception& e) {
		LogManager::getInstance()->message(str(F_("Error loading hash data: %1%") % e.getError()));
		fileIndex.clear();
		treeIndex.clear();
	}
}

namespace {
/* version 2 files were stored in lower-case; carry the file registration over only if the file can
be found, and if it has no case-insensitive duplicate. */

#ifdef _WIN32

bool upgradeFromV2(string& file) {
	WIN32_FIND_DATA data;
	// FindFirstFile does a case-insensitive search by default
	auto handle = ::FindFirstFile(Text::toT(file).c_str(), &data);
	if(handle == INVALID_HANDLE_VALUE) {
		// file not found
		return false;
	}
	if(::FindNextFile(handle, &data)) {
		// found a dupe
		::FindClose(handle);
		return false;
	}
	::FindClose(handle);

	// don't use dcpp::File as that would be case-sensitive
	handle = ::CreateFile((Text::toT(Util::getFilePath(file)) + data.cFileName).c_str(),
		GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if(handle == INVALID_HANDLE_VALUE) {
		return false;
	}

	// use GetFinalPathNameByHandle to retrieve a properly cased path from the
	// lower-case one that the version 2 file registry has provided us with.
	wstring buf(file.size() * 2, 0);
	buf.resize(::GetFinalPathNameByHandle(handle, &buf[0], buf.size(), 0));

	::CloseHandle(handle);

	if(buf.empty()) {
		return false;
	}
	// GetFinalPathNameByHandle prepends "\\?\"; remove it.
	if(buf.size() >= 4 && buf.substr(0, 4) == L"\\\\?\\") {
		buf.erase(0, 4);
	}

	auto buf8 = Text::fromT(buf);
	if(Text::toLower(buf8) == file) {
		file = move(buf8);
		return true;
	}

	return false;
}

#else

bool upgradeFromV2(string& file) {
	/// @todo implement this on Linux; by default, force re-hashing.
	return false;
}

#endif

}

static const string sHashStore = "HashStore";
static const string sversion = "version"; // Oops, v1 was like this
static const string sVersion = "Version";
static const string sTrees = "Trees";
static const string sFiles = "Files";
static const string sFile = "File";
static const string sName = "Name";
static const string sSize = "Size";
static const string sHash = "Hash";
static const string sType = "Type";
static const string sTTH = "TTH";
static const string sIndex = "Index";
static const string sBlockSize = "BlockSize";
static const string sTimeStamp = "TimeStamp";
static const string sRoot = "Root";

void HashLoader::startTag(const string& name, StringPairList& attribs, bool simple) {
	ScopedFunctor([this] {
		auto readBytes = countedStream.getReadBytes();
		if(readBytes != streamPos) {
			streamPos = readBytes;
			progressF(static_cast<float>(readBytes) / static_cast<float>(fileSize));
		}
	});

	if (!inHashStore && name == sHashStore) {
		version = Util::toInt(getAttrib(attribs, sVersion, 0));
		if (version == 0) {
			version = Util::toInt(getAttrib(attribs, sversion, 0));
		}
		inHashStore = !simple;
	} else if (inHashStore && (version == 2 || version == 3)) {
		if (inTrees && name == sHash) {
			const string& type = getAttrib(attribs, sType, 0);
			int64_t index = Util::toInt64(getAttrib(attribs, sIndex, 1));
			int64_t blockSize = Util::toInt64(getAttrib(attribs, sBlockSize, 2));
			int64_t size = Util::toInt64(getAttrib(attribs, sSize, 3));
			const string& root = getAttrib(attribs, sRoot, 4);
			if (!root.empty() && type == sTTH && (index >= 8 || index == HashManager::SMALL_TREE) && blockSize >= 1024) {
				store.treeIndex[TTHValue(root)] = HashManager::HashStore::TreeInfo(size, index, blockSize);
			}
		} else if (inFiles && name == sFile) {
			file = getAttrib(attribs, sName, 0);
			auto timeStamp = Util::toUInt32(getAttrib(attribs, sTimeStamp, 1));
			const auto& root = getAttrib(attribs, sRoot, 2);
			if(!file.empty() && timeStamp > 0 && !root.empty() && (version != 2 || upgradeFromV2(file))) {
				auto fname = Util::getFileName(file), fpath = Util::getFilePath(file);
				store.fileIndex[fpath].emplace_back(fname, TTHValue(root), timeStamp, false);
			}
		} else if (name == sTrees) {
			inTrees = !simple;
		} else if (name == sFiles) {
			inFiles = !simple;
		}
	}
}

HashManager::HashStore::HashStore() :
	dirty(false),
	writeTransactionStatements(0) {
}

HashManager::HashStore::~HashStore() {
	flushWritesNoexcept();
}

void HashManager::Hasher::hashFile(const string& fileName, int64_t size) noexcept {
	Lock l(cs);
	if(w.insert(make_pair(fileName, size)).second && idle) {
		s.signal();
	}
}

bool HashManager::Hasher::pause() noexcept {
	Lock l(cs);
	return paused++;
}

void HashManager::Hasher::resume() noexcept {
	Lock l(cs);
	dcassert(paused > 0);
	if(--paused == 0)
		s.signal();
}

bool HashManager::Hasher::isPaused() const noexcept {
	Lock l(cs);
	return paused > 0;
}

void HashManager::Hasher::stopHashing(const string& baseDir) {
	Lock l(cs);
	for(auto i = w.begin(); i != w.end();) {
		if(strncmp(baseDir.c_str(), i->first.c_str(), baseDir.size()) == 0) {
			w.erase(i++);
		} else {
			++i;
		}
	}
}

void HashManager::Hasher::getStats(string& curFile, uint64_t& bytesLeft, size_t& filesLeft) const {
	Lock l(cs);
	curFile = currentFile;
	filesLeft = w.size();
	if (running)
		filesLeft++;
	bytesLeft = 0;
	for (auto& i: w) {
		bytesLeft += i.second;
	}
	bytesLeft += currentSize;
}

void HashManager::Hasher::instantPause() {
	bool wait = false;
	{
		Lock l(cs);
		if(paused > 0 && !stop) {
			wait = true;
		}
	}
	if(wait)
		s.wait();
}

void HashManager::Hasher::scheduleRebuild() {
	rebuild = true; 
	{
		Lock l(cs);
		if(idle) {
			s.signal();
			return;
		}
	}

	LogManager::getInstance()->message(_("Hash database rebuild has been scheduled"));
}

int HashManager::Hasher::run() {
	setThreadPriority(Thread::IDLE);

	string fname;

	for(;;) {
		s.wait();
		if(stop)
			break;
		if(rebuild) {
			HashManager::getInstance()->doRebuild();
			rebuild = false;
			LogManager::getInstance()->message(_("Hash database rebuilt"));
			s.signal();
			continue;
		}
		{
			Lock l(cs);
			if(!w.empty()) {
				currentFile = fname = w.begin()->first;
				currentSize = w.begin()->second;
				w.erase(w.begin());
				idle = false;
			} else {
				fname.clear();
				idle = true;
				continue;
			}
		}

		running = true;
		if(!fname.empty()) {
			try {
				auto start = GET_TICK();

				File f(fname, File::READ, File::OPEN);
				auto size = f.getSize();
				auto timestamp = f.getLastModified();

				auto sizeLeft = size;
				auto bs = max(TigerTree::calcBlockSize(size, 10), MIN_BLOCK_SIZE);

				TigerTree tt(bs);

				CRC32Filter crc32;
				SFVReader sfv(fname);
				CRC32Filter* xcrc32 = 0;
				if(sfv.hasCRC())
					xcrc32 = &crc32;

				auto lastRead = GET_TICK();

				FileReader fr(true);

				fr.read(fname, [&](const void* buf, size_t n) -> bool {
					if(SETTING(MAX_HASH_SPEED)> 0) {
						uint64_t now = GET_TICK();
						uint64_t minTime = n * 1000LL / (SETTING(MAX_HASH_SPEED) * 1024LL * 1024LL);
						if(lastRead + minTime> now) {
							Thread::sleep(minTime - (now - lastRead));
						}
						lastRead = lastRead + minTime;
					} else {
						lastRead = GET_TICK();
					}

					tt.update(buf, n);
					if(xcrc32)
						(*xcrc32)(buf, n);

					{
						Lock l(cs);
						currentSize = max(static_cast<uint64_t>(currentSize - n), static_cast<uint64_t>(0));
					}
					sizeLeft -= n;

					instantPause();
					return !stop;
				});

				f.close();
				tt.finalize();
				uint64_t end = GET_TICK();
				int64_t speed = 0;
				if(end > start) {
					speed = size * 1000 / (end - start);
				}

				if(xcrc32 && xcrc32->getValue() != sfv.getCRC()) {
					LogManager::getInstance()->message(str(F_("%1% not shared; calculated CRC32 does not match the one found in SFV file.") % Util::addBrackets(fname)));
				} else if(sizeLeft != 0) {
					LogManager::getInstance()->message(str(F_("%1% not shared; hashing did not complete.") % Util::addBrackets(fname)));
				} else {
					HashManager::getInstance()->hashDone(fname, timestamp, tt, speed, size);
				}
			} catch(const FileException& e) {
				LogManager::getInstance()->message(str(F_("Error hashing %1%: %2%") % Util::addBrackets(fname) % e.getError()));
			}
		}
		{
			Lock l(cs);
			currentFile.clear();
			currentSize = 0;
		}
		running = false;
		s.signal();
	}
	return 0;
}

HashManager::HashPauser::HashPauser() {
	HashManager::getInstance()->pauseHashing();
}

HashManager::HashPauser::~HashPauser() {
	HashManager::getInstance()->resumeHashing();
}

bool HashManager::pauseHashing() noexcept {
	Lock l(cs);
	return hasher.pause();
}

void HashManager::resumeHashing() noexcept {
	Lock l(cs);
	hasher.resume();
}

bool HashManager::isHashingPaused() const noexcept {
	Lock l(cs);
	return hasher.isPaused();
}

} // namespace dcpp
