/*
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "testbase.h"
#include <dcpp/FilteredFile.h>
#include <dcpp/ZstdUtils.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/Download.h>
#include <dcpp/QueueItem.h>
#include <dcpp/File.h>
#include <filesystem>
#define private public
#include <dcpp/UserConnection.h>
#undef private

using namespace dcpp;
namespace {
string encode(const string& data) {
	MemoryInputStream input(data);
	FilteredInputStream<ZstdFilter, false> compressor(&input);
	string result;
	char buffer[137];
	for(;;) {
		size_t n = sizeof(buffer);
		auto produced = compressor.read(buffer, n);
		if(!produced) break;
		result.append(buffer, produced);
	}
	return result;
}
string decode(const string& data) {
	StringOutputStream result;
	FilteredOutputStream<UnZstdFilter, false> decoder(&result);
	for(size_t pos = 0; pos < data.size(); pos += 7) decoder.write(data.data() + pos, std::min<size_t>(7, data.size() - pos));
	decoder.flush();
	return result.getString();
}
class ZstdNegotiationTest : public ::testing::Test {
	void SetUp() override { SettingsManager::newInstance(); }
	void TearDown() override { SettingsManager::deleteInstance(); }
};
}

TEST(ZstdStreams, handles_empty_fragmented_and_large_streams) {
	EXPECT_EQ(string(), decode(encode("")));
	string data;
	uint32_t seed = 1;
	for(size_t i = 0; i < 2 * 1024 * 1024; ++i) {
		seed = seed * 1664525 + 1013904223;
		data += static_cast<char>(seed >> 24);
	}
	EXPECT_EQ(data, decode(encode(data)));
	EXPECT_EQ(string(1000000, 'x'), decode(encode(string(1000000, 'x'))));
}

TEST(ZstdStreams, rejects_truncation_checksum_damage_and_trailing_data) {
	auto frame = encode("hello world");
	EXPECT_THROW(decode(frame.substr(0, frame.size() - 1)), Exception);
	EXPECT_THROW(decode("garbage"), Exception);
	EXPECT_THROW(decode(frame + "trailing"), Exception);
	EXPECT_THROW(decode(frame + frame), Exception);
	frame.back() ^= 1;
	EXPECT_THROW(decode(frame), Exception);
}

TEST(ZstdStreams, enforces_decompressed_output_limit) {
	StringOutputStream result;
	LimitedOutputStream<false> bounded(&result, 5);
	FilteredOutputStream<UnZstdFilter, false> decoder(&bounded);
	EXPECT_THROW(decoder.write(encode(string(100000, 'x'))), Exception);
}

TEST(ZstdStreams, rejects_skippable_frames_and_oversized_windows) {
	EXPECT_THROW(decode(string("\x50\x2a\x4d\x18\0\0\0\0", 8)), Exception);
	// Standard frame header advertising a 128 MiB window, above ZST1's 8 MiB cap.
	EXPECT_THROW(decode(string("\x28\xb5\x2f\xfd\0\x88\x01\0\0", 9)), Exception);
}

TEST_F(ZstdNegotiationTest, preserves_legacy_defaults_and_selects_only_supported_compression) {
	UserConnection connection(false);
	QueueItem item("test-list", -1, QueueItem::NORMAL, QueueItem::FLAG_USER_LIST, 0, TTHValue());
	Download download(connection, item);
	download.setFlag(Download::FLAG_XML_BZ_LIST);
	auto cmd = download.getCommand(true, true);
	EXPECT_EQ("files.xml.bz2", cmd.getParam(1));
	EXPECT_FALSE(cmd.hasFlag("ZS", 4));
	EXPECT_FALSE(cmd.hasFlag("ZL", 4));
	SettingsManager::getInstance()->set(SettingsManager::PREFER_ZSTD, true);
	cmd = download.getCommand(true, false);
	EXPECT_EQ("files.xml.bz2", cmd.getParam(1));
	EXPECT_FALSE(cmd.hasFlag("ZS", 4));
	cmd = download.getCommand(true, true);
	EXPECT_EQ("files.xml", cmd.getParam(1));
	EXPECT_TRUE(cmd.hasFlag("ZS", 4));
	EXPECT_FALSE(cmd.hasFlag("ZL", 4));
	EXPECT_EQ("CGET file files.xml 0 -1 ZS1\n", cmd.toString(0));
	EXPECT_EQ("$ADCGET file files.xml 0 -1 ZS1|", cmd.toString(0, true));
	SettingsManager::getInstance()->set(SettingsManager::COMPRESS_TRANSFERS, false);
	EXPECT_FALSE(download.getCommand(true, true).hasFlag("ZS", 4));
}

TEST_F(ZstdNegotiationTest, partial_lists_fall_back_to_zlib) {
	UserConnection connection(false);
	QueueItem item("test-list", -1, QueueItem::NORMAL, QueueItem::FLAG_USER_LIST | QueueItem::FLAG_PARTIAL_LIST, 0, TTHValue());
	item.setTempTarget("Share\\");
	Download download(connection, item);
	EXPECT_TRUE(download.getCommand(true, true).hasFlag("ZL", 4));
	SettingsManager::getInstance()->set(SettingsManager::PREFER_ZSTD, true);
	EXPECT_TRUE(download.getCommand(true, true).hasFlag("ZS", 4));
	EXPECT_TRUE(download.getCommand(true, false).hasFlag("ZL", 4));
	EXPECT_TRUE(download.getCommand(false, true).hasFlag("ZS", 4));
	EXPECT_FALSE(download.getCommand(false, false).hasFlag("ZS", 4));
}

TEST_F(ZstdNegotiationTest, full_list_retains_frame_and_counts_decoded_bytes) {
	UserConnection connection(false);
	const auto path = (std::filesystem::temp_directory_path() / ("dcpp-zstd-" + CID::generate().toBase32())).string();
	QueueItem item(path, -1, QueueItem::NORMAL, QueueItem::FLAG_USER_LIST, 0, TTHValue());
	Download download(connection, item);
	const string xml = "<?xml version=\"1.0\"?><FileListing Base=\"/\"/>";
	const auto compressed = encode(xml);
	download.open(xml.size(), false, true);
	size_t total = 0;
	for(size_t pos = 0; pos < compressed.size(); ++pos) total += download.getOutput()->write(compressed.data() + pos, 1);
	EXPECT_EQ(xml.size(), total);
	EXPECT_TRUE(download.getOutput()->eof());
	download.getOutput()->flush(); download.close();
	EXPECT_TRUE(download.hasValidFileListSignature());
	EXPECT_TRUE(download.isSet(Download::FLAG_XML_ZST_LIST));
	EXPECT_EQ(compressed, File(path + ".xml.zst", File::READ, File::OPEN).read());
	std::filesystem::remove(path + ".xml.zst");
}
