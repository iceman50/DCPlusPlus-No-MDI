#include "testbase.h"

#include <dcpp/File.h>
#include <dcpp/FileReader.h>
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
			current += PATH_SEPARATOR_STR "segment-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
			directories.push_back(current);
		}

		file = current + PATH_SEPARATOR_STR "hash-me.txt";
	}

	~LongPathFixture() {
		File::deleteFile(file);
		for(auto i = directories.rbegin(); i != directories.rend(); ++i) {
			::RemoveDirectory(File::toNativePath(*i).c_str());
		}
	}

	string file;
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

#endif
