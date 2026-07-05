#include "testbase.h"

#include <dcpp/AdcCommand.h>
#include <dcpp/SearchManager.h>

using namespace dcpp;

namespace {

class TestCommandHandler : public CommandHandler<TestCommandHandler> {
public:
	template<typename T>
	void handle(T, const AdcCommand&) { }

	void handle(AdcCommand::PMI, const AdcCommand&) { pmiHandled = true; }
	void handle(AdcCommand::TCP, const AdcCommand&) { tcpHandled = true; }

	bool pmiHandled = false;
	bool tcpHandled = false;
};

}

TEST(testadc, test_adccommand)
{
	string sidStr = "ABCD";
	auto sid = AdcCommand::toSID(sidStr);

	string sidStr2 = "1234";
	auto sid2 = AdcCommand::toSID(sidStr2);

	ASSERT_EQ("CSTA 151 lol\n",
		AdcCommand(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_FILE_NOT_AVAILABLE, "lol").toString(sid));
	ASSERT_EQ("DCTM " + sidStr + " " + sidStr2 + " param1 param2\n",
		AdcCommand(AdcCommand::CMD_CTM, sid2, AdcCommand::TYPE_DIRECT).addParam("param1").addParam("param2").toString(sid));
	ASSERT_EQ("CPMI TP1\n", AdcCommand(AdcCommand::CMD_PMI).addParam("TP", "1").toString(0));

	AdcCommand pmi("CPMI SN1");
	ASSERT_TRUE(pmi == AdcCommand::CMD_PMI);
	ASSERT_TRUE(pmi.hasFlag("SN", 0));

	TestCommandHandler handler;
	handler.dispatch("CPMI TP1");
	ASSERT_TRUE(handler.pmiHandled);
	handler.dispatch("ITCP I4203.0.113.1 P4411 TOtoken");
	ASSERT_TRUE(handler.tcpHandled);
}

TEST(testadc, recognizes_plaintext_udp_before_sudp)
{
	const string cid(39, 'A');
	const string adcResult = "URES " + cid + " FNfile.txt SL1 SI1 TRTTH TOtoken\n";
	const string adcExtension = "UPSR " + cid + " TRhash PC1\n";
	const string alignedAdcResult = "URES " + cid + " " + string(18, 'X') + "\n";
	const string alignedNmdcResult = "$SR " + string(28, 'X');

	EXPECT_TRUE(SearchManager::isAdcUdpPacket(adcResult));
	EXPECT_TRUE(SearchManager::isAdcUdpPacket(adcExtension));
	EXPECT_TRUE(SearchManager::isPlaintextUdpPacket(adcResult));
	EXPECT_TRUE(SearchManager::isPlaintextUdpPacket("$SR nick file.txt"));
	ASSERT_EQ(size_t(0), alignedAdcResult.size() % 16);
	ASSERT_EQ(size_t(0), alignedNmdcResult.size() % 16);
	EXPECT_TRUE(SearchManager::isPlaintextUdpPacket(alignedAdcResult));
	EXPECT_TRUE(SearchManager::isPlaintextUdpPacket(alignedNmdcResult));

	EXPECT_FALSE(SearchManager::isAdcUdpPacket("CRES " + cid + " FNfile.txt\n"));
	EXPECT_FALSE(SearchManager::isAdcUdpPacket("UrES " + cid + " FNfile.txt\n"));
	EXPECT_FALSE(SearchManager::isAdcUdpPacket("U1ES " + cid + " FNfile.txt\n"));
	EXPECT_FALSE(SearchManager::isAdcUdpPacket("URES " + cid + " FNfile.txt"));
	EXPECT_FALSE(SearchManager::isAdcUdpPacket("URES invalid1 FNfile.txt\n"));
	EXPECT_FALSE(SearchManager::isAdcUdpPacket("URES " + cid + " FNfile.txt\nextra"));
	EXPECT_FALSE(SearchManager::isAdcUdpPacket("URES " + cid + " FN\xFF\n"));
	EXPECT_FALSE(SearchManager::isPlaintextUdpPacket("$Search something"));
	EXPECT_FALSE(SearchManager::isPlaintextUdpPacket(string(32, '\xA5')));
}
