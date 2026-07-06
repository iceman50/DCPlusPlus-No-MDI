#include "testbase.h"

#include <dcpp/Util.h>

using namespace dcpp;

TEST(testnetwork, classifies_ipv4_addresses)
{
	EXPECT_FALSE(Util::isPublicIp("", false));
	EXPECT_FALSE(Util::isPublicIp("not-an-address", false));
	EXPECT_FALSE(Util::isPublicIp("0.0.0.0", false));
	EXPECT_FALSE(Util::isPublicIp("255.255.255.255", false));
	EXPECT_FALSE(Util::isPublicIp("239.1.2.3", false));

	EXPECT_TRUE(Util::isLocalIp("127.0.0.2", false));
	EXPECT_TRUE(Util::isLocalIp("169.254.10.20", false));
	EXPECT_FALSE(Util::isLocalIp("169.1.2.3", false));

	EXPECT_TRUE(Util::isPrivateIp("10.0.0.1", false));
	EXPECT_TRUE(Util::isPrivateIp("172.31.255.255", false));
	EXPECT_TRUE(Util::isPrivateIp("192.168.1.1", false));
	EXPECT_FALSE(Util::isPrivateIp("172.32.0.1", false));

	EXPECT_TRUE(Util::isPublicIp("8.8.8.8", false));
}

TEST(testnetwork, classifies_ipv6_addresses)
{
	EXPECT_FALSE(Util::isPublicIp("", true));
	EXPECT_FALSE(Util::isPublicIp("not-an-address", true));
	EXPECT_FALSE(Util::isPublicIp("::", true));
	EXPECT_FALSE(Util::isPublicIp("ff02::1", true));
	EXPECT_FALSE(Util::isPublicIp("::ffff:192.0.2.1", true));

	EXPECT_TRUE(Util::isLocalIp("::1", true));
	EXPECT_TRUE(Util::isLocalIp("fe80::1%21", true));
	EXPECT_TRUE(Util::isLocalIp("febf::1", true));
	EXPECT_FALSE(Util::isLocalIp("fec0::1", true));

	EXPECT_TRUE(Util::isPrivateIp("fc00::1", true));
	EXPECT_TRUE(Util::isPrivateIp("fdff::1", true));
	EXPECT_FALSE(Util::isPrivateIp("fe00::1", true));

	EXPECT_TRUE(Util::isPublicIp("2606:4700:4700::1111", true));
}

TEST(testnetwork, validates_peer_endpoints)
{
	EXPECT_TRUE(Util::isSafePeerEndpoint("203.0.113.10", "411", "198.51.100.20"));
	EXPECT_TRUE(Util::isSafePeerEndpoint("192.168.1.10", "411", "192.168.1.1"));
	EXPECT_TRUE(Util::isSafePeerEndpoint("fd00::10", "411", "fd00::1"));
	EXPECT_TRUE(Util::isSafePeerEndpoint("127.0.0.1", "411", "127.0.0.1"));

	EXPECT_FALSE(Util::isSafePeerEndpoint("127.0.0.1", "411", "198.51.100.20"));
	EXPECT_FALSE(Util::isSafePeerEndpoint("192.168.1.10", "411", "198.51.100.20"));
	EXPECT_FALSE(Util::isSafePeerEndpoint("192.168.1.10", "0", "192.168.1.1"));
	EXPECT_FALSE(Util::isSafePeerEndpoint("192.168.1.10", "65536", "192.168.1.1"));
	EXPECT_FALSE(Util::isSafePeerEndpoint("not-an-ip", "411", "192.168.1.1"));
}
