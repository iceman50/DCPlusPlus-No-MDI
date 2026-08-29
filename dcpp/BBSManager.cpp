/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 * Copyright (C) 2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "BBSManager.h"

#include "AdcCommand.h"
#include "Encoder.h"
#include "File.h"
#include "HashManager.h"
#include "QueueItem.h"
#include "QueueManager.h"
#include "SearchManager.h"
#include "SearchResult.h"
#include "ShareManager.h"
#include "SimpleXML.h"
#include "Text.h"
#include "Util.h"

#include <limits>
#include <set>

namespace dcpp {

namespace {

constexpr size_t MAX_INDEX_ENTRIES_PER_BOARD = 10000;

bool isBase32(const string& value) noexcept {
	return !value.empty() && Encoder::isBase32(value);
}

bool parseNonNegative(const string& value, uint64_t& result) noexcept {
	if(value.empty()) return false;
	uint64_t parsed = 0;
	for(const auto ch: value) {
		if(ch < '0' || ch > '9') return false;
		const auto digit = static_cast<uint64_t>(ch - '0');
		if(parsed > (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - digit) / 10) return false;
		parsed = parsed * 10 + digit;
	}
	result = parsed;
	return true;
}

string hashDocument(const string& raw) {
	const auto size = static_cast<int64_t>(raw.size());
	TigerTree tree(std::max(TigerTree::calcBlockSize(size, 10), HashManager::MIN_BLOCK_SIZE));
	tree.update(raw.data(), raw.size());
	tree.finalize();
	return tree.getRoot().toBase32();
}

int fieldOrder(const string& code) noexcept {
	if(code == "ID") return 0;
	if(code == "PA") return 1;
	if(code == "SJ") return 2;
	if(code == "DA") return 3;
	if(code == "RT") return 4;
	return -1;
}

string normalizeNFC(const string& value) {
#ifdef _WIN32
	using NormalizeStringF = int (WINAPI *)(NORM_FORM, LPCWSTR, int, LPWSTR, int);
	static const auto normalize = [] {
		auto module = ::LoadLibraryW(L"Normaliz.dll");
		NormalizeStringF result = nullptr;
		if(module) {
			auto address = ::GetProcAddress(module, "NormalizeString");
			static_assert(sizeof(result) == sizeof(address), "Windows function pointers must fit in FARPROC");
			memcpy(&result, &address, sizeof(result));
		}
		return result;
	}();
	if(normalize) {
		const auto wide = Text::toT(value);
		const auto needed = normalize(NormalizationC, wide.data(), static_cast<int>(wide.size()), nullptr, 0);
		if(needed > 0) {
			wstring normalized(static_cast<size_t>(needed), L'\0');
			const auto written = normalize(NormalizationC, wide.data(), static_cast<int>(wide.size()),
				normalized.data(), needed);
			if(written > 0) {
				normalized.resize(static_cast<size_t>(written));
				return Text::fromT(normalized);
			}
		}
	}
#endif
	return value;
}

}

BBSManager::BBSManager() {
	QueueManager::getInstance()->addListener(this);
	SearchManager::getInstance()->addListener(this);
	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
}

BBSManager::~BBSManager() {
	TimerManager::getInstance()->removeListener(this);
	SettingsManager::getInstance()->removeListener(this);
	SearchManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
}

bool BBSManager::validBoardName(const string& value) noexcept {
	if(value.empty() || value.size() > 64) return false;
	return std::all_of(value.begin(), value.end(), [](char ch) {
		return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_';
	});
}

string BBSManager::sanitizeDisplayText(const string& value, size_t maxBytes) {
	string result;
	result.reserve(std::min(value.size(), maxBytes));
	for(size_t i = 0; i < value.size() && result.size() < maxBytes; ++i) {
		const auto ch = static_cast<uint8_t>(value[i]);
		if(ch < 0x20 || ch == 0x7f) continue;
		// Strip bidi embedding/isolate controls (U+200E/F, U+202A-E, U+2066-9)
		// and the Arabic letter mark (U+061C) from dense, persistent UI labels.
		if(i + 2 < value.size() && ch == 0xe2 && static_cast<uint8_t>(value[i + 1]) == 0x80 &&
			(static_cast<uint8_t>(value[i + 2]) == 0x8e || static_cast<uint8_t>(value[i + 2]) == 0x8f ||
			(static_cast<uint8_t>(value[i + 2]) >= 0xaa && static_cast<uint8_t>(value[i + 2]) <= 0xae))) {
			i += 2;
			continue;
		}
		if(i + 2 < value.size() && ch == 0xe2 && static_cast<uint8_t>(value[i + 1]) == 0x81 &&
			static_cast<uint8_t>(value[i + 2]) >= 0xa6 && static_cast<uint8_t>(value[i + 2]) <= 0xa9) {
			i += 2;
			continue;
		}
		if(i + 1 < value.size() && ch == 0xd8 && static_cast<uint8_t>(value[i + 1]) == 0x9c) {
			++i;
			continue;
		}
		result += value[i];
	}
	while(!Text::validateUtf8(result) && !result.empty()) result.pop_back();
	return result;
}

bool BBSManager::composeDocument(const string& authorId, const string& parent, const string& subject, const string& body, bool richText, uint64_t composed, string& raw, BBSDocument& document, string& error) noexcept {
	try {
		if(authorId.size() != 39 || !isBase32(authorId)) {
			error = _("The BBS post author CID is invalid");
			return false;
		}
		if(!parent.empty() && (parent.size() != 39 || !isBase32(parent))) {
			error = _("The BBS parent hash is invalid");
			return false;
		}
		if(parent.empty() && subject.empty()) {
			error = _("A new BBS thread requires a subject");
			return false;
		}
		const auto normalizedSubject = normalizeNFC(subject);
		if(normalizedSubject.find_first_of("\r\n") != string::npos) {
			error = _("A BBS subject cannot contain a newline");
			return false;
		}
		if(!Text::validateUtf8(subject) || !Text::validateUtf8(body)) {
			error = _("The BBS post is not valid UTF-8");
			return false;
		}

		raw = "IBB0 ID" + authorId;
		if(!parent.empty()) raw += " PA" + parent;
		if(!normalizedSubject.empty()) raw += " SJ" + AdcCommand::escape(normalizedSubject, false);
		raw += " DA" + std::to_string(composed);
		if(richText) raw += " RT1";
		raw += '\n';
		raw += body;

		if(raw.find('\n') + 1 > MAX_HEADER_SIZE || static_cast<int64_t>(raw.size()) > MAX_DOCUMENT_SIZE) {
			error = _("The BBS post exceeds the local safety limit");
			return false;
		}

		document = BBSDocument();
		document.tth = hashDocument(raw);
		document.authorId = authorId;
		document.parent = parent;
		document.subject = normalizedSubject;
		document.composed = composed;
		document.richText = richText ? 1 : 0;
		document.body = body;
		document.size = static_cast<int64_t>(raw.size());
		return true;
	} catch(const Exception& e) {
		error = e.getError();
	} catch(...) {
		error = _("Unable to compose the BBS post");
	}
	return false;
}

bool BBSManager::parseDocument(const string& raw, const string& expectedTTH, BBSDocument& document, string& error) noexcept {
	try {
		if(expectedTTH.size() != 39 || !isBase32(expectedTTH)) {
			error = _("The expected BBS post hash is invalid");
			return false;
		}
		if(static_cast<int64_t>(raw.size()) > MAX_DOCUMENT_SIZE) {
			error = _("The BBS post exceeds the local safety limit");
			return false;
		}
		if(hashDocument(raw) != expectedTTH) {
			error = _("The downloaded BBS post does not match its TTH");
			return false;
		}

		const auto lf = raw.find('\n');
		if(lf == string::npos || lf + 1 > MAX_HEADER_SIZE) {
			error = _("The BBS post header is missing or too large");
			return false;
		}
		const auto headerText = raw.substr(0, lf);
		if(headerText.find('\r') != string::npos) {
			error = _("The BBS post header contains a carriage return");
			return false;
		}

		if(!Text::validateUtf8(headerText)) {
			error = _("The BBS post header is not valid UTF-8");
			return false;
		}
		if(normalizeNFC(headerText) != headerText) {
			error = _("The BBS post header is not in Unicode normalization form C");
			return false;
		}

		AdcCommand header(headerText);
		if(header.getType() != AdcCommand::TYPE_INFO || header.getCommand() != AdcCommand::CMD_BB0 ||
			!header.isValidSyntax())
		{
			error = _("The BBS post header is not a valid IBB0 message");
			return false;
		}

		std::set<string> fields;
		int lastOrder = -1;
		bool unknownSeen = false;
		string canonical = "IBB0";
		BBSDocument parsed;
		parsed.tth = expectedTTH;
		parsed.size = static_cast<int64_t>(raw.size());
		parsed.body = raw.substr(lf + 1);
		for(const auto& parameter: header.getParameters()) {
			if(parameter.size() <= 2) {
				error = _("The BBS post header contains an empty parameter");
				return false;
			}
			const auto code = parameter.substr(0, 2);
			const auto value = parameter.substr(2);
			if(!fields.insert(code).second) {
				error = _("The BBS post header repeats a parameter");
				return false;
			}

			const auto order = fieldOrder(code);
			if(order < 0) {
				unknownSeen = true;
			} else {
				if(unknownSeen || order < lastOrder) {
					error = _("The BBS post header parameters are not in canonical order");
					return false;
				}
				lastOrder = order;
			}

			if(code == "ID") {
				if(value.size() != 39 || !isBase32(value)) {
					error = _("The BBS post contains an invalid author CID");
					return false;
				}
				parsed.authorId = value;
			} else if(code == "PA") {
				if(value.size() != 39 || !isBase32(value)) {
					error = _("The BBS post contains an invalid parent hash");
					return false;
				}
				parsed.parent = value;
			} else if(code == "SJ") {
				if(value.find('\n') != string::npos) {
					error = _("The BBS post subject contains a newline");
					return false;
				}
				parsed.subject = value;
			} else if(code == "DA") {
				if(!parseNonNegative(value, parsed.composed)) {
					error = _("The BBS post contains an invalid composition time");
					return false;
				}
			} else if(code == "RT") {
				uint64_t rt = 0;
				if(!parseNonNegative(value, rt) || rt > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
					error = _("The BBS post contains an invalid text format");
					return false;
				}
				parsed.richText = static_cast<int>(rt);
			}

			canonical += ' ';
			canonical += AdcCommand::escape(parameter, false);
		}

		if(canonical != headerText) {
			error = _("The BBS post header is not in canonical form");
			return false;
		}
		if(parsed.authorId.empty()) {
			error = _("The BBS post has no author CID");
			return false;
		}
		if(parsed.parent.empty() && parsed.subject.empty()) {
			error = _("The BBS thread has no subject");
			return false;
		}
		if(!Text::validateUtf8(parsed.body)) {
			error = _("The BBS post body is not valid UTF-8");
			return false;
		}

		document = std::move(parsed);
		return true;
	} catch(const ParseException& e) {
		error = e.getError();
	} catch(const Exception& e) {
		error = e.getError();
	} catch(...) {
		error = _("Unable to verify the BBS post");
	}
	return false;
}

void BBSManager::setHubSupported(const string& hubUrl, bool supported) noexcept {
	std::vector<BBSEntry> cachedEntries;
	{
		Lock l(cs);
		auto& hub = hubs[hubUrl];
		if(hub.supported == supported) return;
		hub.supported = supported;
		for(auto& board: hub.boards) {
			board.second.descriptor.subscribed = false;
			if(supported) {
				for(const auto& entry: board.second.entries) {
					if(!entry.second.withdrawn && entry.second.size >= 0 &&
						entry.second.size <= MAX_DOCUMENT_SIZE) cachedEntries.push_back(entry.second);
				}
			}
		}
	}
	fire(BBSManagerListener::SupportUpdated(), hubUrl, supported);
	if(!supported) return;
	for(const auto& entry: cachedEntries) {
		try {
			const auto path = getCachePath(entry.tth);
			if(File::getSize(path) != entry.size) continue;
			File file(path, File::READ, File::OPEN | File::SHARED);
			const auto timestamp = file.getLastModified();
			file.close();
			if(timestamp != 0) ShareManager::getInstance()->addTTHOnlyShare(path, entry.size, timestamp,
				TTHValue(entry.tth), hubUrl);
		} catch(...) { }
	}
}

bool BBSManager::isHubSupported(const string& hubUrl) const noexcept {
	Lock l(cs);
	auto i = hubs.find(hubUrl);
	return i != hubs.end() && i->second.supported;
}

bool BBSManager::updateBoard(const string& hubUrl, BBSBoard board) noexcept {
	bool subscribe = false;
	const auto boardName = board.name;
	{
		Lock l(cs);
		auto& hub = hubs[hubUrl];
		auto& state = hub.boards[board.name];
		board.cursor = state.descriptor.cursor;
		board.subscribed = state.descriptor.subscribed && board.canSubscribe();
		board.gap = board.oldest != 0 && board.cursor < board.oldest;
		state.descriptor = std::move(board);
		subscribe = hub.supported && state.descriptor.canSubscribe() && !state.descriptor.subscribed;
	}
	fire(BBSManagerListener::BoardUpdated(), hubUrl, boardName);
	return subscribe;
}

void BBSManager::removeBoard(const string& hubUrl, const string& board) noexcept {
	{
		Lock l(cs);
		auto hub = hubs.find(hubUrl);
		if(hub == hubs.end()) return;
		hub->second.boards.erase(board);
	}
	fire(BBSManagerListener::BoardUpdated(), hubUrl, board);
}

void BBSManager::setSubscribed(const string& hubUrl, const string& board, bool subscribed) noexcept {
	bool changed = false;
	{
		Lock l(cs);
		auto hub = hubs.find(hubUrl);
		if(hub == hubs.end()) return;
		auto i = hub->second.boards.find(board);
		if(i != hub->second.boards.end() && i->second.descriptor.subscribed != subscribed) {
			i->second.descriptor.subscribed = subscribed;
			changed = true;
		}
	}
	if(changed) fire(BBSManagerListener::BoardUpdated(), hubUrl, board);
}

uint64_t BBSManager::getResumeTimestamp(const string& hubUrl, const string& board) const noexcept {
	Lock l(cs);
	auto hub = hubs.find(hubUrl);
	if(hub == hubs.end()) return 0;
	auto i = hub->second.boards.find(board);
	return i == hub->second.boards.end() ? 0 : i->second.descriptor.cursor;
}

bool BBSManager::updateEntry(const string& hubUrl, BBSEntry entry) noexcept {
	const auto boardName = entry.board;
	const auto tth = entry.tth;
	const auto withdrawn = entry.withdrawn;
	bool liveOnHub = false;
	bool liveAnywhere = false;
	{
		Lock l(cs);
		auto& board = hubs[hubUrl].boards[entry.board];
		if(board.descriptor.name.empty()) board.descriptor.name = entry.board;
		auto existing = board.entries.find(entry.tth);
		if(existing != board.entries.end() && entry.timestamp <= existing->second.timestamp) return false;
		board.entries[entry.tth] = std::move(entry);
		board.descriptor.cursor = std::max(board.descriptor.cursor, board.entries[tth].timestamp);

		if(board.entries.size() > MAX_INDEX_ENTRIES_PER_BOARD) {
			auto oldest = board.entries.end();
			for(auto i = board.entries.begin(); i != board.entries.end(); ++i) {
				if(i->first == tth) continue;
				if(oldest == board.entries.end() || i->second.timestamp < oldest->second.timestamp ||
					(i->second.timestamp == oldest->second.timestamp && i->first < oldest->first)) oldest = i;
			}
			if(oldest != board.entries.end()) board.entries.erase(oldest);
		}
		if(withdrawn) {
			for(const auto& hub: hubs) {
				for(const auto& candidateBoard: hub.second.boards) {
					auto candidate = candidateBoard.second.entries.find(tth);
					if(candidate != candidateBoard.second.entries.end() && !candidate->second.withdrawn) {
						liveAnywhere = true;
						if(hubHintsEqual(hub.first, hubUrl)) liveOnHub = true;
					}
				}
			}
		}
	}
	if(withdrawn) {
		try {
			const auto path = getCachePath(tth);
			const TTHValue root(tth);
			if(!liveOnHub) ShareManager::getInstance()->removeTTHOnlyShare(path, root, hubUrl);
			if(!liveAnywhere) {
				{
					Lock l(cs);
					documents.erase(tth);
				}
				File::deleteFile(path);
			}
		} catch(...) { }
	}
	fire(BBSManagerListener::EntryUpdated(), hubUrl, boardName, tth);
	return true;
}

std::vector<BBSBoard> BBSManager::getBoards(const string& hubUrl) const {
	std::vector<BBSBoard> result;
	Lock l(cs);
	auto hub = hubs.find(hubUrl);
	if(hub == hubs.end()) return result;
	for(const auto& board: hub->second.boards) result.push_back(board.second.descriptor);
	return result;
}

std::vector<BBSEntry> BBSManager::getEntries(const string& hubUrl, const string& board, bool includeWithdrawn) const {
	std::vector<BBSEntry> result;
	{
		Lock l(cs);
		auto hub = hubs.find(hubUrl);
		if(hub == hubs.end()) return result;
		auto i = hub->second.boards.find(board);
		if(i == hub->second.boards.end()) return result;
		for(const auto& entry: i->second.entries) {
			if(includeWithdrawn || !entry.second.withdrawn) result.push_back(entry.second);
		}
	}
	std::sort(result.begin(), result.end(), [](const BBSEntry& a, const BBSEntry& b) {
		return a.timestamp != b.timestamp ? a.timestamp < b.timestamp : a.tth < b.tth;
	});
	return result;
}

std::optional<BBSBoard> BBSManager::getBoard(const string& hubUrl, const string& board) const {
	Lock l(cs);
	auto hub = hubs.find(hubUrl);
	if(hub == hubs.end()) return std::nullopt;
	auto i = hub->second.boards.find(board);
	return i == hub->second.boards.end() ? std::nullopt : std::optional<BBSBoard>(i->second.descriptor);
}

std::optional<BBSEntry> BBSManager::getEntry(const string& hubUrl, const string& board, const string& tth) const {
	Lock l(cs);
	auto hub = hubs.find(hubUrl);
	if(hub == hubs.end()) return std::nullopt;
	auto b = hub->second.boards.find(board);
	if(b == hub->second.boards.end()) return std::nullopt;
	auto entry = b->second.entries.find(tth);
	return entry == b->second.entries.end() ? std::nullopt : std::optional<BBSEntry>(entry->second);
}

std::optional<BBSDocument> BBSManager::getDocument(const string& tth) const {
	Lock l(cs);
	auto i = documents.find(tth);
	return i == documents.end() ? std::nullopt : std::optional<BBSDocument>(i->second);
}

bool BBSManager::isDocumentPending(const string& hubUrl, const string& board, const string& tth) const noexcept {
	Lock l(cs);
	auto requests = pending.find(tth);
	if(requests == pending.end()) return false;
	return std::any_of(requests->second.begin(), requests->second.end(), [&](const PendingRequest& request) { return hubHintsEqual(request.hubUrl, hubUrl) && request.board == board; });
}

string BBSManager::getCachePath(const string& tth) const {
	return Util::getPath(Util::PATH_USER_LOCAL) + "BBS" PATH_SEPARATOR_STR + tth + ".bbs";
}

bool BBSManager::cacheRawDocument(const string& raw, BBSDocument& document, string& error) noexcept {
	try {
		document.path = getCachePath(document.tth);
		const auto temp = document.path + ".tmp";
		File::ensureDirectory(document.path);
		{
			File file(temp, File::WRITE, File::CREATE | File::TRUNCATE);
			file.write(raw);
			file.flush();
		}
		File::deleteFile(document.path);
		File::renameFile(temp, document.path);
		return true;
	} catch(const Exception& e) {
		error = e.getError();
	} catch(...) {
		error = _("Unable to cache the BBS post");
	}
	return false;
}

void BBSManager::registerDocument(const string& hubUrl, const BBSDocument& document) noexcept {
	try {
		File file(document.path, File::READ, File::OPEN | File::SHARED);
		const auto timestamp = file.getLastModified();
		file.close();
		if(timestamp != 0) {
			ShareManager::getInstance()->addTTHOnlyShare(document.path, document.size, timestamp,
				TTHValue(document.tth), hubUrl);
		}
	} catch(...) { }
}

bool BBSManager::preparePost(const string& hubUrl, const string& authorId, const string& parent, const string& subject, const string& body, bool richText, int64_t boardLimit, BBSDocument& document, string& error) noexcept {
	string raw;
	if(!composeDocument(authorId, parent, subject, body, richText, GET_TIME(), raw, document, error)) return false;
	if(boardLimit < 0 || document.size > boardLimit) {
		error = str(F_("The post is %1% bytes; this board accepts at most %2% bytes") % document.size % boardLimit);
		return false;
	}
	if(!cacheRawDocument(raw, document, error)) return false;

	try {
		const TTHValue root(document.tth);
		if(!HashManager::getInstance()->verifyFileTTH(document.path, document.size, root)) {
			error = _("The cached BBS post failed its local TTH check");
			return false;
		}
		File file(document.path, File::READ, File::OPEN | File::SHARED);
		const auto timestamp = file.getLastModified();
		file.close();
		if(timestamp == 0 || !ShareManager::getInstance()->addTTHOnlyShare(
			document.path, document.size, timestamp, root, hubUrl))
		{
			error = _("The BBS post could not be exposed for exact-TTH transfers");
			return false;
		}
		Lock l(cs);
		documents[document.tth] = document;
		return true;
	} catch(const Exception& e) {
		error = e.getError();
	} catch(...) {
		error = _("Unable to prepare the BBS post for transfer");
	}
	return false;
}

bool BBSManager::loadCachedDocument(const string& hubUrl, const string& board, const BBSEntry& entry, BBSDocument& document, string& error) noexcept {
	try {
		const auto path = getCachePath(entry.tth);
		if(File::getSize(path) != entry.size || entry.size < 0 || entry.size > MAX_DOCUMENT_SIZE) return false;
		File file(path, File::READ, File::OPEN | File::SHARED);
		const auto raw = file.read(static_cast<size_t>(entry.size));
		if(static_cast<int64_t>(raw.size()) != entry.size || !parseDocument(raw, entry.tth, document, error)) {
			file.close();
			File::deleteFile(path);
			return false;
		}
		document.path = path;
		{
			Lock l(cs);
			documents[entry.tth] = document;
		}
		registerDocument(hubUrl, document);
		return true;
	} catch(...) {
		return false;
	}
}

bool BBSManager::searchFor(const PendingRequest& request, const string& tth) noexcept {
	{
		Lock l(cs);
		auto requests = pending.find(tth);
		if(requests == pending.end()) return false;
		auto current = std::find_if(requests->second.begin(), requests->second.end(), [&](const PendingRequest& item) {
			return hubHintsEqual(item.hubUrl, request.hubUrl) && item.board == request.board &&
				item.size == request.size && item.queuedAt == 0;
		});
		if(current == requests->second.end()) return false;
	}
	try {
		StringList hubsToSearch { request.hubUrl };
		if(!SearchManager::getInstance()->search(hubsToSearch, tth, 0, SearchManager::TYPE_TTH,
			SearchManager::SIZE_DONTCARE, "BBS0-" + tth.substr(0, 12), StringList())) return false;
		const auto now = GET_TICK();
		Lock l(cs);
		auto requests = pending.find(tth);
		if(requests != pending.end()) {
			auto current = std::find_if(requests->second.begin(), requests->second.end(), [&](const PendingRequest& item) {
				return hubHintsEqual(item.hubUrl, request.hubUrl) && item.board == request.board &&
					item.size == request.size && item.queuedAt == 0;
			});
			if(current != requests->second.end()) current->lastSearch = now;
		}
		return true;
	} catch(...) { }
	return false;
}

bool BBSManager::requestDocument(const string& hubUrl, const string& board, const string& tth, string& error) noexcept {
	auto entry = getEntry(hubUrl, board, tth);
	if(!entry || entry->withdrawn) {
		error = _("No active BBS index entry has that hash");
		return false;
	}
	if(entry->size < 0 || entry->size > MAX_DOCUMENT_SIZE) {
		error = str(F_("The declared BBS post size exceeds the %1% byte local safety limit") % MAX_DOCUMENT_SIZE);
		return false;
	}

	BBSDocument document;
	{
		Lock l(cs);
		auto i = documents.find(tth);
		if(i != documents.end()) document = i->second;
	}
	if(!document.tth.empty() || loadCachedDocument(hubUrl, board, *entry, document, error)) {
		registerDocument(hubUrl, document);
		fire(BBSManagerListener::DocumentUpdated(), hubUrl, board, tth);
		return true;
	}

	const auto now = GET_TICK();
	PendingRequest request { hubUrl, board, entry->size, now, 0, 0 };
	bool shouldSearch = false;
	bool added = false;
	{
		Lock l(cs);
		auto& requests = pending[tth];
		auto i = std::find_if(requests.begin(), requests.end(), [&](const PendingRequest& item) {
			return hubHintsEqual(item.hubUrl, hubUrl) && item.board == board;
		});
		if(i == requests.end()) {
			requests.push_back(request);
			shouldSearch = true;
			added = true;
		} else if(i->queuedAt == 0 && (i->lastSearch == 0 || now - i->lastSearch >= DOCUMENT_SEARCH_RETRY_MS)) {
			i->size = request.size;
			request = *i;
			shouldSearch = true;
		}
	}
	if(added) fire(BBSManagerListener::DocumentUpdated(), hubUrl, board, tth);
	if(shouldSearch) searchFor(request, tth);
	return true;
}

bool BBSManager::loadCachedDocument(const string& hubUrl, const string& board, const string& tth, string& error) noexcept {
	auto entry = getEntry(hubUrl, board, tth);
	if(!entry || entry->withdrawn) {
		error = _("No active BBS index entry has that hash");
		return false;
	}
	if(entry->size < 0 || entry->size > MAX_DOCUMENT_SIZE) {
		error = str(F_("The declared BBS post size exceeds the %1% byte local safety limit") % MAX_DOCUMENT_SIZE);
		return false;
	}
	BBSDocument document;
	{
		Lock l(cs);
		auto cached = documents.find(tth);
		if(cached != documents.end()) document = cached->second;
	}
	if(document.tth.empty()) {
		if(!loadCachedDocument(hubUrl, board, *entry, document, error)) {
			if(error.empty()) error = _("The BBS post is not cached locally");
			return false;
		}
	} else {
		registerDocument(hubUrl, document);
	}
	fire(BBSManagerListener::DocumentUpdated(), hubUrl, board, tth);
	return true;
}

void BBSManager::on(SearchManagerListener::SR, const SearchResultPtr& result) noexcept {
	if(!result || result->getType() != SearchResult::TYPE_FILE) return;
	const auto tth = result->getTTH().toBase32();
	const auto target = getCachePath(tth);
	std::optional<PendingRequest> request;
	{
		Lock l(cs);
		auto i = pending.find(tth);
		if(i == pending.end()) return;
		auto match = std::find_if(i->second.begin(), i->second.end(), [&](const PendingRequest& item) {
			return item.size == result->getSize() && hubHintsEqual(item.hubUrl, result->getUser().hint);
		});
		if(match != i->second.end()) request = *match;
	}
	if(!request) return;

	auto markQueued = [&]() {
		const auto now = GET_TICK();
		bool marked = false;
		Lock l(cs);
		auto requests = pending.find(tth);
		if(requests == pending.end()) return false;
		for(auto& item: requests->second) {
			if(item.size == result->getSize() && hubHintsEqual(item.hubUrl, result->getUser().hint)) {
				item.queuedAt = now;
				marked = true;
			}
		}
		return marked;
	};
	auto failRequest = [&](const string& message) {
		std::vector<PendingRequest> failed;
		{
			Lock l(cs);
			auto requests = pending.find(tth);
			if(requests == pending.end()) return;
			auto& items = requests->second;
			for(auto i = items.begin(); i != items.end();) {
				if(hubHintsEqual(i->hubUrl, request->hubUrl) && i->board == request->board) {
					failed.push_back(*i);
					i = items.erase(i);
				} else {
					++i;
				}
			}
			if(items.empty()) pending.erase(requests);
		}
		for(const auto& item: failed) {
			reportStatus(item.hubUrl, message);
			fire(BBSManagerListener::DocumentUpdated(), item.hubUrl, item.board, tth);
		}
	};

	try {
		QueueManager::getInstance()->add(target, request->size, result->getTTH(),
			result->getUser(), QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_TEXT);
		if(!markQueued()) QueueManager::getInstance()->remove(target);
	} catch(const QueueException& e) {
		BBSDocument document;
		string error;
		auto entry = getEntry(request->hubUrl, request->board, tth);
		if(entry && loadCachedDocument(request->hubUrl, request->board, *entry, document, error)) {
			std::vector<PendingRequest> completed;
			{
				Lock l(cs);
				auto requests = pending.find(tth);
				if(requests != pending.end()) {
					completed = std::move(requests->second);
					pending.erase(requests);
				}
			}
			for(const auto& item: completed) {
				if(item.size != document.size) continue;
				registerDocument(item.hubUrl, document);
				fire(BBSManagerListener::DocumentUpdated(), item.hubUrl, item.board, tth);
			}
			return;
		}
		TTHValue queuedRoot;
		if(QueueManager::getInstance()->getSize(target) == request->size &&
			QueueManager::getInstance()->getTTH(target, queuedRoot) && queuedRoot == result->getTTH())
		{
			markQueued();
			return;
		}
		failRequest(e.getError());
	} catch(const Exception& e) {
		failRequest(e.getError());
	} catch(...) {
		failRequest(_("Unable to queue the BBS post download"));
	}
}

void BBSManager::completeDocument(const string& tth, const string& path) noexcept {
	std::vector<PendingRequest> requests;
	{
		Lock l(cs);
		auto i = pending.find(tth);
		if(i == pending.end()) return;
		requests = std::move(i->second);
		pending.erase(i);
	}
	if(requests.empty()) return;
	std::vector<PendingRequest> stale;
	requests.erase(std::remove_if(requests.begin(), requests.end(), [&](const PendingRequest& request) {
		auto entry = getEntry(request.hubUrl, request.board, tth);
		const auto invalid = !entry || entry->withdrawn || entry->size != request.size;
		if(invalid) stale.push_back(request);
		return invalid;
	}), requests.end());
	for(const auto& request: stale) {
		reportStatus(request.hubUrl, _("The BBS index entry changed while the post was downloading"));
		fire(BBSManagerListener::DocumentUpdated(), request.hubUrl, request.board, tth);
	}
	if(requests.empty()) {
		try { File::deleteFile(path); } catch(...) { }
		return;
	}

	BBSDocument document;
	string error;
	try {
		const auto size = File::getSize(path);
		if(size < 0 || size > MAX_DOCUMENT_SIZE) {
			error = _("The downloaded BBS post exceeded the local safety limit");
		} else {
			File file(path, File::READ, File::OPEN | File::SHARED);
			const auto raw = file.read(static_cast<size_t>(size));
			if(static_cast<int64_t>(raw.size()) == size && parseDocument(raw, tth, document, error)) {
				document.path = path;
			}
		}
	} catch(const Exception& e) {
		error = e.getError();
	} catch(...) {
		error = _("Unable to read the downloaded BBS post");
	}

	if(document.tth.empty()) {
		try { File::deleteFile(path); } catch(...) { }
		for(const auto& request: requests) {
			reportStatus(request.hubUrl, error.empty() ? _("The downloaded BBS post failed verification") : error);
			fire(BBSManagerListener::DocumentUpdated(), request.hubUrl, request.board, tth);
		}
		return;
	}

	bool accepted = false;
	for(const auto& request: requests) {
		if(request.size != document.size) {
			reportStatus(request.hubUrl, _("The downloaded BBS post size did not match its index entry"));
			fire(BBSManagerListener::DocumentUpdated(), request.hubUrl, request.board, tth);
			continue;
		}
		if(!accepted) {
			Lock l(cs);
			documents[tth] = document;
			accepted = true;
		}
		registerDocument(request.hubUrl, document);
		fire(BBSManagerListener::DocumentUpdated(), request.hubUrl, request.board, tth);
	}
	if(!accepted) {
		try { File::deleteFile(path); } catch(...) { }
	}
}

void BBSManager::on(QueueManagerListener::Finished, QueueItem* item, const string&, int64_t) noexcept {
	if(!item || !item->isSet(QueueItem::FLAG_CLIENT_VIEW)) return;
	const auto tth = item->getTTH().toBase32();
	if(Util::stricmp(item->getTarget(), getCachePath(tth)) != 0) return;
	completeDocument(tth, item->getTarget());
}

void BBSManager::on(QueueManagerListener::Removed, QueueItem* item) noexcept {
	if(!item || !item->isSet(QueueItem::FLAG_CLIENT_VIEW)) return;
	const auto tth = item->getTTH().toBase32();
	if(Util::stricmp(item->getTarget(), getCachePath(tth)) != 0) return;
	std::vector<PendingRequest> requests;
	{
		Lock l(cs);
		auto pendingRequests = pending.find(tth);
		if(pendingRequests == pending.end()) return;
		requests = std::move(pendingRequests->second);
		pending.erase(pendingRequests);
	}
	for(const auto& request: requests) {
		reportStatus(request.hubUrl, _("The BBS post download ended before it could be verified"));
		fire(BBSManagerListener::DocumentUpdated(), request.hubUrl, request.board, tth);
	}
}

void BBSManager::on(TimerManagerListener::Second, uint64_t tick) noexcept {
	using PendingAction = std::pair<string, PendingRequest>;
	std::vector<PendingAction> searches;
	std::vector<PendingAction> expired;
	std::set<string> queuesToRemove;
	{
		Lock l(cs);
		for(auto requests = pending.begin(); requests != pending.end();) {
			auto& items = requests->second;
			bool queuedExpired = false;
			for(auto item = items.begin(); item != items.end();) {
				const auto since = item->queuedAt != 0 ? item->queuedAt : item->started;
				const auto timeout = item->queuedAt != 0 ? DOCUMENT_QUEUE_TIMEOUT_MS : DOCUMENT_SEARCH_TIMEOUT_MS;
				if(since != 0 && tick >= since && tick - since >= timeout) {
					queuedExpired = queuedExpired || item->queuedAt != 0;
					expired.emplace_back(requests->first, *item);
					item = items.erase(item);
					continue;
				}
				if(item->queuedAt == 0 && (item->lastSearch == 0 ||
					(tick >= item->lastSearch && tick - item->lastSearch >= DOCUMENT_SEARCH_RETRY_MS)))
				{
					searches.emplace_back(requests->first, *item);
				}
				++item;
			}
			if(items.empty()) {
				if(queuedExpired) queuesToRemove.insert(getCachePath(requests->first));
				requests = pending.erase(requests);
			} else {
				++requests;
			}
		}
	}
	for(const auto& action: expired) {
		const auto& request = action.second;
		reportStatus(request.hubUrl, request.queuedAt != 0 ?
			_("The BBS post download timed out") : _("No online peer responded with the BBS post"));
		fire(BBSManagerListener::DocumentUpdated(), request.hubUrl, request.board, action.first);
	}
	for(const auto& action: searches) searchFor(action.second, action.first);
	for(const auto& target: queuesToRemove) QueueManager::getInstance()->remove(target);
}

bool BBSManager::hasLiveReference(const string& hubUrl, const string& tth) const noexcept {
	Lock l(cs);
	auto hub = hubs.find(hubUrl);
	if(hub == hubs.end()) return false;
	for(const auto& board: hub->second.boards) {
		auto entry = board.second.entries.find(tth);
		if(entry != board.second.entries.end() && !entry->second.withdrawn) return true;
	}
	return false;
}

void BBSManager::reportStatus(const string& hubUrl, const string& message) noexcept {
	fire(BBSManagerListener::Status(), hubUrl, message);
}

void BBSManager::on(SettingsManagerListener::Load, SimpleXML& xml) noexcept {
	try {
		xml.resetCurrentChild();
		if(!xml.findChild("BBS0")) return;
		xml.stepIn();
		while(xml.findChild("Hub")) {
			const auto hubUrl = xml.getChildAttrib("URL");
			if(hubUrl.empty()) continue;
			xml.stepIn();
			while(xml.findChild("Board")) {
				BBSBoard descriptor;
				descriptor.name = xml.getChildAttrib("Name");
				if(!validBoardName(descriptor.name)) continue;
				descriptor.title = xml.getChildAttrib("Title");
				descriptor.description = xml.getChildAttrib("Description");
				descriptor.permissions = static_cast<uint32_t>(xml.getLongLongChildAttrib("Permissions"));
				descriptor.maxSize = xml.getLongLongChildAttrib("MaxSize");
				descriptor.newest = static_cast<uint64_t>(xml.getLongLongChildAttrib("Newest"));
				descriptor.oldest = static_cast<uint64_t>(xml.getLongLongChildAttrib("Oldest"));
				descriptor.postCount = xml.getLongLongChildAttrib("PostCount");
				descriptor.cursor = static_cast<uint64_t>(xml.getLongLongChildAttrib("Cursor"));
				descriptor.gap = xml.getBoolChildAttrib("Gap");
				xml.stepIn();
				auto& board = hubs[hubUrl].boards[descriptor.name];
				board.descriptor = descriptor;
				while(xml.findChild("Entry")) {
					BBSEntry entry;
					entry.tth = xml.getChildAttrib("TR");
					entry.size = xml.getLongLongChildAttrib("Size");
					entry.board = descriptor.name;
					entry.authorId = xml.getChildAttrib("ID");
					entry.nick = xml.getChildAttrib("Nick");
					entry.parent = xml.getChildAttrib("Parent");
					entry.thread = xml.getChildAttrib("Thread");
					entry.subject = xml.getChildAttrib("Subject");
					entry.timestamp = static_cast<uint64_t>(xml.getLongLongChildAttrib("Timestamp"));
					entry.withdrawn = xml.getBoolChildAttrib("Withdrawn");
					if(entry.tth.size() == 39 && isBase32(entry.tth)) board.entries[entry.tth] = std::move(entry);
				}
				xml.stepOut();
			}
			xml.stepOut();
		}
		xml.stepOut();
	} catch(...) { }
}

void BBSManager::on(SettingsManagerListener::Save, SimpleXML& xml) noexcept {
	try {
		xml.addTag("BBS0");
		xml.stepIn();
		Lock l(cs);
		for(const auto& hub: hubs) {
			if(hub.second.boards.empty()) continue;
			xml.addTag("Hub");
			xml.addChildAttrib("URL", hub.first);
			xml.stepIn();
			for(const auto& board: hub.second.boards) {
				const auto& descriptor = board.second.descriptor;
				xml.addTag("Board");
				xml.addChildAttrib("Name", descriptor.name);
				xml.addChildAttrib("Title", descriptor.title);
				xml.addChildAttrib("Description", descriptor.description);
				xml.addChildAttrib("Permissions", descriptor.permissions);
				xml.addChildAttrib("MaxSize", descriptor.maxSize);
				xml.addChildAttrib("Newest", static_cast<int64_t>(descriptor.newest));
				xml.addChildAttrib("Oldest", static_cast<int64_t>(descriptor.oldest));
				xml.addChildAttrib("PostCount", descriptor.postCount);
				xml.addChildAttrib("Cursor", static_cast<int64_t>(descriptor.cursor));
				xml.addChildAttrib("Gap", descriptor.gap);
				xml.stepIn();
				for(const auto& item: board.second.entries) {
					const auto& entry = item.second;
					xml.addTag("Entry");
					xml.addChildAttrib("TR", entry.tth);
					xml.addChildAttrib("Size", entry.size);
					xml.addChildAttrib("ID", entry.authorId);
					xml.addChildAttrib("Nick", entry.nick);
					xml.addChildAttrib("Parent", entry.parent);
					xml.addChildAttrib("Thread", entry.thread);
					xml.addChildAttrib("Subject", entry.subject);
					xml.addChildAttrib("Timestamp", static_cast<int64_t>(entry.timestamp));
					xml.addChildAttrib("Withdrawn", entry.withdrawn);
				}
				xml.stepOut();
			}
			xml.stepOut();
		}
		xml.stepOut();
	} catch(...) { }
}

} // namespace dcpp
