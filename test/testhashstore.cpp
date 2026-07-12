#include "testbase.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#define private public
#include <dcpp/HashManager.h>
#undef private

#include <dcpp/File.h>
#include <dcpp/LogManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SQLiteDB.h>
#include <dcpp/Util.h>

using namespace dcpp;

namespace {

class HashStoreTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto base = std::filesystem::temp_directory_path() /
			("dcpp-hashstore-test-" + std::to_string(ticks) + "-" + std::to_string(++counter));
		configPath = base.string() + PATH_SEPARATOR;
		std::filesystem::remove_all(base);
		std::filesystem::create_directories(base);

		Util::PathsMap paths;
		paths[Util::PATH_USER_CONFIG] = configPath;
		paths[Util::PATH_USER_LOCAL] = configPath;
		paths[Util::PATH_GLOBAL_CONFIG] = configPath;
		paths[Util::PATH_RESOURCES] = configPath;
		paths[Util::PATH_LOCALE] = configPath;
		paths[Util::PATH_DOWNLOADS] = configPath;
		paths[Util::PATH_FILE_LISTS] = configPath;
		paths[Util::PATH_HUB_LISTS] = configPath;
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);

		SettingsManager::newInstance();
		LogManager::newInstance();
	}

	void TearDown() override {
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	string path(const string& name) const {
		return configPath + name;
	}

	static TigerTree makeTree(size_t size) {
		string data(size, '\0');
		for(size_t i = 0; i < data.size(); ++i) {
			data[i] = static_cast<char>('a' + (i % 26));
		}

		TigerTree tree(1024);
		tree.update(data.data(), data.size());
		tree.finalize();
		return tree;
	}

	static TTHValue makeRoot(size_t size) {
		return makeTree(size).getRoot();
	}

	static int scalarInt(SQLiteDB& db, const char* sql) {
		auto stmt = db.prepare(sql);
		return stmt.step() ? stmt.columnInt(0) : 0;
	}

	static void insertTree(SQLiteDB& db, const TigerTree& tree) {
		auto stmt = db.prepare("INSERT INTO trees(root, size, block_size, leaves) VALUES(?1, ?2, ?3, ?4)");
		stmt.bind(1, tree.getRoot().data, TTHValue::BYTES);
		stmt.bind(2, tree.getFileSize());
		stmt.bind(3, tree.getBlockSize());
		if(tree.getLeaves().size() == 1) {
			stmt.bindNull(4);
		} else {
			stmt.bind(4, tree.getLeaves()[0].data, tree.getLeaves().size() * TTHValue::BYTES);
		}
		stmt.stepDone();
	}

	static void insertFile(SQLiteDB& db, const string& fileName, uint32_t timeStamp, int64_t size, const TTHValue& root) {
		auto stmt = db.prepare("INSERT INTO files(path, size, timestamp, root) VALUES(?1, ?2, ?3, ?4)");
		stmt.bind(1, fileName);
		stmt.bind(2, size);
		stmt.bind(3, static_cast<int64_t>(timeStamp));
		stmt.bind(4, root.data, TTHValue::BYTES);
		stmt.stepDone();
	}

	void writeV1Store(const string& fileName, uint32_t timeStamp, const TigerTree& tree, const string& orphanFile) const {
		SQLiteDB db(path("HashStore.sqlite3"));
		db.execute(
			"CREATE TABLE trees ("
			"root BLOB PRIMARY KEY NOT NULL CHECK(length(root) = 24),"
			"size INTEGER NOT NULL CHECK(size >= 0),"
			"block_size INTEGER NOT NULL CHECK(block_size >= 1024),"
			"leaves BLOB"
			") WITHOUT ROWID;"
			"CREATE TABLE files ("
			"path TEXT PRIMARY KEY NOT NULL,"
			"size INTEGER NOT NULL CHECK(size >= 0),"
			"timestamp INTEGER NOT NULL CHECK(timestamp > 0),"
			"root BLOB NOT NULL CHECK(length(root) = 24)"
			") WITHOUT ROWID;"
			"CREATE INDEX idx_hash_files_root ON files(root);"
			"PRAGMA user_version = 1;"
		);
		insertTree(db, tree);
		insertFile(db, fileName, timeStamp, tree.getFileSize(), tree.getRoot());
		insertFile(db, orphanFile, timeStamp + 1, tree.getFileSize(), makeRoot(33));
	}

	static void writeLegacyStore(const string& configPath, const string& fileName, uint32_t timeStamp, const TigerTree& tree) {
		const auto dataPath = configPath + "HashData.dat";
		const auto indexPath = configPath + "HashIndex.xml";

		std::ofstream data(dataPath, std::ios::binary | std::ios::trunc);
		const int64_t leafOffset = sizeof(int64_t);
		const int64_t nextOffset = leafOffset + static_cast<int64_t>(tree.getLeaves().size() * TTHValue::BYTES);
		data.write(reinterpret_cast<const char*>(&nextOffset), sizeof(nextOffset));
		data.write(reinterpret_cast<const char*>(tree.getLeaves()[0].data), tree.getLeaves().size() * TTHValue::BYTES);
		data.close();

		std::ofstream index(indexPath, std::ios::binary | std::ios::trunc);
		index << "<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n";
		index << "<HashStore Version=\"3\">\r\n";
		index << "\t<Trees>\r\n";
		index << "\t\t<Hash Type=\"TTH\" Index=\"" << leafOffset << "\" BlockSize=\"" << tree.getBlockSize() <<
			"\" Size=\"" << tree.getFileSize() << "\" Root=\"" << tree.getRoot().toBase32() << "\"/>\r\n";
		index << "\t</Trees>\r\n\t<Files>\r\n";
		index << "\t\t<File Name=\"" << fileName << "\" TimeStamp=\"" << timeStamp <<
			"\" Root=\"" << tree.getRoot().toBase32() << "\"/>\r\n";
		index << "\t</Files>\r\n</HashStore>";
	}

	string configPath;
	static inline int counter = 0;
};

TEST_F(HashStoreTest, persists_file_and_tree_in_sqlite) {
	const auto fileName = path("shared-file.bin");
	const uint32_t timeStamp = 12345;
	const auto tree = makeTree(4097);

	{
		HashManager::HashStore store;
		store.load([](float) {});
		store.addFile(fileName, timeStamp, tree, true);
	}

	{
		HashManager::HashStore store;
		store.load([](float) {});
		auto root = store.getTTH(fileName, tree.getFileSize(), timeStamp);
		ASSERT_TRUE(root);
		EXPECT_EQ(tree.getRoot(), *root);
		EXPECT_EQ(tree.getBlockSize(), store.getBlockSize(tree.getRoot()));

		TigerTree loaded;
		ASSERT_TRUE(store.getTree(tree.getRoot(), loaded));
		EXPECT_EQ(tree.getRoot(), loaded.getRoot());
		EXPECT_EQ(tree.getFileSize(), loaded.getFileSize());
		EXPECT_EQ(tree.getBlockSize(), loaded.getBlockSize());
		EXPECT_EQ(tree.getLeaves().size(), loaded.getLeaves().size());
	}
}

TEST_F(HashStoreTest, batches_writes_and_flushes_on_save) {
	SettingsManager::getInstance()->set(SettingsManager::HASH_DB_WRITE_BATCH_SIZE, 1000);
	const auto fileName = path("batched-file.bin");
	const uint32_t timeStamp = 12345;
	const auto tree = makeTree(4097);

	HashManager::HashStore store;
	store.load([](float) {});
	store.addFile(fileName, timeStamp, tree, true);

	{
		SQLiteDB db(path("HashStore.sqlite3"));
		EXPECT_EQ(0, scalarInt(db, "SELECT COUNT(*) FROM files"));
	}

	store.save();

	{
		SQLiteDB db(path("HashStore.sqlite3"));
		EXPECT_EQ(1, scalarInt(db, "SELECT COUNT(*) FROM files"));
		EXPECT_EQ(1, scalarInt(db, "SELECT COUNT(*) FROM trees"));
	}
}

TEST_F(HashStoreTest, stale_file_lookup_is_removed_from_sqlite) {
	const auto fileName = path("stale-file.bin");
	const uint32_t timeStamp = 12345;
	const auto tree = makeTree(2048);

	{
		HashManager::HashStore store;
		store.load([](float) {});
		store.addFile(fileName, timeStamp, tree, true);
		EXPECT_FALSE(store.getTTH(fileName, tree.getFileSize() + 1, timeStamp));
	}

	{
		HashManager::HashStore store;
		store.load([](float) {});
		EXPECT_FALSE(store.getTTH(fileName, tree.getFileSize(), timeStamp));
	}
}

TEST_F(HashStoreTest, rebuild_keeps_only_used_files_and_trees) {
	const auto keepName = path("keep.bin");
	const auto pruneName = path("prune.bin");
	const auto keepTree = makeTree(3000);
	const auto pruneTree = makeTree(5000);

	{
		HashManager::HashStore store;
		store.load([](float) {});
		store.addFile(keepName, 111, keepTree, true);
		store.addFile(pruneName, 222, pruneTree, false);
		store.rebuild();
	}

	{
		HashManager::HashStore store;
		store.load([](float) {});
		EXPECT_TRUE(store.getTTH(keepName, keepTree.getFileSize(), 111));
		EXPECT_FALSE(store.getTTH(pruneName, pruneTree.getFileSize(), 222));

		TigerTree loaded;
		EXPECT_TRUE(store.getTree(keepTree.getRoot(), loaded));
		EXPECT_FALSE(store.getTree(pruneTree.getRoot(), loaded));
	}
}

TEST_F(HashStoreTest, compact_and_verify_keep_valid_hashes) {
	const auto fileName = path("compact-file.bin");
	const uint32_t timeStamp = 12345;
	const auto tree = makeTree(4097);

	HashManager::HashStore store;
	store.load([](float) {});
	store.addFile(fileName, timeStamp, tree, true);
	store.save();

	EXPECT_TRUE(store.verify(false));
	store.optimize();
	store.compact();
	EXPECT_TRUE(store.verify(true));
	EXPECT_TRUE(store.getTTH(fileName, tree.getFileSize(), timeStamp));
}

TEST_F(HashStoreTest, v1_sqlite_schema_is_migrated_and_orphans_are_dropped) {
	const auto fileName = path("valid-v1.bin");
	const auto orphanName = path("orphan-v1.bin");
	const uint32_t timeStamp = 22222;
	const auto tree = makeTree(10);
	writeV1Store(fileName, timeStamp, tree, orphanName);

	HashManager::HashStore store;
	store.load([](float) {});
	EXPECT_EQ(2, store.getSchemaVersion());
	EXPECT_TRUE(store.getTTH(fileName, tree.getFileSize(), timeStamp));
	EXPECT_FALSE(store.getTTH(orphanName, tree.getFileSize(), timeStamp + 1));

	SQLiteDB db(path("HashStore.sqlite3"));
	EXPECT_EQ(1, scalarInt(db, "SELECT COUNT(*) FROM files"));
	EXPECT_EQ(1, scalarInt(db, "SELECT COUNT(*) FROM trees"));
	EXPECT_EQ(2, scalarInt(db, "PRAGMA user_version"));
}

TEST_F(HashStoreTest, foreign_key_rejects_new_orphan_file_records) {
	const auto tree = makeTree(20);
	const auto root = makeRoot(21);

	HashManager::HashStore store;
	store.load([](float) {});
	store.addFile(path("known.bin"), 111, tree, true);
	store.save();

	SQLiteDB db(path("HashStore.sqlite3"));
	EXPECT_THROW(insertFile(db, path("orphan.bin"), 222, tree.getFileSize(), root), SQLiteException);
}

TEST_F(HashStoreTest, invalid_tree_leaf_blob_is_ignored) {
	const auto fileName = path("invalid-tree.bin");
	const uint32_t timeStamp = 33333;
	const auto tree = makeTree(4097);

	{
		HashManager::HashStore store;
		store.load([](float) {});
		store.addFile(fileName, timeStamp, tree, true);
		store.save();
	}

	{
		SQLiteDB db(path("HashStore.sqlite3"));
		auto stmt = db.prepare("UPDATE trees SET leaves=?1 WHERE root=?2");
		const uint8_t badLeaf = 0;
		stmt.bind(1, &badLeaf, sizeof(badLeaf));
		stmt.bind(2, tree.getRoot().data, TTHValue::BYTES);
		stmt.stepDone();
	}

	HashManager::HashStore store;
	store.load([](float) {});
	TigerTree loaded;
	EXPECT_FALSE(store.getTree(tree.getRoot(), loaded));
}

TEST_F(HashStoreTest, compact_on_rebuild_setting_runs_after_pruning) {
	SettingsManager::getInstance()->set(SettingsManager::HASH_DB_COMPACT_ON_REBUILD, true);
	const auto keepName = path("keep-compact.bin");
	const auto pruneName = path("prune-compact.bin");
	const auto keepTree = makeTree(3000);
	const auto pruneTree = makeTree(5000);

	HashManager::HashStore store;
	store.load([](float) {});
	store.addFile(keepName, 111, keepTree, true);
	store.addFile(pruneName, 222, pruneTree, false);
	store.rebuild();

	EXPECT_TRUE(store.verify(false));
	EXPECT_TRUE(store.getTTH(keepName, keepTree.getFileSize(), 111));
	EXPECT_FALSE(store.getTTH(pruneName, pruneTree.getFileSize(), 222));
}

TEST_F(HashStoreTest, migrates_legacy_xml_and_dat_store) {
	const auto fileName = path("legacy-file.bin");
	const uint32_t timeStamp = 67890;
	const auto tree = makeTree(4097);
	writeLegacyStore(configPath, fileName, timeStamp, tree);

	{
		HashManager::HashStore store;
		store.load([](float) {});
		auto root = store.getTTH(fileName, tree.getFileSize(), timeStamp);
		ASSERT_TRUE(root);
		EXPECT_EQ(tree.getRoot(), *root);
	}

	EXPECT_TRUE(std::filesystem::exists(configPath + "HashStore.sqlite3"));

	{
		HashManager::HashStore store;
		store.load([](float) {});
		auto root = store.getTTH(fileName, tree.getFileSize(), timeStamp);
		ASSERT_TRUE(root);
		EXPECT_EQ(tree.getRoot(), *root);
	}
}

} // namespace
