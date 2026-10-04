// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "access.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace bmagicam {

namespace {

constexpr size_t kMaxFailures = 10;
constexpr auto kFailureWindow = std::chrono::minutes(1);
constexpr auto kBlockTime = std::chrono::minutes(1);

// The four parts of a dotted IPv4 address
bool parse_ipv4(const std::string &text, std::array<int, 4> &parts)
{
	size_t start = 0;
	for (size_t index = 0; index < 4; index++) {
		const size_t end = index < 3 ? text.find('.', start) : text.size();
		if (end == std::string::npos || end == start || end - start > 3)
			return false;
		int value = 0;
		for (size_t at = start; at < end; at++) {
			if (!std::isdigit(static_cast<unsigned char>(text[at])))
				return false;
			value = value * 10 + (text[at] - '0');
		}
		if (value > 255)
			return false;
		parts[index] = value;
		start = end + 1;
	}
	return true;
}

bool local_ipv4(const std::array<int, 4> &parts)
{
	const int a = parts[0];
	const int b = parts[1];
	return a == 127 || a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
	       (a == 100 && b >= 64 && b <= 127) || (a == 169 && b == 254);
}

} // namespace

bool is_local_address(const std::string &address)
{
	std::string text = address;
	if (!text.empty() && text.front() == '[' && text.back() == ']')
		text = text.substr(1, text.size() - 2);
	if (const size_t zone = text.find('%'); zone != std::string::npos)
		text.resize(zone);
	std::transform(text.begin(), text.end(), text.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	std::array<int, 4> parts{};
	if (parse_ipv4(text, parts))
		return local_ipv4(parts);
	if (text.find(':') == std::string::npos)
		return false;
	// IPv4 in IPv6, as dual-stack servers report IPv4 clients
	if (text.rfind("::ffff:", 0) == 0 && parse_ipv4(text.substr(7), parts))
		return local_ipv4(parts);
	if (text == "::1")
		return true;
	// Unique local fc00::/7 and link-local fe80::/10, from the first group
	const std::string first = text.substr(0, text.find(':'));
	if (first.empty() || first.size() > 4)
		return false;
	char *end = nullptr;
	const unsigned long group = std::strtoul(first.c_str(), &end, 16);
	if (*end != '\0')
		return false;
	return (group & 0xfe00) == 0xfc00 || (group & 0xffc0) == 0xfe80;
}

bool same_password(const std::string &given, const std::string &expected)
{
	unsigned char difference = given.size() == expected.size() ? 0 : 1;
	for (size_t index = 0; index < given.size(); index++)
		difference |= static_cast<unsigned char>(given[index] ^
							 expected[index % std::max<size_t>(1, expected.size())]);
	return difference == 0 && !expected.empty();
}

bool PasswordGate::blocked(const std::string &address, Clock::time_point now)
{
	std::lock_guard lock(mutex_);
	const auto found = blocked_until_.find(address);
	if (found == blocked_until_.end())
		return false;
	if (now < found->second)
		return true;
	blocked_until_.erase(found);
	return false;
}

void PasswordGate::failed(const std::string &address, Clock::time_point now)
{
	std::lock_guard lock(mutex_);
	std::deque<Clock::time_point> &times = failures_[address];
	times.push_back(now);
	while (!times.empty() && now - times.front() > kFailureWindow)
		times.pop_front();
	if (times.size() >= kMaxFailures) {
		blocked_until_[address] = now + kBlockTime;
		times.clear();
	}
}

void PasswordGate::succeeded(const std::string &address)
{
	std::lock_guard lock(mutex_);
	failures_.erase(address);
}

} // namespace bmagicam
