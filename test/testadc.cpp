#include "testbase.h"

#include <dcpp/AdcCommand.h>

using namespace dcpp;

namespace {

class TestCommandHandler : public CommandHandler<TestCommandHandler> {
public:
	template<typename T>
	void handle(T, const AdcCommand&) { }

	void handle(AdcCommand::PMI, const AdcCommand&) { pmiHandled = true; }

	bool pmiHandled = false;
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
}
