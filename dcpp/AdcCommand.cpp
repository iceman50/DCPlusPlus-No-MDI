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
#include "AdcCommand.h"

#include "ClientManager.h"

#include <limits>

namespace dcpp {

namespace {

bool isSimpleAlpha(char value) noexcept {
	return value >= 'A' && value <= 'Z';
}

bool isSimpleAlphaNum(char value) noexcept {
	return isSimpleAlpha(value) || (value >= '0' && value <= '9');
}

bool isBase32(const string& value) noexcept {
	if(value.empty()) {
		return false;
	}

	for(const auto ch: value) {
		if(!isSimpleAlpha(ch) && (ch < '2' || ch > '7')) {
			return false;
		}
	}
	return true;
}

bool isNamedParam(const string& value) noexcept {
	return value.size() >= 2 && isSimpleAlpha(value[0]) && isSimpleAlphaNum(value[1]);
}

bool areNamedParams(const StringList& params, size_t start) noexcept {
	for(auto i = start; i < params.size(); ++i) {
		if(!isNamedParam(params[i])) {
			return false;
		}
	}
	return true;
}

bool parseInteger(const string& value, int64_t& result) noexcept {
	if(value.empty()) {
		return false;
	}

	size_t pos = 0;
	const bool negative = value[0] == '-';
	if(negative && ++pos == value.size()) {
		return false;
	}

	const uint64_t positiveLimit = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
	const uint64_t limit = negative ? positiveLimit + 1 : positiveLimit;
	uint64_t parsed = 0;
	for(; pos < value.size(); ++pos) {
		const auto ch = value[pos];
		if(ch < '0' || ch > '9') {
			return false;
		}
		const auto digit = static_cast<uint64_t>(ch - '0');
		if(parsed > (limit - digit) / 10) {
			return false;
		}
		parsed = parsed * 10 + digit;
	}

	if(negative) {
		result = parsed == limit ? std::numeric_limits<int64_t>::min() : -static_cast<int64_t>(parsed);
	} else {
		result = static_cast<int64_t>(parsed);
	}
	return true;
}

bool isNonNegativeInteger(const string& value) noexcept {
	int64_t parsed = 0;
	return parseInteger(value, parsed) && parsed >= 0;
}

bool isPort(const string& value) noexcept {
	int64_t parsed = 0;
	return value.size() <= 5 && parseInteger(value, parsed) && parsed > 0 && parsed <= 65535;
}

bool isOptionalPort(const string& value) noexcept {
	int64_t parsed = 0;
	return value.size() <= 5 && parseInteger(value, parsed) && parsed >= 0 && parsed <= 65535;
}

bool isProtocolName(const string& value) noexcept {
	if(value.empty()) {
		return false;
	}
	for(const auto ch: value) {
		const auto byte = static_cast<uint8_t>(ch);
		if(byte < 33 || byte > 127) {
			return false;
		}
	}
	return true;
}

bool isTransferType(const string& value) noexcept {
	if(value.empty()) {
		return false;
	}
	for(const auto ch: value) {
		if(!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
			return false;
		}
	}
	return true;
}

bool isTransferRange(const StringList& params, bool snd) noexcept {
	if(params.size() < 4 || !isTransferType(params[0]) || params[1].empty()) {
		return false;
	}

	int64_t start = 0;
	int64_t bytes = 0;
	if(!parseInteger(params[2], start) || !parseInteger(params[3], bytes) || start < 0 || bytes < (snd ? 0 : -1)) {
		return false;
	}
	if(bytes >= 0 && start > std::numeric_limits<int64_t>::max() - bytes) {
		return false;
	}
	if(params[0] == "blom") {
		const auto maxBloomBytes = std::min<uint64_t>(std::numeric_limits<uint32_t>::max(),
			std::numeric_limits<size_t>::max() / 8);
		return params[1] == "/" && start == 0 && bytes >= 0 && static_cast<uint64_t>(bytes) <= maxBloomBytes;
	}
	return true;
}

bool validInfParams(const StringList& params) noexcept {
	for(const auto& param: params) {
		if(!isNamedParam(param)) {
			return false;
		}
		const auto value = param.substr(2);
		const auto code = AdcCommand::toCode(param.c_str());
		switch(code) {
		case AdcCommand::toCode("HN"):
		case AdcCommand::toCode("HR"):
		case AdcCommand::toCode("HO"):
		case AdcCommand::toCode("CT"):
		case AdcCommand::toCode("SL"):
		case AdcCommand::toCode("FS"):
		case AdcCommand::toCode("SS"):
		case AdcCommand::toCode("SF"):
		case AdcCommand::toCode("DS"):
		case AdcCommand::toCode("US"):
		case AdcCommand::toCode("AS"):
		case AdcCommand::toCode("AM"):
			if(!value.empty() && !isNonNegativeInteger(value)) {
				return false;
			}
			break;
		case AdcCommand::toCode("AW"):
			if(!value.empty() && value != "1" && value != "2") {
				return false;
			}
			break;
		case AdcCommand::toCode("U4"):
		case AdcCommand::toCode("U6"):
			if(!value.empty() && !isOptionalPort(value)) {
				return false;
			}
			break;
		case AdcCommand::toCode("ID"):
		case AdcCommand::toCode("PD"):
			if(!value.empty() && !isBase32(value)) {
				return false;
			}
			break;
		}
	}
	return true;
}

bool validSearchParams(const StringList& params) noexcept {
	for(const auto& param: params) {
		if(!isNamedParam(param)) {
			return false;
		}
		const auto value = param.substr(2);
		const auto code = AdcCommand::toCode(param.c_str());
		if((code == AdcCommand::toCode("LE") || code == AdcCommand::toCode("GE") ||
			code == AdcCommand::toCode("EQ")) && !isNonNegativeInteger(value))
		{
			return false;
		}
		if(code == AdcCommand::toCode("TY") && value != "1" && value != "2") {
			return false;
		}
	}
	return true;
}

bool validResultParams(const StringList& params) noexcept {
	for(const auto& param: params) {
		if(!isNamedParam(param)) {
			return false;
		}
		const auto code = AdcCommand::toCode(param.c_str());
		if((code == AdcCommand::toCode("SI") || code == AdcCommand::toCode("SL")) &&
			!isNonNegativeInteger(param.substr(2)))
		{
			return false;
		}
	}
	return true;
}

unsigned contextMask(uint32_t command) noexcept {
	const unsigned fromHub = 1U << AdcCommand::CONTEXT_FROM_HUB;
	const unsigned toHub = 1U << AdcCommand::CONTEXT_TO_HUB;
	const unsigned client = 1U << AdcCommand::CONTEXT_CLIENT;
	const unsigned udp = 1U << AdcCommand::CONTEXT_UDP;

	switch(command) {
	case AdcCommand::CMD_STA: return fromHub | toHub | client | udp;
	case AdcCommand::CMD_SUP: return fromHub | toHub | client;
	case AdcCommand::CMD_SID: return fromHub;
	case AdcCommand::CMD_INF: return fromHub | toHub | client;
	case AdcCommand::CMD_MSG: return fromHub | toHub | client; // CCPM extends MSG to C-C.
	case AdcCommand::CMD_SCH: return fromHub | toHub | client | udp;
	case AdcCommand::CMD_RES: return fromHub | toHub | client | udp;
	case AdcCommand::CMD_CTM:
	case AdcCommand::CMD_RCM: return fromHub | toHub;
	case AdcCommand::CMD_GPA: return fromHub;
	case AdcCommand::CMD_PAS: return toHub;
	case AdcCommand::CMD_QUI: return fromHub;
	case AdcCommand::CMD_GET:
	case AdcCommand::CMD_SND: return fromHub | toHub | client; // BLOM also uses hub transfers.
	case AdcCommand::CMD_GFI: return client;
	case AdcCommand::CMD_CMD: return fromHub;
	case AdcCommand::CMD_NAT:
	case AdcCommand::CMD_RNT: return fromHub | toHub;
	case AdcCommand::CMD_ZON:
	case AdcCommand::CMD_ZOF: return fromHub | toHub | client;
	case AdcCommand::CMD_PMI: return client;
	case AdcCommand::CMD_TCP: return fromHub | toHub;
	default: return fromHub | toHub | client | udp;
	}
}

bool validTypeForContext(char type, AdcCommand::ProtocolContext context) noexcept {
	switch(context) {
	case AdcCommand::CONTEXT_FROM_HUB:
		return type == AdcCommand::TYPE_INFO || type == AdcCommand::TYPE_BROADCAST ||
			type == AdcCommand::TYPE_DIRECT || type == AdcCommand::TYPE_ECHO || type == AdcCommand::TYPE_FEATURE;
	case AdcCommand::CONTEXT_TO_HUB:
		return type == AdcCommand::TYPE_HUB || type == AdcCommand::TYPE_BROADCAST ||
			type == AdcCommand::TYPE_DIRECT || type == AdcCommand::TYPE_ECHO || type == AdcCommand::TYPE_FEATURE;
	case AdcCommand::CONTEXT_CLIENT:
		return type == AdcCommand::TYPE_CLIENT;
	case AdcCommand::CONTEXT_UDP:
		return type == AdcCommand::TYPE_UDP;
	}
	return false;
}

}

AdcCommand::AdcCommand(uint32_t aCmd, char aType /* = TYPE_CLIENT */) : cmdInt(aCmd), from(0), to(0), type(aType) { }
AdcCommand::AdcCommand(uint32_t aCmd, const uint32_t aTarget, char aType) : cmdInt(aCmd), from(0), to(aTarget), type(aType) { }
AdcCommand::AdcCommand(Severity sev, Error err, const string& desc, char aType /* = TYPE_CLIENT */) : cmdInt(CMD_STA), from(0), to(0), type(aType) {
	addParam((sev == SEV_SUCCESS) ? "000" : Util::toString(sev * 100 + err));
	addParam(desc);
}

AdcCommand::AdcCommand(const string& aLine, bool nmdc /* = false */) : cmdInt(0), from(0), to(0), type(TYPE_CLIENT) {
	parse(aLine, nmdc);
}

void AdcCommand::parse(const string& aLine, bool nmdc /* = false */) {
	string::size_type i = 5;

	if(nmdc) {
		// "$ADCxxx ..."
		if(aLine.length() < 7)
			throw ParseException("Too short");
		type = TYPE_CLIENT;
		cmd[0] = aLine[4];
		cmd[1] = aLine[5];
		cmd[2] = aLine[6];
		i += 3;
	} else {
		// "yxxx ..."
		if(aLine.length() < 4)
			throw ParseException("Too short");
		type = aLine[0];
		cmd[0] = aLine[1];
		cmd[1] = aLine[2];
		cmd[2] = aLine[3];
	}

	if(type != TYPE_BROADCAST && type != TYPE_CLIENT && type != TYPE_DIRECT && type != TYPE_ECHO && type != TYPE_FEATURE && type != TYPE_INFO && type != TYPE_HUB && type != TYPE_UDP) {
		throw ParseException("Invalid type");
	}
	if(!isSimpleAlpha(cmd[0]) || !isSimpleAlphaNum(cmd[1]) || !isSimpleAlphaNum(cmd[2])) {
		throw ParseException("Invalid command name");
	}

	if(type == TYPE_INFO) {
		from = HUB_SID;
	}

	string::size_type len = aLine.length();
	const char* buf = aLine.c_str();
	string cur;
	cur.reserve(128);

	bool toSet = false;
	bool featureSet = false;
	bool fromSet = nmdc; // $ADCxxx never have a from CID...

	while(i < len) {
		switch(buf[i]) {
		case '\\':
			++i;
			if(i == len)
				throw ParseException("Escape at eol");
			if(buf[i] == 's')
				cur += ' ';
			else if(buf[i] == 'n')
				cur += '\n';
			else if(buf[i] == '\\')
				cur += '\\';
			else if(buf[i] == ' ' && nmdc)	// $ADCGET escaping, leftover from old specs
				cur += ' ';
			else
				throw ParseException("Unknown escape");
			break;
		case ' ':
			// New parameter...
			{
				if((type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) && !fromSet) {
					if(cur.length() != 4 || !isBase32(cur)) {
						throw ParseException("Invalid SID length");
					}
					from = toSID(cur);
					fromSet = true;
				} else if((type == TYPE_DIRECT || type == TYPE_ECHO) && !toSet) {
					if(cur.length() != 4 || !isBase32(cur)) {
						throw ParseException("Invalid SID length");
					}
					to = toSID(cur);
					toSet = true;
				} else if(type == TYPE_FEATURE && !featureSet) {
					if(cur.empty() || cur.length() % 5 != 0) {
						throw ParseException("Invalid feature length");
					}
					features = cur;
					featureSet = true;
				} else {
					parameters.push_back(cur);
				}
				cur.clear();
			}
			break;
		default:
			cur += buf[i];
		}
		++i;
	}
	if(!cur.empty()) {
		if((type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) && !fromSet) {
			if(cur.length() != 4 || !isBase32(cur)) {
				throw ParseException("Invalid SID length");
			}
			from = toSID(cur);
			fromSet = true;
		} else if((type == TYPE_DIRECT || type == TYPE_ECHO) && !toSet) {
			if(cur.length() != 4 || !isBase32(cur)) {
				throw ParseException("Invalid SID length");
			}
			to = toSID(cur);
			toSet = true;
		} else if(type == TYPE_FEATURE && !featureSet) {
			if(cur.empty() || cur.length() % 5 != 0) {
				throw ParseException("Invalid feature length");
			}
			features = cur;
			featureSet = true;
		} else {
			parameters.push_back(cur);
		}
	}

	if((type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) && !fromSet) {
		throw ParseException("Missing from_sid");
	}

	if(type == TYPE_FEATURE && !featureSet) {
		throw ParseException("Missing feature");
	}

	if((type == TYPE_DIRECT || type == TYPE_ECHO) && !toSet) {
		throw ParseException("Missing to_sid");
	}
}

bool AdcCommand::isAllowedInState(uint32_t command, ProtocolState state) noexcept {
	switch(command) {
	case CMD_STA:
		return state != STATE_DATA;
	case CMD_SUP:
		return state == STATE_PROTOCOL || state == STATE_NORMAL;
	case CMD_SID:
		return state == STATE_PROTOCOL;
	case CMD_INF:
		return state == STATE_IDENTIFY || state == STATE_NORMAL;
	case CMD_GPA:
		return true;
	case CMD_PAS:
		return state == STATE_VERIFY;
	case CMD_QUI:
		return state == STATE_IDENTIFY || state == STATE_VERIFY || state == STATE_NORMAL;
	default:
		return state == STATE_NORMAL;
	}
}

bool AdcCommand::isValidSyntax() const noexcept {
	if(type != TYPE_BROADCAST && type != TYPE_CLIENT && type != TYPE_DIRECT && type != TYPE_ECHO &&
		type != TYPE_FEATURE && type != TYPE_INFO && type != TYPE_HUB && type != TYPE_UDP)
	{
		return false;
	}
	if(!isSimpleAlpha(cmd[0]) || !isSimpleAlphaNum(cmd[1]) || !isSimpleAlphaNum(cmd[2]) || cmd[3] != 0) {
		return false;
	}
	if(type == TYPE_FEATURE) {
		if(features.empty() || features.size() % 5 != 0) {
			return false;
		}
		for(size_t i = 0; i < features.size(); i += 5) {
			if((features[i] != '+' && features[i] != '-') || !isSimpleAlpha(features[i + 1]) ||
				!isSimpleAlphaNum(features[i + 2]) || !isSimpleAlphaNum(features[i + 3]) ||
				!isSimpleAlphaNum(features[i + 4]))
			{
				return false;
			}
		}
	} else if(!features.empty()) {
		return false;
	}
	if((type == TYPE_DIRECT || type == TYPE_ECHO) && !isBase32(fromSID(to))) {
		return false;
	}

	for(const auto& param: parameters) {
		if(param.empty()) {
			return false;
		}
	}

	switch(cmdInt) {
	case CMD_STA:
		if(parameters.size() < 2 || parameters[0].size() != 3 || parameters[1].empty() ||
			parameters[0][0] < '0' || parameters[0][0] > '2' ||
			parameters[0][1] < '0' || parameters[0][1] > '9' ||
			parameters[0][2] < '0' || parameters[0][2] > '9' ||
			(parameters[0][0] == '0' && parameters[0] != "000"))
		{
			return false;
		}
		if(!areNamedParams(parameters, 2)) {
			return false;
		}
		for(size_t i = 2; i < parameters.size(); ++i) {
			const auto code = toCode(parameters[i].c_str());
			if(code == toCode("QP") && !isNonNegativeInteger(parameters[i].substr(2))) {
				return false;
			}
			if(code == toCode("FC") && parameters[i].size() != 6) {
				return false;
			}
		}
		return true;
	case CMD_SUP:
		if(parameters.empty()) {
			return false;
		}
		for(const auto& param: parameters) {
			if(param.size() != 6 || (param.compare(0, 2, "AD") != 0 && param.compare(0, 2, "RM") != 0) ||
				!isSimpleAlpha(param[2]) || !isSimpleAlphaNum(param[3]) ||
				!isSimpleAlphaNum(param[4]) || !isSimpleAlphaNum(param[5]))
			{
				return false;
			}
		}
		return true;
	case CMD_SID:
		return parameters.size() == 1 && parameters[0].size() == 4 && isBase32(parameters[0]);
	case CMD_INF:
		return validInfParams(parameters);
	case CMD_MSG:
		if(parameters.empty() || !areNamedParams(parameters, 1)) {
			return false;
		}
		for(size_t i = 1; i < parameters.size(); ++i) {
			const auto code = toCode(parameters[i].c_str());
			const auto value = parameters[i].substr(2);
			if(code == toCode("TS") && !isNonNegativeInteger(value)) {
				return false;
			}
			if(code == toCode("PM") && (value.size() != 4 || !isBase32(value))) {
				return false;
			}
		}
		return true;
	case CMD_SCH:
		return validSearchParams(parameters);
	case CMD_RES:
		return validResultParams(parameters);
	case CMD_CTM:
	case CMD_NAT:
	case CMD_RNT:
		return parameters.size() >= 3 && isProtocolName(parameters[0]) && isPort(parameters[1]) &&
			!parameters[2].empty() && areNamedParams(parameters, 3);
	case CMD_RCM:
		return parameters.size() >= 2 && isProtocolName(parameters[0]) && !parameters[1].empty() &&
			areNamedParams(parameters, 2);
	case CMD_GPA:
		return parameters.size() == 1 && parameters[0].size() >= 39 && isBase32(parameters[0]);
	case CMD_PAS:
		return parameters.size() == 1 && isBase32(parameters[0]);
	case CMD_QUI:
		if(parameters.empty() || parameters[0].size() != 4 || !isBase32(parameters[0]) ||
			!areNamedParams(parameters, 1))
		{
			return false;
		}
		for(size_t i = 1; i < parameters.size(); ++i) {
			const auto code = toCode(parameters[i].c_str());
			if(code == toCode("TL")) {
				int64_t value = 0;
				if(!parseInteger(parameters[i].substr(2), value) || value < -1 ||
					value > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
				{
					return false;
				}
			}
			if(code == toCode("ID")) {
				const auto value = parameters[i].substr(2);
				if(value.size() != 4 || !isBase32(value)) {
					return false;
				}
			}
		}
		return true;
	case CMD_GET:
		return isTransferRange(parameters, false) && areNamedParams(parameters, 4);
	case CMD_SND:
		return isTransferRange(parameters, true) && areNamedParams(parameters, 4);
	case CMD_GFI:
		return parameters.size() >= 2 && isTransferType(parameters[0]) && !parameters[1].empty() &&
			areNamedParams(parameters, 2);
	case CMD_CMD:
		if(parameters.empty() || parameters[0].empty() || !areNamedParams(parameters, 1)) {
			return false;
		}
		for(size_t i = 1; i < parameters.size(); ++i) {
			if(toCode(parameters[i].c_str()) == toCode("CT")) {
				int64_t value = 0;
				if(!parseInteger(parameters[i].substr(2), value) || value < 1 || value > 15) {
					return false;
				}
			}
		}
		return true;
	case CMD_ZON:
	case CMD_ZOF:
		return parameters.empty();
	case CMD_PMI:
	case CMD_TCP:
		return areNamedParams(parameters, 0);
	default:
		return true;
	}
}

bool AdcCommand::isValidFor(ProtocolState state, ProtocolContext context) const noexcept {
	return isValidSyntax() && isAllowedInState(cmdInt, state) && validTypeForContext(type, context) &&
		(contextMask(cmdInt) & (1U << context)) != 0;
}

string AdcCommand::toString(const CID& aCID) const {
	return getHeaderString(aCID) + getParamString(false);
}

string AdcCommand::toString(uint32_t sid /* = 0 */, bool nmdc /* = false */) const {
	return getHeaderString(sid, nmdc) + getParamString(nmdc);
}

string AdcCommand::escape(const string& str, bool old) {
	string tmp = str;
	string::size_type i = 0;
	while( (i = tmp.find_first_of(" \n\\", i)) != string::npos) {
		if(old) {
			tmp.insert(i, "\\");
		} else {
			switch(tmp[i]) {
				case ' ': tmp.replace(i, 1, "\\s"); break;
				case '\n': tmp.replace(i, 1, "\\n"); break;
				case '\\': tmp.replace(i, 1, "\\\\"); break;
			}
		}
		i+=2;
	}
	return tmp;
}

string AdcCommand::getHeaderString(uint32_t sid, bool nmdc) const {
	string tmp;
	if(nmdc) {
		tmp += "$ADC";
	} else {
		tmp += getType();
	}

	tmp += cmdChar;

	if(type == TYPE_BROADCAST || type == TYPE_DIRECT || type == TYPE_ECHO || type == TYPE_FEATURE) {
		tmp += ' ';
		tmp += fromSID(sid);
	}

	if(type == TYPE_DIRECT || type == TYPE_ECHO) {
		tmp += ' ';
		tmp += fromSID(to);
	}

	if(type == TYPE_FEATURE) {
		tmp += ' ';
		tmp += features;
	}
	return tmp;
}

string AdcCommand::getHeaderString(const CID& cid) const {
	dcassert(type == TYPE_UDP);
	string tmp;

	tmp += getType();
	tmp += cmdChar;
	tmp += ' ';
	tmp += cid.toBase32();
	return tmp;
}

string AdcCommand::getParamString(bool nmdc) const {
	string tmp;
	for(auto& i: getParameters()) {
		tmp += ' ';
		tmp += escape(i, nmdc);
	}
	if(nmdc) {
		tmp += '|';
	} else {
		tmp += '\n';
	}
	return tmp;
}

const string& AdcCommand::getParam(size_t n) const {
	return getParameters().size() > n ? getParameters()[n] : Util::emptyString;
}

bool AdcCommand::getParam(const char* name, size_t start, string& ret) const {
	for(auto i = start; i < getParameters().size(); ++i) {
		if(getParameters()[i].size() >= 2 && toCode(name) == toCode(getParameters()[i].c_str())) {
			ret = getParameters()[i].substr(2);
			return true;
		}
	}
	return false;
}

bool AdcCommand::hasFlag(const char* name, size_t start) const {
	for(auto i = start; i < getParameters().size(); ++i) {
		if(getParameters()[i].size() >= 2 && toCode(name) == toCode(getParameters()[i].c_str()) &&
			getParameters()[i].size() == 3 &&
			getParameters()[i][2] == '1')
		{
			return true;
		}
	}
	return false;
}

} // namespace dcpp
