#include "testbase.h"

#include <dcpp/Socket.h>
#include <dcpp/TimerManager.h>
#include <dcpp/Util.h>

using namespace dcpp;

namespace {

class NetworkSession {
public:
	NetworkSession() {
#ifdef _WIN32
		WSADATA data;
		result = WSAStartup(MAKEWORD(2, 2), &data);
#endif
	}

	~NetworkSession() {
#ifdef _WIN32
		if(result == 0) {
			WSACleanup();
		}
#endif
	}

	bool ready() const { return result == 0; }

private:
	int result = 0;
};

bool waitForConnect(Socket& socket, uint32_t timeout) {
	const auto deadline = GET_TICK() + timeout;
	do {
		if(socket.waitConnected(100)) {
			return true;
		}
	} while(GET_TICK() < deadline);
	return false;
}

}

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

TEST(testnetwork, connects_to_ipv4_loopback_with_preconfigured_buffers)
{
	NetworkSession network;
	ASSERT_TRUE(network.ready());

	Socket listener(Socket::TYPE_TCP);
	listener.setV4only(true);
	listener.setLocalIp4("127.0.0.1");
	const auto port = listener.listen("0");

	Socket client(Socket::TYPE_TCP);
	client.setV4only(true);
	client.setSocketOpt(SO_RCVBUF, 32 * 1024);
	client.setSocketOpt(SO_SNDBUF, 32 * 1024);
	client.connect("127.0.0.1", port);

	EXPECT_TRUE(waitForConnect(client, 2000));
	EXPECT_GE(client.getSocketOptInt(SO_RCVBUF), 32 * 1024);
	EXPECT_GE(client.getSocketOptInt(SO_SNDBUF), 32 * 1024);
}

TEST(testnetwork, reports_failed_nonblocking_connect)
{
	NetworkSession network;
	ASSERT_TRUE(network.ready());

	Socket listener(Socket::TYPE_TCP);
	listener.setV4only(true);
	listener.setLocalIp4("127.0.0.1");
	const auto port = listener.listen("0");
	listener.disconnect();

	Socket client(Socket::TYPE_TCP);
	client.setV4only(true);
	bool failed = false;
	try {
		client.connect("127.0.0.1", port);
		waitForConnect(client, 2000);
	} catch(const SocketException&) {
		failed = true;
	}

	EXPECT_TRUE(failed);
}

TEST(testnetwork, clears_resolved_ip_before_reusing_a_socket)
{
	NetworkSession network;
	ASSERT_TRUE(network.ready());

	Socket listener(Socket::TYPE_TCP);
	listener.setV4only(true);
	listener.setLocalIp4("127.0.0.1");
	const auto port = listener.listen("0");

	Socket client(Socket::TYPE_TCP);
	client.setV4only(true);
	client.connect("127.0.0.1", port);
	ASSERT_TRUE(waitForConnect(client, 2000));
	ASSERT_FALSE(client.getIp().empty());

	client.setLocalIp4("192.0.2.1");
	EXPECT_THROW(client.connect("127.0.0.1", port), SocketException);
}
