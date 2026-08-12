/*
 * Copyright (C) 2001-2025 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef DCPLUSPLUS_DCPP_SHARE_MANAGER_H
#define DCPLUSPLUS_DCPP_SHARE_MANAGER_H

#include <atomic>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "TimerManager.h"
#include "SearchManager.h"
#include "SettingsManager.h"
#include "HashManagerListener.h"
#include "QueueManagerListener.h"

#include "Exception.h"
#include "CriticalSection.h"
#include "StringSearch.h"
#include "Singleton.h"
#include "BloomFilter.h"
#include "FastAlloc.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "StringMatch.h"

namespace dcpp {

using std::function;
using std::map;
using std::nullopt;
using std::optional;
using std::set;
using std::unique_ptr;
using std::unordered_map;

STANDARD_EXCEPTION(ShareException);

class SimpleXML;
class Client;
class File;
class OutputStream;
class MemoryInputStream;
class SQLiteDB;
class SQLiteStatement;

struct ShareLoader;
class ShareManager : public Singleton<ShareManager>, private SettingsManagerListener, private Thread, private TimerManagerListener,
	private HashManagerListener, private QueueManagerListener
{
public:
	struct TempShareInfo {
		string realPath;
		string hubUrl;
		TTHValue tth;
		int64_t size;
		uint32_t timestamp;
	};
	struct ChatAttachmentInfo {
		string realPath;
		string hubUrl;
		TTHValue tth;
		int64_t size;
		uint32_t timestamp;
		bool temporary;
	};
	struct ChatAttachmentResult {
		uint64_t requestId;
		optional<ChatAttachmentInfo> attachment;
		string error;
	};
	using ChatAttachmentCallback = function<void (ChatAttachmentResult)>;
	struct ResolvedFileInfo {
		string realPath;
		int64_t size;
		optional<TTHValue> tth;
		bool temporary;
		uint32_t timestamp;
	};

	/**
	 * @param aDirectory Physical directory location
	 * @param aName Virtual name
	 */
	void addDirectory(const string& realPath, const string &virtualName);
	void removeDirectory(const string& realPath);
	void renameDirectory(const string& realPath, const string& virtualName);

	string toVirtual(const TTHValue& tth) const;
	string toVirtual(const TTHValue& tth, const string& hubUrl) const;
	optional<TTHValue> getTTHFromReal(const string& realPath) noexcept;
	/** Register a transient, TTH-only file for one chat route. Temporary files
	 * are searchable and uploadable only on that route, but never appear in file lists. */
	bool addTempShare(const string& realPath, int64_t size, uint32_t timestamp,
		const TTHValue& tth, const string& hubUrl) noexcept;
	bool isTempShare(const TTHValue& tth, const string& realPath, const string& hubUrl) const noexcept;
	vector<TempShareInfo> getTempShares() const;
	bool removeTempShare(const string& realPath, const TTHValue& tth, const string& hubUrl) noexcept;
	size_t clearTempShares() noexcept;
	/** Prepare a local file as an exact-TTH attachment for one chat route. The
	 * callback may run immediately or on the hashing thread and is called exactly
	 * once unless the request is cancelled. */
	uint64_t prepareChatAttachment(const string& realPath, const string& hubUrl,
		ChatAttachmentCallback callback) noexcept;
	void cancelChatAttachment(uint64_t requestId) noexcept;
	/** Verify that an attachment is currently visible and, for temporary entries,
	 * still matches its saved file snapshot and TTH. */
	bool validateChatAttachment(const TTHValue& tth, int64_t size, const string& hubUrl) const noexcept;
	string toReal(const string& virtualFile);
	string toReal(const string& virtualFile, const string& hubUrl);
	/** @return Actual file path & size. Returns 0 for file lists. */
	pair<string, int64_t> toRealWithSize(const string& virtualFile);
	pair<string, int64_t> toRealWithSize(const string& virtualFile, const string& hubUrl);
	/** Resolve an upload candidate and atomically retain whether it came from the
	 * route-scoped temporary-share table. */
	ResolvedFileInfo resolveFile(const string& virtualFile, const string& hubUrl);
	StringList getRealPaths(const string& virtualPath);
	/** Return all currently shared real paths for a TTH that are visible from the requesting hub. */
	StringList getRealPaths(const TTHValue& tth, const string& hubUrl) const;
	optional<TTHValue> getTTH(const string& virtualFile) const;
	optional<TTHValue> getTTH(const string& virtualFile, const string& hubUrl) const;

	void refresh(bool dirs = false, bool aUpdate = true, bool block = false, function<void (float)> progressF = nullptr) noexcept;
	/** Load a validated cached share tree when possible, then refresh the real filesystem in the background. */
	void startupRefresh(function<void (float)> progressF = nullptr) noexcept;
	/** True while a share refresh is building or applying a live filesystem view. */
	bool isRefreshing() const noexcept { return refreshActive; }
	void setDirty() { xmlDirty = true; }

	SearchResultList search(const StringList& adcParams, size_t maxResults) noexcept;
	SearchResultList search(const StringList& adcParams, size_t maxResults, const string& hubUrl) noexcept;
	SearchResultList search(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults) noexcept;
	SearchResultList search(const string& nmdcString, int searchType, int64_t size, int fileType, size_t maxResults, const string& hubUrl) noexcept;

	StringPairList getDirectories() const noexcept;

	MemoryInputStream* generatePartialList(const string& dir, bool recurse) const;
	MemoryInputStream* generatePartialList(const string& dir, bool recurse, const string& hubUrl) const;
	MemoryInputStream* getTree(const string& virtualFile) const;
	MemoryInputStream* getTree(const string& virtualFile, const string& hubUrl) const;
	MemoryInputStream* generateFileList(const string& hubUrl, bool compressed) const;
	bool hasCustomShare(const string& hubUrl) const;

	AdcCommand getFileInfo(const string& aFile);
	AdcCommand getFileInfo(const string& aFile, const string& hubUrl);

	int64_t getShareSize() const noexcept;
	int64_t getShareSizeForHub(const string& hubUrl) const;
	int64_t getShareSize(const string& realPath) const noexcept;

	size_t getSharedFiles() const noexcept;
	size_t getSharedFiles(const string& hubUrl) const;

	string getShareSizeString() const { return std::to_string(getShareSize()); }
	string getShareSizeString(const string& aDir) const { return std::to_string(getShareSize(aDir)); }

	void getBloom(ByteVector& v, size_t k, size_t m, size_t h) const;
	void getBloom(ByteVector& v, size_t k, size_t m, size_t h, const string& hubUrl) const;

	SearchManager::TypeModes getType(const string& fileName) const noexcept;

	string validateVirtual(const string& /*aVirt*/) const noexcept;
	bool hasVirtual(const string& name) const noexcept;

	void addHits(uint32_t aHits) {
		hits += aHits;
	}

	const string& getOwnListFile() {
		generateXmlList();
		return getBZXmlFile();
	}

	bool isTTHShared(const TTHValue& tth){
		Lock l(cs);
		return tthIndex.find(tth) != tthIndex.end();
	}

	void updateFilterCache();

	GETSET(uint32_t, hits, Hits);
	GETSET(string, bzXmlFile, BZXmlFile);

private:
	struct SearchQuery;

	struct ShareAccess {
		bool unrestricted = false;
		std::set<string> directories;
	};

	class Directory : public FastAlloc<Directory>, public intrusive_ptr_base<Directory> {
	public:
		typedef intrusive_ptr<Directory> Ptr;

		Directory(const Directory&) = delete;
		Directory& operator=(const Directory&) = delete;

		struct File {
			File() : size(0), lastWrite(0), parent(0) { }
			File(const string& aName, int64_t aSize, const Directory::Ptr& aParent, const optional<TTHValue>& aRoot,
				time_t aLastWrite = 0) :
				name(aName), tth(aRoot), size(aSize), lastWrite(aLastWrite), parent(aParent.get()) { }

			bool operator==(const File& rhs) const {
				return getParent() == rhs.getParent() && (Util::stricmp(getName(), rhs.getName()) == 0);
			}

			struct StringComp {
				StringComp(const string& s) : a(s) { }
				bool operator()(const File& b) const { return Util::stricmp(a, b.getName()) == 0; }
				const string& a;
			};
			struct FileLess {
				bool operator()(const File& a, const File& b) const { return (Util::stricmp(a.getName(), b.getName()) < 0); }
			};

			/** Store this file's real path and ensure its virtual name doesn't clash with the names
			of the parent directory's sub-directories or files; rename to "file (N).ext" otherwise.
			@param sourcePath Real path (on the disk) of the directory this file came from. */
			void validateName(const string& sourcePath);

			string getADCPath() const { return parent->getADCPath() + name; }
			string getFullName() const { return parent->getFullName() + name; }
			string getRealPath() const { return realPath ? *realPath : parent->getRealPath(name); }

			GETSET(string, name, Name);
			optional<string> realPath; // Exact path on disk; older cache records may not have this.
			optional<TTHValue> tth;
			GETSET(int64_t, size, Size);
			GETSET(time_t, lastWrite, LastWrite);
			GETSET(Directory*, parent, Parent);
		};

		int64_t size;
		unordered_map<string, Ptr, noCaseStringHash, noCaseStringEq> directories;
		set<File, File::FileLess> files;

		static Ptr create(const string& aName, const Ptr& aParent = Ptr(), time_t aLastWrite = 0) {
			return Ptr(new Directory(aName, aParent, aLastWrite));
		}

		const string& getRealName() const noexcept;
		const optional<string>& getRealNameOverride() const noexcept { return realName; }
		template<typename SetT> void setRealName(SetT&& realName) noexcept { this->realName = std::forward<SetT>(realName); }

		string getADCPath() const noexcept;
		string getFullName() const noexcept;
		string getRealPath(const std::string& path) const;

		/** Check whether the given name would clash with this directory's sub-directories or
		files. */
		bool nameInUse(const string& name) const;

		int64_t getSize() const noexcept;

		void search(SearchResultList& results, SearchQuery& query, size_t maxResults) const noexcept;

		/// @param level -1 to include all levels, or the current level.
		void toXml(OutputStream& xmlFile, string& indent, string& tmp2, int8_t level, bool addFileDates) const;
		void filesToXml(OutputStream& xmlFile, string& indent, string& tmp2, bool addDates) const;

		auto findFile(const string& aFile) const -> decltype(files.cbegin()) { return find_if(files.begin(), files.end(), File::StringComp(aFile)); }

		void merge(const Ptr& source, const string& realPath);

		GETSET(string, name, Name);
		GETSET(Directory*, parent, Parent);
		GETSET(time_t, lastWrite, LastWrite);

	private:
		friend void intrusive_ptr_release(intrusive_ptr_base<Directory>*);

		Directory(const string& aName, const Ptr& aParent, time_t aLastWrite);
		~Directory() { }

		optional<string> realName; // only defined if this directory had to be renamed to avoid duplication.
	};

	friend class Directory;
	friend struct ShareLoader;

	friend class Singleton<ShareManager>;
	ShareManager();

	virtual ~ShareManager();

	struct SearchQuery {
		SearchQuery();
		SearchQuery(const StringList& adcParams);
		SearchQuery(const string& nmdcString, int searchType, int64_t size, int fileType);

		bool isExcluded(const string& str);
		bool hasExt(const string& name);

		StringSearch::List* include;
		StringSearch::List includeInit;
		StringSearch::List exclude;
		StringList ext;
		StringList noExt;

		int64_t gt;
		int64_t lt;

		optional<TTHValue> root;

		bool isDirectory;
	};

	int64_t xmlListLen;
	optional<TTHValue> xmlRoot;
	int64_t bzXmlListLen;
	optional<TTHValue> bzXmlRoot;
	unique_ptr<File> bzXmlRef;

	bool xmlDirty;
	bool forceXmlRefresh; /// bypass the 15-minutes guard
	bool refreshDirs;
	bool update;

	int listN;

	static std::atomic_flag refreshing;
	static std::atomic<bool> refreshActive;

	uint64_t lastXmlUpdate;
	uint64_t lastFullUpdate;

	mutable CriticalSection cs;

	// List of root directory items
	unordered_map<string, Directory::Ptr, noCaseStringHash, noCaseStringEq> directories;

	/** Map real name to virtual name - multiple real names may be mapped to a single virtual one.
	The map is sorted to make sure conflicts are always resolved in the same order when merging. */
	map<string, string> shares;

	unordered_map<TTHValue, const Directory::File*> tthIndex;

	vector<TempShareInfo> tempShares;
	struct PendingChatAttachment {
		uint64_t requestId;
		string realPath;
		string hubUrl;
		int64_t size;
		uint32_t timestamp;
		uint64_t generation;
		uint64_t hashJobId;
		ChatAttachmentCallback callback;
	};
	enum class ChatAttachmentRequestState { READY, CANCELLED, CLEARED };
	mutable CriticalSection chatAttachmentCs;
	unordered_map<uint64_t, PendingChatAttachment> pendingChatAttachments;
	std::unordered_set<uint64_t> activeChatAttachments;
	std::unordered_set<uint64_t> cancelledChatAttachments;
	uint64_t chatAttachmentGeneration = 0;
	std::atomic<uint64_t> nextChatAttachmentRequest { 1 };

	BloomFilter<5> bloom;

	std::list<StringMatch> cachedFilterSkiplistRegEx;
	std::list<StringMatch> cachedFilterSkiplistFileExtensions;
	std::list<StringMatch> cachedFilterSkiplistPaths;

	const Directory::File& findFile(const string& virtualFile) const;
	const TempShareInfo* findTempShare(const TTHValue& tth, const string* hubUrl = nullptr) const noexcept;
	bool addValidatedTempShare(const string& realPath, int64_t size, uint32_t timestamp,
		const TTHValue& tth, const string& hubUrl) noexcept;
	void finishChatAttachment(PendingChatAttachment request, const TTHValue& tth) noexcept;
	void failChatAttachments(const string& realPath, int64_t size, uint32_t timestamp,
		uint64_t hashJobId, const string& error) noexcept;
	void completeChatAttachments(const string& realPath, const TTHValue& tth,
		int64_t size, uint32_t timestamp, uint64_t hashJobId) noexcept;
	static void invokeChatAttachmentCallback(PendingChatAttachment&& request,
		optional<ChatAttachmentInfo> attachment, string error) noexcept;
	void deliverChatAttachmentCallback(PendingChatAttachment&& request,
		optional<ChatAttachmentInfo> attachment, string error) noexcept;
	ChatAttachmentRequestState getChatAttachmentRequestState(uint64_t requestId,
		uint64_t generation) const noexcept;
	ChatAttachmentRequestState beginChatAttachmentCallback(uint64_t requestId,
		uint64_t generation) noexcept;
	ShareAccess getShareAccess(const string& hubUrl) const;
	bool isVirtualAllowed(const string& virtualName, const ShareAccess& access) const;
	bool isFileAllowed(const Directory::File& file, const ShareAccess& access) const;
	string generateFileListData(const string& hubUrl, bool compressed) const;

	Directory::Ptr buildTree(const string& realPath, optional<std::reference_wrapper<const string>> dirName = nullopt,
		const Directory::Ptr& parent = nullptr, time_t lastWrite = 0);
	bool checkHidden(const string& realPath) const;
	bool checkInvalidFileName(const string& realPath) const;
	bool checkInvalidPaths(const string& realPath) const;
	bool checkInvalidFileSize(uint64_t size) const;
	bool checkRegEx(const StringMatch& matcher, const string& match) const;

	void updateFilterCache(const std::string& strSetting, std::list<StringMatch>& lst);
	void updateFilterCache(const std::string& strSetting, const std::string& strExtraPattern, bool escapeDot, std::list<StringMatch>& lst);

	/** Rebuild TTH and bloom indexes after replacing the share tree; expectedFiles pre-sizes large TTH maps. */
	void rebuildIndices(size_t expectedFiles = 0);

	void updateIndices(Directory& aDirectory);
	void updateIndices(Directory& dir, const decltype(std::declval<Directory>().files.begin())& i);

	void merge(const Directory::Ptr& directory, const string& realPath);

	void generateXmlList();
	pair<Directory::Ptr, string> splitVirtual(const string& virtualPath) const;
	string findRealRoot(const string& virtualRoot, const string& virtualLeaf) const;

	SearchResultList search(SearchQuery&& query, size_t maxResults) noexcept;
	SearchResultList search(SearchQuery&& query, size_t maxResults,
		const ShareAccess& access, const string& hubUrl) noexcept;
	void appendTempSearchResults(SearchResultList& results, SearchQuery& query,
		size_t maxResults, const string& hubUrl) noexcept;

	/** Get the directory pointer corresponding to a given real path (on disk). Note that only
	directories are considered here but not the file's base name. */
	Directory::Ptr getDirectory(const string& realPath) noexcept;
	/** Get the file corresponding to a given real path (on disk). */
	optional<std::reference_wrapper<const ShareManager::Directory::File>> getFile(const string& realPath, Directory::Ptr d = nullptr) noexcept;

	virtual int run();
	void runRefresh(function<void (float)> progressF = nullptr);

	/** Return the per-profile SQLite snapshot path for cached share metadata. */
	string getShareCacheFile() const;
	/** Hash the configured shares and share-affecting settings used to validate a snapshot. */
	string getShareCacheFingerprint() const;
	/** Try to load a complete, validated share snapshot without changing state on failure. */
	bool loadShareCache() noexcept;
	/** Persist the current in-memory share tree as one transaction for the next startup. */
	void saveShareCache() noexcept;
	/** Create or upgrade the share-cache schema; destructive migrations should use a new schema version. */
	void createShareCacheSchema(SQLiteDB& db);
	/** Recursively write one directory subtree using stable parent ids for fast parent-ordered loading. */
	void saveShareCacheDirectory(SQLiteStatement& dirStmt, SQLiteStatement& fileStmt, const Directory& dir,
		optional<int64_t> parentId, int64_t& nextId, uint64_t& directoryCount, uint64_t& fileCount) const;

	// QueueManagerListener
	virtual void on(QueueManagerListener::FileMoved, const string& realPath) noexcept;

	// HashManagerListener
	virtual void on(HashManagerListener::TTHDone, const string& realPath, const TTHValue& root,
		int64_t size, uint32_t timestamp, uint64_t hashJobId) noexcept;
	virtual void on(HashManagerListener::TTHFailed, const string& realPath, int64_t size,
		uint32_t timestamp, uint64_t hashJobId, HashManagerListener::Failure reason,
		const string& detail) noexcept;

	// SettingsManagerListener
	virtual void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
		save(xml);
	}
	virtual void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
		load(xml);
	}

	// TimerManagerListener
	virtual void on(TimerManagerListener::Minute, uint64_t tick) noexcept;
	void load(SimpleXML& aXml);
	void save(SimpleXML& aXml);

};

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
