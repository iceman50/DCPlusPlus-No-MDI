#include "testbase.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#define private public
#include <dcpp/HashManager.h>
#undef private

#include <dcpp/File.h>
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
	}

	void TearDown() override {
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
