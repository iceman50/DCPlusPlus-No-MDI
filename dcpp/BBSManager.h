/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_BBS_MANAGER_H
#define DCPLUSPLUS_DCPP_BBS_MANAGER_H

#include <map>
#include <optional>
#include <unordered_map>

#include "CriticalSection.h"
#include "MerkleTree.h"
#include "QueueManagerListener.h"
#include "SearchManagerListener.h"
#include "SettingsManager.h"
#include "Singleton.h"
#include "Speaker.h"

namespace dcpp {

struct BBSBoard {
	string name;
	string title;
	string description;
	uint32_t permissions = 0;
	int64_t maxSize = 0;
	uint64_t newest = 0;
	uint64_t oldest = 0;
	int64_t postCount = -1;
	uint64_t cursor = 0;
	bool gap = false;
	bool subscribed = false;

	bool canSubscribe() const noexcept { return (permissions & 1) != 0; }
	bool canPost() const noexcept { return (permissions & 2) != 0; }
	bool canReply() const noexcept { return (permissions & 4) != 0; }
	bool canWithdrawOwn() const noexcept { return (permissions & 8) != 0; }
	bool canWithdrawAny() const noexcept { return (permissions & 16) != 0; }
};

struct BBSEntry {
	string tth;
	int64_t size = -1;
	string board;
	string authorId;
	string nick;
	string parent;
	string thread;
	string subject;
	uint64_t timestamp = 0;
	bool withdrawn = false;
};

struct BBSDocument {
	string tth;
	string authorId;
	string parent;
	string subject;
	uint64_t composed = 0;
	int richText = 0;
	string body;
	string path;
	int64_t size = 0;
};

class BBSManagerListener {
public:
	virtual ~BBSManagerListener() { }
	template<int I> struct X { enum { TYPE = I }; };

	typedef X<0> BoardUpdated;
	typedef X<1> EntryUpdated;
	typedef X<2> DocumentUpdated;
	typedef X<3> Status;

	virtual void on(BoardUpdated, const string&, const string&) noexcept { }
	virtual void on(EntryUpdated, const string&, const string&, const string&) noexcept { }
	virtual void on(DocumentUpdated, const string&, const string&, const string&) noexcept { }
	virtual void on(Status, const string&, const string&) noexcept { }
};

/** Client-side implementation of the ADC BBS0 index and post-document cache. */
class BBSManager : public Singleton<BBSManager>, public Speaker<BBSManagerListener>,
	private QueueManagerListener, private SearchManagerListener, private SettingsManagerListener
{
public:
	static constexpr int64_t MAX_DOCUMENT_SIZE = 4 * 1024 * 1024;
	static constexpr size_t MAX_HEADER_SIZE = 8192;

	void setHubSupported(const string& hubUrl, bool supported) noexcept;
	bool isHubSupported(const string& hubUrl) const noexcept;

	/** Store a descriptor. Returns true when the caller should establish/replace a subscription. */
	bool updateBoard(const string& hubUrl, BBSBoard board) noexcept;
	void removeBoard(const string& hubUrl, const string& board) noexcept;
	void setSubscribed(const string& hubUrl, const string& board, bool subscribed) noexcept;
	uint64_t getResumeTimestamp(const string& hubUrl, const string& board) const noexcept;

	/** Apply BBS0 timestamp collision/deduplication rules. */
	bool updateEntry(const string& hubUrl, BBSEntry entry) noexcept;

	std::vector<BBSBoard> getBoards(const string& hubUrl) const;
	std::vector<BBSEntry> getEntries(const string& hubUrl, const string& board,
		bool includeWithdrawn = false) const;
	std::optional<BBSBoard> getBoard(const string& hubUrl, const string& board) const;
	std::optional<BBSEntry> getEntry(const string& hubUrl, const string& board,
		const string& tth) const;
	std::optional<BBSDocument> getDocument(const string& tth) const;

	/** Compose, hash, cache and expose a post before HBBP is sent. */
	bool preparePost(const string& hubUrl, const string& authorId, const string& parent,
		const string& subject, const string& body, bool richText, int64_t boardLimit,
		BBSDocument& document, string& error) noexcept;

	/** Fetch on explicit user action. No body is fetched merely because an entry arrived. */
	bool requestDocument(const string& hubUrl, const string& board, const string& tth,
		string& error) noexcept;

	void reportStatus(const string& hubUrl, const string& message) noexcept;

	static bool validBoardName(const string& value) noexcept;
	static string sanitizeDisplayText(const string& value, size_t maxBytes = 512);
	static bool composeDocument(const string& authorId, const string& parent,
		const string& subject, const string& body, bool richText, uint64_t composed,
		string& raw, BBSDocument& document, string& error) noexcept;
	static bool parseDocument(const string& raw, const string& expectedTTH,
		BBSDocument& document, string& error) noexcept;

private:
	friend class Singleton<BBSManager>;

	struct BoardState {
		BBSBoard descriptor;
		std::unordered_map<string, BBSEntry> entries;
	};

	struct HubState {
		bool supported = false;
		std::map<string, BoardState> boards;
	};

	struct PendingRequest {
		string hubUrl;
		string board;
		int64_t size = -1;
		uint64_t lastSearch = 0;
	};

	BBSManager();
	~BBSManager();

	mutable CriticalSection cs;
	std::unordered_map<string, HubState> hubs;
	std::unordered_map<string, BBSDocument> documents;
	std::unordered_map<string, std::vector<PendingRequest>> pending;

	string getCachePath(const string& tth) const;
	bool loadCachedDocument(const string& hubUrl, const string& board, const BBSEntry& entry,
		BBSDocument& document, string& error) noexcept;
	bool cacheRawDocument(const string& raw, BBSDocument& document, string& error) noexcept;
	void registerDocument(const string& hubUrl, const BBSDocument& document) noexcept;
	void completeDocument(const string& tth, const string& path) noexcept;
	void searchFor(const PendingRequest& request, const string& tth) noexcept;
	bool hasLiveReference(const string& hubUrl, const string& tth) const noexcept;

	void on(QueueManagerListener::Finished, QueueItem*, const string&, int64_t) noexcept override;
	void on(SearchManagerListener::SR, const SearchResultPtr&) noexcept override;
	void on(SettingsManagerListener::Load, SimpleXML&) noexcept override;
	void on(SettingsManagerListener::Save, SimpleXML&) noexcept override;
};

} // namespace dcpp

#endif // DCPLUSPLUS_DCPP_BBS_MANAGER_H
