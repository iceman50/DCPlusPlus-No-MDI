#include "testbase.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#define private public
#include <dcpp/ShareManager.h>
#undef private

#include <dcpp/ClientManager.h>
#include <dcpp/FavoriteManager.h>
#include <dcpp/File.h>
#include <dcpp/HashManager.h>
#include <dcpp/HttpManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchResult.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SQLiteDB.h>
#include <dcpp/Streams.h>
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
		HttpManager::newInstance();
		FavoriteManager::newInstance();
	}

	void TearDown() override {
		ShareManager::deleteInstance();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
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

		auto root = ShareManager::Directory::create("Virtual", ShareManager::Directory::Ptr(), 1700000000);
		auto child = ShareManager::Directory::create("Child", root, 1700000100);
		root->directories.emplace("Child", child);

		ShareManager::Directory::File file("file.bin", 1234, child,
			TTHValue("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), 1700000200);
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

	string readStream(std::unique_ptr<MemoryInputStream> stream) {
		if(!stream) {
			return Util::emptyString;
		}
		string result(stream->getSize(), '\0');
		size_t size = result.size();
		stream->read(result.data(), size);
		result.resize(size);
		return result;
	}

	struct CachedFile {
		string path;
		int64_t size;
		uint32_t timestamp;
		TTHValue tth;
	};

	CachedFile cacheFile(const string& path, const string& contents) {
		{
			std::ofstream output(path, std::ios::binary);
			output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		}

		TigerTree tree(HashManager::MIN_BLOCK_SIZE);
		tree.update(contents.data(), contents.size());
		tree.finalize();

		File file(path, File::READ, File::OPEN | File::SHARED);
		const auto size = file.getSize();
		const auto timestamp = file.getLastModified();
		file.close();
		EXPECT_EQ(size, static_cast<int64_t>(contents.size()));
		EXPECT_TRUE(HashManager::getInstance()->verifyFileTTH(path, size, tree.getRoot()));
		return { path, size, timestamp, tree.getRoot() };
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

	auto child = ShareManager::getInstance()->directories["Virtual"]->directories["Child"];
	EXPECT_EQ(1700000000, ShareManager::getInstance()->directories["Virtual"]->getLastWrite());
	EXPECT_EQ(1700000100, child->getLastWrite());
	auto file = child->findFile("file.bin");
	ASSERT_NE(file, child->files.cend());
	ASSERT_TRUE(file->realPath);
	EXPECT_EQ(*file->realPath, sharePath + "Child" PATH_SEPARATOR_STR "file.bin");
	EXPECT_EQ(1700000200, file->getLastWrite());
}

TEST_F(ShareCacheTest, emits_dates_only_for_non_recursive_partial_file_items) {
	populateShare();

	const auto partial = readStream(std::unique_ptr<MemoryInputStream>(
		ShareManager::getInstance()->generatePartialList("/Virtual/Child/", false)));
	EXPECT_NE(string::npos, partial.find("BaseDate=\"1700000100\""));
	EXPECT_NE(string::npos, partial.find("<File Name=\"file.bin\" Size=\"1234\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date=\"1700000200\"/>"));

	const auto recursive = readStream(std::unique_ptr<MemoryInputStream>(
		ShareManager::getInstance()->generatePartialList("/Virtual/Child/", true)));
	EXPECT_NE(string::npos, recursive.find("BaseDate=\"1700000100\""));
	EXPECT_NE(string::npos, recursive.find("<File Name=\"file.bin\" Size=\"1234\" TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"));
	EXPECT_EQ(string::npos, recursive.find("TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date="));

	string full;
	StringRefOutputStream fullOutput(full);
	string indent;
	string tmp;
	ShareManager::getInstance()->directories["Virtual"]->toXml(fullOutput, indent, tmp, -1, false);
	EXPECT_NE(string::npos, full.find("<Directory Name=\"Virtual\" Date=\"1700000000\""));
	EXPECT_EQ(string::npos, full.find("TTH=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Date="));
}

TEST_F(ShareCacheTest, incomplete_partial_directories_include_size_and_content_metadata) {
	auto sm = ShareManager::getInstance();
	sm->shares[sharePath] = "Virtual";
	auto root = ShareManager::Directory::create("Virtual", ShareManager::Directory::Ptr(), 1700000000);
	auto large = ShareManager::Directory::create("Large", root, 1700000100);
	root->directories.emplace("Large", large);
	for(int i = 0; i < 5; ++i) {
		string tth(39, 'A');
		tth[0] = static_cast<char>('A' + i);
		large->files.insert(ShareManager::Directory::File("file" + std::to_string(i) + ".bin", 10, large,
			TTHValue(tth), 1700000200 + i));
	}
	sm->directories["Virtual"] = root;
	sm->rebuildIndices(5);

	const auto partial = readStream(std::unique_ptr<MemoryInputStream>(sm->generatePartialList("/", false)));
	EXPECT_NE(string::npos, partial.find("<Directory Name=\"Large\" Date=\"1700000100\" Size=\"50\" Directories=\"0\" Files=\"5\" Incomplete=\"1\" />"));
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

TEST_F(ShareCacheTest, replaces_old_share_cache_schema_without_self_lock) {
	{
		SQLiteDB db(configPath + "ShareCache.sqlite3");
		db.execute(
			"CREATE TABLE metadata (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID;"
			"CREATE TABLE directories (id INTEGER PRIMARY KEY NOT NULL, parent_id INTEGER, name TEXT NOT NULL, real_name TEXT);"
			"CREATE TABLE files (id INTEGER PRIMARY KEY NOT NULL, directory_id INTEGER NOT NULL, name TEXT NOT NULL, size INTEGER NOT NULL, tth TEXT, real_path TEXT);"
			"PRAGMA user_version = 1;"
		);
	}

	populateShare();
	ShareManager::getInstance()->saveShareCache();

	clearLoadedShare();
	ASSERT_TRUE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->toReal("/Virtual/Child/file.bin"), sharePath + "Child" PATH_SEPARATOR_STR "file.bin");
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

TEST_F(ShareCacheTest, skips_cache_for_unc_share_roots) {
	populateShare();
	ShareManager::getInstance()->saveShareCache();

	clearLoadedShare();
	ShareManager::getInstance()->shares.clear();
	ShareManager::getInstance()->shares["\\\\server\\share\\"] = "Virtual";

	EXPECT_FALSE(ShareManager::getInstance()->loadShareCache());
	EXPECT_EQ(ShareManager::getInstance()->getSharedFiles(), 0U);
}

TEST_F(ShareCacheTest, temp_shares_are_searchable_and_downloadable_only_on_their_route) {
	auto sm = ShareManager::getInstance();
	const auto path = configPath + "dropped photo.jpg";
	const string route = "adc://example.invalid";
	const string contents = "temporary attachment";
	std::ofstream(path, std::ios::binary) << contents;
	TigerTree tree(HashManager::MIN_BLOCK_SIZE);
	tree.update(contents.data(), contents.size());
	tree.finalize();
	File file(path, File::READ, File::OPEN | File::SHARED);
	const auto timestamp = file.getLastModified();
	file.close();
	ASSERT_TRUE(HashManager::getInstance()->verifyFileTTH(path, contents.size(), tree.getRoot()));
	ASSERT_TRUE(sm->addTempShare(path, contents.size(), timestamp, tree.getRoot(), route));
	const string nmdcRoute = "dchub://legacy.example.invalid";
	ASSERT_TRUE(sm->addTempShare(path, contents.size(), timestamp, tree.getRoot(), nmdcRoute));

	EXPECT_EQ(sm->getSharedFiles(), 0U);
	EXPECT_EQ(sm->getShareSize(), 0);
	EXPECT_TRUE(sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10).empty());

	const string matchingRoute = "ADC://EXAMPLE.INVALID";
	const string otherRoute = "adc://other.invalid";
	const auto matchingResults = sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, matchingRoute);
	ASSERT_EQ(matchingResults.size(), 1U);
	EXPECT_EQ(matchingResults.front()->getType(), SearchResult::TYPE_FILE);
	EXPECT_EQ(matchingResults.front()->getFile(), "dropped photo.jpg");
	EXPECT_EQ(matchingResults.front()->getSize(), static_cast<int64_t>(contents.size()));
	EXPECT_EQ(matchingResults.front()->getTTH(), tree.getRoot());
	EXPECT_TRUE(sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, otherRoute).empty());
	const auto nmdcResults = sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, nmdcRoute);
	ASSERT_EQ(nmdcResults.size(), 1U);
	EXPECT_EQ(nmdcResults.front()->getTTH(), tree.getRoot());

	const auto adcNameResults = sm->search(StringList { "ANphoto", "EXjpg" }, 10, matchingRoute);
	ASSERT_EQ(adcNameResults.size(), 1U);
	EXPECT_EQ(adcNameResults.front()->getTTH(), tree.getRoot());
	const auto adcTthResults = sm->search(StringList { "TR" + tree.getRoot().toBase32() }, 10, matchingRoute);
	ASSERT_EQ(adcTthResults.size(), 1U);
	EXPECT_EQ(adcTthResults.front()->getTTH(), tree.getRoot());
	EXPECT_TRUE(sm->search(StringList { "TR" + tree.getRoot().toBase32() }, 10, otherRoute).empty());

	EXPECT_EQ(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), matchingRoute),
		std::make_pair(path, static_cast<int64_t>(contents.size())));
	EXPECT_EQ(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), nmdcRoute),
		std::make_pair(path, static_cast<int64_t>(contents.size())));
	EXPECT_THROW(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), otherRoute), ShareException);
	const auto resolved = sm->resolveFile("TTH/" + tree.getRoot().toBase32(), matchingRoute);
	EXPECT_EQ(resolved.realPath, path);
	EXPECT_EQ(resolved.size, static_cast<int64_t>(contents.size()));
	EXPECT_TRUE(resolved.temporary);

	ASSERT_NE(sm->findTempShare(tree.getRoot(), &matchingRoute), nullptr);
	EXPECT_EQ(sm->findTempShare(tree.getRoot(), &otherRoute), nullptr);
	EXPECT_TRUE(sm->isTempShare(tree.getRoot(), path, matchingRoute));
	EXPECT_FALSE(sm->isTempShare(tree.getRoot(), path, otherRoute));
	const auto tempShares = sm->getTempShares();
	ASSERT_EQ(tempShares.size(), 2U);
	EXPECT_EQ(tempShares.front().realPath, path);

	SettingsManager::getInstance()->set(SettingsManager::ENABLE_RTF_TEMP_SHARES, false);
	EXPECT_TRUE(sm->search("dropped", SearchManager::SIZE_DONTCARE, 0,
		SearchManager::TYPE_ANY, 10, matchingRoute).empty());
	EXPECT_THROW(sm->toRealWithSize("TTH/" + tree.getRoot().toBase32(), matchingRoute), ShareException);
	EXPECT_FALSE(sm->removeTempShare(path, tree.getRoot(), otherRoute));
	EXPECT_TRUE(sm->removeTempShare(path, tree.getRoot(), matchingRoute));
	EXPECT_TRUE(sm->removeTempShare(path, tree.getRoot(), nmdcRoute));
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, prepares_cached_unshared_attachment_with_exact_route_metadata) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto cached = cacheFile(configPath + "cached attachment.png", "cached temporary attachment");
	optional<ShareManager::ChatAttachmentResult> result;

	const auto requestId = sm->prepareChatAttachment(cached.path, route,
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });

	ASSERT_TRUE(result);
	EXPECT_EQ(result->requestId, requestId);
	EXPECT_TRUE(result->error.empty());
	ASSERT_TRUE(result->attachment);
	EXPECT_EQ(result->attachment->realPath, cached.path);
	EXPECT_EQ(result->attachment->hubUrl, route);
	EXPECT_EQ(result->attachment->tth, cached.tth);
	EXPECT_EQ(result->attachment->size, cached.size);
	EXPECT_EQ(result->attachment->timestamp, cached.timestamp);
	EXPECT_TRUE(result->attachment->temporary);
	EXPECT_TRUE(sm->validateChatAttachment(cached.tth, cached.size, route));
	EXPECT_FALSE(sm->validateChatAttachment(cached.tth, cached.size, "adc://other.example.invalid"));

	{
		std::ofstream output(cached.path, std::ios::binary | std::ios::trunc);
		output << "changed temporary attachment with a different size";
	}
	EXPECT_FALSE(sm->validateChatAttachment(cached.tth, cached.size, route));
}

TEST_F(ShareCacheTest, opened_file_verification_ignores_stale_same_metadata_cache_entries) {
	const auto cached = cacheFile(configPath + "same metadata attachment.bin", "AAAAAAAAAAAAAAAA");
	const string replacement = "BBBBBBBBBBBBBBBB";
	{
		std::ofstream output(cached.path, std::ios::binary | std::ios::trunc);
		output.write(replacement.data(), static_cast<std::streamsize>(replacement.size()));
	}

	TigerTree replacementTree(HashManager::MIN_BLOCK_SIZE);
	replacementTree.update(replacement.data(), replacement.size());
	replacementTree.finalize();

	File file(cached.path, File::READ, File::OPEN);
	file.setPos(3);
	EXPECT_FALSE(HashManager::getInstance()->verifyFileTTH(file, cached.size, cached.tth));
	EXPECT_EQ(file.getPos(), 3);
	EXPECT_TRUE(HashManager::getInstance()->verifyFileTTH(
		file, static_cast<int64_t>(replacement.size()), replacementTree.getRoot()));
	EXPECT_EQ(file.getPos(), 3);
}

TEST_F(ShareCacheTest, preparing_existing_temp_share_moves_it_to_newest_position) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto first = cacheFile(configPath + "first attachment.bin", "first attachment");
	const auto second = cacheFile(configPath + "second attachment.bin", "second attachment");
	int callbackCount = 0;
	auto prepare = [&](const CachedFile& file) {
		sm->prepareChatAttachment(file.path, route, [&](ShareManager::ChatAttachmentResult result) {
			++callbackCount;
			EXPECT_TRUE(result.error.empty());
			ASSERT_TRUE(result.attachment);
			EXPECT_TRUE(result.attachment->temporary);
		});
	};

	prepare(first);
	prepare(second);
	ASSERT_EQ(callbackCount, 2);
	auto shares = sm->getTempShares();
	ASSERT_EQ(shares.size(), 2U);
	EXPECT_EQ(shares[0].tth, first.tth);
	EXPECT_EQ(shares[1].tth, second.tth);

	prepare(first);
	ASSERT_EQ(callbackCount, 3);
	shares = sm->getTempShares();
	ASSERT_EQ(shares.size(), 2U);
	EXPECT_EQ(shares[0].tth, second.tth);
	EXPECT_EQ(shares[1].tth, first.tth);
}

TEST_F(ShareCacheTest, preparing_permanently_visible_file_does_not_create_temp_share) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto cached = cacheFile(sharePath + "visible attachment.jpg", "permanently shared attachment");

	auto root = ShareManager::Directory::create("Visible");
	root->files.insert(ShareManager::Directory::File("visible attachment.jpg", cached.size, root, cached.tth));
	sm->shares[sharePath] = "Visible";
	sm->directories["Visible"] = root;
	sm->rebuildIndices(1);

	optional<ShareManager::ChatAttachmentResult> result;
	sm->prepareChatAttachment(cached.path, route,
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });

	ASSERT_TRUE(result);
	EXPECT_TRUE(result->error.empty());
	ASSERT_TRUE(result->attachment);
	EXPECT_FALSE(result->attachment->temporary);
	EXPECT_EQ(result->attachment->realPath, cached.path);
	EXPECT_EQ(result->attachment->tth, cached.tth);
	EXPECT_EQ(result->attachment->size, cached.size);
	EXPECT_TRUE(sm->getTempShares().empty());
	EXPECT_TRUE(sm->validateChatAttachment(cached.tth, cached.size, route));
}

TEST_F(ShareCacheTest, cancelling_pending_chat_attachment_removes_request_without_callback) {
	auto sm = ShareManager::getInstance();
	const auto path = configPath + "uncached attachment.bin";
	{
		std::ofstream output(path, std::ios::binary);
		output << "not present in the hash store";
	}
	bool callbackCalled = false;
	const auto requestId = sm->prepareChatAttachment(path, "adc://attachments.example.invalid",
		[&callbackCalled](ShareManager::ChatAttachmentResult) { callbackCalled = true; });

	EXPECT_FALSE(callbackCalled);
	{
		Lock l(sm->chatAttachmentCs);
		auto pending = sm->pendingChatAttachments.find(requestId);
		ASSERT_NE(pending, sm->pendingChatAttachments.end());
		EXPECT_NE(pending->second.hashJobId, 0U);
	}

	sm->cancelChatAttachment(requestId);
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_EQ(sm->pendingChatAttachments.find(requestId), sm->pendingChatAttachments.end());
	}
	EXPECT_FALSE(callbackCalled);
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, terminal_hash_event_only_completes_its_own_job_cohort) {
	auto sm = ShareManager::getInstance();
	const string path = configPath + "same snapshot retry.bin";
	const string route = "adc://attachments.example.invalid";
	constexpr int64_t size = 123;
	constexpr uint32_t timestamp = 456;
	constexpr uint64_t oldJob = 7001;
	constexpr uint64_t retryJob = 7002;
	optional<ShareManager::ChatAttachmentResult> oldResult;
	optional<ShareManager::ChatAttachmentResult> retryResult;
	uint64_t generation;
	{
		Lock l(sm->chatAttachmentCs);
		generation = sm->chatAttachmentGeneration;
		const auto oldId = sm->nextChatAttachmentRequest.fetch_add(1);
		const auto retryId = sm->nextChatAttachmentRequest.fetch_add(1);
		sm->pendingChatAttachments.emplace(oldId, ShareManager::PendingChatAttachment {
			oldId, path, route, size, timestamp, generation, oldJob,
			[&oldResult](ShareManager::ChatAttachmentResult value) { oldResult = std::move(value); } });
		sm->pendingChatAttachments.emplace(retryId, ShareManager::PendingChatAttachment {
			retryId, path, route, size, timestamp, generation, retryJob,
			[&retryResult](ShareManager::ChatAttachmentResult value) { retryResult = std::move(value); } });
	}

	sm->failChatAttachments(path, size, timestamp, oldJob, "old job failed");
	ASSERT_TRUE(oldResult);
	EXPECT_FALSE(oldResult->attachment);
	EXPECT_EQ(oldResult->error, "old job failed");
	EXPECT_FALSE(retryResult);
	{
		Lock l(sm->chatAttachmentCs);
		ASSERT_EQ(sm->pendingChatAttachments.size(), 1U);
		EXPECT_EQ(sm->pendingChatAttachments.begin()->second.hashJobId, retryJob);
	}

	sm->failChatAttachments(path, size, timestamp, retryJob, "retry failed");
	ASSERT_TRUE(retryResult);
	EXPECT_EQ(retryResult->error, "retry failed");
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_TRUE(sm->pendingChatAttachments.empty());
		EXPECT_TRUE(sm->activeChatAttachments.empty());
	}
}

TEST_F(ShareCacheTest, cancelling_extracted_chat_attachment_suppresses_callback_and_cleans_state) {
	auto sm = ShareManager::getInstance();
	const auto cached = cacheFile(configPath + "extracted cancellation.bin", "cancel extracted attachment");
	bool callbackCalled = false;
	uint64_t generation;
	{
		Lock l(sm->chatAttachmentCs);
		generation = sm->chatAttachmentGeneration;
	}
	const auto requestId = sm->nextChatAttachmentRequest.fetch_add(1);
	ShareManager::PendingChatAttachment request { requestId, cached.path,
		"adc://attachments.example.invalid", cached.size, cached.timestamp, generation, 0,
		[&callbackCalled](ShareManager::ChatAttachmentResult) { callbackCalled = true; } };
	{
		Lock l(sm->chatAttachmentCs);
		sm->activeChatAttachments.insert(requestId);
	}

	sm->cancelChatAttachment(requestId);
	sm->finishChatAttachment(std::move(request), cached.tth);

	EXPECT_FALSE(callbackCalled);
	EXPECT_TRUE(sm->getTempShares().empty());
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_EQ(sm->activeChatAttachments.count(requestId), 0U);
		EXPECT_EQ(sm->cancelledChatAttachments.count(requestId), 0U);
	}
}

TEST_F(ShareCacheTest, clearing_temp_shares_completes_pending_attachment_with_cancellation) {
	auto sm = ShareManager::getInstance();
	const auto path = configPath + "pending clear attachment.bin";
	{
		std::ofstream output(path, std::ios::binary);
		output << "uncached attachment cancelled by clear";
	}
	optional<ShareManager::ChatAttachmentResult> result;
	const auto requestId = sm->prepareChatAttachment(path, "adc://attachments.example.invalid",
		[&result](ShareManager::ChatAttachmentResult value) { result = std::move(value); });
	EXPECT_FALSE(result);

	EXPECT_EQ(sm->clearTempShares(), 0U);
	ASSERT_TRUE(result);
	EXPECT_EQ(result->requestId, requestId);
	EXPECT_FALSE(result->attachment);
	EXPECT_FALSE(result->error.empty());
	{
		Lock l(sm->chatAttachmentCs);
		EXPECT_EQ(sm->pendingChatAttachments.find(requestId), sm->pendingChatAttachments.end());
		EXPECT_EQ(sm->activeChatAttachments.count(requestId), 0U);
	}
	EXPECT_TRUE(sm->getTempShares().empty());
}

TEST_F(ShareCacheTest, stale_attachment_completion_reports_clear_and_cannot_readd) {
	auto sm = ShareManager::getInstance();
	const string route = "adc://attachments.example.invalid";
	const auto cached = cacheFile(configPath + "clear race attachment.bin", "clear race attachment");
	uint64_t staleGeneration;
	{
		Lock l(sm->chatAttachmentCs);
		staleGeneration = sm->chatAttachmentGeneration;
	}

	EXPECT_EQ(sm->clearTempShares(), 0U);
	optional<ShareManager::ChatAttachmentResult> staleResult;
	const auto staleRequestId = sm->nextChatAttachmentRequest.fetch_add(1);
	ShareManager::PendingChatAttachment staleRequest { staleRequestId, cached.path, route,
		cached.size, cached.timestamp, staleGeneration, 0,
		[&staleResult](ShareManager::ChatAttachmentResult value) { staleResult = std::move(value); } };
	{
		Lock l(sm->chatAttachmentCs);
		sm->activeChatAttachments.insert(staleRequestId);
	}
	sm->finishChatAttachment(std::move(staleRequest), cached.tth);

	ASSERT_TRUE(staleResult);
	EXPECT_FALSE(staleResult->attachment);
	EXPECT_FALSE(staleResult->error.empty());
	EXPECT_TRUE(sm->getTempShares().empty());
	optional<ShareManager::ChatAttachmentResult> freshResult;
	sm->prepareChatAttachment(cached.path, route,
		[&freshResult](ShareManager::ChatAttachmentResult value) { freshResult = std::move(value); });
	ASSERT_TRUE(freshResult);
	ASSERT_TRUE(freshResult->attachment);
	EXPECT_TRUE(freshResult->attachment->temporary);
	ASSERT_EQ(sm->getTempShares().size(), 1U);
}
