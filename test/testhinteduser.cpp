#include "testbase.h"

#include <chrono>
#include <filesystem>

#include <dcpp/ClientManager.h>
#include <dcpp/File.h>
#include <dcpp/HintedUser.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/Text.h>
#include <dcpp/TimerManager.h>

using namespace dcpp;

namespace {

class HubHintQueueTest : public ::testing::Test {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		configPath = (std::filesystem::temp_directory_path() /
			("dcpp-hubhint-test-" + std::to_string(ticks))).string() + PATH_SEPARATOR;

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
		SearchManager::newInstance();
		ClientManager::newInstance();
		QueueManager::newInstance();
	}

	void TearDown() override {
		QueueManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

private:
	string configPath;
};

}

TEST(testhinteduser, compares_hub_urls_case_insensitively)
{
	EXPECT_TRUE(hubHintsEqual("ADCS://Example.com:1511", "adcs://example.COM:1511"));
	EXPECT_FALSE(hubHintsEqual("adc://example.com:1511", "adc://example.com:1512"));
}

TEST(testhinteduser, treats_only_the_hint_as_optional)
{
	EXPECT_TRUE(hubHintMatches("", "adc://example.com:1511"));
	EXPECT_TRUE(hubHintMatches("ADC://Example.com:1511", "adc://example.com:1511"));
	EXPECT_FALSE(hubHintMatches("adc://example.com:1511", ""));
	EXPECT_FALSE(hubHintMatches("adc://one.example", "adc://two.example"));
}

TEST(testhinteduser, keeps_user_identity_independent_of_the_route)
{
	UserPtr user(new User(CID()));

	EXPECT_TRUE(HintedUser(user, "adc://one.example") == HintedUser(user, "adc://two.example"));
}

TEST_F(HubHintQueueTest, replaces_the_route_for_a_hub_specific_file_list)
{
	uint8_t cidData[CID::SIZE] = { 1 };
	UserPtr user(new User(CID(cidData)));
	auto queue = QueueManager::getInstance();

	queue->addList(HintedUser(user, "adc://one.example"), QueueItem::FLAG_CLIENT_VIEW);
	queue->addList(HintedUser(user, "ADC://TWO.example"), QueueItem::FLAG_CLIENT_VIEW);

	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		auto item = items.begin()->second;
		EXPECT_FALSE(item->isSourceForHub(user, "adc://one.example"));
		EXPECT_TRUE(item->isSourceForHub(user, "adc://two.EXAMPLE"));
	});
}

#ifdef _WIN32

TEST_F(HubHintQueueTest, accepts_download_targets_beyond_legacy_max_path)
{
	uint8_t cidData[CID::SIZE] = { 6 };
	UserPtr user(new User(CID(cidData)));
	auto queue = QueueManager::getInstance();
	auto target = Util::getPath(Util::PATH_DOWNLOADS);
	while(target.size() < MAX_PATH + 32) {
		target += "segment-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\\";
	}
	target += "download.bin";

	EXPECT_NO_THROW(queue->add(target, 1, TTHValue(), HintedUser(user, "adc://example.invalid")));
	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		EXPECT_EQ(target, items.begin()->second->getTarget());
	});
	queue->remove(target);
}

TEST_F(HubHintQueueTest, shortens_overlong_temporary_names_without_changing_the_target) {
	SettingsManager::getInstance()->set(SettingsManager::TEMP_DOWNLOAD_DIRECTORY, Util::getPath(Util::PATH_USER_LOCAL));
	const auto root = TTHValue();
	const auto shortTarget = Util::getPath(Util::PATH_DOWNLOADS) + "short.cbz";
	QueueItem shortItem(shortTarget, 1, QueueItem::DEFAULT, 0, GET_TIME(), root);
	EXPECT_EQ(Util::getPath(Util::PATH_USER_LOCAL) + "short.cbz." + root.toBase32() + ".dctmp", shortItem.getTempTarget());
	const auto persistedShortTemp = Util::getPath(Util::PATH_USER_LOCAL) + "persisted.part";
	shortItem.setTempTarget(persistedShortTemp);
	EXPECT_EQ(persistedShortTemp, shortItem.getTempTarget());
	const string boundaryName(209, 'b');
	QueueItem boundary(Util::getPath(Util::PATH_DOWNLOADS) + boundaryName, 1, QueueItem::DEFAULT, 0, GET_TIME(), root);
	EXPECT_EQ(Util::getPath(Util::PATH_USER_LOCAL) + boundaryName + "." + root.toBase32() + ".dctmp", boundary.getTempTarget());

	const string commonName(180, 'a');
	string firstCombiningMarks;
	string secondCombiningMarks;
	for(size_t i = 0; i < 30; ++i) {
		firstCombiningMarks += "\xCC\x82";
		secondCombiningMarks += "\xCC\x88";
	}
	const auto firstTarget = Util::getPath(Util::PATH_DOWNLOADS) + commonName + "-" + firstCombiningMarks + ".cbz";
	const auto secondTarget = Util::getPath(Util::PATH_DOWNLOADS) + commonName + "-" + secondCombiningMarks + ".cbz";
	QueueItem first(firstTarget, 1, QueueItem::DEFAULT, 0, GET_TIME(), root);
	QueueItem second(secondTarget, 1, QueueItem::DEFAULT, 0, GET_TIME(), root);

	const auto firstTemp = first.getTempTarget();
	const auto secondTemp = second.getTempTarget();
	EXPECT_EQ(firstTarget, first.getTarget());
	EXPECT_EQ(secondTarget, second.getTarget());
	EXPECT_EQ(255U, Text::toT(Util::getFileName(firstTemp)).size());
	EXPECT_EQ(255U, Text::toT(Util::getFileName(secondTemp)).size());
	EXPECT_NE(string::npos, Util::getFileName(firstTemp).find("..."));
	EXPECT_NE(firstTemp, secondTemp);
	EXPECT_NO_THROW(File(firstTemp, File::WRITE, File::CREATE));
	EXPECT_NO_THROW(File(secondTemp, File::WRITE, File::CREATE));
	File::deleteFile(firstTemp);
	File::deleteFile(secondTemp);
	const auto emojiTarget = Util::getPath(Util::PATH_DOWNLOADS) + string(165, 'b') + "\xF0\x9F\x98\x80" + string(50, 'c') + ".bin";
	QueueItem emoji(emojiTarget, 1, QueueItem::DEFAULT, 0, GET_TIME(), root);
	const auto emojiTemp = emoji.getTempTarget();
	EXPECT_LE(Text::toT(Util::getFileName(emojiTemp)).size(), 255U);
	EXPECT_EQ(emojiTemp, Text::fromT(Text::toT(emojiTemp)));
	EXPECT_NO_THROW(File(emojiTemp, File::WRITE, File::CREATE));
	File::deleteFile(emojiTemp);

	first.setTempTarget(Util::getPath(Util::PATH_USER_LOCAL) + string(256, 'x'));
	const auto repairedTemp = first.getTempTarget();
	EXPECT_EQ(firstTemp, repairedTemp);
	EXPECT_EQ(255U, Text::toT(Util::getFileName(repairedTemp)).size());

	QueueItem userList("remote/path", -1, QueueItem::DEFAULT, QueueItem::FLAG_USER_LIST, GET_TIME(), root);
	const string longRemotePath(300, 'z');
	userList.setTempTarget(longRemotePath);
	EXPECT_EQ(longRemotePath, userList.getTempTarget());
}

#endif

TEST_F(HubHintQueueTest, adc_directory_downloads_queue_recursive_partial_lists_with_full_fallback)
{
	uint8_t cidData[CID::SIZE] = { 2 };
	UserPtr user(new User(CID(cidData)));
	auto queue = QueueManager::getInstance();

	queue->addDirectory("Share\\Nested\\", HintedUser(user, "adc://example.invalid"),
		Util::getPath(Util::PATH_DOWNLOADS));

	string listTarget;
	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		auto item = items.begin()->second;
		listTarget = item->getTarget();
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_USER_LIST));
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD));
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_PARTIAL_LIST));
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_RECURSIVE_LIST));
		EXPECT_EQ("Share\\Nested\\", item->getTempTarget());
	});

	ASSERT_TRUE(queue->fallbackRecursiveList(listTarget));
	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		auto item = items.begin()->second;
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_PARTIAL_LIST));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_RECURSIVE_LIST));
		EXPECT_TRUE(item->getTempTarget().empty());
	});
	queue->remove(listTarget);
}

TEST_F(HubHintQueueTest, nmdc_directory_downloads_keep_the_full_list_path)
{
	uint8_t cidData[CID::SIZE] = { 3 };
	UserPtr user(new User(CID(cidData)));
	user->setFlag(User::NMDC);
	auto queue = QueueManager::getInstance();

	queue->addDirectory("Share\\Nested\\", HintedUser(user, "dchub://example.invalid"),
		Util::getPath(Util::PATH_DOWNLOADS));

	string listTarget;
	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		auto item = items.begin()->second;
		listTarget = item->getTarget();
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_PARTIAL_LIST));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_RECURSIVE_LIST));
	});
	queue->remove(listTarget);
}

TEST_F(HubHintQueueTest, an_existing_full_list_remains_full_when_a_directory_request_is_merged)
{
	uint8_t cidData[CID::SIZE] = { 4 };
	UserPtr user(new User(CID(cidData)));
	auto queue = QueueManager::getInstance();
	const HintedUser hintedUser(user, "adc://example.invalid");

	queue->addList(hintedUser, QueueItem::FLAG_CLIENT_VIEW);
	queue->addDirectory("Share\\Nested\\", hintedUser, Util::getPath(Util::PATH_DOWNLOADS));

	string listTarget;
	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		auto item = items.begin()->second;
		listTarget = item->getTarget();
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_CLIENT_VIEW));
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_DIRECTORY_DOWNLOAD));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_PARTIAL_LIST));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_RECURSIVE_LIST));
	});
	queue->remove(listTarget);
}

TEST_F(HubHintQueueTest, a_waiting_partial_list_upgrades_when_a_full_list_purpose_is_merged)
{
	uint8_t cidData[CID::SIZE] = { 5 };
	UserPtr user(new User(CID(cidData)));
	auto queue = QueueManager::getInstance();
	const HintedUser hintedUser(user, "adc://example.invalid");

	queue->addList(hintedUser, QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_PARTIAL_LIST, "Share\\");
	queue->addList(hintedUser, QueueItem::FLAG_MATCH_QUEUE);

	string listTarget;
	queue->lockedOperation([&](const QueueItem::StringMap& items) {
		ASSERT_EQ(1U, items.size());
		auto item = items.begin()->second;
		listTarget = item->getTarget();
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_CLIENT_VIEW));
		EXPECT_TRUE(item->isSet(QueueItem::FLAG_MATCH_QUEUE));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_PARTIAL_LIST));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_RECURSIVE_LIST));
		EXPECT_FALSE(item->isSet(QueueItem::FLAG_DEFERRED_FULL_LIST));
	});
	queue->remove(listTarget);
}
