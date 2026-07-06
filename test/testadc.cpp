#include "testbase.h"

#include <dcpp/AdcCommand.h>
#include <dcpp/AdcHub.h>
#include <dcpp/Client.h>
#include <dcpp/CryptoManager.h>
#include <dcpp/HubEntry.h>
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

	AdcCommand emptyParameter("CINF  IDABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE");
	string parsedId;
	EXPECT_TRUE(emptyParameter.getParam("ID", 0, parsedId));
	EXPECT_EQ("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE", parsedId);
	EXPECT_EQ(uint32_t(0), AdcCommand::toSID("ABC"));

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

TEST(testadc, broadcast_inf_keeps_sid_out_of_parameters)
{
	const string cid = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE";
	AdcCommand command("BINF ABCD ID" + cid + " NITest");

	EXPECT_EQ(AdcCommand::toSID("ABCD"), command.getFrom());
	ASSERT_EQ(2U, command.getParameters().size());
	EXPECT_EQ("ID" + cid, command.getParam(0));
	EXPECT_EQ("NITest", command.getParam(1));
}

TEST(testadc, advertises_all_enabled_connectivity_families)
{
	// The family used for the hub connection must always be present, even when
	// incoming peer connections for that family are disabled.
	EXPECT_EQ(std::make_pair(true, false), AdcHub::getAdvertisedConnectivity(false, false, false));
	EXPECT_EQ(std::make_pair(false, true), AdcHub::getAdvertisedConnectivity(true, false, false));

	// An enabled secondary family must not depend on optional hub extensions.
	EXPECT_EQ(std::make_pair(true, true), AdcHub::getAdvertisedConnectivity(false, false, true));
	EXPECT_EQ(std::make_pair(true, true), AdcHub::getAdvertisedConnectivity(true, true, false));

	EXPECT_EQ(std::make_pair(true, false), AdcHub::getAdvertisedConnectivity(false, true, false));
	EXPECT_EQ(std::make_pair(false, true), AdcHub::getAdvertisedConnectivity(true, false, true));
	EXPECT_EQ(std::make_pair(true, true), AdcHub::getAdvertisedConnectivity(false, true, true));
	EXPECT_EQ(std::make_pair(true, true), AdcHub::getAdvertisedConnectivity(true, true, true));
}

TEST(testadc, validates_failover_urls)
{
	EXPECT_TRUE(Client::isValidFailoverUrl("adc://example.com:411", true));
	EXPECT_TRUE(Client::isValidFailoverUrl("adcs://[2001:db8::1]:1511", true));
	EXPECT_TRUE(Client::isValidFailoverUrl("dchub://example.com:411", false));
	EXPECT_TRUE(Client::isValidFailoverUrl("nmdcs://example.com:1511", false));

	EXPECT_FALSE(Client::isValidFailoverUrl("dchub://example.com:411", true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adc://example.com:411", false));
	EXPECT_FALSE(Client::isValidFailoverUrl("https://example.com:443", true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adc://example.com", true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adc://example.com:0", true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adc://example.com:65536", true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adc://example.com:41x", true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adc://127.0.0.1:411", true, true));
	EXPECT_FALSE(Client::isValidFailoverUrl("adcs://[::1]:411", true, true));
	EXPECT_TRUE(Client::isValidFailoverUrl("adc://127.0.0.1:411", true, false));
}

TEST(testadc, favorite_hub_preserves_an_explicit_empty_share_profile)
{
	FavoriteHubEntry hub;
	hub.setServer("adc://primary.example:411");
	hub.setFailoverServers(StringList { "adcs://failover.example:1511" });

	EXPECT_FALSE(hub.hasShareProfile());
	hub.setShareDirectories({});
	EXPECT_TRUE(hub.hasShareProfile());
	EXPECT_TRUE(hub.getShareDirectories().empty());
	EXPECT_TRUE(hub.hasServer("adc://primary.example:411"));
	EXPECT_TRUE(hub.hasServer("adcs://failover.example:1511"));
}

TEST(testadc, sudp_crypto_rejects_malformed_packets)
{
	CryptoManager::newInstance();
	auto crypto = CryptoManager::getInstance();
	const uint8_t key[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	const string command = "URES " + string(39, 'A') + " FNfile.txt SL1 SI1 TRTTH TOtoken\n";

	auto encrypted = crypto->encryptSUDP(key, command);
	ASSERT_FALSE(encrypted.empty());
	ASSERT_EQ(size_t(0), encrypted.size() % 16);

	string decrypted;
	EXPECT_TRUE(crypto->decryptSUDP(key, reinterpret_cast<const uint8_t*>(encrypted.data()), encrypted.size(), decrypted));
	EXPECT_EQ(command, decrypted);
	EXPECT_FALSE(crypto->decryptSUDP(nullptr, reinterpret_cast<const uint8_t*>(encrypted.data()), encrypted.size(), decrypted));
	EXPECT_FALSE(crypto->decryptSUDP(key, nullptr, encrypted.size(), decrypted));
	EXPECT_FALSE(crypto->decryptSUDP(key, reinterpret_cast<const uint8_t*>(encrypted.data()), encrypted.size() - 1, decrypted));

	encrypted[encrypted.size() - 17] ^= static_cast<char>(0xFF);
	EXPECT_FALSE(crypto->decryptSUDP(key, reinterpret_cast<const uint8_t*>(encrypted.data()), encrypted.size(), decrypted));

	CryptoManager::deleteInstance();
}
