/* Copyright (C) 2026 iceman50 */

#include "testbase.h"

#include <dcpp/Archive.h>
#include <dcpp/File.h>
#include <dcpp/HashValue.h>
#include <dcpp/TigerHash.h>

using namespace dcpp;

TEST(testarchive, test_archive)
{
	File::deleteFile("test/data/out/gtest.h");

	try {
		Archive("test/data/gtest_h.zip").extract("test/data/out/");

	} catch(const Exception& e) {
		FAIL() << e.getError();
	}

	auto tiger = [](string path) {
		File f(path, File::READ, File::OPEN);
		TigerHash h;
		auto buf = f.read();
		h.update(buf.c_str(), buf.size());
		return TTHValue(h.finalize());
	};

	ASSERT_EQ(tiger("test/gtest.h"), tiger("test/data/out/gtest.h"));
}

TEST(testarchive, reads_a_bounded_entry_without_extracting_the_archive)
{
	const auto expected = File("test/gtest.h", File::READ, File::OPEN).read();
	EXPECT_EQ(expected, Archive("test/data/gtest_h.zip").readFile("gtest.h", expected.size()));
	EXPECT_THROW(Archive("test/data/gtest_h.zip").readFile("gtest.h", 1), Exception);
	EXPECT_THROW(Archive("test/data/gtest_h.zip").readFile("missing", 1024), Exception);
}
