/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <dcpp/File.h>
#include <dcpp/LogManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/Util.h>

using namespace dcpp;

class SettingsMigrationTest : public testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
	}

	void TearDown() override {
		SettingsManager::deleteInstance();
	}
};

TEST_F(SettingsMigrationTest, uses_readable_emoticon_size_by_default)
{
	EXPECT_EQ(24, SettingsManager::getInstance()->get(SettingsManager::EMOTICON_SIZE));
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
