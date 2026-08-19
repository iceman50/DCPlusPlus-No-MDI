#include "testbase.h"

#include <dcpp/AdcCommand.h>
#include <dcpp/BBSManager.h>

using namespace dcpp;

namespace {

string tth(const string& data) {
	TigerTree tree;
	tree.update(data.data(), data.size());
	tree.finalize();
	return tree.getRoot().toBase32();
}

}

TEST(testbbs, validates_wire_commands_and_contexts)
{
	const string hash(39, 'A');
	const string cid(39, 'A');

	AdcCommand descriptor("IBBD BDgeneral NIGeneral PE31 MS262144 TS0 OT0 NP0");
	EXPECT_TRUE(descriptor.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));
	EXPECT_FALSE(descriptor.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));

	AdcCommand subscription("HBBL BDgeneral TS0");
	EXPECT_TRUE(subscription.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(subscription.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));

	AdcCommand entry("IBBL TR" + hash + " SI220 BDgeneral ID" + cid + " TH" + hash + " SJFirst\\spost TS1");
	EXPECT_TRUE(entry.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));

	AdcCommand post("HBBP TR" + hash + " SI220 BDgeneral SJFirst\\spost");
	EXPECT_TRUE(post.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_TO_HUB));
	EXPECT_FALSE(post.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));

	AdcCommand documentHeader("IBB0 ID" + cid + " SJFirst\\spost DA1");
	EXPECT_TRUE(documentHeader.isValidSyntax());
	EXPECT_FALSE(documentHeader.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));

	AdcCommand refusal("ISTA 171 No\\ssuch\\sboard FCBBL BDgeneral");
	EXPECT_TRUE(refusal.isValidFor(AdcCommand::STATE_NORMAL, AdcCommand::CONTEXT_FROM_HUB));

	AdcCommand badSize("IBBD BDgeneral PE31 MS-1 TS0 OT0");
	EXPECT_FALSE(badSize.isValidSyntax());
}

TEST(testbbs, composes_hashes_and_verifies_canonical_documents)
{
	const string cid(39, 'A');
	string raw;
	string error;
	BBSDocument composed;
	ASSERT_TRUE(BBSManager::composeDocument(cid, Util::emptyString, "Hub upgrade on Saturday",
		"The hub will be **offline**.\n", true, 1786439650, raw, composed, error)) << error;
	EXPECT_EQ("IBB0 ID" + cid + " SJHub\\supgrade\\son\\sSaturday DA1786439650 RT1\n"
		"The hub will be **offline**.\n", raw);
	EXPECT_EQ(tth(raw), composed.tth);

	BBSDocument parsed;
	ASSERT_TRUE(BBSManager::parseDocument(raw, composed.tth, parsed, error)) << error;
	EXPECT_EQ(cid, parsed.authorId);
	EXPECT_EQ("Hub upgrade on Saturday", parsed.subject);
	EXPECT_EQ("The hub will be **offline**.\n", parsed.body);
	EXPECT_EQ(1786439650U, parsed.composed);
	EXPECT_EQ(1, parsed.richText);

	EXPECT_FALSE(BBSManager::parseDocument(raw, string(39, 'A'), parsed, error));
}

TEST(testbbs, rejects_noncanonical_or_malformed_documents_after_hash_verification)
{
	const string cid(39, 'A');
	string error;
	BBSDocument parsed;

	const string wrongOrder = "IBB0 SJSubject ID" + cid + " DA1\nBody";
	EXPECT_FALSE(BBSManager::parseDocument(wrongOrder, tth(wrongOrder), parsed, error));

	const string duplicate = "IBB0 ID" + cid + " SJSubject SJAgain DA1\nBody";
	EXPECT_FALSE(BBSManager::parseDocument(duplicate, tth(duplicate), parsed, error));

	const string overEscaped = "IBB0 ID" + cid + " SJSub\\sject DA01\nBody";
	EXPECT_TRUE(BBSManager::parseDocument(overEscaped, tth(overEscaped), parsed, error));
	// Leading zeroes are an ADC integer spelling and are not forbidden by BBS0 canonical form.

	const string crlf = "IBB0 ID" + cid + " SJSubject DA1\r\nBody";
	EXPECT_FALSE(BBSManager::parseDocument(crlf, tth(crlf), parsed, error));

	const string unknownBeforeKnown = "IBB0 ID" + cid + " ZZfuture SJSubject DA1\nBody";
	EXPECT_FALSE(BBSManager::parseDocument(unknownBeforeKnown, tth(unknownBeforeKnown), parsed, error));

	const string unknownLast = "IBB0 ID" + cid + " SJSubject DA1 ZZfuture\nBody";
	EXPECT_TRUE(BBSManager::parseDocument(unknownLast, tth(unknownLast), parsed, error));
	const string crossCommandField = "IBB0 ID" + cid + " SJSubject DA1 SIopaque\nBody";
	EXPECT_TRUE(BBSManager::parseDocument(crossCommandField, tth(crossCommandField), parsed, error));

	const string noSubject = "IBB0 ID" + cid + " DA1\nBody";
	EXPECT_FALSE(BBSManager::parseDocument(noSubject, tth(noSubject), parsed, error));
}

TEST(testbbs, validates_board_names)
{
	EXPECT_TRUE(BBSManager::validBoardName("dev.adc-1_test"));
	EXPECT_TRUE(BBSManager::validBoardName(".."));
	EXPECT_FALSE(BBSManager::validBoardName(""));
	EXPECT_FALSE(BBSManager::validBoardName("general discussion"));
	EXPECT_FALSE(BBSManager::validBoardName(string(65, 'a')));
}
