#include "testbase.h"

#include <filesystem>

#include <dcpp/DirectoryListing.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/User.h>

using namespace dcpp;

namespace {

class DirectoryListingTest : public ::testing::Test {
public:
	void SetUp() override {
		SettingsManager::newInstance();
		user = UserPtr(new User(CID()));
	}

	void TearDown() override {
		SettingsManager::deleteInstance();
	}

	DirectoryListing::Directory* findDirectory(DirectoryListing::Directory* parent, const string& name) {
		auto item = std::find(parent->directories.begin(), parent->directories.end(), name);
		return item == parent->directories.end() ? nullptr : *item;
	}

	DirectoryListing::File* findFile(DirectoryListing::Directory* parent, const string& name) {
		auto item = std::find(parent->files.begin(), parent->files.end(), name);
		return item == parent->files.end() ? nullptr : *item;
	}

	HintedUser hintedUser() const {
		return HintedUser(user, "adc://example.invalid");
	}

	UserPtr user;
};

const string TEST_TTH = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

}

TEST_F(DirectoryListingTest, parses_extended_dates_sizes_and_content_counts) {
	DirectoryListing listing(hintedUser());
	const string xml =
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\" BaseDate=\"1700000000\">"
		"<Directory Name=\"Large\" Date=\"1700000100\" Size=\"987654321\" Directories=\"12\" Files=\"34\" Children=\"1\" Incomplete=\"1\"/>"
		"<File Name=\"dated.bin\" Size=\"42\" TTH=\"" + TEST_TTH + "\" Date=\"1700000200\"/>"
		"</FileListing>";

	EXPECT_EQ("/Share/", listing.updateXML(xml));
	auto share = findDirectory(listing.getRoot(), "Share");
	ASSERT_NE(nullptr, share);
	EXPECT_EQ(1700000000, share->getRemoteDate());

	auto large = findDirectory(share, "Large");
	ASSERT_NE(nullptr, large);
	EXPECT_FALSE(large->getComplete());
	EXPECT_EQ(1700000100, large->getRemoteDate());
	EXPECT_EQ(987654321, large->getRemoteSize());
	EXPECT_EQ(12, large->getRemoteDirectories());
	EXPECT_EQ(34, large->getRemoteFiles());
	EXPECT_TRUE(large->getHasChildren());

	auto file = findFile(share, "dated.bin");
	ASSERT_NE(nullptr, file);
	EXPECT_EQ(1700000200, file->getRemoteDate());
	EXPECT_FALSE(listing.isComplete("Share\\"));
}

TEST_F(DirectoryListingTest, rejects_invalid_or_out_of_range_extended_values) {
	DirectoryListing listing(hintedUser());
	const string xml =
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/\" BaseDate=\"-1\">"
		"<Directory Name=\"Invalid\" Date=\"not-a-date\" Size=\"18446744073709551615\" Directories=\"-2\" Files=\"999999999999999999999\"/>"
		"<File Name=\"invalid.bin\" Size=\"1\" TTH=\"" + TEST_TTH + "\" Date=\"18446744073709551615\"/>"
		"</FileListing>";

	listing.updateXML(xml);
	auto invalid = findDirectory(listing.getRoot(), "Invalid");
	ASSERT_NE(nullptr, invalid);
	EXPECT_EQ(0, invalid->getRemoteDate());
	EXPECT_EQ(-1, invalid->getRemoteSize());
	EXPECT_EQ(-1, invalid->getRemoteDirectories());
	EXPECT_EQ(-1, invalid->getRemoteFiles());

	auto file = findFile(listing.getRoot(), "invalid.bin");
	ASSERT_NE(nullptr, file);
	EXPECT_EQ(0, file->getRemoteDate());
}

TEST_F(DirectoryListingTest, partial_updates_and_adl_copies_preserve_dates) {
	DirectoryListing listing(hintedUser());
	listing.updateXML(
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\" BaseDate=\"1700000000\">"
		"<File Name=\"dated.bin\" Size=\"42\" TTH=\"" + TEST_TTH + "\" Date=\"1700000200\"/>"
		"</FileListing>");
	listing.updateXML(
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\" BaseDate=\"1700000300\">"
		"<Directory Name=\"Metadata\" Date=\"1700000350\" Size=\"123\" Directories=\"1\" Files=\"2\" Children=\"1\" Incomplete=\"1\"/>"
		"<File Name=\"dated.bin\" Size=\"43\" TTH=\"" + TEST_TTH + "\" Date=\"1700000400\"/>"
		"</FileListing>");
	listing.updateXML(
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\">"
		"<Directory Name=\"Metadata\" Incomplete=\"1\"/>"
		"<File Name=\"dated.bin\" Size=\"44\" TTH=\"" + TEST_TTH + "\"/>"
		"</FileListing>");

	auto share = findDirectory(listing.getRoot(), "Share");
	ASSERT_NE(nullptr, share);
	EXPECT_EQ(1700000300, share->getRemoteDate());
	auto file = findFile(share, "dated.bin");
	ASSERT_NE(nullptr, file);
	EXPECT_EQ(44, file->getSize());
	EXPECT_EQ(1700000400, file->getRemoteDate());
	auto metadata = findDirectory(share, "Metadata");
	ASSERT_NE(nullptr, metadata);
	EXPECT_EQ(1700000350, metadata->getRemoteDate());
	EXPECT_EQ(123, metadata->getRemoteSize());
	EXPECT_EQ(1, metadata->getRemoteDirectories());
	EXPECT_EQ(2, metadata->getRemoteFiles());
	EXPECT_TRUE(metadata->getHasChildren());

	DirectoryListing::File adlCopy(*file, true);
	EXPECT_EQ(1700000400, adlCopy.getRemoteDate());
}

TEST_F(DirectoryListingTest, saved_partial_lists_round_trip_extended_metadata) {
	DirectoryListing listing(hintedUser());
	listing.updateXML(
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\" BaseDate=\"1700000000\">"
		"<Directory Name=\"Large\" Date=\"1700000100\" Size=\"1234\" Directories=\"2\" Files=\"3\" Children=\"1\" Incomplete=\"1\"/>"
		"<File Name=\"dated.bin\" Size=\"42\" TTH=\"" + TEST_TTH + "\" Date=\"1700000200\"/>"
		"</FileListing>");

	const auto path = (std::filesystem::temp_directory_path() / "dcpp-extended-filelist-test.xml").string();
	listing.save(path);

	DirectoryListing reloaded(hintedUser());
	reloaded.loadFile(path);
	std::filesystem::remove(path);

	auto share = findDirectory(reloaded.getRoot(), "Share");
	ASSERT_NE(nullptr, share);
	EXPECT_EQ(1700000000, share->getRemoteDate());
	auto large = findDirectory(share, "Large");
	ASSERT_NE(nullptr, large);
	EXPECT_EQ(1700000100, large->getRemoteDate());
	EXPECT_EQ(1234, large->getRemoteSize());
	EXPECT_EQ(2, large->getRemoteDirectories());
	EXPECT_EQ(3, large->getRemoteFiles());
	EXPECT_TRUE(large->getHasChildren());
	auto file = findFile(share, "dated.bin");
	ASSERT_NE(nullptr, file);
	EXPECT_EQ(1700000200, file->getRemoteDate());
}

TEST_F(DirectoryListingTest, recursive_completeness_checks_all_descendants) {
	DirectoryListing listing(hintedUser());
	listing.updateXML(
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\">"
		"<Directory Name=\"Complete\"><File Name=\"ok.bin\" Size=\"1\" TTH=\"" + TEST_TTH + "\"/></Directory>"
		"</FileListing>");
	EXPECT_TRUE(listing.isComplete("Share\\"));

	listing.updateXML(
		"<FileListing Version=\"1\" CID=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\" Base=\"/Share/\">"
		"<Directory Name=\"Incomplete\" Incomplete=\"1\"/>"
		"</FileListing>");
	EXPECT_FALSE(listing.isComplete("Share\\"));
}
