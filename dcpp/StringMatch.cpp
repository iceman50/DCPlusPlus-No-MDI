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

#include "stdinc.h"
#include "StringMatch.h"

#include "format.h"
#include "LogManager.h"
#include "StringTokenizer.h"

namespace dcpp {

StringMatch::Method StringMatch::getMethod() const {
	return std::holds_alternative<StringSearch::List>(search) ? PARTIAL : std::holds_alternative<string>(search) ? EXACT : REGEX;
}

void StringMatch::setMethod(Method method) {
	switch(method) {
	case PARTIAL: search = StringSearch::List(); break;
	case EXACT: search = string(); break;
	case REGEX: search = std::regex(); break;
	case METHOD_LAST: break;
	}
}

bool StringMatch::operator==(const StringMatch& rhs) const {
	return pattern == rhs.pattern && getMethod() == rhs.getMethod();
}

bool StringMatch::prepare() {
	if(pattern.empty()) {
		return false;
	}

	return std::visit([this](auto& matcher) {
		using MatcherT = std::decay_t<decltype(matcher)>;
		if constexpr(std::is_same_v<MatcherT, StringSearch::List>) {
			matcher.clear();
			StringTokenizer<string> st(pattern, ' ');
			for(auto& i: st.getTokens()) {
				if(!i.empty()) {
					matcher.emplace_back(i);
				}
			}
			return true;
		} else if constexpr(std::is_same_v<MatcherT, string>) {
			matcher = pattern;
			return true;
		} else {
			try {
				matcher.assign(pattern);
				return true;
			} catch(const std::regex_error&) {
				LogManager::getInstance()->message(str(F_("Invalid regular expression: %1%") % pattern));
				return false;
			}
		}
	}, search);
}

bool StringMatch::match(const string& str) const {
	if(str.empty() || pattern.empty()) {
		return false;
	}

	return std::visit([&str](const auto& matcher) {
		using MatcherT = std::decay_t<decltype(matcher)>;
		if constexpr(std::is_same_v<MatcherT, StringSearch::List>) {
			for(auto& i: matcher) {
				if(!i.match(str)) {
					return false;
				}
			}
			return !matcher.empty();
		} else if constexpr(std::is_same_v<MatcherT, string>) {
			return str == matcher;
		} else {
			try {
				return std::regex_search(str, matcher);
			} catch(const std::regex_error&) {
				// most likely a stack overflow, ignore...
				return false;
			}
		}
	}, search);
}

} // namespace dcpp
