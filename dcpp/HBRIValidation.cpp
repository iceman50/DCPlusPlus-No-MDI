/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "HBRIValidation.h"

#include "AdcCommand.h"
#include "CryptoManager.h"
#include "format.h"
#include "SettingsManager.h"
#include "Socket.h"
#include "SSLSocket.h"
#include "Text.h"
#include "TimerManager.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace dcpp {

namespace {
	constexpr uint64_t HBRI_TIMEOUT = 10 * 1000;
	constexpr size_t MAX_HBRI_LINE = 8 * 1024;

	string invalidResponse() {
		return _("The hub returned an invalid response");
	}

	uint32_t timeRemaining(uint64_t deadline) {
		const auto now = GET_TICK();
		return now >= deadline ? 0 : static_cast<uint32_t>(std::min<uint64_t>(deadline - now, UINT32_MAX));
	}

	bool validNumericAddress(const string& ip, bool v6) noexcept {
		if(ip.empty() || ip.size() > (v6 ? INET6_ADDRSTRLEN - 1 : INET_ADDRSTRLEN - 1)) {
			return false;
		}

		if(v6) {
			in6_addr address = {};
			return ::inet_pton(AF_INET6, ip.c_str(), &address) == 1 &&
				!IN6_IS_ADDR_UNSPECIFIED(&address) && !IN6_IS_ADDR_MULTICAST(&address) &&
				!IN6_IS_ADDR_V4MAPPED(&address) && !IN6_IS_ADDR_V4COMPAT(&address) &&
				Util::isPublicIp(ip, true);
		}

		in_addr address = {};
		if(::inet_pton(AF_INET, ip.c_str(), &address) != 1) {
			return false;
		}

		const auto hostAddress = ntohl(address.s_addr);
		return hostAddress != INADDR_ANY && hostAddress != INADDR_BROADCAST && !IN_MULTICAST(hostAddress) &&
			Util::isPublicIp(ip, false);
	}

	bool validPort(const string& port) noexcept {
		if(port.empty() || port.size() > 5) {
			return false;
		}

		unsigned value = 0;
		for(const auto ch: port) {
			if(ch < '0' || ch > '9') {
				return false;
			}
			value = value * 10 + static_cast<unsigned>(ch - '0');
		}
		return value > 0 && value <= 65535;
	}

	void report(const HBRIValidator::MessageCallback& callback, const string& message) noexcept {
		if(!callback) {
			return;
		}
		try {
			callback(message);
		} catch(...) {
			dcdebug("HBRI: status callback failed\n");
		}
	}
}

HBRIValidator::HBRISocket::HBRISocket(bool aV6, bool secure, const std::atomic_bool& aStopping) :
	v6(aV6), stopping(aStopping)
{
	if(secure) {
		socket = std::make_unique<SSLSocket>(CryptoManager::SSL_CLIENT,
			SETTING(ALLOW_UNTRUSTED_HUBS), Util::emptyString, Util::emptyString);
	} else {
		socket = std::make_unique<Socket>(Socket::TYPE_TCP);
	}

	if(v6) {
		socket->setLocalIp6(SETTING(BIND_ADDRESS6));
		socket->setV4only(false);
	} else {
		socket->setLocalIp4(SETTING(BIND_ADDRESS));
		socket->setV4only(true);
	}
}

bool HBRIValidator::HBRISocket::connect(const string& ip, const string& port) {
	socket->connect(ip, port);

	const auto deadline = GET_TICK() + HBRI_TIMEOUT;
	while(!stopping.load(std::memory_order_relaxed)) {
		const auto remaining = timeRemaining(deadline);
		if(!remaining) {
			return false;
		}
		if(socket->waitConnected(std::min<uint32_t>(remaining, 100))) {
			return true;
		}
	}
	return false;
}

void HBRIValidator::HBRISocket::send(const string& data) {
	if(data.empty() || data.size() > MAX_HBRI_LINE) {
		throw Exception(invalidResponse());
	}
	socket->writeAll(data.data(), static_cast<int>(data.size()), static_cast<uint32_t>(HBRI_TIMEOUT));
}

bool HBRIValidator::HBRISocket::readLine(string& data) {
	std::array<char, 2048> buffer;
	data.clear();
	const auto deadline = GET_TICK() + HBRI_TIMEOUT;

	while(!stopping.load(std::memory_order_relaxed)) {
		const auto remaining = timeRemaining(deadline);
		if(!remaining) {
			return false;
		}

		if(!socket->wait(std::min<uint32_t>(remaining, 100), true, false).first) {
			continue;
		}

		const auto bytesRead = socket->read(buffer.data(), static_cast<int>(buffer.size()));
		if(bytesRead == 0) {
			return false;
		}
		if(bytesRead < 0) {
			continue;
		}

		data.append(buffer.data(), static_cast<size_t>(bytesRead));
		if(data.size() > MAX_HBRI_LINE) {
			throw Exception(invalidResponse());
		}

		const auto lineEnd = data.find('\n');
		if(lineEnd != string::npos) {
			data.resize(lineEnd);
			if(!data.empty() && data.back() == '\r') {
				data.pop_back();
			}
			return true;
		}
	}
	return false;
}

void HBRIValidator::validateConnectInfo(const ConnectInfo& connectInfo) {
	if(!validNumericAddress(connectInfo.ip, connectInfo.v6) || !validPort(connectInfo.port)) {
		throw Exception(invalidResponse());
	}
}

void HBRIValidator::validateResponse(const string& response) {
	if(response.empty() || response.size() > MAX_HBRI_LINE || !Text::validateUtf8(response)) {
		throw Exception(invalidResponse());
	}

	AdcCommand command(response);
	if(command.getCommand() != AdcCommand::CMD_STA || command.getParameters().size() < 2) {
		throw Exception(invalidResponse());
	}

	const auto& status = command.getParam(0);
	if(status.size() != 3 || !std::all_of(status.begin(), status.end(), [](unsigned char ch) {
		return std::isdigit(ch) != 0;
	})) {
		throw Exception(invalidResponse());
	}

	if(status.front() != '0') {
		auto message = command.getParam(1);
		if(message.size() > 512) {
			message.resize(512);
		}
		throw Exception(message);
	}
}

bool HBRIValidator::runValidation(const ConnectInfo& connectInfo, const string& request,
	const std::atomic_bool& stopping)
{
	validateConnectInfo(connectInfo);
	HBRISocket socket(connectInfo.v6, connectInfo.secure, stopping);
	if(!socket.connect(connectInfo.ip, connectInfo.port)) {
		return false;
	}

	socket.send(request);
	string response;
	if(!socket.readLine(response)) {
		return false;
	}
	validateResponse(response);
	return true;
}

void HBRIValidator::run(const ConnectInfo& connectInfo, const string& request,
	const std::shared_ptr<std::atomic_bool>& stopping, const MessageCallback& callback) noexcept
{
	dcdebug("HBRI: validating %s endpoint %s:%s (secure: %s)\n",
		connectInfo.v6 ? "IPv6" : "IPv4", connectInfo.ip.c_str(), connectInfo.port.c_str(),
		connectInfo.secure ? "true" : "false");

	try {
		if(!runValidation(connectInfo, request, *stopping)) {
			if(!stopping->load(std::memory_order_relaxed)) {
				throw Exception(_("Connection timeout"));
			}
			return;
		}

		if(!stopping->load(std::memory_order_relaxed)) {
			report(callback, _("Validation succeeded"));
		}
	} catch(const Exception& e) {
		if(!stopping->load(std::memory_order_relaxed)) {
			report(callback, str(F_("Validation failed: %1%. %2% connectivity has been disabled in this hub.") %
				e.getError() % (connectInfo.v6 ? "IPv6" : "IPv4")));
		}
	} catch(const std::exception& e) {
		if(!stopping->load(std::memory_order_relaxed)) {
			report(callback, str(F_("Validation failed: %1%. %2% connectivity has been disabled in this hub.") %
				e.what() % (connectInfo.v6 ? "IPv6" : "IPv4")));
		}
	} catch(...) {
		if(!stopping->load(std::memory_order_relaxed)) {
			report(callback, str(F_("Validation failed: %1%. %2% connectivity has been disabled in this hub.") %
				invalidResponse() % (connectInfo.v6 ? "IPv6" : "IPv4")));
		}
	}
}

HBRIValidator::HBRIValidator(const ConnectInfo& connectInfo, const string& request,
	MessageCallback callback) : stopping(std::make_shared<std::atomic_bool>(false)),
	worker(&HBRIValidator::run, connectInfo, request, stopping, std::move(callback))
{
}

HBRIValidator::~HBRIValidator() {
	stopAndWait();
}

void HBRIValidator::stopAndWait() noexcept {
	stopping->store(true, std::memory_order_relaxed);
	if(!worker.joinable()) {
		return;
	}
	if(worker.get_id() == std::this_thread::get_id()) {
		worker.detach();
	} else {
		worker.join();
	}
}

} // namespace dcpp
