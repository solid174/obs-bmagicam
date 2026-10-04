// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "remote/access.hpp"

#include <doctest/doctest.h>

using namespace bmagicam;

TEST_CASE("only loopback and private addresses may use Remote Control")
{
	for (const char *address :
	     {"127.0.0.1", "10.1.2.3", "172.16.0.9", "172.31.255.1", "192.168.1.20", "100.64.0.1", "169.254.10.10",
	      "::1", "[::1]", "fd12:3456::1", "fe80::1%en0", "::ffff:192.168.0.5", "FE80::ABCD"})
		CHECK_MESSAGE(is_local_address(address), address);
	for (const char *address : {"8.8.8.8", "172.32.0.1", "192.169.1.1", "100.128.0.1", "2001:db8::1",
				    "::ffff:1.1.1.1", "", "localhost", "256.1.1.1", "1.2.3", "fe8g::1"})
		CHECK_FALSE_MESSAGE(is_local_address(address), address);
}

TEST_CASE("passwords must match exactly")
{
	CHECK(same_password("abc123", "abc123"));
	CHECK_FALSE(same_password("abc124", "abc123"));
	CHECK_FALSE(same_password("abc12", "abc123"));
	CHECK_FALSE(same_password("", ""));
}

TEST_CASE("ten wrong passwords within a minute block the address for a minute")
{
	PasswordGate gate;
	const auto start = PasswordGate::Clock::now();
	for (int attempt = 0; attempt < 9; attempt++)
		gate.failed("192.168.1.9", start);
	CHECK_FALSE(gate.blocked("192.168.1.9", start));
	gate.failed("192.168.1.9", start);
	CHECK(gate.blocked("192.168.1.9", start + std::chrono::seconds(30)));
	CHECK_FALSE(gate.blocked("192.168.1.10", start));
	CHECK_FALSE(gate.blocked("192.168.1.9", start + std::chrono::seconds(61)));
}

TEST_CASE("failures spread over more than a minute do not block")
{
	PasswordGate gate;
	auto now = PasswordGate::Clock::now();
	for (int attempt = 0; attempt < 20; attempt++) {
		gate.failed("10.0.0.2", now);
		now += std::chrono::seconds(10);
	}
	CHECK_FALSE(gate.blocked("10.0.0.2", now));
}
