#include "testbase.h"

#include <dcpp/Exception.h>
#include <dcpp/HBRIValidation.h>

using namespace dcpp;

namespace {

HBRIValidator::ConnectInfo connectInfo(bool v6, const string& ip, const string& port) {
	HBRIValidator::ConnectInfo info(v6, false);
	info.ip = ip;
	info.port = port;
	return info;
}

}

TEST(testhbri, validates_numeric_endpoints)
{
	EXPECT_NO_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "203.0.113.1", "411")));
	EXPECT_NO_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "2001:db8::1", "65535")));

	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "example.com", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "2001:db8::1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "203.0.113.1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "0.0.0.0", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "127.0.0.1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "10.0.0.1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "169.254.1.1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "239.1.2.3", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "::", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "::1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "fe80::1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "fc00::1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "ff02::1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(true, "::ffff:203.0.113.1", "411")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "203.0.113.1", "0")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "203.0.113.1", "65536")), Exception);
	EXPECT_THROW(HBRIValidator::validateConnectInfo(connectInfo(false, "203.0.113.1", "41x")), Exception);
}

TEST(testhbri, validates_adc_status_response)
{
	EXPECT_NO_THROW(HBRIValidator::validateResponse("CSTA 000 Validation\\ssucceeded"));

	EXPECT_THROW(HBRIValidator::validateResponse("CINF IDAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), Exception);
	EXPECT_THROW(HBRIValidator::validateResponse("CSTA 000"), Exception);
	EXPECT_THROW(HBRIValidator::validateResponse("CSTA 0x0 Invalid"), Exception);
	EXPECT_THROW(HBRIValidator::validateResponse("CSTA 142 Rejected"), Exception);
	EXPECT_THROW(HBRIValidator::validateResponse(string("CSTA 000 Invalid") + static_cast<char>(0xFF)), Exception);
	EXPECT_THROW(HBRIValidator::validateResponse(string(8193, 'x')), Exception);
}
