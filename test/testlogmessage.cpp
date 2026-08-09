#include "testbase.h"

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
