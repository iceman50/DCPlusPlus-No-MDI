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

// inspired by Bjarke Viksoe's Simple HTML Viewer <https://www.viksoe.dk/code/simplehtmlviewer.htm>.

#include "stdafx.h"
#include "HtmlToRtf.h"

#include "Emoticons.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>

#include <dcpp/debug.h>
#include <dcpp/Flags.h>
#include <dcpp/ScopedFunctor.h>
#include <dcpp/SettingsManager.h>
#include <dcpp/SimpleXML.h>
#include <dcpp/StringTokenizer.h>
#include <dcpp/Text.h>

#include <dwt/util/GDI.h>
#include <dwt/widgets/RichTextBox.h>

namespace {
	inline void trimInPlace(string& s) {
		auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
		auto first = std::find_if_not(s.begin(), s.end(), isSpace);
		auto last = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();
		if(first >= last) {
			s.clear();
			return;
		}
		s.assign(first, last);
	}

	inline string trimCopy(string s) {
		trimInPlace(s);
		return s;
	}
}

struct Parser : SimpleXMLReader::CallBack {
	Parser(dwt::RichTextBox* box);
	void startTag(const string& name, StringPairList& attribs, bool simple);
	void data(const string& data);
	void endTag(const string& name);
	tstring finalize();

private:
	struct Context : Flags {
		enum { Bold = 1 << 0, Italic = 1 << 1, Underlined = 1 << 2 };
		size_t font; // index in the "fonts" table
		int fontSize;
		size_t textColor; // index in the "colors" table
		size_t bgColor; // index in the "colors" table
		string link;

		Context(dwt::RichTextBox* box, Parser& parser);

		tstring getBegin() const;
		tstring getEnd() const;
	};

	size_t addFont(string&& font);
	static int rtfFontSize(float px);
	size_t addColor(COLORREF color);

	void parseFont(const string& s);
	void parseColor(size_t& contextColor, const string& s);
	void parseDecoration(const string& s);
	void applySemanticStyle(Context& context, const string& id);
	void ensureContrast(Context& context);
	static tstring rtfEscape(const string& s);

	tstring ret;

	StringList fonts;
	StringList colors;
	std::unordered_map<string, size_t> fontIndexes;
	std::unordered_map<COLORREF, size_t> colorIndexes;
	vector<COLORREF> colorValues;

	vector<Context> contexts;
	unsigned suppressDepth = 0;
	vector<bool> emoticonSuppressions;
};

tstring HtmlToRtf::convert(const string& html, dwt::RichTextBox* box) {
	Parser parser(box);
	try { SimpleXMLReader(&parser).parse(html); }
	catch(const SimpleXMLException& e) { return Text::toT(e.getError()); }
	return parser.finalize();
}

Parser::Parser(dwt::RichTextBox* box) {
	// create a default context with the Rich Edit control's current formatting.
	contexts.emplace_back(box, *this);
}

void Parser::startTag(const string& name_, StringPairList& attribs, bool simple) {
	auto name = trimCopy(name_);

	if(name == "emoticon") {
		const auto emoticon = getAttrib(attribs, "name", 0);
		// The source shortcut remains inside the tag for non-rich consumers. Suppress that text after
		// emitting exactly one packaged icon. A missing/invalid asset leaves the shortcut untouched.
		const auto configuredSize = SETTING(EMOTICON_SIZE);
		const auto imageSize = configuredSize == 20 || configuredSize == 22 || configuredSize == 24 ?
			configuredSize : 16;
		const auto configuredDepth = SETTING(EMOTICON_BIT_DEPTH);
		// Values written by builds that offered 4/8 bpp migrate to the new 16-bpp minimum.
		const auto bitDepth = configuredDepth == 24 || configuredDepth == 32 ? configuredDepth : 16;
		auto image = Emoticons::rtf(emoticon, imageSize, bitDepth);
		const bool rendered = !image.empty();
		if(rendered) ret += std::move(image);
		if(!simple) {
			emoticonSuppressions.push_back(rendered);
			if(rendered) ++suppressDepth;
		}
		return;
	}

	if(name == "br") {
		ret += _T("\\line\n");
	}

	if(simple) {
		return;
	}

	contexts.push_back(contexts.back());
	ScopedFunctor([this] { ret += contexts.back().getBegin(); });

	applySemanticStyle(contexts.back(), getAttrib(attribs, "id", 0));

	if(name == "b") {
		contexts.back().setFlag(Context::Bold);
	} else if(name == "i") {
		contexts.back().setFlag(Context::Italic);
	} else if(name == "u") {
		contexts.back().setFlag(Context::Underlined);
	}

	if(attribs.empty()) {
		return;
	}

	if(name == "a") {
		const auto link = getAttrib(attribs, "href", 0);
		if(!link.empty()) {
			auto& context = contexts.back();
			context.link = link;
			context.setFlag(Context::Underlined);
			context.textColor = addColor(SETTING(LINK_COLOR)); /// @todo move to styles
		}
	}

	const auto style = getAttrib(attribs, "style", 0);
	size_t begin = 0;
	while(begin < style.size()) {
		auto end = style.find(';', begin);
		if(end == string::npos) end = style.size();
		auto declaration = style.substr(begin, end - begin);
		auto separator = declaration.find(':');
		if(separator != string::npos) {
			auto property = declaration.substr(0, separator);
			auto value = declaration.substr(separator + 1);
			trimInPlace(property);
			trimInPlace(value);
			if(property == "font") parseFont(value);
			else if(property == "color") parseColor(contexts.back().textColor, value);
			else if(property == "background-color") parseColor(contexts.back().bgColor, value);
			else if(property == "text-decoration") parseDecoration(value);
			else if(property == "font-weight" && (value == "bold" || Util::toInt(value) >= FW_BOLD)) contexts.back().setFlag(Context::Bold);
			else if(property == "font-style" && value == "italic") contexts.back().setFlag(Context::Italic);
		}
		begin = end + 1;
	}
	ensureContrast(contexts.back());
}

tstring Parser::rtfEscape(const string& data) {
	return dwt::RichTextBox::rtfEscape(Text::toT(Text::toDOS(data)));
}

void Parser::data(const string& data) {
	if(suppressDepth == 0) ret += rtfEscape(data);
}

void Parser::endTag(const string& name) {
	if(trimCopy(name) == "emoticon") {
		if(!emoticonSuppressions.empty()) {
			if(emoticonSuppressions.back() && suppressDepth) --suppressDepth;
			emoticonSuppressions.pop_back();
		}
		return;
	}
	if(contexts.size() <= 1) return;
	ret += contexts.back().getEnd();
	contexts.pop_back();
}

tstring Parser::finalize() {
	return Text::toT("{{\\fonttbl" + Util::toString(Util::emptyString, fonts) +
		"}{\\colortbl" + Util::toString(Util::emptyString, colors) + "}") + ret + _T("}");
}

Parser::Context::Context(dwt::RichTextBox* box, Parser& parser) {
	// create a default context with the Rich Edit control's current formatting.
	auto lf = box->getFont()->getLogFont();
	font = parser.addFont("\\fnil\\fcharset" + std::to_string(lf.lfCharSet) + " " + Text::fromT(lf.lfFaceName));
	fontSize = rtfFontSize(static_cast<float>(std::abs(lf.lfHeight)) / dwt::util::dpiFactor());
	if(lf.lfWeight >= FW_BOLD) { setFlag(Bold); }
	if(lf.lfItalic) { setFlag(Italic); }

	textColor = parser.addColor(box->getTextColor());
	bgColor = parser.addColor(box->getBgColor());
}

tstring Parser::Context::getBegin() const {
	string ret = "{";

	if(!link.empty()) {
		/* Wine doesn't support chat links so display them as plain text.
		 * See <https://bugs.winehq.org/show_bug.cgi?id=34824>. */
		if(SETTING(CLICKABLE_CHAT_LINKS)) {
			//RFC 3986 allows {}\ etc... in URIs so links also need escaping for proper display and to avoid formatting issues
			ret += "\\field{\\*\\fldinst HYPERLINK \"" + Text::fromT(rtfEscape(link)) + "\"}{\\fldrslt";
		} else {
			ret += "{";
		}
	}

	ret += "\\f" + std::to_string(font) + "\\fs" + std::to_string(fontSize) +
		"\\cf" + std::to_string(textColor) + "\\highlight" + std::to_string(bgColor);
	if(isSet(Bold)) { ret += "\\b"; }
	if(isSet(Italic)) { ret += "\\i"; }
	if(isSet(Underlined)) { ret += "\\ul"; }

	ret += " ";

	if(!link.empty()) {
		// add an invisible space; otherwise link formatting may get lost...
		ret += "{\\v  }";

		// Throw the link before its label when in the Wine workaround...
		if(!SETTING(CLICKABLE_CHAT_LINKS)) { ret += "<" + link + "> "; }
	}

	return Text::toT(ret);
}

tstring Parser::Context::getEnd() const {
	return link.empty() ? _T("}") : _T("}}");
}

size_t Parser::addFont(string&& font) {
	if(auto existing = fontIndexes.find(font); existing != fontIndexes.end()) return existing->second;
	auto ret = fonts.size();
	fontIndexes.emplace(font, ret);
	fonts.push_back("{\\f" + std::to_string(ret) + std::move(font) + ";}");
	return ret;
}

int Parser::rtfFontSize(float px) {
	// the px value must not take DPI settings into account; the Rich Edit control handles that.
	if(!std::isfinite(px)) px = 16.0f;
	px = std::clamp(px, 1.0f, 256.0f);
	return static_cast<int>(std::floor(px
		* 72.0 / 96.0 // px -> font points
		* 2.0)); // RTF font sizes are expressed in half-points
}

size_t Parser::addColor(COLORREF color) {
	if(auto existing = colorIndexes.find(color); existing != colorIndexes.end()) return existing->second;
	const auto value = "\\red" + std::to_string(GetRValue(color)) +
		"\\green" + std::to_string(GetGValue(color)) +
		"\\blue" + std::to_string(GetBValue(color)) + ";";
	auto ret = colors.size();
	colorIndexes.emplace(color, ret);
	colors.push_back(value);
	colorValues.push_back(color);
	return ret;
}

void Parser::applySemanticStyle(Context& context, const string& id) {
	if(id == "timestamp" || id == "messageTimestamp") {
		context.textColor = addColor(SETTING(CHAT_TIMESTAMP_COLOR));
	} else if(id == "ownTimestamp" || id == "ownMessageTimestamp") {
		context.textColor = addColor(SETTING(CHAT_OWN_TIMESTAMP_COLOR));
	} else if(id == "nick") {
		context.textColor = addColor(SETTING(CHAT_NICK_COLOR));
		context.setFlag(Context::Bold);
	} else if(id == "ownNick") {
		context.textColor = addColor(SETTING(CHAT_OWN_NICK_COLOR));
		context.setFlag(Context::Bold);
	} else if(id == "text") {
		context.textColor = addColor(SETTING(CHAT_TEXT_COLOR));
	} else if(id == "ownText") {
		context.textColor = addColor(SETTING(CHAT_OWN_TEXT_COLOR));
	} else if(id == "systemMessage") {
		context.textColor = addColor(SETTING(CHAT_SYSTEM_COLOR));
		context.setFlag(Context::Italic);
	} else if(id == "mention") {
		context.textColor = addColor(SETTING(CHAT_MENTION_COLOR));
		context.bgColor = addColor(SETTING(CHAT_MENTION_BG_COLOR));
		context.setFlag(Context::Bold);
	}
}

void Parser::ensureContrast(Context& context) {
	if(context.textColor >= colorValues.size() || context.bgColor >= colorValues.size()) return;
	const auto luminance = [](COLORREF color) {
		return (0.2126 * GetRValue(color) + 0.7152 * GetGValue(color) + 0.0722 * GetBValue(color)) / 255.0;
	};
	const auto background = luminance(colorValues[context.bgColor]);
	const auto foreground = luminance(colorValues[context.textColor]);
	const auto high = std::max(background, foreground);
	const auto low = std::min(background, foreground);
	if((high + 0.05) / (low + 0.05) < 3.0) {
		const auto blackContrast = (background + 0.05) / 0.05;
		const auto whiteContrast = 1.05 / (background + 0.05);
		context.textColor = addColor(blackContrast >= whiteContrast ? RGB(0, 0, 0) : RGB(255, 255, 255));
	}
}

void Parser::parseFont(const string& s) {
	// this contains multiple params separated by spaces.
	StringTokenizer<string> tok(s, ' ');
	auto& l = tok.getTokens();

	// remove empty strings.
	l.erase(std::remove_if(l.begin(), l.end(), [](const string& s) { return s.empty(); }), l.end());

	if(l.size() < 2) // the last 2 params (font size & font family) are compulsory.
		return;

	// the last param (font family) may contain spaces; merge if that is the case.
	while(*(l.back().end() - 1) == '\'' && (l.back().size() <= 1 || *l.back().begin() != '\'')) {
		*(l.end() - 2) += ' ' + std::move(l.back());
		l.erase(l.end() - 1);
		if(l.size() < 2)
			return;
	}

	// parse the last param (font family).
	/// @todo handle multiple fonts separated by commas...
	auto& family = l.back();
	family.erase(std::remove(family.begin(), family.end(), '\''), family.end());
	if(family.empty())
		return;
	contexts.back().font = addFont("\\fnil " + std::move(family));

	// parse the second to last param (font size).
	/// @todo handle more than px sizes
	auto& size = *(l.end() - 2);
	if(size.size() > 2 && *(size.end() - 2) == 'p' && *(size.end() - 1) == 'x') { // 16px
		contexts.back().fontSize = rtfFontSize(Util::toFloat(size.substr(0, size.size() - 2)));
	}

	// parse the optional third to last param (font weight).
	if(l.size() > 2 && Util::toInt(*(l.end() - 3)) >= FW_BOLD) {
		contexts.back().setFlag(Context::Bold);
	}

	// parse the optional first param (font style).
	if(l.size() > 2 && l[0] == "italic") {
		contexts.back().setFlag(Context::Italic);
	}
}

void Parser::parseDecoration(const string& s) {
	if(s == "underline") {
		contexts.back().setFlag(Context::Underlined);
	}
}

void Parser::parseColor(size_t& contextColor, const string& s) {
	auto sharp = s.find('#');
	if(sharp != string::npos && s.size() > sharp + 6) {
		try {
#if defined(__MINGW32__) && defined(_GLIBCXX_HAVE_BROKEN_VSWPRINTF)
			/// @todo use stol on MinGW when it's available
			unsigned int color = 0;
			auto colStr = s.substr(sharp + 1, 6);
			sscanf(colStr.c_str(), "%X", &color);
#else
			size_t pos = 0;
			auto color = std::stol(s.substr(sharp + 1, 6), &pos, 16);
#endif
			contextColor = addColor(RGB((color & 0xFF0000) >> 16, (color & 0xFF00) >> 8, color & 0xFF));
		} catch(const std::exception& e) { dcdebug("color parsing exception: %s with str: %s\n", e.what(), s.c_str()); }
	}
}
