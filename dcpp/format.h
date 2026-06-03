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

#ifndef DCPLUSPLUS_DCPP_FORMAT_H_
#define DCPLUSPLUS_DCPP_FORMAT_H_

#include <functional>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <vector>

#include <libintl.h>

// libintl's "#define snprintf libintl_snprintf" does not play nicely with
// Boost using std::snprintf. https://github.com/boostorg/system/issues/32
// describes this issue in the context of #define snprintf _snprintf which
// creates analogous problems. libintl's snprintf isn't part of localizing
// per se, so allow Boost to use std::snprintf(...), unobstructed by macro
// definitions.
//
// https://www.c-plusplus.net/forum/topic/231492/bin-am-verzweifeln-error-libintl_snprintf-is-not-a-member-of-std
// notes, for example: "Für mich sieht das sehr stark danach aus, daß einer
// deiner anderen Header sowas wie #define snprintf libintl_snprintf macht.
// Und sowas macht man natürlich einfach nicht." But libintl does, exactly,
// that.
#undef snprintf

#ifdef BUILDING_DCPP

#define PACKAGE "libdcpp"
#define LOCALEDIR dcpp::Util::getPath(Util::PATH_LOCALE).c_str()
#define _(String) dgettext(PACKAGE, String)
#define gettext_noop(String) String
#define N_(String) gettext_noop (String)
#define F_(String) dcpp::dcpp_fmt(dgettext(PACKAGE, String))
#define FN_(String1,String2, N) dcpp::dcpp_fmt(dngettext(PACKAGE, String1, String2, N))

#endif

namespace dcpp {

template<typename T>
class basic_dcpp_format {
public:
	using string_type = std::basic_string<T>;
	using arg_formatter = std::function<string_type(const string_type&)>;

	explicit basic_dcpp_format(string_type fmt) : formatString(std::move(fmt)) { }

	template<typename V>
	basic_dcpp_format& operator%(V&& value) {
		values.push_back(makeFormatter(std::forward<V>(value)));
		return *this;
	}

	string_type str() const {
		string_type out;
		out.reserve(formatString.size());

		for(size_t i = 0; i < formatString.size();) {
			if(formatString[i] != static_cast<T>('%')) {
				out += formatString[i++];
				continue;
			}

			if(i + 1 < formatString.size() && formatString[i + 1] == static_cast<T>('%')) {
				out += static_cast<T>('%');
				i += 2;
				continue;
			}

			size_t j = i + 1;
			size_t index = 0;
			while(j < formatString.size() && isDigit(formatString[j])) {
				index = index * 10 + static_cast<size_t>(formatString[j] - static_cast<T>('0'));
				++j;
			}

			if(index == 0 || j >= formatString.size()) {
				out += formatString[i++];
				continue;
			}

			string_type spec;
			if(formatString[j] == static_cast<T>('%')) {
				++j;
			} else if(formatString[j] == static_cast<T>('$')) {
				++j;
				size_t specStart = j;
				while(j < formatString.size() && !isAlpha(formatString[j])) {
					++j;
				}
				if(j < formatString.size()) {
					spec.assign(formatString.begin() + specStart, formatString.begin() + j + 1);
					++j;
				} else {
					out += formatString[i++];
					continue;
				}
			} else {
				out += formatString[i++];
				continue;
			}

			out += render(index - 1, spec);
			i = j;
		}

		return out;
	}

private:
	string_type formatString;
	std::vector<arg_formatter> values;

	static bool isDigit(T c) {
		return c >= static_cast<T>('0') && c <= static_cast<T>('9');
	}

	static bool isAlpha(T c) {
		return (c >= static_cast<T>('a') && c <= static_cast<T>('z')) || (c >= static_cast<T>('A') && c <= static_cast<T>('Z'));
	}

	template<typename V>
	static arg_formatter makeFormatter(V&& value) {
		using D = std::decay_t<V>;
		if constexpr(std::is_same_v<D, string_type>) {
			return [captured = std::forward<V>(value)](const string_type&) { return captured; };
		} else if constexpr(std::is_same_v<D, const T*>) {
			return [captured = std::forward<V>(value)](const string_type&) { return captured ? string_type(captured) : string_type(); };
		} else if constexpr(std::is_same_v<D, T*>) {
			return [captured = std::forward<V>(value)](const string_type&) { return captured ? string_type(captured) : string_type(); };
		} else {
			return [captured = std::forward<V>(value)](const string_type& spec) {
				return formatValue(captured, spec);
			};
		}
	}

	template<typename V>
	static string_type formatValue(const V& value, const string_type& spec) {
		std::basic_ostringstream<T> os;
		if(spec.empty()) {
			os << value;
			return os.str();
		}

		const auto type = static_cast<char>(spec.back());
		if constexpr(std::is_arithmetic_v<V>) {
			int precision = -1;
			int width = 0;
			for(size_t i = 0; i + 1 < spec.size(); ++i) {
				if(spec[i] == static_cast<T>('.')) {
					precision = 0;
					for(size_t j = i + 1; j + 1 < spec.size() && isDigit(spec[j]); ++j) {
						precision = precision * 10 + static_cast<int>(spec[j] - static_cast<T>('0'));
					}
					break;
				}
				if(isDigit(spec[i])) {
					width = width * 10 + static_cast<int>(spec[i] - static_cast<T>('0'));
				}
			}

			if(width > 0) {
				os << std::setw(width);
			}

			switch(type) {
			case 'x': os << std::hex << std::nouppercase; break;
			case 'X': os << std::hex << std::uppercase; break;
			case 'o': os << std::oct; break;
			case 'd':
			case 'i':
			case 'u': os << std::dec; break;
			case 'f':
			case 'F':
				os << std::fixed;
				if(precision >= 0) {
					os << std::setprecision(precision);
				}
				break;
			case 'e':
			case 'E':
				os << std::scientific;
				if(precision >= 0) {
					os << std::setprecision(precision);
				}
				if(type == 'E') {
					os << std::uppercase;
				}
				break;
			case 'g':
			case 'G':
				if(precision >= 0) {
					os << std::setprecision(precision);
				}
				if(type == 'G') {
					os << std::uppercase;
				}
				break;
			default:
				break;
			}
		}

		os << value;
		return os.str();
	}

	string_type render(size_t index, const string_type& spec) const {
		if(index >= values.size()) {
			return string_type();
		}

		return values[index](spec);
	}
};

template<typename T>
basic_dcpp_format<T> dcpp_fmt(const std::basic_string<T>& t) {
	return basic_dcpp_format<T>(t);
}

template<typename T>
basic_dcpp_format<T> dcpp_fmt(const T* t) {
	return dcpp_fmt(std::basic_string<T>(t));
}

}

template<typename T>
std::basic_string<T> str(const dcpp::basic_dcpp_format<T>& fmt) {
	return fmt.str();
}

#endif /* DCPLUSPLUS_DCPP_FORMAT_H_ */
