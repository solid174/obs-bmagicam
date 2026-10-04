// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace bmagicam {

// Whether an address may use Remote Control: loopback and private networks only (WEB-4). Takes IPv4 and IPv6, with or
// without brackets and zone.
bool is_local_address(const std::string &address);

// Compares a password in time that does not depend on where it differs
bool same_password(const std::string &given, const std::string &expected);

// Slows down guessing: ten wrong passwords from an address within a minute block it for a minute (docs/remote-api.md)
class PasswordGate {
public:
	using Clock = std::chrono::steady_clock;

	bool blocked(const std::string &address, Clock::time_point now);
	void failed(const std::string &address, Clock::time_point now);
	void succeeded(const std::string &address);

private:
	std::mutex mutex_;
	std::map<std::string, std::deque<Clock::time_point>> failures_;
	std::map<std::string, Clock::time_point> blocked_until_;
};

} // namespace bmagicam
