#include "testbase.h"

#include <chrono>
#include <filesystem>

#include <dcpp/ClientManager.h>
#include <dcpp/HintedUser.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SettingsManager.h>
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
