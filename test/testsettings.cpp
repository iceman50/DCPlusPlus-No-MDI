#include "testbase.h"

#include <dcpp/File.h>
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
