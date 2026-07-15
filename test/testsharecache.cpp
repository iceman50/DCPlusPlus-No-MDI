#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#define private public
#include <dcpp/ShareManager.h>
#undef private

#include <dcpp/ClientManager.h>
#include <dcpp/File.h>
#include <dcpp/HashManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchResult.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/TimerManager.h>
#include <dcpp/UploadManager.h>
#include <dcpp/Util.h>

using namespace dcpp;

namespace {

class ShareCacheTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		const auto base = std::filesystem::temp_directory_path() /
			("dcpp-sharecache-test-" + std::to_string(ticks) + "-" + std::to_string(++counter));
		configPath = base.string() + PATH_SEPARATOR;
		sharePath = configPath + "Share" PATH_SEPARATOR_STR;
		secondSharePath = configPath + "SecondShare" PATH_SEPARATOR_STR;
		std::filesystem::remove_all(base);
		std::filesystem::create_directories(sharePath);
		std::filesystem::create_directories(secondSharePath);

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
		TimerManager::newInstance();
		HashManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		UploadManager::newInstance();
		QueueManager::newInstance();
		ShareManager::newInstance();
	}

	void TearDown() override {
		ShareManager::deleteInstance();
		QueueManager::deleteInstance();
		UploadManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		HashManager::getInstance()->shutdown();
		HashManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	void populateShare() {
		auto sm = ShareManager::getInstance();
		sm->shares[sharePath] = "Virtual";
		std::filesystem::create_directories(sharePath + "Child");
		std::ofstream(sharePath + "Child" PATH_SEPARATOR_STR "file.bin") << "data";

		auto root = ShareManager::Directory::create("Virtual");
		auto child = ShareManager::Directory::create("Child", root);
		root->directories.emplace("Child", child);

		ShareManager::Directory::File file("file.bin", 1234, child,
			TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
		child->files.insert(std::move(file));

		sm->directories["Virtual"] = root;
		sm->rebuildIndices(1);
	}

	void populateMergedShares() {
		auto sm = ShareManager::getInstance();
		sm->shares[sharePath] = "Virtual";
		sm->shares[secondSharePath] = "Virtual";
		std::filesystem::create_directories(sharePath + "Child");
		std::filesystem::create_directories(secondSharePath + "Other");
		std::ofstream(sharePath + "Child" PATH_SEPARATOR_STR "file.bin") << "data";
		std::ofstream(secondSharePath + "Other" PATH_SEPARATOR_STR "other.bin") << "more";

		auto root = ShareManager::Directory::create("Virtual");
		auto child = ShareManager::Directory::create("Child", root);
		auto other = ShareManager::Directory::create("Other", root);
		root->directories.emplace("Child", child);
		root->directories.emplace("Other", other);

		child->files.insert(ShareManager::Directory::File("file.bin", 1234, child,
			TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));
		other->files.insert(ShareManager::Directory::File("other.bin", 2345, other,
			TTHValue("BAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));

		sm->directories["Virtual"] = root;
		sm->rebuildIndices(2);
	}

	void clearLoadedShare() {
		auto sm = ShareManager::getInstance();
		sm->directories.clear();
		sm->tthIndex.clear();
		sm->bloom.clear();
	}

	string configPath;
	string sharePath;
	string secondSharePath;
	static int counter;
};

int ShareCacheTest::counter = 0;

} // namespace

TEST_F(ShareCacheTest, round_trips_share_tree_and_indices) {
	populateShare();
	ASSERT_EQ(ShareManager::getInstance()->getSharedFiles(), 1U);
	ASSERT_EQ(ShareManager::getInstance()->getShareSize(), 1234);

	ShareManager::getInstance()->saveShareCache();
	ASSERT_GE(File::getSize(configPath + "ShareCache.sqlite3"), 0);

	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());

	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 1U);
	EXPECT_EQ(ShareManager::getInstance()->getShareSize(), 1234);
	EXPECT_TRUE(ShareManager::getInstance()->isTTHShared(TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")));
	EXPECT_EQ(ShareManager::getInstance()->toReal("/Virtual/Child/file.bin"), sharePath + "Child" PATH_SEPARATOR_STR "file.bin");
}

TEST_F(ShareCacheTest, preserves_merged_shares_search_and_protocol_lookups) {
	populateMergedShares();
	ASSERT_EQ(ShareManager::getInstance()->getSharedFiles(), 2U);
	ASSERT_EQ(ShareManager::getInstance()->getShareSize(), 3579);

	ShareManager::getInstance()->saveShareCache();
	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());

	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 2U);
	EXPECT_EQ(ShareManager::getInstance()->toVirtual(TTHValue("BAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")),
		"/Virtual/Other/other.bin");
	EXPECT_EQ(ShareManager::getInstance()->toReal("/Virtual/Other/other.bin"),
		secondSharePath + "Other" PATH_SEPARATOR_STR "other.bin");

	auto results = ShareManager::getInstance()->search("other", SearchManager::SIZE_DONTCARE, 0, SearchManager::TYPE_ANY, 10);
	EXPECT_TRUE(std::any_of(results.begin(), results.end(), [](const SearchResultPtr& result) {
		return result->getFile() == "Virtual\\Other\\other.bin";
	}));
}

TEST_F(ShareCacheTest, full_refresh_saves_cache_snapshot_for_refresh_command_path) {
	ShareManager::getInstance()->shares[sharePath] = "Virtual";
	std::filesystem::create_directories(sharePath + "Fresh");
	std::ofstream(sharePath + "Fresh" PATH_SEPARATOR_STR "new.bin") << "data";

	ShareManager::getInstance()->refresh(true, false, true);

	EXPECT_GE(File::getSize(configPath + "ShareCache.sqlite3"), 0);
	clearLoadedShare();
	EXPECT_TRUE(ShareManager::getInstance()->loadShareCache());
	EXPECT_TRUE(ShareManager::getInstance()->hasVirtual("Virtual"));
}

TEST_F(ShareCacheTest, rejects_cache_when_share_settings_change) {
	populateShare();
	ShareManager::getInstance()->saveShareCache();

	SettingsManager::getInstance()->set(SettingsManager::SHARING_SKIPLIST_EXTENSIONS, ".bin");
	clearLoadedShare();

	EXPECT_FALSE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 0U);
}

TEST_F(ShareCacheTest, skips_cache_when_queue_duplicate_removal_requires_fresh_share) {
	populateShare();
	ShareManager::getInstance()->saveShareCache();

	SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, true);
	clearLoadedShare();

	EXPECT_FALSE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 0U);
}
