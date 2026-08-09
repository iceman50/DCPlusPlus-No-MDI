/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_LOG_MESSAGE_H
#define DCPLUSPLUS_DCPP_LOG_MESSAGE_H

#include <ctime>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace dcpp {

class LogMessage {
public:
	enum Severity : uint8_t {
		SEV_VERBOSE,
		SEV_INFO,
		SEV_WARNING,
		SEV_ERROR,
		SEV_LAST
	};

	LogMessage(time_t aTime, std::string aText, Severity aSeverity, std::string aArea) noexcept :
		time(aTime), text(std::move(aText)), severity(aSeverity), area(std::move(aArea)) { }

	time_t getTime() const noexcept { return time; }
	const std::string& getText() const noexcept { return text; }
	Severity getSeverity() const noexcept { return severity; }
	const std::string& getArea() const noexcept { return area; }

	static const char* getSeverityName(Severity severity) noexcept {
		switch(severity) {
		case SEV_VERBOSE: return "Verbose";
		case SEV_INFO: return "Info";
		case SEV_WARNING: return "Warning";
		case SEV_ERROR: return "Error";
		default: return "Unknown";
		}
	}

private:
	const time_t time;
	const std::string text;
	const Severity severity;
	const std::string area;
};

using LogMessagePtr = std::shared_ptr<LogMessage>;

} // namespace dcpp

#endif // !defined(DCPLUSPLUS_DCPP_LOG_MESSAGE_H)
