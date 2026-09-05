/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"

#include <dcpp/ChatMessage.h>
#include <dcpp/EmoticonManager.h>
#include <dcpp/File.h>
#include <dcpp/RichText.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SimpleXMLReader.h>
#include <dcpp/Tagger.h>
#include <dcpp/Util.h>
#include <dcpp/version.h>

#include <array>

using namespace dcpp;

namespace {

class RichTextXmlCollector : public SimpleXMLReader::CallBack {
public:
	void startTag(const string&, StringPairList&, bool simple) override {
		if(depth == 0) ++roots;
		if(!simple) ++depth;
	}

	void data(const string& value) override { text += value; }
	void endTag(const string&) override { if(depth) --depth; }

	size_t roots = 0;
	size_t depth = 0;
	string text;
};

}

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

TEST_F(testchatformat, overlong_chat_links_are_not_clickable)
{
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::CHAT_LINK_MAX_LENGTH, 24);

	string scratch;
	Tagger tags("short http://ok.invalid long http://example.invalid/abcdefghijklmnopqrstuvwxyz");
	ChatMessage::format(tags, scratch);
	const auto html = tags.merge(scratch);

	EXPECT_NE(string::npos, html.find("<a href=\"http://ok.invalid\">http://ok.invalid</a>"));
	EXPECT_EQ(string::npos, html.find("href=\"http://example.invalid/abcdefghijklmnopqrstuvwxyz\""));
	EXPECT_NE(string::npos, html.find("http://example.invalid/abcdefghijklmnopqrstuvwxyz"));
}

TEST_F(testchatformat, plain_magnets_keep_the_link_visible_and_show_the_file_size)
{
	const string hash = "VN6PLQ7ZQGKD3NDBK6ZTZG5PYQXSNMFYVJH4TXA";
	const string magnet = "magnet:?xt=urn:tree:tiger:" + hash + "&xl=204800&dn=cat.jpg";
	string scratch;
	Tagger tags(magnet);
	ChatMessage::format(tags, scratch);
	const auto html = tags.merge(scratch);

	EXPECT_NE(string::npos, html.find("<a href=\"magnet:?xt=urn:tree:tiger:" + hash));
	EXPECT_NE(string::npos, html.find("magnet:?xt=urn:tree:tiger:" + hash));
	// File sizes use the current locale, including its decimal separator.
	EXPECT_NE(string::npos, html.find("cat.jpg \xe2\x80\x94 " + Util::formatBytes(204800))) << html;
}

TEST_F(testchatformat, rich_text_parses_the_complete_safe_dialect)
{
	const auto parsed = RichText::parse(
		"# Heading\n\n**strong** and _emphasis_ and ~strike~.\n\n"
		"> quote\n\n1. first\n2. second\n\n```\ncode <tag>\n```\n\n"
		"| left | right |\n|:-----|------:|\n| a | b |\n\n"
		"<b>**inside**</b> &copy; <https://example.org>", 512);

	ASSERT_TRUE(parsed.valid);
	EXPECT_TRUE(parsed.formatted);
	EXPECT_NE(string::npos, parsed.html.find("id=\"rtfHeading1\""));
	EXPECT_NE(string::npos, parsed.html.find("<b>strong</b>"));
	EXPECT_NE(string::npos, parsed.html.find("<i>emphasis</i>"));
	EXPECT_NE(string::npos, parsed.html.find("id=\"rtfStrike\""));
	EXPECT_NE(string::npos, parsed.html.find("1. "));
	EXPECT_NE(string::npos, parsed.html.find("id=\"rtfCode\""));
	EXPECT_NE(string::npos, parsed.html.find("id=\"rtfTable\" columns=\"2\""));
	EXPECT_NE(string::npos, parsed.html.find("id=\"rtfTableRow\""));
	EXPECT_NE(string::npos, parsed.html.find("text-align: right"));
	EXPECT_EQ(string::npos, parsed.html.find('\t'));
	EXPECT_NE(string::npos, parsed.html.find("&lt;b&gt;<b>inside</b>&lt;/b&gt;"));
	EXPECT_NE(string::npos, parsed.html.find("\xc2\xa9"));
	EXPECT_NE(string::npos, parsed.html.find("href=\"https://example.org\""));

	const auto formattedLink = RichText::parse(
		"[release (**candidate**)](https://example.org/download)", 512);
	ASSERT_TRUE(formattedLink.valid);
	EXPECT_NE(string::npos, formattedLink.html.find(
		"<a href=\"https://example.org/download\">release (<b>candidate</b>)</a>"));
}

TEST_F(testchatformat, rich_text_editor_constructs_are_complete_xml_documents)
{
	const string attachment = "magnet:?xt=urn:tree:tiger:VN6PLQ7ZQGKD3NDBK6ZTZG5PYQXSNMFYVJH4TXA&xl=204800&dn=cat.jpg";
	const std::array<string, 14> samples = {{
		"**bold text**", "_italic text_", "~~removed text~~", "## Heading",
		"[link text](https://example.org/)", "> Quoted text", "`code`",
		"- First item\n- Second item", "1. First item\n2. Second item",
		"```\ncode\n```", "| Left | Center | Right |\n| :--- | :---: | ---: |\n| A | B | C |", "---",
		"[cat.jpg](" + attachment + ")", "![cat](" + attachment + ")"
	}};

	for(const auto& sample: samples) {
		SCOPED_TRACE(sample);
		const auto parsed = RichText::parse(sample, 512);
		ASSERT_TRUE(parsed.valid);
		ASSERT_TRUE(parsed.formatted);
		ASSERT_EQ(0U, parsed.html.find("<span>"));
		ASSERT_EQ(parsed.html.size() - 7, parsed.html.rfind("</span>"));

		RichTextXmlCollector collector;
		SimpleXMLReader reader(&collector);
		EXPECT_NO_THROW(reader.parse(parsed.html));
		EXPECT_EQ(1U, collector.roots);
		EXPECT_EQ(0U, collector.depth);
	}

	const auto lists = RichText::parse("- First item\n- Second item\n\n1. Third item\n2. Fourth item", 512);
	RichTextXmlCollector collector;
	SimpleXMLReader reader(&collector);
	ASSERT_NO_THROW(reader.parse(lists.html));
	EXPECT_NE(string::npos, collector.text.find("First item"));
	EXPECT_NE(string::npos, collector.text.find("Second item"));
	EXPECT_NE(string::npos, collector.text.find("Third item"));
	EXPECT_NE(string::npos, collector.text.find("Fourth item"));
}

TEST_F(testchatformat, rich_text_uses_magnets_for_attachments_and_inline_media)
{
	const string hash = "VN6PLQ7ZQGKD3NDBK6ZTZG5PYQXSNMFYVJH4TXA";
	const string magnet = "magnet:?xt=urn:tree:tiger:" + hash + "&dn=cat.jpg&xl=204800";
	auto parsed = RichText::parse("[file](" + magnet + ") ![cat](" + magnet +
		") ![tracked](https://example.invalid/cat.png)", 512);

	ASSERT_TRUE(parsed.valid);
	ASSERT_EQ(size_t(2), parsed.attachments.size());
	EXPECT_FALSE(parsed.attachments[0].inlineMedia);
	EXPECT_TRUE(parsed.attachments[1].inlineMedia);
	EXPECT_EQ(hash, parsed.attachments[1].tth);
	EXPECT_EQ("cat.jpg", parsed.attachments[1].name);
	EXPECT_EQ(204800, parsed.attachments[1].size);
	EXPECT_NE(string::npos, parsed.html.find("<rtfimage src=\"magnet:?xt=urn:tree:tiger:" + hash));
	EXPECT_NE(string::npos, parsed.html.find("[image] cat</rtfimage>"));
	EXPECT_NE(string::npos, parsed.html.find("tracked"));
	EXPECT_EQ(string::npos, parsed.html.find("https://example.invalid/cat.png"));
	EXPECT_FALSE(parsed.safeToSend);
	string invalidInline = "![tracked](https://example.invalid/cat.png)";
	EXPECT_FALSE(RichText::prepareOutgoingMessage(invalidInline, true));

	RichText::Attachment attachment;
	EXPECT_TRUE(RichText::parseMagnetUri(magnet, 512, attachment));
	EXPECT_FALSE(RichText::parseMagnetUri(
		"magnet:?xt=urn:tree:tiger:" + hash + "&dn=no-size", 512, attachment));
	EXPECT_FALSE(RichText::parseMagnetUri(
		"magnet:?xt=urn:tree:tiger:vn6plq7zqgkd3ndbk6ztzg5pyqxsnmfyvjh4txa&xl=1", 512, attachment));
	EXPECT_TRUE(RichText::isSafeLink("https://example.org/path", 256));
	EXPECT_FALSE(RichText::isSafeLink("javascript:alert(1)", 256));
	EXPECT_FALSE(RichText::isSafeLink("file:///tmp/private", 256));
	EXPECT_TRUE(RichText::isInlineMediaFile("shared icon.ico"));
	EXPECT_TRUE(RichText::isInlineMediaFile("SHARED ICON.ICO"));
	EXPECT_FALSE(RichText::isInlineMediaFile("renamed icon.ico.exe"));

	const auto built = RichText::makeAttachmentMarkdown(
		"photo (final) [1].jpg", hash, 204800, true);
	EXPECT_NE(string::npos, built.find("&dn=photo+%28final%29+%5B1%5D.jpg"));
	EXPECT_EQ("magnet:?xt=urn:tree:tiger:" + hash +
		"&xl=204800&dn=photo+%28final%29+%5B1%5D.jpg",
		RichText::makeAttachmentMagnet("photo (final) [1].jpg", hash, 204800));
	const auto builtParsed = RichText::parse(built, 512);
	ASSERT_TRUE(builtParsed.valid);
	ASSERT_EQ(size_t(1), builtParsed.attachments.size());
	EXPECT_TRUE(builtParsed.attachments[0].inlineMedia);
	EXPECT_EQ("photo (final) [1].jpg", builtParsed.attachments[0].name);
	EXPECT_EQ(204800, builtParsed.attachments[0].size);
	EXPECT_NE(string::npos, builtParsed.html.find("magnet:?xt=urn:tree:tiger:" + hash));
	EXPECT_NE(string::npos, builtParsed.html.find(Util::formatBytes(204800))) << builtParsed.html;
}

TEST_F(testchatformat, rich_text_message_policy_is_protocol_independent)
{
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::ENABLE_RICH_TEXT, true);
	settings->set(SettingsManager::RICH_TEXT_MAX_SIZE, 1024);
	settings->set(SettingsManager::CHAT_LINK_MAX_LENGTH, 256);

	const char incomingRaw[] = "safe\0text\t  \nnext";
	string incoming(incomingRaw, sizeof(incomingRaw) - 1);
	const auto incomingCopy = incoming;
	EXPECT_TRUE(RichText::prepareIncomingMessage(incoming, true));
	EXPECT_EQ(incomingCopy, incoming);

	string outgoing = "**formatted**";
	EXPECT_FALSE(RichText::prepareOutgoingMessage(outgoing));
	EXPECT_TRUE(RichText::prepareOutgoingMessage(outgoing, true));
	string windowsLines = "**first**\r\n\r\n_second_\rthird";
	EXPECT_TRUE(RichText::prepareOutgoingMessage(windowsLines, true));
	EXPECT_EQ("**first**\n\n_second_\nthird", windowsLines);
	string plain = "plain text";
	EXPECT_FALSE(RichText::prepareOutgoingMessage(plain, true));

	string oversized(1025, 'x');
	EXPECT_FALSE(RichText::prepareIncomingMessage(oversized, true));
	EXPECT_FALSE(oversized.empty());
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

TEST_F(testchatformat, mcn_connection_limits_round_trip)
{
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::MAX_MCN_DOWNLOADS, 3);
	settings->set(SettingsManager::MAX_MCN_UPLOADS, 4);

	const auto path = Util::getTempPath() + "dcpp-test-mcn-settings.xml";
	File::deleteFile(path);
	settings->save(path);
	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);

	EXPECT_EQ(3, settings->get(SettingsManager::MAX_MCN_DOWNLOADS));
	EXPECT_EQ(4, settings->get(SettingsManager::MAX_MCN_UPLOADS));
	File::deleteFile(path);
}

TEST_F(testchatformat, rich_text_limits_round_trip)
{
	auto settings = SettingsManager::getInstance();
	settings->set(SettingsManager::ENABLE_RICH_TEXT, false);
	settings->set(SettingsManager::RICH_TEXT_MAX_SIZE, 32768);
	settings->set(SettingsManager::ENABLE_RTF_TEMP_SHARES, false);
	settings->set(SettingsManager::RTF_DROPPED_IMAGES_INLINE, false);
	settings->set(SettingsManager::RTF_TEMP_SHARE_LIMIT, 42);

	const auto path = Util::getTempPath() + "dcpp-test-rich-text-settings.xml";
	File::deleteFile(path);
	settings->save(path);
	SettingsManager::deleteInstance();
	SettingsManager::newInstance();
	settings = SettingsManager::getInstance();
	settings->load(path);

	EXPECT_FALSE(settings->get(SettingsManager::ENABLE_RICH_TEXT));
	EXPECT_EQ(32768, settings->get(SettingsManager::RICH_TEXT_MAX_SIZE));
	EXPECT_FALSE(settings->get(SettingsManager::ENABLE_RTF_TEMP_SHARES));
	EXPECT_FALSE(settings->get(SettingsManager::RTF_DROPPED_IMAGES_INLINE));
	EXPECT_EQ(42, settings->get(SettingsManager::RTF_TEMP_SHARE_LIMIT));
	File::deleteFile(path);
}
