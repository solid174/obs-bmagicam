// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "phone-browser.hpp"

#include <plugin-support.h>

#include <arpa/inet.h>
#include <dns_sd.h>
#include <sys/select.h>

#include <atomic>
#include <thread>

namespace bmagicam {

namespace {

// DNS-SD through the system's dns_sd API, on one shared connection served by a thread of its own
class MacBrowser final : public PhoneBrowser {
public:
	MacBrowser()
	{
		if (DNSServiceCreateConnection(&connection_) != kDNSServiceErr_NoError) {
			obs_log(LOG_WARNING, "cannot look for phones: Bonjour is not available");
			connection_ = nullptr;
			return;
		}
		browse_ = connection_;
		if (DNSServiceBrowse(&browse_, kDNSServiceFlagsShareConnection, kDNSServiceInterfaceIndexAny,
				     "_http._tcp", "local.", on_browse, this) != kDNSServiceErr_NoError) {
			obs_log(LOG_WARNING, "cannot look for phones: Bonjour refused the search");
			browse_ = nullptr;
			return;
		}
		thread_ = std::thread(&MacBrowser::run, this);
	}

	~MacBrowser() override
	{
		stopping_ = true;
		if (thread_.joinable())
			thread_.join();
		for (auto &[instance, lookup] : lookups_)
			cancel(*lookup);
		if (browse_)
			DNSServiceRefDeallocate(browse_);
		if (connection_)
			DNSServiceRefDeallocate(connection_);
	}

private:
	// Resolving one service instance: its TXT record and port, then its address
	struct Lookup {
		MacBrowser *browser;
		std::string instance;
		uint32_t interface_index;
		FoundPhone phone;
		DNSServiceRef resolve = nullptr;
		DNSServiceRef address = nullptr;
	};

	static void cancel(Lookup &lookup)
	{
		if (lookup.resolve)
			DNSServiceRefDeallocate(lookup.resolve);
		if (lookup.address)
			DNSServiceRefDeallocate(lookup.address);
		lookup.resolve = nullptr;
		lookup.address = nullptr;
	}

	void run()
	{
		const int socket = DNSServiceRefSockFD(connection_);
		while (!stopping_) {
			fd_set readable;
			FD_ZERO(&readable);
			FD_SET(socket, &readable);
			timeval timeout = {0, 250'000};
			const int ready = select(socket + 1, &readable, nullptr, nullptr, &timeout);
			if (ready > 0 && DNSServiceProcessResult(connection_) != kDNSServiceErr_NoError) {
				obs_log(LOG_WARNING, "stopped looking for phones: Bonjour failed");
				return;
			}
		}
	}

	static void DNSSD_API on_browse(DNSServiceRef, DNSServiceFlags flags, uint32_t interface_index,
					DNSServiceErrorType error, const char *name, const char *type,
					const char *domain, void *context)
	{
		auto self = static_cast<MacBrowser *>(context);
		if (error != kDNSServiceErr_NoError)
			return;

		const std::string instance = name;
		if (!(flags & kDNSServiceFlagsAdd)) {
			const auto found = self->lookups_.find(instance);
			if (found != self->lookups_.end() && found->second->interface_index == interface_index) {
				cancel(*found->second);
				self->lookups_.erase(found);
				self->phone_lost(instance);
			}
			return;
		}

		// A service appears once per network interface; one resolution is enough
		if (self->lookups_.count(instance))
			return;

		auto lookup = std::make_unique<Lookup>();
		lookup->browser = self;
		lookup->instance = instance;
		lookup->interface_index = interface_index;
		lookup->resolve = self->connection_;
		if (DNSServiceResolve(&lookup->resolve, kDNSServiceFlagsShareConnection, interface_index, name, type,
				      domain, on_resolve, lookup.get()) != kDNSServiceErr_NoError) {
			lookup->resolve = nullptr;
			return;
		}
		self->lookups_[instance] = std::move(lookup);
	}

	static void DNSSD_API on_resolve(DNSServiceRef, DNSServiceFlags, uint32_t interface_index,
					 DNSServiceErrorType error, const char *, const char *host, uint16_t port,
					 uint16_t txt_length, const unsigned char *txt, void *context)
	{
		auto lookup = static_cast<Lookup *>(context);
		DNSServiceRefDeallocate(lookup->resolve);
		lookup->resolve = nullptr;
		if (error != kDNSServiceErr_NoError)
			return;

		std::map<std::string, std::string> values;
		const uint16_t count = TXTRecordGetCount(txt_length, txt);
		for (uint16_t i = 0; i < count; i++) {
			char key[256] = {};
			uint8_t value_length = 0;
			const void *value = nullptr;
			if (TXTRecordGetItemAtIndex(txt_length, txt, i, sizeof(key), key, &value_length, &value) ==
			    kDNSServiceErr_NoError)
				values[key] = value ? std::string(static_cast<const char *>(value), value_length)
						    : std::string();
		}

		// Other web servers announce _http._tcp too; only Blackmagic Camera is kept
		lookup->phone = phone_from_txt(values);
		if (lookup->phone.id.empty())
			return;
		lookup->phone.port = ntohs(port);

		lookup->address = lookup->browser->connection_;
		if (DNSServiceGetAddrInfo(&lookup->address, kDNSServiceFlagsShareConnection, interface_index,
					  kDNSServiceProtocol_IPv4, host, on_address, lookup) != kDNSServiceErr_NoError)
			lookup->address = nullptr;
	}

	static void DNSSD_API on_address(DNSServiceRef, DNSServiceFlags flags, uint32_t, DNSServiceErrorType error,
					 const char *, const sockaddr *address, uint32_t, void *context)
	{
		auto lookup = static_cast<Lookup *>(context);
		if (error != kDNSServiceErr_NoError || !address || address->sa_family != AF_INET ||
		    !(flags & kDNSServiceFlagsAdd))
			return;

		char text[INET_ADDRSTRLEN] = {};
		inet_ntop(AF_INET, &reinterpret_cast<const sockaddr_in *>(address)->sin_addr, text, sizeof(text));
		lookup->phone.address = text;
		DNSServiceRefDeallocate(lookup->address);
		lookup->address = nullptr;
		lookup->browser->phone_found(lookup->instance, lookup->phone);
	}

	DNSServiceRef connection_ = nullptr;
	DNSServiceRef browse_ = nullptr;
	// Only used on the browser's thread, where all callbacks run
	std::map<std::string, std::unique_ptr<Lookup>> lookups_;
	std::atomic<bool> stopping_ = false;
	std::thread thread_;
};

} // namespace

std::unique_ptr<PhoneBrowser> make_platform_browser()
{
	return std::make_unique<MacBrowser>();
}

} // namespace bmagicam
