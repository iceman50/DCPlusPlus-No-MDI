#include "testbase.h"

#include <dcpp/NmdcHub.h>

using namespace dcpp;

TEST(testnmdc, classifies_starred_status_spoofs)
{
	EXPECT_EQ(NmdcHub::classifyStatusFrame("Hub maintenance"), NmdcHub::STATUS_FRAME_NORMAL);
	EXPECT_EQ(NmdcHub::classifyStatusFrame("$ForceMove dchub://example.invalid"), NmdcHub::STATUS_FRAME_NORMAL);
	EXPECT_EQ(NmdcHub::classifyStatusFrame("*** Hub maintenance"), NmdcHub::STATUS_FRAME_SPOOF);
	EXPECT_EQ(NmdcHub::classifyStatusFrame(" \t*** Price is $5"), NmdcHub::STATUS_FRAME_SPOOF);
	EXPECT_EQ(NmdcHub::classifyStatusFrame("*** Documentation mentions $ForceMove safely"),
		NmdcHub::STATUS_FRAME_SPOOF);
	EXPECT_EQ(NmdcHub::classifyStatusFrame("*** Documentation mentions $ForceMoveX"),
		NmdcHub::STATUS_FRAME_SPOOF);
}

TEST(testnmdc, detects_commands_concatenated_into_starred_status)
{
	EXPECT_EQ(NmdcHub::classifyStatusFrame("*** notice$ForceMove dchub://evil.invalid"),
		NmdcHub::STATUS_FRAME_DESYNC);
	EXPECT_EQ(NmdcHub::classifyStatusFrame("*** $To: victim From: attacker $<attacker> message"),
		NmdcHub::STATUS_FRAME_DESYNC);
	EXPECT_EQ(NmdcHub::classifyStatusFrame("*** notice$HubIsFull"),
		NmdcHub::STATUS_FRAME_DESYNC);
}

TEST(testnmdc, sanitizes_forged_status_for_inert_display)
{
	const string data(" \t*** forged\r\n[Info] local-looking\x01");
	EXPECT_EQ(NmdcHub::sanitizeStatusMessage(data),
		"forged\\r\\n[Info] local-looking\\x01");
}
