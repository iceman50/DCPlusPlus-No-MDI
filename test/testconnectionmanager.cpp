/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <chrono>
#include <filesystem>

// Exercise the real manager transitions without opening external connections.
// This follows the private-access pattern used by the share/cache tests.
#define private public
#include <dcpp/ClientManager.h>
#include <dcpp/ConnectionManager.h>
#include <dcpp/NmdcHub.h>
#include <dcpp/UserConnection.h>
#include <dcpp/UploadManager.h>
#include <dcpp/PluginManager.h>
#undef private

#include <dcpp/ConnectivityManager.h>
#include <dcpp/DownloadManager.h>
#include <dcpp/FavoriteManager.h>
#include <dcpp/HttpManager.h>
#include <dcpp/LogManager.h>
#include <dcpp/QueueManager.h>
#include <dcpp/SearchManager.h>
#include <dcpp/SettingsManager.h>

using namespace dcpp;

namespace {

class RecordingHub : public NmdcHub {
public:

	explicit RecordingHub(const string& url) : NmdcHub(url) { }
	void connect(const OnlineUser&, const string& token, ConnectionType) override {
		attempts.push_back(token);
	}
	StringList attempts;
};

class ConnectionManagerTest : public ::testing::Test, public ConnectionManagerListener {
public:
	void SetUp() override {
		const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
		configPath = (std::filesystem::temp_directory_path() /
			("dcpp-connection-test-" + std::to_string(ticks))).string() + PATH_SEPARATOR;
		Util::PathsMap paths;
		for(auto path: { Util::PATH_USER_CONFIG, Util::PATH_USER_LOCAL, Util::PATH_GLOBAL_CONFIG,
			Util::PATH_RESOURCES, Util::PATH_LOCALE, Util::PATH_DOWNLOADS, Util::PATH_FILE_LISTS,
			Util::PATH_HUB_LISTS })
		{
			paths[path] = configPath;
		}
		paths[Util::PATH_NOTEPAD] = configPath + "Notepad.txt";
		Util::initialize(paths);
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::DONT_DL_ALREADY_SHARED, false);
		LogManager::newInstance();
		TimerManager::newInstance();
		ConnectivityManager::newInstance();
		SearchManager::newInstance();
		ClientManager::newInstance();
		HttpManager::newInstance();
		FavoriteManager::newInstance();
		QueueManager::newInstance();
		DownloadManager::newInstance();
		UploadManager::newInstance();
		ConnectionManager::newInstance();
		manager = ConnectionManager::getInstance();
		manager->addListener(this);
	}

	void TearDown() override {
		for(auto& peer: peers) {
			peer->fire(UserConnectionListener::Failed(), peer.get(), string("Test shutdown"));
		}
		peers.clear();
		manager->removeListener(this);
		ConnectionManager::deleteInstance();
		DownloadManager::deleteInstance();
		UploadManager::deleteInstance();
		QueueManager::deleteInstance();
		ClientManager::getInstance()->onlineUsers.clear();
		onlineUsers.clear();
		hubs.clear();
		FavoriteManager::getInstance()->shutdown();
		FavoriteManager::deleteInstance();
		HttpManager::getInstance()->shutdown();
		HttpManager::deleteInstance();
		ClientManager::deleteInstance();
		SearchManager::deleteInstance();
		ConnectivityManager::deleteInstance();
		TimerManager::getInstance()->shutdown();
		TimerManager::deleteInstance();
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
		std::filesystem::remove_all(configPath);
	}

	UserPtr user(uint8_t id) {
		uint8_t bytes[CID::SIZE] = { 0 };
		bytes[0] = id;
		return UserPtr(new User(CID(bytes)));
	}

	UserConnection* peer(const UserPtr& user, const string& token, bool nmdc, bool upload, bool mcn = false) {
		auto uc = manager->getConnection(nmdc, false);
		peers.emplace_back(uc);
		uc->setUser(user);
		uc->setToken(token);
		uc->setHubUrl("dchub://one.example");
		uc->setFlag(upload ? UserConnection::FLAG_UPLOAD : UserConnection::FLAG_DOWNLOAD);
		uc->setFlag(UserConnection::FLAG_SUPPORTS_ADCGET);
		uc->setFlag(UserConnection::FLAG_SUPPORTS_TTHF);
		if(mcn) { uc->setFlag(UserConnection::FLAG_SUPPORTS_MCN1); }
		return uc;
	}

	OnlineUser& online(const UserPtr& user, const string& hub) {
		hubs.emplace_back(new RecordingHub(hub));
		onlineUsers.emplace_back(new OnlineUser(user, *hubs.back(), 1));
		ClientManager::getInstance()->onlineUsers.emplace(user->getCID(), onlineUsers.back().get());
		return *onlineUsers.back();
	}

	void queueFile(const UserPtr& user, const string& hub) {
		// Queue while offline so the test can control connection creation and time.
		QueueManager::getInstance()->add(configPath + "file-" + user->getCID().toBase32(),
			1024 * 1024, TTHValue(), HintedUser(user, hub));
		user->setFlag(User::ONLINE);
	}

	void on(ConnectionManagerListener::Connected, ConnectionQueueItem* cqi, UserConnection* uc) noexcept override {
		EXPECT_EQ(cqi->getToken(), uc->getToken());
		EXPECT_EQ(cqi->getUser().user, uc->getUser());
		++connected;
	}
	void on(ConnectionManagerListener::Failed, ConnectionQueueItem*, const string& error) noexcept override {
		failures.push_back(error);
	}

	ConnectionManager* manager = nullptr;
	string configPath;
	vector<unique_ptr<UserConnection>> peers;
	vector<unique_ptr<RecordingHub>> hubs;
	vector<unique_ptr<OnlineUser>> onlineUsers;
	StringList failures;
	int connected = 0;
};

} // namespace

TEST(testconnections, repeated_nmdc_expectations_remain_valid) {
	ExpectedMap expected;
	expected.add("Alice", "Me", "dchub://hub.example");
	expected.add("Alice", "Me", "DCHUB://HUB.example");
	expected.add("Bob", "Me", "dchub://hub.example");
	EXPECT_EQ("dchub://hub.example", expected.remove("Alice").second);
	EXPECT_EQ("dchub://hub.example", expected.remove("Bob").second);
	EXPECT_TRUE(expected.remove("Alice").second.empty());
}

TEST(testconnections, ambiguous_nmdc_hubs_are_still_rejected) {
	ExpectedMap expected;
	expected.add("Alice", "Me", "dchub://one.example");
	expected.add("Alice", "Me", "dchub://two.example");
	EXPECT_TRUE(expected.remove("Alice").second.empty());
	expected.add("Alice", "Me", "dchub://one.example");
	EXPECT_EQ("dchub://one.example", expected.remove("Alice").second);
}

TEST_F(ConnectionManagerTest, nmdc_transfers_to_multiple_users_have_independent_ids) {
	StringSet downloadTokens, uploadTokens;
	for(uint8_t id = 1; id <= 4; ++id) {
		auto u = user(id);
		auto token = manager->getCQI(HintedUser(u, "dchub://one.example"), CONNECTION_TYPE_DOWNLOAD).getToken();
		auto down = peer(u, "MyNick", true, false);
		manager->addDownloadConnection(down);
		ASSERT_TRUE(down->isSet(UserConnection::FLAG_ASSOCIATED));
		EXPECT_EQ(token, down->getToken());
		downloadTokens.insert(down->getToken());
		manager->onDownloadStarted(*down);
		EXPECT_TRUE(manager->downloads.back().getRunning());
		auto up = peer(u, "MyNick", true, true);
		manager->addNewConnection(up, CONNECTION_TYPE_UPLOAD);
		ASSERT_TRUE(up->isSet(UserConnection::FLAG_ASSOCIATED));
		uploadTokens.insert(up->getToken());
	}
	EXPECT_EQ(8, connected);
	EXPECT_EQ(4U, downloadTokens.size());
	EXPECT_EQ(4U, uploadTokens.size());
	EXPECT_EQ(0U, downloadTokens.count("MyNick"));
	EXPECT_EQ(0U, uploadTokens.count("MyNick"));

	peers.front()->fire(UserConnectionListener::Failed(), peers.front().get(), string("Connection lost"));
	EXPECT_EQ(ConnectionQueueItem::WAITING, manager->downloads.front().getState());
	for(size_t i = 1; i < manager->downloads.size(); ++i) {
		EXPECT_EQ(ConnectionQueueItem::ACTIVE, manager->downloads[i].getState());
	}
}

TEST_F(ConnectionManagerTest, adc_upload_tokens_are_scoped_to_the_peer) {
	auto alice = user(1), bob = user(2);
	auto first = peer(alice, "same-token", false, true, true);
	auto second = peer(bob, "same-token", false, true, true);
	manager->addNewConnection(first, CONNECTION_TYPE_UPLOAD);
	manager->addNewConnection(second, CONNECTION_TYPE_UPLOAD);
	EXPECT_EQ(2, connected);
	EXPECT_NE(first->getToken(), second->getToken());
	auto duplicate = peer(alice, "same-token", false, true, true);
	manager->addNewConnection(duplicate, CONNECTION_TYPE_UPLOAD);
	EXPECT_FALSE(duplicate->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(2, connected);
	auto extra = peer(alice, "another-token", false, true, true);
	manager->addNewConnection(extra, CONNECTION_TYPE_UPLOAD);
	EXPECT_EQ(3, connected);
	first->fire(UserConnectionListener::Failed(), first, string("Connection lost"));
	EXPECT_EQ(2U, manager->cqis[CONNECTION_TYPE_UPLOAD].size());
	EXPECT_TRUE(second->isSet(UserConnection::FLAG_ASSOCIATED));
}

TEST_F(ConnectionManagerTest, adc_download_matching_requires_both_user_and_token) {
	auto alice = user(1), bob = user(2);
	auto token = manager->getCQI(HintedUser(alice, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD).getToken();
	manager->getCQI(HintedUser(bob, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	auto wrong = peer(bob, token, false, false);
	EXPECT_FALSE(manager->checkDownload(wrong));
	manager->addDownloadConnection(wrong);
	EXPECT_FALSE(wrong->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(0, connected);
	auto right = peer(alice, token, false, false);
	EXPECT_TRUE(manager->checkDownload(right));
	manager->addDownloadConnection(right);
	EXPECT_TRUE(right->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(1, connected);
}

TEST_F(ConnectionManagerTest, mcn_download_connections_keep_distinct_queue_ids) {
	auto u = user(1);
	StringSet tokens;
	for(int n = 0; n < 2; ++n) {
		auto token = manager->getCQI(HintedUser(u, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD).getToken();
		auto down = peer(u, token, false, false, true);
		manager->addDownloadConnection(down);
		EXPECT_TRUE(down->isSet(UserConnection::FLAG_ASSOCIATED));
		EXPECT_TRUE(down->isMCNNormal());
		EXPECT_EQ(token, down->getToken());
		tokens.insert(down->getToken());
	}
	EXPECT_EQ(2, connected);
	EXPECT_EQ(2U, tokens.size());
	EXPECT_EQ(2U, manager->downloads.size());
}

TEST_F(ConnectionManagerTest, nmdc_handshake_does_not_reset_other_users_retry_errors) {
	const string hub = "dchub://one.example";
	auto alice = UserPtr(new User(ClientManager::getInstance()->makeCid("Alice", hub)));
	auto bob = user(2);
	manager->getCQI(HintedUser(alice, hub), CONNECTION_TYPE_DOWNLOAD).setErrors(3);
	manager->getCQI(HintedUser(bob, hub), CONNECTION_TYPE_DOWNLOAD).setErrors(5);
	auto uc = manager->getConnection(true, false);
	peers.emplace_back(uc);
	uc->setHubUrl(hub);
	uc->setEncoding(Text::utf8);
	uc->setState(UserConnection::STATE_SUPNICK);
	manager->on(UserConnectionListener::MyNick(), uc, "Alice");
	EXPECT_EQ(alice, uc->getUser());
	EXPECT_EQ(0, manager->downloads[0].getErrors());
	EXPECT_EQ(5, manager->downloads[1].getErrors());
}

TEST_F(ConnectionManagerTest, incoming_adc_expectations_with_the_same_token_do_not_overwrite_peers) {
	auto alice = user(1), bob = user(2);
	auto& a = online(alice, "adc://one.example");
	auto& b = online(bob, "adc://two.example");
	manager->addToken("same-token", a, CONNECTION_TYPE_DOWNLOAD);
	manager->addToken("same-token", b, CONNECTION_TYPE_UPLOAD);
	manager->addToken("same-token", a, CONNECTION_TYPE_DOWNLOAD);
	ASSERT_EQ(2U, manager->tokens.size());
	auto first = peer(alice, "same-token", false, false);
	auto second = peer(bob, "same-token", false, true);
	auto firstCheck = manager->checkToken(first);
	EXPECT_TRUE(firstCheck.first);
	EXPECT_EQ(CONNECTION_TYPE_DOWNLOAD, firstCheck.second);
	EXPECT_EQ("adc://one.example", first->getHubUrl());
	auto secondCheck = manager->checkToken(second);
	EXPECT_TRUE(secondCheck.first);
	EXPECT_EQ(CONNECTION_TYPE_UPLOAD, secondCheck.second);
	EXPECT_EQ("adc://two.example", second->getHubUrl());
	EXPECT_TRUE(manager->tokens.empty());
}

TEST_F(ConnectionManagerTest, upload_slot_notifications_keep_the_downloaders_protocol_token) {
	auto u = user(1);
	online(u, "dchub://one.example");
	u->setFlag(User::ONLINE);
	auto up = peer(u, "downloaders-token", false, true);
	manager->addNewConnection(up, CONNECTION_TYPE_UPLOAD);
	ASSERT_TRUE(up->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_NE("downloaders-token", up->getToken());
	auto uploads = UploadManager::getInstance();
	uploads->addFailedUpload(*up, "file.bin");
	uploads->notifyQueuedUsers();
	ASSERT_EQ(1U, hubs.front()->attempts.size());
	EXPECT_EQ("downloaders-token", hubs.front()->attempts.front());
}

TEST_F(ConnectionManagerTest, removing_a_download_preserves_another_peers_identical_token) {
	auto alice = user(1), bob = user(2);
	auto& a = online(alice, "adc://one.example");
	auto& b = online(bob, "adc://two.example");
	auto& cqi = manager->getCQI(HintedUser(alice, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	auto token = cqi.getToken();
	manager->addToken(token, a, CONNECTION_TYPE_DOWNLOAD);
	manager->addToken(token, b, CONNECTION_TYPE_UPLOAD);
	manager->putCQI(cqi);
	EXPECT_TRUE(manager->wasRemovedDownload(token, alice->getCID()));
	EXPECT_FALSE(manager->wasRemovedDownload(token, bob->getCID()));
	auto up = peer(bob, token, false, true);
	EXPECT_TRUE(manager->checkToken(up).first);
	EXPECT_TRUE(manager->tokens.empty());
}

TEST_F(ConnectionManagerTest, mcn_connection_limits_apply_per_user) {
	SettingsManager::getInstance()->set(SettingsManager::MAX_MCN_UPLOADS, 2);
	auto alice = user(1), bob = user(2);
	// Two regular connections plus the reserved priority connection.
	for(int n = 0; n < 3; ++n) {
		auto up = peer(alice, std::to_string(n), false, true, true);
		manager->addNewConnection(up, CONNECTION_TYPE_UPLOAD);
		EXPECT_TRUE(up->isSet(UserConnection::FLAG_ASSOCIATED));
	}
	auto excess = peer(alice, "extra", false, true, true);
	manager->addNewConnection(excess, CONNECTION_TYPE_UPLOAD);
	EXPECT_FALSE(excess->isSet(UserConnection::FLAG_ASSOCIATED));
	auto other = peer(bob, "0", false, true, true);
	manager->addNewConnection(other, CONNECTION_TYPE_UPLOAD);
	EXPECT_TRUE(other->isSet(UserConnection::FLAG_ASSOCIATED));
	EXPECT_EQ(4, connected);
}

TEST_F(ConnectionManagerTest, pending_failover_attempt_times_out_without_resetting_its_clock) {
	auto u = user(1);
	online(u, "adc://one.example");
	online(u, "adc://two.example");
	queueFile(u, "adc://one.example");
	auto& cqi = manager->getCQI(HintedUser(u, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	cqi.setState(ConnectionQueueItem::CONNECTING);
	cqi.setLastAttempt(1000);
	cqi.setErrors(1);
	for(uint64_t tick = 2000; tick <= 51000; tick += 1000) {
		manager->on(TimerManagerListener::Second(), tick);
		EXPECT_EQ(1000U, cqi.getLastAttempt());
		EXPECT_EQ(ConnectionQueueItem::CONNECTING, cqi.getState());
	}
	manager->on(TimerManagerListener::Second(), 52000);
	EXPECT_EQ(ConnectionQueueItem::WAITING, cqi.getState());
	EXPECT_EQ(2, cqi.getErrors());
	EXPECT_EQ(1U, failures.size());
	manager->on(TimerManagerListener::Second(), 62000);
	EXPECT_EQ(ConnectionQueueItem::CONNECTING, cqi.getState());
	EXPECT_EQ(1U, hubs[0]->attempts.size());
}

TEST_F(ConnectionManagerTest, retries_use_the_next_eligible_hub_and_lists_keep_their_route) {
	auto u = user(1);
	online(u, "adc://one.example");
	online(u, "adc://two.example");
	queueFile(u, "adc://one.example");
	auto& cqi = manager->getCQI(HintedUser(u, "adc://one.example"), CONNECTION_TYPE_DOWNLOAD);
	cqi.setLastAttempt(1000);
	cqi.setErrors(1);
	manager->on(TimerManagerListener::Second(), 3000);
	EXPECT_TRUE(hubs[0]->attempts.empty());
	EXPECT_EQ(1U, hubs[1]->attempts.size());

	auto listUser = user(2);
	online(listUser, "adc://one.example");
	online(listUser, "adc://two.example");
	QueueManager::getInstance()->addList(HintedUser(listUser, "ADC://ONE.example"), QueueItem::FLAG_CLIENT_VIEW);
	auto routes = manager->getDownloadRoutes(HintedUser(listUser, "adc://one.example"), MCNDownloadType::ANY);
	ASSERT_EQ(1U, routes.size());
	EXPECT_EQ("adc://one.example", routes.front());
	const string wrongHub = "adc://two.example";
	EXPECT_EQ(QueueItem::PAUSED, QueueManager::getInstance()->hasDownload(listUser, MCNDownloadType::ANY, &wrongHub));
}

TEST_F(ConnectionManagerTest, negotiates_zstd_independently_for_adc_and_nmdc) {
#ifdef _WIN32
	WSADATA wsa;
	ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa));
#endif
	// The ADC handler sends INF after SUP. Supply an idle transport and bypass
	// plugins, so that the real handler runs without external connections.
	PluginManager::newInstance();
	PluginManager::getInstance()->shutdown = true;
	auto adc = peer(user(1), "adc-zstd", false, false);
	adc->socket = BufferedSocket::getSocket('\n');
	adc->socket->sock = std::make_unique<Socket>(Socket::TYPE_TCP);
	adc->setState(UserConnection::STATE_SUPNICK);
	manager->on(AdcCommand::SUP(), adc, AdcCommand("CSUP ADBASE ADTIGR ADZLIG ADZST1"));
	EXPECT_TRUE(adc->isSet(UserConnection::FLAG_SUPPORTS_ZSTD_GET));
	EXPECT_TRUE(adc->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET));
	auto nmdc = peer(user(2), "nmdc-zstd", true, false);
	manager->on(UserConnectionListener::Supports(), nmdc, StringList { "ADCGet", "ZLIG", "ZST1" });
	EXPECT_TRUE(nmdc->isSet(UserConnection::FLAG_SUPPORTS_ZSTD_GET));
	EXPECT_TRUE(nmdc->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET));
	auto legacy = peer(user(3), "nmdc-old", true, false);
	manager->on(UserConnectionListener::Supports(), legacy, StringList { "ADCGet", "ZLIG" });
	EXPECT_FALSE(legacy->isSet(UserConnection::FLAG_SUPPORTS_ZSTD_GET));
	EXPECT_TRUE(legacy->isSet(UserConnection::FLAG_SUPPORTS_ZLIB_GET));
	BufferedSocket::putSocket(adc->socket);
	adc->socket = nullptr;
	BufferedSocket::waitShutdown();
	PluginManager::deleteInstance();
#ifdef _WIN32
	WSACleanup();
#endif
}
