#include "testbase.h"

#include <dcpp/Archive.h>
#include <dcpp/File.h>
#include <dcpp/FileReader.h>
#include <dcpp/SQLiteDB.h>
#include <dcpp/Text.h>
#include <dcpp/Util.h>

using namespace dcpp;

#ifdef _WIN32

namespace {

struct LongPathFixture {
	LongPathFixture() {
		auto current = Util::getTempPath() + "dcpp-long-path-" +
			std::to_string(::GetCurrentProcessId()) + '-' + std::to_string(::GetTickCount64());
		directories.push_back(current);

		while(current.size() < MAX_PATH + 32) {
			current += PATH_SEPARATOR_STR "segment-\xE6\xB5\x8B\xE8\xAF\x95-xxxxxxxxxxxxxxxxxxxxxxxx";
			directories.push_back(current);
		}

		directory = current;
		file = current + PATH_SEPARATOR_STR "hash-me.txt";
	}

	~LongPathFixture() {
		File::deleteFile(file);
		for(auto i = directories.rbegin(); i != directories.rend(); ++i) {
			::RemoveDirectory(File::toNativePath(*i).c_str());
		}
	}

	string file;
	string directory;
	StringList directories;
};

}

TEST(FileTest, nativePathUsesExtendedSyntaxForLongAbsolutePaths) {
	const string tail(MAX_PATH, 'x');
	const auto drivePath = "C:\\" + tail;
	const auto uncPath = "\\\\server\\share\\" + tail;

	EXPECT_EQ(L"\\\\?\\" + Text::toT(drivePath), File::toNativePath(drivePath));
	EXPECT_EQ(L"\\\\?\\UNC\\server\\share\\" + Text::toT(tail), File::toNativePath(uncPath));
	EXPECT_EQ(Text::toT("relative.txt"), File::toNativePath("relative.txt"));
}

TEST(FileTest, readsAndEnumeratesLongPaths) {
	LongPathFixture fixture;
	const string expected = "long-path hashing data";

	File::ensureDirectory(fixture.file);
	File(fixture.file, File::WRITE, File::CREATE | File::TRUNCATE).write(expected);

	EXPECT_TRUE(File::isDirectory(fixture.directory));
	EXPECT_EQ(static_cast<int64_t>(expected.size()), File::getSize(fixture.file));
	FileFindIter found(fixture.file);
	ASSERT_TRUE(found != FileFindIter());
	EXPECT_EQ("hash-me.txt", found->getFileName());

	string actual;
	const auto bytes = FileReader(true).read(fixture.file, [&](const void* data, size_t size) {
		actual.append(static_cast<const char*>(data), size);
		return true;
	});

	EXPECT_EQ(expected.size(), bytes);
	EXPECT_EQ(expected, actual);
}

TEST(FileTest, opensLongPathsInIntegratedFileConsumers) {
	LongPathFixture fixture;
	File::ensureDirectory(fixture.file);

	const auto database = fixture.directory + PATH_SEPARATOR_STR "LongPath.sqlite3";
	{
		SQLiteDB db(database);
		db.execute("CREATE TABLE test(value INTEGER); INSERT INTO test VALUES(1);");
		auto statement = db.prepare("SELECT value FROM test");
		ASSERT_TRUE(statement.step());
		EXPECT_EQ(1, statement.columnInt(0));
	}
	File::deleteFile(database);
	File::deleteFile(database + "-shm");
	File::deleteFile(database + "-wal");

	const auto archive = fixture.directory + PATH_SEPARATOR_STR "test.zip";
	const auto outputDirectory = fixture.directory + PATH_SEPARATOR_STR "archive-out" PATH_SEPARATOR_STR;
	const auto extracted = outputDirectory + "gtest.h";
	File::copyFile("test/data/gtest_h.zip", archive);
	Archive(archive).extract(outputDirectory);
	EXPECT_GT(File::getSize(extracted), 0);

	File::deleteFile(extracted);
	File::deleteFile(archive);
	::RemoveDirectory(File::toNativePath(outputDirectory.substr(0, outputDirectory.size() - 1)).c_str());
}

#endif
