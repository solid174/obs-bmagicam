// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <string>

namespace bmagicam {

// This computer's IPv4 address on the route to the given host, which is the address the phone can reach it at. Empty
// when the host cannot be resolved or there is no route to it.
std::string local_address_toward(const std::string &host);

// This computer's name, without a domain, for the destination shown on the phone.
std::string computer_name();

} // namespace bmagicam
