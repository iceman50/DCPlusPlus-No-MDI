/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <dcpp/ConnectivityManager.h>
#include <dcpp/File.h>
#include <dcpp/HubEntry.h>
#include <dcpp/LogManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/Util.h>

using namespace dcpp;

class SettingsMigrationTest : public testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
		ConnectivityManager::newInstance();
	}

	void TearDown() override {
		ConnectivityManager::deleteInstance();
		SettingsManager::deleteInstance();
	}
};

TEST_F(SettingsMigrationTest, uses_readable_emoticon_size_by_default)
{
	EXPECT_EQ(24, SettingsManager::getInstance()->get(SettingsManager::EMOTICON_SIZE));
}

TEST_F(SettingsMigrationTest, persists_global_hub_user_icon_size_and_embedded_icons)
{
	const auto path = Util::getTempPath() + "dcpp-test-hub-icon-settings.xml";
	File::deleteFile(path);
	auto settings = SettingsManager::getInstance();
	EXPECT_EQ(16, settings->get(SettingsManager::HUB_USER_ICON_SIZE));
	settings->set(SettingsManager::HUB_USER_ICON_SIZE, 32);
	settings->set(SettingsManager::ICON_PACK, "@embedded");
	settings->save(path);
	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);
	EXPECT_EQ(32, settings->get(SettingsManager::HUB_USER_ICON_SIZE));
	EXPECT_EQ("@embedded", settings->get(SettingsManager::ICON_PACK));
	File::deleteFile(path);
}

TEST_F(SettingsMigrationTest, hub_user_icon_sizes_inherit_without_changing_other_hubs)
{
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::HUB_USER_ICON_SIZE, 24);
	auto resolved = settings->getHubSettings();
	EXPECT_EQ(24, resolved.get(HubSettings::UserIconSize));
	HubSettings group;
	group.get(HubSettings::UserIconSize) = 28;
	resolved.merge(group);
	FavoriteHubEntry favorite, otherHub;
	favorite.get(HubSettings::UserIconSize) = 40;
	resolved.merge(favorite);
	EXPECT_EQ(40, resolved.get(HubSettings::UserIconSize));
	EXPECT_EQ(24, settings->get(SettingsManager::HUB_USER_ICON_SIZE));
	auto otherResolved = settings->getHubSettings();
	otherResolved.merge(otherHub);
	EXPECT_EQ(24, otherResolved.get(HubSettings::UserIconSize));

	settings->set(SettingsManager::HUB_USER_ICON_SIZE, 32);
	resolved = settings->getHubSettings();
	resolved.merge(favorite);
	EXPECT_EQ(40, resolved.get(HubSettings::UserIconSize));
	favorite.get(HubSettings::UserIconSize) = HubSettings::getMinInt();
	resolved = settings->getHubSettings();
	resolved.merge(favorite);
	EXPECT_EQ(32, resolved.get(HubSettings::UserIconSize));
	resolved.merge(group);
	resolved.merge(favorite);
	EXPECT_EQ(28, resolved.get(HubSettings::UserIconSize));
	settings->set(SettingsManager::HUB_USER_ICON_SIZE, 999);
	EXPECT_EQ(16, settings->getHubSettings().get(HubSettings::UserIconSize));
}

TEST_F(SettingsMigrationTest, favorite_hub_icon_size_round_trip_and_default_reset)
{
	FavoriteHubEntry favorite;
	for(auto size: HubSettings::userIconSizes) {
		favorite.get(HubSettings::UserIconSize) = size;
		SimpleXML saved;
		saved.addTag("Hub");
		favorite.save(saved);
		SimpleXML restored;
		restored.fromXML(saved.toXML());
		ASSERT_TRUE(restored.findChild("Hub"));
		FavoriteHubEntry loaded;
		loaded.load(restored);
		EXPECT_EQ(size, loaded.get(HubSettings::UserIconSize));
	}
	favorite.get(HubSettings::UserIconSize) = HubSettings::getMinInt();
	SimpleXML defaults;
	defaults.addTag("Hub");
	favorite.save(defaults);
	EXPECT_EQ(string::npos, defaults.toXML().find("UserIconSize"));
	for(const auto& xml: { "<Hub/>", "<Hub UserIconSize=\"0\"/>", "<Hub UserIconSize=\"17\"/>", "<Hub UserIconSize=\"999\"/>" }) {
		SimpleXML legacy;
		legacy.fromXML(xml);
		ASSERT_TRUE(legacy.findChild("Hub"));
		favorite.get(HubSettings::UserIconSize) = 48;
		favorite.load(legacy);
		EXPECT_EQ(HubSettings::getMinInt(), favorite.get(HubSettings::UserIconSize));
	}
}

TEST_F(SettingsMigrationTest, persists_icon_pack_selection)
{
	const auto path = Util::getTempPath() + "dcpp-test-icon-pack-settings.xml";
	File::deleteFile(path);
	auto settings = SettingsManager::getInstance();
	EXPECT_TRUE(settings->get(SettingsManager::ICON_PACK).empty());
	settings->set(SettingsManager::ICON_PACK, "IconPacks/Custom.dcico");
	settings->save(path);

	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);
	EXPECT_EQ("IconPacks/Custom.dcico", settings->get(SettingsManager::ICON_PACK));
	File::deleteFile(path);
}

TEST_F(SettingsMigrationTest, repairs_values_truncated_by_experimental_page_spinner)
{
	const auto path = Util::getTempPath() + "dcpp-test-truncated-spinner-settings.xml";
	File::deleteFile(path);

	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::MAX_QUEUED_PROTOCOL_DATA, 32767);
	settings->set(SettingsManager::MAX_PARTIAL_LIST_BYTES, 32767);
	settings->set(SettingsManager::RICH_TEXT_MAX_SIZE, 32767);
	// Other settings that legitimately used the same maximum must remain untouched.
	settings->set(SettingsManager::MAX_SUDP_PACKET, 32767);
	settings->save(path);

	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);

	EXPECT_TRUE(settings->isDefault(SettingsManager::MAX_QUEUED_PROTOCOL_DATA));
	EXPECT_EQ(16 * 1024 * 1024, settings->get(SettingsManager::MAX_QUEUED_PROTOCOL_DATA));
	EXPECT_TRUE(settings->isDefault(SettingsManager::MAX_PARTIAL_LIST_BYTES));
	EXPECT_EQ(64 * 1024 * 1024, settings->get(SettingsManager::MAX_PARTIAL_LIST_BYTES));
	EXPECT_TRUE(settings->isDefault(SettingsManager::RICH_TEXT_MAX_SIZE));
	EXPECT_EQ(64 * 1024, settings->get(SettingsManager::RICH_TEXT_MAX_SIZE));
	EXPECT_EQ(32767, settings->get(SettingsManager::MAX_SUDP_PACKET));

	File::deleteFile(path);
}

TEST_F(SettingsMigrationTest, bounds_system_log_history_setting)
{
	auto settings = SettingsManager::getInstance();
	EXPECT_EQ(size_t(100), LogManager::getHistoryLimit());

	settings->set(SettingsManager::MAX_SYSTEM_LOG_ITEMS, 0);
	EXPECT_EQ(size_t(LogManager::MIN_HISTORY_ITEMS), LogManager::getHistoryLimit());

	settings->set(SettingsManager::MAX_SYSTEM_LOG_ITEMS, LogManager::MAX_HISTORY_ITEMS + 1);
	EXPECT_EQ(size_t(LogManager::MAX_HISTORY_ITEMS), LogManager::getHistoryLimit());

	settings->set(SettingsManager::MAX_SYSTEM_LOG_ITEMS, 250);
	EXPECT_EQ(size_t(250), LogManager::getHistoryLimit());
}

TEST_F(SettingsMigrationTest, persists_ccpm_reconnect_policy)
{
	const auto path = Util::getTempPath() + "dcpp-test-ccpm-reconnect-settings.xml";
	File::deleteFile(path);

	auto settings = SettingsManager::getInstance();
	EXPECT_EQ(5, settings->get(SettingsManager::CCPM_RECONNECT_BASE_DELAY));
	EXPECT_EQ(60, settings->get(SettingsManager::CCPM_RECONNECT_MAX_DELAY));
	EXPECT_EQ(60, settings->get(SettingsManager::CCPM_STABLE_CONNECTION_TIME));
	EXPECT_EQ(5, settings->get(SettingsManager::CCPM_MAX_AUTOMATIC_ATTEMPTS));

	settings->set(SettingsManager::CCPM_RECONNECT_BASE_DELAY, 7);
	settings->set(SettingsManager::CCPM_RECONNECT_MAX_DELAY, 90);
	settings->set(SettingsManager::CCPM_STABLE_CONNECTION_TIME, 120);
	settings->set(SettingsManager::CCPM_MAX_AUTOMATIC_ATTEMPTS, 8);
	settings->save(path);

	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);

	EXPECT_EQ(7, settings->get(SettingsManager::CCPM_RECONNECT_BASE_DELAY));
	EXPECT_EQ(90, settings->get(SettingsManager::CCPM_RECONNECT_MAX_DELAY));
	EXPECT_EQ(120, settings->get(SettingsManager::CCPM_STABLE_CONNECTION_TIME));
	EXPECT_EQ(8, settings->get(SettingsManager::CCPM_MAX_AUTOMATIC_ATTEMPTS));

	File::deleteFile(path);
}

TEST_F(SettingsMigrationTest, persists_multithreaded_hashing_limit)
{
	const auto path = Util::getTempPath() + "dcpp-test-hashing-thread-settings.xml";
	File::deleteFile(path);

	auto settings = SettingsManager::getInstance();
	EXPECT_EQ(2, settings->get(SettingsManager::HASHING_THREADS));
	settings->set(SettingsManager::HASHING_THREADS, 6);
	settings->save(path);

	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);

	EXPECT_EQ(6, settings->get(SettingsManager::HASHING_THREADS));
	File::deleteFile(path);
}
