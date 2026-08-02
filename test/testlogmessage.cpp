#include "testbase.h"

#include <dcpp/LogMessage.h>
#include <dcpp/LogManager.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/debug.h>

using namespace dcpp;

TEST(testlogmessage, preserves_structured_fields)
{
	LogMessage message(123, "Connection restored", LogMessage::SEV_INFO, "Connectivity");

	EXPECT_EQ(message.getTime(), 123);
	EXPECT_EQ(message.getText(), "Connection restored");
	EXPECT_EQ(message.getSeverity(), LogMessage::SEV_INFO);
	EXPECT_EQ(message.getArea(), "Connectivity");
	EXPECT_FALSE(message.isDebug());
	EXPECT_STREQ(LogMessage::getSeverityName(LogMessage::SEV_WARNING), "Warning");
}

TEST(testlogmessage, dcdebug_uses_the_structured_logger)
{
	SettingsManager::newInstance();
	LogManager::newInstance();
	EXPECT_FALSE(SETTING(SHOW_SYSTEM_LOG_DEBUG));

	dcdebug("Release-capable diagnostic %d\n", 42);
	const auto messages = LogManager::getInstance()->getLastLogs();
	ASSERT_FALSE(messages.empty());
	const auto& message = messages.back();
	EXPECT_EQ(message->getText(), "Release-capable diagnostic 42");
	EXPECT_EQ(message->getSeverity(), LogMessage::SEV_VERBOSE);
	EXPECT_EQ(message->getArea(), "Debug");
	EXPECT_TRUE(message->isDebug());

	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
}

TEST(testlogmessage, debug_messages_have_a_separate_history_quota)
{
	SettingsManager::newInstance();
	LogManager::newInstance();

	for(int i = 0; i <= 100; ++i) {
		LogManager::getInstance()->message("Info " + std::to_string(i), LogMessage::SEV_INFO, "Test");
		LogManager::getInstance()->message("Debug " + std::to_string(i), LogMessage::SEV_VERBOSE, "Debug");
	}

	const auto messages = LogManager::getInstance()->getLastLogs();
	const auto debugCount = std::count_if(messages.begin(), messages.end(), [](const LogMessagePtr& message) {
		return message->isDebug();
	});
	EXPECT_EQ(messages.size(), 200U);
	EXPECT_EQ(debugCount, 100);
	EXPECT_EQ(messages.front()->getText(), "Info 1");

	LogManager::deleteInstance();
	SettingsManager::deleteInstance();
}
