#include "testbase.h"

#include <dcpp/ChatMessage.h>
#include <dcpp/EmoticonManager.h>
#include <dcpp/File.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/Tagger.h>
#include <dcpp/Util.h>
#include <dcpp/version.h>

using namespace dcpp;

class testchatformat : public testing::Test {
protected:
	void SetUp() override { SettingsManager::newInstance(); }
	void TearDown() override {
		EmoticonManager::reload();
		SettingsManager::deleteInstance();
	}
};

TEST_F(testchatformat, package_rules_respect_boundaries)
{
	auto settings = SettingsManager::getInstance();
	const auto package = Util::getTempPath() + "dcpp-test-emoticons.dcemo";
	const auto sourceManifest = Util::getTempPath() + "dcpp-test-source-emoticons.xml";
	const auto smileIcon = Util::getTempPath() + "dcpp-test-source-emoticons" PATH_SEPARATOR_STR "smile.bmp";
	const auto laughIcon = Util::getTempPath() + "dcpp-test-source-emoticons" PATH_SEPARATOR_STR "laugh.png";
	File::deleteFile(package);
	File::ensureDirectory(smileIcon);
	File(smileIcon, File::WRITE, File::CREATE | File::TRUNCATE).write("bmp", 3);
	File(laughIcon, File::WRITE, File::CREATE | File::TRUNCATE).write("png", 3);
	const string sourceXml =
		"<?xml version=\"1.0\" encoding=\"windows-1252\"?>\r\nTest pack v1.0\r\n<Emoticons>"
		"<Emoticon PasteText=\" :)\" Bitmap=\"dcpp-test-source-emoticons\\smile.bmp\"/>"
		"<Emoticon PasteText=\" :-)\" Bitmap=\"dcpp-test-source-emoticons\\smile.bmp\"/>"
		"<Emoticon Expression=\" :D\" Bitmap=\"dcpp-test-source-emoticons\\laugh.png\"/>"
		"</Emoticons>";
	File(sourceManifest, File::WRITE, File::CREATE | File::TRUNCATE).write(sourceXml);
	const auto imported = EmoticonManager::importEmoticonPackage(sourceManifest);
	ASSERT_EQ(size_t(2), imported.items.size());
	ASSERT_EQ(size_t(2), imported.items.front().rules.size());
	EmoticonManager::exportPackage(package, imported.name, imported.items);
	ASSERT_NE(string::npos, File(package, File::READ, File::OPEN).read().find(
		"<Version>" VERSIONSTRING "</Version>"));
	settings->set(SettingsManager::ENABLE_EMOTICONS, true);
	settings->set(SettingsManager::EMOTICON_PACK, package);
	EmoticonManager::reload();

	string scratch;
	Tagger tags("hello :) http://example.invalid/:) :D");
	ChatMessage::format(tags, scratch);
	const auto html = tags.merge(scratch);
	ASSERT_NE(string::npos, html.find("<emoticon name=\"smile\">:)</emoticon>"));
	ASSERT_NE(string::npos, html.find("http://example.invalid/:)"));
	ASSERT_NE(string::npos, html.find("<emoticon name=\"laugh\">:D</emoticon>"));
	File::deleteFile(package);
	File::deleteFile(sourceManifest);
	File::deleteFile(smileIcon);
	File::deleteFile(laughIcon);
}

TEST_F(testchatformat, emoticons_can_be_disabled)
{
	auto settings = SettingsManager::getInstance();
	const auto previous = settings->get(SettingsManager::ENABLE_EMOTICONS);
	settings->set(SettingsManager::ENABLE_EMOTICONS, false);

	string scratch;
	Tagger tags("hello :)");
	ChatMessage::format(tags, scratch);
	ASSERT_EQ(string::npos, tags.merge(scratch).find("<emoticon"));

	settings->set(SettingsManager::ENABLE_EMOTICONS, previous);
}

TEST_F(testchatformat, hub_specific_nickname_is_colored_as_a_mention)
{
	string scratch;
	Tagger tags("hello HubSpecificNick, welcome");
	ChatMessage::format(tags, scratch, "HubSpecificNick");
	ASSERT_NE(string::npos, tags.merge(scratch).find(
		"<span id=\"mention\">HubSpecificNick</span>"));
}

TEST_F(testchatformat, semantic_chat_styles_round_trip)
{
	const SettingsManager::StrSetting fonts[] = {
		SettingsManager::LINK_FONT, SettingsManager::LOG_FONT,
		SettingsManager::CHAT_TIMESTAMP_FONT, SettingsManager::CHAT_NICK_FONT,
		SettingsManager::CHAT_TEXT_FONT, SettingsManager::CHAT_SYSTEM_FONT,
		SettingsManager::CHAT_OWN_TIMESTAMP_FONT, SettingsManager::CHAT_OWN_NICK_FONT,
		SettingsManager::CHAT_OWN_TEXT_FONT, SettingsManager::CHAT_MENTION_FONT
	};
	const SettingsManager::IntSetting backgrounds[] = {
		SettingsManager::LINK_BG_COLOR, SettingsManager::LOG_BG_COLOR,
		SettingsManager::CHAT_TIMESTAMP_BG_COLOR, SettingsManager::CHAT_NICK_BG_COLOR,
		SettingsManager::CHAT_TEXT_BG_COLOR, SettingsManager::CHAT_SYSTEM_BG_COLOR,
		SettingsManager::CHAT_OWN_TIMESTAMP_BG_COLOR, SettingsManager::CHAT_OWN_NICK_BG_COLOR,
		SettingsManager::CHAT_OWN_TEXT_BG_COLOR, SettingsManager::CHAT_MENTION_BG_COLOR
	};

	auto settings = SettingsManager::getInstance();
	for(size_t i = 0; i < std::size(fonts); ++i) {
		settings->set(fonts[i], "TestFont" + std::to_string(i));
		settings->set(backgrounds[i], static_cast<int>(RGB(i + 1, i + 11, i + 21)));
	}

	const auto path = Util::getTempPath() + "dcpp-test-chat-styles.xml";
	File::deleteFile(path);
	settings->save(path);
	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);

	for(size_t i = 0; i < std::size(fonts); ++i) {
		EXPECT_EQ("TestFont" + std::to_string(i), settings->get(fonts[i]));
		EXPECT_EQ(static_cast<int>(RGB(i + 1, i + 11, i + 21)), settings->get(backgrounds[i]));
	}
	File::deleteFile(path);
}
