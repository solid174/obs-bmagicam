// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <string>

namespace bmagicam {

inline constexpr int kDefaultRemotePort = 4466;

// Remote Control's settings, in remote.json in the plugin's config folder (WEB-1, WEB-4, WEB-5)
struct RemoteSettings {
	bool enabled = false;
	int port = kDefaultRemotePort;
	bool authentication = true;
	std::string password;
};

// The saved settings; a password is generated and saved on first use
RemoteSettings load_remote_settings();
bool save_remote_settings(const RemoteSettings &settings);
// A new random password: 16 letters and digits
std::string generate_password();

} // namespace bmagicam
