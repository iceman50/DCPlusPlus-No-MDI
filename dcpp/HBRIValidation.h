/*
 * Copyright (C) 2001-2026 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef DCPLUSPLUS_DCPP_HBRI_VALIDATION_H
#define DCPLUSPLUS_DCPP_HBRI_VALIDATION_H

#include "typedefs.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace dcpp {

class Socket;

/** Performs the ADC HBRI callback without blocking the hub socket thread. */
class HBRIValidator {
public:
	struct ConnectInfo {
		ConnectInfo(bool aV6, bool aSecure) : v6(aV6), secure(aSecure) { }

		string ip;
		string port;
		bool v6;
		bool secure;
	};

	using MessageCallback = std::function<void(const string&)>;

	HBRIValidator(const ConnectInfo& connectInfo, const string& request, MessageCallback callback);
	~HBRIValidator();

	HBRIValidator(const HBRIValidator&) = delete;
	HBRIValidator& operator=(const HBRIValidator&) = delete;

	void stopAndWait() noexcept;

	/** Public for focused protocol tests; throws on peer-controlled invalid input. */
	static void validateConnectInfo(const ConnectInfo& connectInfo);
	static void validateResponse(const string& response);

private:
	class HBRISocket {
	public:
		HBRISocket(bool v6, bool secure, const std::atomic_bool& stopping);

		bool connect(const string& ip, const string& port);
		void send(const string& data);
		bool readLine(string& data);

	private:
		std::unique_ptr<Socket> socket;
		const bool v6;
		const std::atomic_bool& stopping;
	};

	static bool runValidation(const ConnectInfo& connectInfo, const string& request,
		const std::atomic_bool& stopping);
	static void run(const ConnectInfo& connectInfo, const string& request,
		const std::shared_ptr<std::atomic_bool>& stopping, const MessageCallback& callback) noexcept;

	std::shared_ptr<std::atomic_bool> stopping;
	std::thread worker;
};

} // namespace dcpp

#endif
