/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_RICH_TEXT_H
#define DCPLUSPLUS_DCPP_RICH_TEXT_H

#include <cstdint>
#include <string>
#include <vector>

namespace dcpp {

using std::string;
using std::vector;

/** Parser and protocol boundary for the ADC RTF0 extension.
 *
 * RTF0 is CommonMark 0.31.2 with GFM tables, strikethrough and permissive
 * URL autolinks. Raw HTML is text, and inline media is restricted to ADC
 * magnet URIs. The parser emits only the small, trusted XML vocabulary used
 * by ChatMessage/HtmlToRtf; remote input never becomes display-layer markup.
 */
class RichText {
public:
	static constexpr size_t MAX_NESTING_DEPTH = 32;

	struct Attachment {
		string uri;
		string tth;
		string name;
		int64_t size = -1;
		bool inlineMedia = false;
	};

	struct Parsed {
		string html;
		vector<Attachment> attachments;
		bool formatted = false;
		bool valid = true;
		bool safeToSend = true;
	};

	/** Parse already ADC-unescaped UTF-8 into trusted semantic XML. */
	static Parsed parse(const string& text, size_t maxTargetLength);

	/** Validate the RTF0 magnet form (xt=urn:tree:tiger plus mandatory xl). */
	static bool parseMagnetUri(const string& uri, size_t maxLength, Attachment& attachment) noexcept;

	/** Whether a target may become an actionable RTF0 link. */
	static bool isSafeLink(const string& url, size_t maxLength) noexcept;

	/** Build a TTH magnet suitable for plain chat or a CommonMark target. */
	static string makeAttachmentMagnet(const string& displayName, const string& tth, int64_t size);
	/** Build a spec-compliant attachment link. The display name is escaped for
	 * CommonMark and also used as the percent-encoded magnet dn field. */
	static string makeAttachmentMarkdown(const string& displayName, const string& tth,
		int64_t size, bool inlineMedia);
	/** Whether a local file type is supported by the inline-image renderer. */
	static bool isInlineMediaFile(const string& path) noexcept;

	static bool hasFormatting(const string& text, size_t maxTargetLength);

	/** Apply the configured receive bound. False means render as plain text. */
	static bool prepareIncomingMessage(string& text, bool markedRichText);

	/** Return whether an explicitly rich message should carry RT1. Windows CRLF
	 * input is normalized to LF for ADC escaping; all other whitespace remains
	 * untouched and ADC escaping remains a later, independent wire step. */
	static bool prepareOutgoingMessage(string& text, bool explicitRichText = false,
		const string& hubUrl = string());
};

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_RICH_TEXT_H)
