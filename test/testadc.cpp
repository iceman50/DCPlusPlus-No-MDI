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

	const string markdown = "2 \\* 3  \n\nnext";
	const auto wire = AdcCommand(AdcCommand::CMD_MSG).addParam(markdown).addParam("RT", "1").toString(0);
	EXPECT_EQ("CMSG 2\\s\\\\*\\s3\\s\\s\\n\\nnext RT1\n", wire);
	AdcCommand decoded(wire.substr(0, wire.size() - 1));
	EXPECT_EQ(markdown, decoded.getParam(0));
	EXPECT_TRUE(decoded.hasFlag("RT", 1));

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

TEST(testadc, enforces_protocol_states_and_contexts)
{
	AdcCommand sup(AdcCommand::CMD_SUP, AdcCommand::TYPE_HUB);
	sup.addParam("ADBASE").addParam("ADTIGR");
	EXPECT_TRUE(sup.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_TRUE(sup.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(sup.isValidFor(AdcCommand::STATE_IDENTIFY, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(sup.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_FROM_HUB));
	AdcCommand clientSup(AdcCommand::CMD_SUP);
	clientSup.addParam("ADBAS0").addParam("ADRTF0");
	EXPECT_TRUE(clientSup.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_CLIENT));

	AdcCommand inf(AdcCommand::CMD_INF, AdcCommand::TYPE_BROADCAST);
	inf.addParam("ID", "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE");
	EXPECT_TRUE(inf.isValidFor(AdcCommand::STATE_IDENTIFY, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_TRUE(inf.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(inf.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_TO_HUB));

	AdcCommand pas(AdcCommand::CMD_PAS, AdcCommand::TYPE_HUB);
	pas.addParam("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE");
	EXPECT_TRUE(pas.isValidFor(AdcCommand::STATE_VERIFY, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(pas.isValidFor(AdcCommand::STATE_IDENTIFY, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(pas.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));

	AdcCommand gpa(AdcCommand::CMD_GPA, AdcCommand::TYPE_INFO);
	gpa.addParam("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDEFG");
	for(auto state: { AdcCommand::STATE_PROTOCOL, AdcCommand::STATE_IDENTIFY, AdcCommand::STATE_VERIFY,
		AdcCommand::STATE_NORMAL, AdcCommand::STATE_DATA })
	{
		EXPECT_TRUE(gpa.isValidFor(state, AdcCommand::CONTEXT_FROM_HUB));
	}
	EXPECT_FALSE(gpa.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));

	AdcCommand msg(AdcCommand::CMD_MSG);
	msg.addParam("hello").addParam("RT", "1");
	EXPECT_TRUE(msg.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_CLIENT));
	EXPECT_FALSE(msg.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(msg.isValidFor(AdcCommand::STATE_DATA, AdcCommand::CONTEXT_CLIENT));

	AdcCommand status(AdcCommand::SEV_RECOVERABLE, AdcCommand::ERROR_FILE_NOT_AVAILABLE, "Not available");
	EXPECT_TRUE(status.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_CLIENT));
	EXPECT_TRUE(status.isValidFor(AdcCommand::STATE_VERIFY, AdcCommand::CONTEXT_CLIENT));
	EXPECT_FALSE(status.isValidFor(AdcCommand::STATE_DATA, AdcCommand::CONTEXT_CLIENT));

	AdcCommand extension(AdcCommand::toFourCC("XYZ"), AdcCommand::TYPE_HUB);
	EXPECT_TRUE(extension.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(extension.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_TO_HUB));

	AdcCommand udpResult(AdcCommand::CMD_RES, AdcCommand::TYPE_UDP);
	udpResult.addParam("FNfile.txt").addParam("SI1").addParam("SL1").addParam("TOtoken");
	EXPECT_TRUE(udpResult.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_UDP));
	EXPECT_FALSE(udpResult.isValidFor(AdcCommand::STATE_PROTOCOL, AdcCommand::CONTEXT_UDP));
}

TEST(testadc, validates_adc_grammar_and_numeric_bounds)
{
	EXPECT_THROW(AdcCommand("Cmsg hello"), ParseException);
	EXPECT_THROW(AdcCommand("BMSG ABC1 hello"), ParseException);

	AdcCommand featureSearch("FSCH ABCD +TCP4-SEGA ANlinux");
	EXPECT_EQ("+TCP4-SEGA", featureSearch.getFeatures());
	EXPECT_TRUE(featureSearch.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));
	featureSearch.setFeatures("TCP4");
	EXPECT_FALSE(featureSearch.isValidSyntax());

	AdcCommand badSid(AdcCommand::CMD_SID, AdcCommand::TYPE_INFO);
	badSid.addParam("ABC1");
	EXPECT_FALSE(badSid.isValidSyntax());

	AdcCommand ctm(AdcCommand::CMD_CTM, AdcCommand::toSID("ABCD"), AdcCommand::TYPE_DIRECT);
	ctm.addParam("ADC/1.0").addParam("65535").addParam("token");
	EXPECT_TRUE(ctm.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	ctm.getParameters()[1] = "65536";
	EXPECT_FALSE(ctm.isValidSyntax());
	ctm.getParameters()[1] = "12x";
	EXPECT_FALSE(ctm.isValidSyntax());
	ctm.setTo(AdcCommand::toSID("1234"));
	EXPECT_FALSE(ctm.isValidSyntax());

	AdcCommand infPorts(AdcCommand::CMD_INF, AdcCommand::TYPE_BROADCAST);
	infPorts.addParam("U4", "0").addParam("U6", "65535").addParam("AS", "0");
	EXPECT_TRUE(infPorts.isValidSyntax());
	infPorts.getParameters()[1] = "U665536";
	EXPECT_FALSE(infPorts.isValidSyntax());

	AdcCommand get(AdcCommand::CMD_GET);
	get.addParam("file").addParam("TTH/ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE").addParam("0").addParam("-1");
	EXPECT_TRUE(get.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_CLIENT));
	get.getParameters()[2] = "9223372036854775807";
	get.getParameters()[3] = "1";
	EXPECT_FALSE(get.isValidSyntax());
	get.getParameters()[2] = "-1";
	EXPECT_FALSE(get.isValidSyntax());
	get.getParameters()[2] = "0";
	get.getParameters()[3] = "9223372036854775808";
	EXPECT_FALSE(get.isValidSyntax());
	get.getParameters() = { "blom", "/", "0", "1024", "BK8", "BH24" };
	EXPECT_TRUE(get.isValidSyntax());
	get.getParameters()[1] = "/wrong/";
	EXPECT_FALSE(get.isValidSyntax());

	AdcCommand snd(AdcCommand::CMD_SND);
	snd.addParam("file").addParam("TTH/ABCDEFGHIJKLMNOPQRSTUVWXYZ234567ABCDE").addParam("0").addParam("-1");
	EXPECT_FALSE(snd.isValidSyntax());

	AdcCommand shortSalt(AdcCommand::CMD_GPA, AdcCommand::TYPE_INFO);
	shortSalt.addParam("JJWEPPPLCA3PF2ZCRRYO3333");
	EXPECT_TRUE(shortSalt.isValidFor(AdcCommand::STATE_VERIFY, AdcCommand::CONTEXT_FROM_HUB));
	shortSalt.getParameters()[0] = "AB";
	EXPECT_TRUE(shortSalt.isValidSyntax());
	shortSalt.getParameters()[0] = "A";
	EXPECT_FALSE(shortSalt.isValidSyntax());
	shortSalt.getParameters()[0] = "A1";
	EXPECT_FALSE(shortSalt.isValidSyntax());

	AdcCommand badStatus(AdcCommand::CMD_STA);
	badStatus.addParam("099").addParam("Invalid success code");
	EXPECT_FALSE(badStatus.isValidSyntax());

	AdcCommand timestamped(AdcCommand::CMD_MSG);
	timestamped.addParam("hello").addParam("TS", "9223372036854775808");
	EXPECT_FALSE(timestamped.isValidSyntax());
	timestamped.getParameters()[1] = "PM1234";
	EXPECT_FALSE(timestamped.isValidSyntax());
	timestamped.getParameters()[1] = "RT1";
	EXPECT_TRUE(timestamped.isValidSyntax());
	timestamped.getParameters()[1] = "RT2";
	EXPECT_TRUE(timestamped.isValidSyntax());

	AdcCommand userCommand(AdcCommand::CMD_CMD, AdcCommand::TYPE_INFO);
	userCommand.addParam("Example").addParam("CT", "16");
	EXPECT_FALSE(userCommand.isValidSyntax());
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
