#include "testbase.h"

#include <dcpp/LogManager.h>
#include <dcpp/LogMessage.h>
#include <dcpp/SettingsManager.h>

using namespace dcpp;

TEST(testlogmessage, preserves_structured_fields)
{
	LogMessage message(123, "Connection restored", LogMessage::SEV_INFO, "Connectivity");

	EXPECT_EQ(message.getTime(), 123);
	EXPECT_EQ(message.getText(), "Connection restored");
	EXPECT_EQ(message.getSeverity(), LogMessage::SEV_INFO);
	EXPECT_EQ(message.getArea(), "Connectivity");
	EXPECT_STREQ(LogMessage::getSeverityName(LogMessage::SEV_WARNING), "Warning");
}

class LogManagerHistoryTest : public testing::Test {
protected:
	void SetUp() override {
		SettingsManager::newInstance();
		SettingsManager::getInstance()->set(SettingsManager::LOG_SYSTEM, false);
		LogManager::newInstance();
	}

	void TearDown() override {
		LogManager::deleteInstance();
		SettingsManager::deleteInstance();
	}
};

TEST_F(LogManagerHistoryTest, retains_and_trims_the_configured_number_of_messages)
{
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::MAX_SYSTEM_LOG_ITEMS, 3);
	for(int i = 0; i < 5; ++i) {
		LogManager::getInstance()->message(std::to_string(i), LogMessage::SEV_INFO, "Test");
	}

	auto messages = LogManager::getInstance()->getLastLogs();
	ASSERT_EQ(size_t(3), messages.size());
	EXPECT_EQ("2", messages.front()->getText());
	EXPECT_EQ("4", messages.back()->getText());

	settings->set(SettingsManager::MAX_SYSTEM_LOG_ITEMS, 2);
	messages = LogManager::getInstance()->getLastLogs();
	ASSERT_EQ(size_t(2), messages.size());
	EXPECT_EQ("3", messages.front()->getText());
}
