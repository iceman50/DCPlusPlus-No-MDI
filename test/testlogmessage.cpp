#include "testbase.h"

#include <dcpp/LogManager.h>
#include <dcpp/LogMessage.h>

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

TEST(testlogmessage, escapes_protocol_data_for_single_line_logging)
{
	const string data("line\r\n\t\\\x01\xff", 10);
	EXPECT_EQ(LogManager::escapeProtocolData(data), "line\\r\\n\\t\\\\\\x01\\xFF");
	EXPECT_EQ(LogManager::escapeProtocolData("abcdef", 3),
		"abc... (truncated, 6 bytes total)");
}

TEST(testlogmessage, exposes_protocol_log_categories)
{
	EXPECT_EQ(LogManager::getProtocolArea(LogManager::PROTOCOL_ADC_STA), "Protocol / ADC STA");
	EXPECT_EQ(LogManager::getProtocolArea(LogManager::PROTOCOL_NMDC_SPOOF), "Protocol / NMDC Spoof");
}

TEST(testlogmessage, maps_adc_status_severity_across_transports)
{
	EXPECT_EQ(LogManager::getAdcStatusSeverity("ISTA 000 Success"), LogMessage::SEV_INFO);
	EXPECT_EQ(LogManager::getAdcStatusSeverity("CSTA 151 File\\snot\\savailable"), LogMessage::SEV_WARNING);
	EXPECT_EQ(LogManager::getAdcStatusSeverity("HSTA 251 Fatal"), LogMessage::SEV_ERROR);
	EXPECT_EQ(LogManager::getAdcStatusSeverity("USTA ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE 251 Fatal"),
		LogMessage::SEV_ERROR);
	EXPECT_EQ(LogManager::getAdcStatusSeverity("$ADCSTA 151 File\\snot\\savailable"),
		LogMessage::SEV_WARNING);
	EXPECT_EQ(LogManager::getAdcStatusSeverity("ISTA malformed"), LogMessage::SEV_WARNING);
}
