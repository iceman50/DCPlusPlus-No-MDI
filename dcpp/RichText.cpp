/*
 * Copyright (C) 2001-2026 iceman50
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "RichText.h"

#include "format.h"
#include "SettingsManager.h"
#include "ShareManager.h"
#include "Util.h"

#include <md4c.h>

extern "C" {
#include <entity.h>
}

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>

namespace dcpp {

namespace {

	bool asciiEqual(char left, char right) noexcept {
		return std::tolower(static_cast<unsigned char>(left)) ==
			std::tolower(static_cast<unsigned char>(right));
	}

	bool startsWith(const string& value, const char* prefix) noexcept {
		const auto length = strlen(prefix);
		return value.size() >= length && std::equal(prefix, prefix + length, value.begin(), asciiEqual);
	}

	string lowerAscii(string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		return value;
	}

	string cleanAttachmentName(const string& value) {
		auto name = Util::getFileName(value);
		name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
			return ch < 0x20 || ch == 0x7f;
		}), name.end());
		if(name.size() > 1024) name.resize(1024);
		return name;
	}

	string escapeMarkdownLabel(const string& value) {
		string result;
		result.reserve(value.size());
		for(const auto ch: value) {
			if(ch == '\\' || ch == '[' || ch == ']') result += '\\';
			result += ch;
		}
		return result;
	}

	bool validPercentEncoding(const string& value) noexcept {
		for(size_t i = 0; i < value.size(); ++i) {
			if(value[i] != '%') continue;
			if(i + 2 >= value.size() || !std::isxdigit(static_cast<unsigned char>(value[i + 1])) ||
				!std::isxdigit(static_cast<unsigned char>(value[i + 2]))) return false;
			i += 2;
		}
		return true;
	}

	void appendCodePoint(string& target, uint32_t value) {
		if(value == 0 || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) value = 0xfffd;
		if(value <= 0x7f) {
			target += static_cast<char>(value);
		} else if(value <= 0x7ff) {
			target += static_cast<char>(0xc0 | (value >> 6));
			target += static_cast<char>(0x80 | (value & 0x3f));
		} else if(value <= 0xffff) {
			target += static_cast<char>(0xe0 | (value >> 12));
			target += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
			target += static_cast<char>(0x80 | (value & 0x3f));
		} else {
			target += static_cast<char>(0xf0 | (value >> 18));
			target += static_cast<char>(0x80 | ((value >> 12) & 0x3f));
			target += static_cast<char>(0x80 | ((value >> 6) & 0x3f));
			target += static_cast<char>(0x80 | (value & 0x3f));
		}
	}

	string decodeEntity(const char* text, size_t size) {
		string result;
		if(size > 3 && text[0] == '&' && text[1] == '#') {
			const bool hex = text[2] == 'x' || text[2] == 'X';
			const size_t begin = hex ? 3 : 2;
			uint32_t value = 0;
			for(size_t i = begin; i + 1 < size; ++i) {
				unsigned digit;
				const auto ch = static_cast<unsigned char>(text[i]);
				if(ch >= '0' && ch <= '9') digit = ch - '0';
				else if(hex && ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
				else if(hex && ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
				else return string(text, size);
				const auto radix = hex ? 16U : 10U;
				if(value > (0x10ffffU - digit) / radix) value = 0x110000U;
				else value = value * radix + digit;
			}
			appendCodePoint(result, value);
			return result;
		}

		if(const auto entity = entity_lookup(text, size)) {
			appendCodePoint(result, entity->codepoints[0]);
			if(entity->codepoints[1]) appendCodePoint(result, entity->codepoints[1]);
			return result;
		}
		return string(text, size);
	}

	string decodeAttribute(const MD_ATTRIBUTE& attribute) {
		string result;
		if(!attribute.text || !attribute.substr_offsets || !attribute.substr_types) return result;
		for(size_t i = 0; attribute.substr_offsets[i] < attribute.size; ++i) {
			const auto offset = attribute.substr_offsets[i];
			const auto size = attribute.substr_offsets[i + 1] - offset;
			switch(attribute.substr_types[i]) {
			case MD_TEXT_ENTITY:
				result += decodeEntity(attribute.text + offset, size);
				break;
			case MD_TEXT_NULLCHAR:
				appendCodePoint(result, 0xfffd);
				break;
			default:
				result.append(attribute.text + offset, size);
				break;
			}
		}
		return result;
	}

	void appendXmlEscaped(string& target, const string& value) {
		for(const auto ch: value) {
			switch(ch) {
			case '&': target += "&amp;"; break;
			case '<': target += "&lt;"; break;
			case '>': target += "&gt;"; break;
			case '\"': target += "&quot;"; break;
			case '\'': target += "&apos;"; break;
			default: target += ch; break;
			}
		}
	}

	void appendXmlTextWithBreaks(string& target, const string& value) {
		size_t begin = 0;
		while(begin < value.size()) {
			auto end = value.find('\n', begin);
			if(end == string::npos) end = value.size();
			appendXmlEscaped(target, value.substr(begin, end - begin));
			if(end != value.size()) target += "<br/>";
			begin = end + 1;
		}
	}

	bool hasAuthority(const string& url, size_t colon) noexcept {
		if(colon + 2 >= url.size() || url[colon + 1] != '/' || url[colon + 2] != '/') return false;
		const auto begin = colon + 3;
		const auto end = url.find_first_of("/?#", begin);
		const auto authorityEnd = end == string::npos ? url.size() : end;
		if(authorityEnd == begin) return false;
		const auto at = url.rfind('@', authorityEnd - 1);
		const auto hostBegin = at != string::npos && at >= begin ? at + 1 : begin;
		return hostBegin < authorityEnd && url[hostBegin] != ':';
	}

	struct Renderer {
		explicit Renderer(size_t aMaxTargetLength) : maxTargetLength(aMaxTargetLength) { }

		struct ListState {
			bool ordered;
			bool tight;
			unsigned next;
		};

		struct ImageState {
			RichText::Attachment attachment;
			string alt;
			bool valid;
		};
		struct LinkState {
			bool safe = false;
			optional<RichText::Attachment> attachment;
			string label;
		};

		RichText::Parsed result;
		size_t maxTargetLength;
		size_t depth = 0;
		size_t paragraphs = 0;
		size_t tableColumns = 0;
		vector<ListState> lists;
		vector<LinkState> links;
		vector<ImageState> images;

		void appendAttachmentMetadata(const RichText::Attachment& attachment, bool uriAlreadyVisible) {
			result.html += " <span id=\"rtfAttachmentMeta\">[";
			if(!uriAlreadyVisible) {
				result.html += "<a href=\"";
				appendXmlEscaped(result.html, attachment.uri);
				result.html += "\">";
				appendXmlEscaped(result.html, attachment.uri);
				result.html += "</a> — ";
			}
			appendXmlEscaped(result.html, Util::formatBytes(attachment.size));
			result.html += "]</span>";
		}

		int enterBlock(MD_BLOCKTYPE type, void* detail) {
			if(++depth > RichText::MAX_NESTING_DEPTH) return 1;
			if(!images.empty()) return 0;

			switch(type) {
			case MD_BLOCK_DOC:
				break;
			case MD_BLOCK_QUOTE:
				result.formatted = true;
				result.html += "<span id=\"rtfQuote\">&#x2502; ";
				break;
			case MD_BLOCK_UL: {
				const auto data = static_cast<MD_BLOCK_UL_DETAIL*>(detail);
				if(!lists.empty()) result.html += "<br/>";
				lists.push_back({ false, data && data->is_tight != 0, 1 });
				result.formatted = true;
				break;
			}
			case MD_BLOCK_OL: {
				const auto data = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
				if(!lists.empty()) result.html += "<br/>";
				lists.push_back({ true, data && data->is_tight != 0, data ? data->start : 1 });
				result.formatted = true;
				break;
			}
			case MD_BLOCK_LI:
				for(size_t i = 1; i < lists.size(); ++i) result.html += "  ";
				if(!lists.empty() && lists.back().ordered) {
					result.html += std::to_string(lists.back().next++) + ". ";
				} else {
					result.html += "&#x2022; ";
				}
				break;
			case MD_BLOCK_HR:
				result.formatted = true;
				result.html += "<span id=\"rtfRule\">&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;&#x2500;</span><br/>";
				break;
			case MD_BLOCK_H: {
				const auto data = static_cast<MD_BLOCK_H_DETAIL*>(detail);
				const auto level = std::clamp(data ? data->level : 1U, 1U, 6U);
				result.formatted = true;
				result.html += "<span id=\"rtfHeading" + std::to_string(level) + "\"><b>";
				break;
			}
			case MD_BLOCK_CODE:
				result.formatted = true;
				result.html += "<span id=\"rtfCode\">";
				break;
			case MD_BLOCK_HTML:
				// MD_FLAG_NOHTML prevents this; keep a safe fallback if the parser changes.
				break;
			case MD_BLOCK_P:
				if(++paragraphs > 1) result.formatted = true;
				result.html += "<span id=\"rtfParagraph\">";
				break;
			case MD_BLOCK_TABLE: {
				const auto data = static_cast<MD_BLOCK_TABLE_DETAIL*>(detail);
			tableColumns = data ? data->col_count : 0;
			result.formatted = true;
			result.html += "<span id=\"rtfTable\" columns=\"" + std::to_string(tableColumns) + "\">";
			break;
			}
			case MD_BLOCK_THEAD:
			case MD_BLOCK_TBODY:
				break;
		case MD_BLOCK_TR:
			result.html += "<span id=\"rtfTableRow\">";
			break;
			case MD_BLOCK_TH:
			case MD_BLOCK_TD: {
				const auto data = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
				const char* alignment = data && data->align == MD_ALIGN_RIGHT ? "right" :
					data && data->align == MD_ALIGN_CENTER ? "center" : "left";
				result.html += type == MD_BLOCK_TH ? "<span id=\"rtfTableHeader\" style=\"text-align: " :
					"<span id=\"rtfTableCell\" style=\"text-align: ";
				result.html += alignment;
				result.html += ";\">";
				break;
			}
			}
			return 0;
		}

		int leaveBlock(MD_BLOCKTYPE type) {
			if(images.empty()) {
				switch(type) {
				case MD_BLOCK_QUOTE:
					result.html += "</span><br/>";
					break;
				case MD_BLOCK_UL:
				case MD_BLOCK_OL:
					if(!lists.empty()) lists.pop_back();
					break;
				case MD_BLOCK_LI:
					result.html += "<br/>";
					break;
				case MD_BLOCK_H:
					result.html += "</b></span><br/>";
					break;
				case MD_BLOCK_CODE:
					result.html += "</span><br/>";
					break;
				case MD_BLOCK_P:
					result.html += "</span>";
					if(lists.empty() || !lists.back().tight) result.html += "<br/><br/>";
					break;
			case MD_BLOCK_TABLE:
				result.html += "</span>";
				tableColumns = 0;
				break;
			case MD_BLOCK_TR:
				result.html += "</span>";
				break;
			case MD_BLOCK_TH:
			case MD_BLOCK_TD:
				result.html += "</span>";
				break;
				default:
					break;
				}
			}
			if(depth) --depth;
			return 0;
		}

		int enterSpan(MD_SPANTYPE type, void* detail) {
			if(++depth > RichText::MAX_NESTING_DEPTH) return 1;

			if(type == MD_SPAN_IMG) {
				const auto data = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
				ImageState image;
				image.valid = data && RichText::parseMagnetUri(decodeAttribute(data->src),
					maxTargetLength, image.attachment);
				if(!image.valid) result.safeToSend = false;
				image.attachment.inlineMedia = true;
				images.push_back(std::move(image));
				result.formatted = true;
				return 0;
			}

			if(!images.empty()) return 0;
			result.formatted = true;
			switch(type) {
			case MD_SPAN_EM: result.html += "<i>"; break;
			case MD_SPAN_STRONG: result.html += "<b>"; break;
			case MD_SPAN_CODE: result.html += "<span id=\"rtfCode\">"; break;
			case MD_SPAN_DEL: result.html += "<span id=\"rtfStrike\">"; break;
			case MD_SPAN_A: {
				const auto data = static_cast<MD_SPAN_A_DETAIL*>(detail);
				const auto href = data ? decodeAttribute(data->href) : string();
				const auto safe = RichText::isSafeLink(href, maxTargetLength);
				LinkState link;
				link.safe = safe;
				if(safe) {
					RichText::Attachment attachment;
					if(RichText::parseMagnetUri(href, maxTargetLength, attachment)) {
						result.attachments.push_back(attachment);
						link.attachment = std::move(attachment);
					}
					result.html += "<a href=\"";
					appendXmlEscaped(result.html, href);
					result.html += "\">";
				}
				links.push_back(std::move(link));
				break;
			}
			default:
				break;
			}
			return 0;
		}

		int leaveSpan(MD_SPANTYPE type) {
			if(type == MD_SPAN_IMG) {
				if(images.empty()) return 1;
				auto image = std::move(images.back());
				images.pop_back();
				const auto description = image.alt.empty() ? string("[image]") : image.alt;
				if(!images.empty()) {
					images.back().alt += description;
				} else if(image.valid) {
					result.attachments.push_back(image.attachment);
					result.html += "<a href=\"";
					appendXmlEscaped(result.html, image.attachment.uri);
					result.html += "\"><rtfimage src=\"";
					appendXmlEscaped(result.html, image.attachment.uri);
					result.html += "\">[image] ";
					appendXmlEscaped(result.html, description);
					result.html += "</rtfimage></a>";
					appendAttachmentMetadata(image.attachment, false);
				} else {
					appendXmlEscaped(result.html, description);
				}
				if(depth) --depth;
				return 0;
			}

			if(images.empty()) {
				switch(type) {
				case MD_SPAN_EM: result.html += "</i>"; break;
				case MD_SPAN_STRONG: result.html += "</b>"; break;
				case MD_SPAN_CODE:
				case MD_SPAN_DEL: result.html += "</span>"; break;
				case MD_SPAN_A:
					if(links.empty()) return 1;
					{
						auto link = std::move(links.back());
						if(link.safe) result.html += "</a>";
						if(link.attachment) appendAttachmentMetadata(
							*link.attachment, link.label == link.attachment->uri);
					}
					links.pop_back();
					break;
				default:
					break;
				}
			}
			if(depth) --depth;
			return 0;
		}

		int text(MD_TEXTTYPE type, const char* value, size_t size) {
			string decoded;
			switch(type) {
			case MD_TEXT_ENTITY:
				decoded = decodeEntity(value, size);
				result.formatted = true;
				break;
			case MD_TEXT_NULLCHAR:
				appendCodePoint(decoded, 0xfffd);
				break;
			default:
				decoded.assign(value, size);
				break;
			}

			if(!images.empty()) {
				if(type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) images.back().alt += ' ';
				else images.back().alt += decoded;
				return 0;
			}
			if(!links.empty()) links.back().label += decoded;

			switch(type) {
			case MD_TEXT_BR:
				result.formatted = true;
				result.html += "<br/>";
				break;
			case MD_TEXT_SOFTBR:
				result.formatted = true;
				result.html += ' ';
				break;
			case MD_TEXT_CODE:
				appendXmlTextWithBreaks(result.html, decoded);
				break;
			default:
				appendXmlEscaped(result.html, decoded);
				break;
			}
			return 0;
		}

		static int onEnterBlock(MD_BLOCKTYPE type, void* detail, void* user) {
			return static_cast<Renderer*>(user)->enterBlock(type, detail);
		}
		static int onLeaveBlock(MD_BLOCKTYPE type, void*, void* user) {
			return static_cast<Renderer*>(user)->leaveBlock(type);
		}
		static int onEnterSpan(MD_SPANTYPE type, void* detail, void* user) {
			return static_cast<Renderer*>(user)->enterSpan(type, detail);
		}
		static int onLeaveSpan(MD_SPANTYPE type, void*, void* user) {
			return static_cast<Renderer*>(user)->leaveSpan(type);
		}
		static int onText(MD_TEXTTYPE type, const MD_CHAR* value, MD_SIZE size, void* user) {
			return static_cast<Renderer*>(user)->text(type, value, size);
		}
	};

}

bool RichText::parseMagnetUri(const string& uri, size_t maxLength, Attachment& attachment) noexcept {
	try {
		attachment = Attachment();
		if(uri.empty() || uri.size() > maxLength || !startsWith(uri, "magnet:?")) return false;
		for(const auto ch: uri) {
			const auto value = static_cast<unsigned char>(ch);
			if(value <= 0x20 || value >= 0x7f || ch == '\\' || ch == '"' || ch == '<' || ch == '>') return false;
		}

		bool foundXt = false;
		bool foundXl = false;
		for(size_t begin = 8; begin <= uri.size();) {
			auto end = uri.find('&', begin);
			if(end == string::npos) end = uri.size();
			const auto separator = uri.find('=', begin);
			if(separator == string::npos || separator >= end) return false;
			const auto name = lowerAscii(uri.substr(begin, separator - begin));
			const auto encoded = uri.substr(separator + 1, end - separator - 1);
			if(!validPercentEncoding(encoded)) return false;
			const auto value = Util::encodeURI(encoded, true);

			if(name == "xt") {
				if(foundXt || value.size() != 54 || !startsWith(value, "urn:tree:tiger:")) return false;
				attachment.tth = value.substr(15);
				if(attachment.tth.size() != 39 || !std::all_of(attachment.tth.begin(), attachment.tth.end(), [](char ch) {
					return (ch >= 'A' && ch <= 'Z') || (ch >= '2' && ch <= '7');
				})) return false;
				foundXt = true;
			} else if(name == "xl") {
				if(foundXl || value.empty()) return false;
				uint64_t size = 0;
				for(const auto ch: value) {
					if(ch < '0' || ch > '9') return false;
					const auto digit = static_cast<unsigned>(ch - '0');
					if(size > (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - digit) / 10) return false;
					size = size * 10 + digit;
				}
				attachment.size = static_cast<int64_t>(size);
				foundXl = true;
			} else if(name == "dn" && attachment.name.empty()) {
				auto finalName = value;
				const auto slash = finalName.find_last_of("/\\");
				if(slash != string::npos) finalName.erase(0, slash + 1);
				if(finalName.size() > 1024) finalName.resize(1024);
				finalName.erase(std::remove_if(finalName.begin(), finalName.end(), [](unsigned char ch) {
					return ch < 0x20 || ch == 0x7f;
				}), finalName.end());
				attachment.name = std::move(finalName);
			}

			if(end == uri.size()) break;
			begin = end + 1;
		}
		if(!foundXt || !foundXl) return false;
		attachment.uri = uri;
		return true;
	} catch(...) {
		return false;
	}
}

bool RichText::isSafeLink(const string& url, size_t maxLength) noexcept {
	try {
		if(url.empty() || url.size() > maxLength) return false;
		for(const auto ch: url) {
			const auto value = static_cast<unsigned char>(ch);
			if(value <= 0x20 || value == 0x7f || ch == '\\' || ch == '"' || ch == '<' || ch == '>') return false;
		}
		const auto colon = url.find(':');
		if(colon == string::npos || colon == 0 || !std::isalpha(static_cast<unsigned char>(url[0]))) return false;
		for(size_t i = 1; i < colon; ++i) {
			const auto ch = static_cast<unsigned char>(url[i]);
			if(!std::isalnum(ch) && ch != '+' && ch != '-' && ch != '.') return false;
		}
		const auto scheme = lowerAscii(url.substr(0, colon));
		if(scheme == "magnet") {
			Attachment attachment;
			return parseMagnetUri(url, maxLength, attachment);
		}
		if(scheme != "http" && scheme != "https" && scheme != "adc" &&
			scheme != "adcs" && scheme != "dchub") return false;
		return hasAuthority(url, colon);
	} catch(...) {
		return false;
	}
}

RichText::Parsed RichText::parse(const string& text, size_t maxTargetLength) {
	Renderer renderer(std::max<size_t>(1, maxTargetLength));
	if(text.size() > std::numeric_limits<MD_SIZE>::max()) {
		renderer.result.valid = false;
		return std::move(renderer.result);
	}

	const MD_PARSER parser = {
		0,
		MD_FLAG_NOHTML | MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_PERMISSIVEURLAUTOLINKS,
		&Renderer::onEnterBlock,
		&Renderer::onLeaveBlock,
		&Renderer::onEnterSpan,
		&Renderer::onLeaveSpan,
		&Renderer::onText,
		nullptr,
		nullptr
	};
	if(md_parse(text.data(), static_cast<MD_SIZE>(text.size()), &parser, &renderer) != 0) {
		renderer.result = Parsed();
		renderer.result.valid = false;
		return std::move(renderer.result);
	}

	while(renderer.result.html.size() >= 5 &&
		renderer.result.html.compare(renderer.result.html.size() - 5, 5, "<br/>") == 0)
	{
		renderer.result.html.erase(renderer.result.html.size() - 5);
	}
	// HtmlToRtf consumes a complete XML document, not an XML fragment. A
	// single root also keeps top-level list markers and every later block.
	renderer.result.html.insert(0, "<span>");
	renderer.result.html += "</span>";
	return std::move(renderer.result);
}

string RichText::makeAttachmentMagnet(const string& displayName, const string& tth, int64_t size) {
	const auto name = cleanAttachmentName(displayName);
	auto encodedName = Util::encodeURI(name);
	// Parentheses can terminate Markdown targets and plain chat link detection.
	// Encoding them keeps unusual file names inside the magnet in either mode.
	for(size_t pos = 0; (pos = encodedName.find_first_of("()", pos)) != string::npos; pos += 3) {
		encodedName.replace(pos, 1, encodedName[pos] == '(' ? "%28" : "%29");
	}
	return "magnet:?xt=urn:tree:tiger:" + tth + "&xl=" + std::to_string(size) +
		"&dn=" + encodedName;
}

string RichText::makeAttachmentMarkdown(const string& displayName, const string& tth,
	int64_t size, bool inlineMedia)
{
	const auto name = cleanAttachmentName(displayName);
	return string(inlineMedia ? "![" : "[") + escapeMarkdownLabel(name) + "](" +
		makeAttachmentMagnet(name, tth, size) + ")";
}

bool RichText::isInlineMediaFile(const string& path) noexcept {
	const auto ext = Text::toLower(Util::getFileExt(path));
	return ext == ".bmp" || ext == ".gif" || ext == ".ico" || ext == ".jpg" || ext == ".jpeg" ||
		ext == ".png" || ext == ".tif" || ext == ".tiff" || ext == ".webp";
}

bool RichText::hasFormatting(const string& text, size_t maxTargetLength) {
	const auto parsed = parse(text, maxTargetLength);
	return parsed.valid && parsed.formatted;
}

bool RichText::prepareIncomingMessage(string& text, bool markedRichText) {
	if(!markedRichText || !SETTING(ENABLE_RICH_TEXT)) return false;
	const auto maxSize = static_cast<size_t>(std::max(1024, SETTING(RICH_TEXT_MAX_SIZE)));
	if(text.size() > maxSize) {
		text = _("Rich text message omitted because it exceeds the configured size limit");
		return false;
	}
	return true;
}

bool RichText::prepareOutgoingMessage(string& text, bool explicitRichText, const string& hubUrl) {
	if(!explicitRichText || !SETTING(ENABLE_RICH_TEXT)) return false;
	for(size_t i = 0; i < text.size(); ++i) {
		if(text[i] != '\r') continue;
		if(i + 1 < text.size() && text[i + 1] == '\n') {
			text.erase(i, 1);
		} else {
			text[i] = '\n';
		}
	}
	const auto maxSize = static_cast<size_t>(std::max(1024, SETTING(RICH_TEXT_MAX_SIZE)));
	if(text.size() > maxSize) return false;
	const auto parsed = parse(text,
		static_cast<size_t>(std::max(1, SETTING(CHAT_LINK_MAX_LENGTH))));
	if(!parsed.valid || !parsed.safeToSend || !parsed.formatted) return false;

	// RTF0 attachments are promises by the sender: the exact TTH and size must
	// be available through the share that is visible on the message's route.
	for(const auto& attachment: parsed.attachments) {
		try {
			if(!ShareManager::getInstance()->validateChatAttachment(
				TTHValue(attachment.tth), attachment.size, hubUrl)) return false;
		} catch(const Exception&) {
			return false;
		}
	}
	return true;
}

} // namespace dcpp
