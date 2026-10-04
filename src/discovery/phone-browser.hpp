// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bmagicam {

// A phone running Blackmagic Camera with its HTTP server on, as Bonjour announces it.
struct FoundPhone {
	// device_id from the TXT record: stays the same when the address changes
	std::string id;
	// "iPhone 17 Pro (A)"
	std::string name;
	// The app's version, "3.5.100017"
	std::string version;
	std::string address;
	int port = 4444;
};

// Finds Blackmagic Camera phones on the local network (docs/camera-api.md, "Discovery") with the system's DNS-SD
// service: dns_sd on macOS, DnsServiceBrowse on Windows, Avahi on Linux. One browser serves the whole plugin; it starts
// when first used and stops at module unload.
class PhoneBrowser {
public:
	using Listener = std::function<void()>;

	static PhoneBrowser &instance();
	static void shutdown();

	virtual ~PhoneBrowser() = default;

	std::vector<FoundPhone> phones() const;
	// Whether the phone with the given ID is on the network, and where
	bool find(const std::string &id, FoundPhone &phone) const;

	// Listeners hear that the list changed; they are called on the browser's thread
	int listen(Listener listener);
	void unlisten(int id);

	// Reads a phone from its TXT record. Empty ID when the service is not Blackmagic Camera.
	static FoundPhone phone_from_txt(const std::map<std::string, std::string> &txt);

protected:
	PhoneBrowser() = default;

	// For the platform browsers, by DNS-SD service instance
	void phone_found(const std::string &instance, const FoundPhone &phone);
	void phone_lost(const std::string &instance);

private:
	void notify();

	mutable std::mutex mutex_;
	std::map<std::string, FoundPhone> phones_;
	std::map<int, Listener> listeners_;
	int next_listener_ = 1;
};

// The platform's browser, implemented once per system
std::unique_ptr<PhoneBrowser> make_platform_browser();

} // namespace bmagicam
