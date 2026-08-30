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
#include "LogManager.h"

#include "AdcCommand.h"
#include "File.h"
#include "Text.h"
#include "TimerManager.h"

namespace dcpp {

void LogManager::log(Area area, ParamMap& params) noexcept {
	log(getPath(area, params), Util::formatParams(getSetting(area, FORMAT), params));
}

void LogManager::message(const string& msg, LogMessage::Severity severity, const string& area) noexcept {
	auto messageData = std::make_shared<LogMessage>(GET_TIME(), msg, severity, area);
	if(SETTING(LOG_SYSTEM)) {
		ParamMap params;
		params["message"] = msg;
		params["level"] = LogMessage::getSeverityName(severity);
		params["area"] = area;
		log(SYSTEM, params);
	}
	{
		Lock l(cs);
		// Keep the last 100 messages (completely arbitrary number...)
		while(lastLogs.size() >= 100)
			lastLogs.pop_front();
		lastLogs.push_back(messageData);
	}
	fire(LogManagerListener::Message(), messageData);
}

string LogManager::escapeProtocolData(const string& data, size_t maxBytes) {
	static const char hex[] = "0123456789ABCDEF";
	const auto validUtf8 = Text::validateUtf8(data);
	const auto length = std::min(data.size(), maxBytes);
	string escaped;
	escaped.reserve(length);

	for(size_t i = 0; i < length; ++i) {
		const auto c = static_cast<uint8_t>(data[i]);
		switch(c) {
		case '\\': escaped += "\\\\"; break;
		case '\r': escaped += "\\r"; break;
		case '\n': escaped += "\\n"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if(c < 0x20 || c == 0x7f || (!validUtf8 && c >= 0x80)) {
				escaped += "\\x";
				escaped += hex[c >> 4];
				escaped += hex[c & 0x0f];
			} else {
				escaped += static_cast<char>(c);
			}
		}
	}

	if(length < data.size()) {
		escaped += "... (truncated, ";
		escaped += Util::toString(static_cast<int64_t>(data.size()));
		escaped += " bytes total)";
	}
	return escaped;
}

string LogManager::getProtocolArea(ProtocolCategory category) {
	const string protocol = _("Protocol");
	switch(category) {
	case PROTOCOL_ADC_STA: return protocol + " / ADC STA";
	case PROTOCOL_NMDC_SPOOF: return protocol + " / " + _("NMDC Spoof");
	default: return protocol;
	}
}

void LogManager::protocol(ProtocolCategory category, ProtocolDirection direction, const string& endpoint,
	const string& data, LogMessage::Severity severity) noexcept
{
	string entry = direction == PROTOCOL_IN ? "[IN]" : "[OUT]";
	if(!endpoint.empty()) {
		entry += " [";
		entry += escapeProtocolData(endpoint, 1024);
		entry += ']';
	}
	if(!data.empty()) {
		entry += ' ';
		entry += escapeProtocolData(data);
	}
	message(entry, severity, getProtocolArea(category));
}

LogMessage::Severity LogManager::getAdcStatusSeverity(const string& data) noexcept {
	const auto nmdc = data.compare(0, 7, "$ADCSTA") == 0;
	try {
		AdcCommand command(data, nmdc);
		const size_t statusParam = command.getType() == AdcCommand::TYPE_UDP ? 1 : 0;
		if(command.getCommand() == AdcCommand::CMD_STA && command.getParameters().size() > statusParam &&
			!command.getParam(statusParam).empty())
		{
			switch(command.getParam(statusParam)[0]) {
			case '0': return LogMessage::SEV_INFO;
			case '1': return LogMessage::SEV_WARNING;
			case '2': return LogMessage::SEV_ERROR;
			}
		}
	} catch(const ParseException&) {
		// Malformed STA-shaped input is classified as a warning.
	}
	return LogMessage::SEV_WARNING;
}

void LogManager::adcStatus(ProtocolDirection direction, const string& endpoint, const string& data) noexcept {
	const auto nmdc = data.compare(0, 7, "$ADCSTA") == 0;
	if(!nmdc && (data.size() < 4 || data[1] != 'S' || data[2] != 'T' || data[3] != 'A')) {
		return;
	}
	protocol(PROTOCOL_ADC_STA, direction, endpoint, data, getAdcStatusSeverity(data));
}

LogManager::List LogManager::getLastLogs() {
	Lock l(cs);
	return lastLogs;
}

string LogManager::getPath(Area area, ParamMap& params) const {
	return SETTING(LOG_DIRECTORY) + Util::formatParams(getSetting(area, FILE), params, Util::cleanPathChars);
}

string LogManager::getPath(Area area) const {
	ParamMap params;
	return getPath(area, params);
}

const string& LogManager::getSetting(int area, int sel) const {
	return SettingsManager::getInstance()->get(static_cast<SettingsManager::StrSetting>(options[area][sel]), true);
}

void LogManager::saveSetting(int area, int sel, const string& setting) {
	SettingsManager::getInstance()->set(static_cast<SettingsManager::StrSetting>(options[area][sel]), setting);
}

void LogManager::log(const string& area, const string& msg) noexcept {
	Lock l(cs);
	try {
		string aArea = Util::validateFileName(area);
		File::ensureDirectory(aArea);
		File f(aArea, File::WRITE, File::OPEN | File::CREATE);
		f.setEndPos(0);
		f.write(msg + "\r\n");
	} catch (const FileException&) {
		// ...
	}
}

LogManager::LogManager() {
	options[UPLOAD][FILE]		= SettingsManager::LOG_FILE_UPLOAD;
	options[UPLOAD][FORMAT]		= SettingsManager::LOG_FORMAT_POST_UPLOAD;
	options[DOWNLOAD][FILE]		= SettingsManager::LOG_FILE_DOWNLOAD;
	options[DOWNLOAD][FORMAT]	= SettingsManager::LOG_FORMAT_POST_DOWNLOAD;
	options[FINISHED_DOWNLOAD][FILE] = SettingsManager::LOG_FILE_FINISHED_DOWNLOAD;
	options[FINISHED_DOWNLOAD][FORMAT] = SettingsManager::LOG_FORMAT_POST_FINISHED_DOWNLOAD;
	options[CHAT][FILE]		= SettingsManager::LOG_FILE_MAIN_CHAT;
	options[CHAT][FORMAT]		= SettingsManager::LOG_FORMAT_MAIN_CHAT;
	options[PM][FILE]		= SettingsManager::LOG_FILE_PRIVATE_CHAT;
	options[PM][FORMAT]		= SettingsManager::LOG_FORMAT_PRIVATE_CHAT;
	options[SYSTEM][FILE]		= SettingsManager::LOG_FILE_SYSTEM;
	options[SYSTEM][FORMAT]		= SettingsManager::LOG_FORMAT_SYSTEM;
	options[STATUS][FILE]		= SettingsManager::LOG_FILE_STATUS;
	options[STATUS][FORMAT]		= SettingsManager::LOG_FORMAT_STATUS;
}

LogManager::~LogManager() {
}

} // namespace dcpp
