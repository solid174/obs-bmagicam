// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "phone-browser.hpp"

#include <plugin-support.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/address.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

namespace bmagicam {

namespace {

// DNS-SD through the Avahi daemon. Its callbacks run on Avahi's poll thread.
class AvahiBrowser final : public PhoneBrowser {
public:
	AvahiBrowser()
	{
		poll_ = avahi_threaded_poll_new();
		if (!poll_)
			return;

		// Keeps trying when the daemon is not running yet
		int error = 0;
		client_ =
			avahi_client_new(avahi_threaded_poll_get(poll_), AVAHI_CLIENT_NO_FAIL, on_client, this, &error);
		if (!client_) {
			obs_log(LOG_WARNING, "cannot look for phones: %s", avahi_strerror(error));
			return;
		}
		avahi_threaded_poll_start(poll_);
	}

	~AvahiBrowser() override
	{
		if (poll_)
			avahi_threaded_poll_stop(poll_);
		if (browser_)
			avahi_service_browser_free(browser_);
		if (client_)
			avahi_client_free(client_);
		if (poll_)
			avahi_threaded_poll_free(poll_);
	}

private:
	static void on_client(AvahiClient *client, AvahiClientState state, void *userdata)
	{
		auto self = static_cast<AvahiBrowser *>(userdata);
		if (state == AVAHI_CLIENT_S_RUNNING && !self->browser_) {
			self->browser_ = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET,
								   "_http._tcp", nullptr,
								   static_cast<AvahiLookupFlags>(0), on_browse, self);
		} else if ((state == AVAHI_CLIENT_FAILURE || state == AVAHI_CLIENT_CONNECTING) && self->browser_) {
			avahi_service_browser_free(self->browser_);
			self->browser_ = nullptr;
		}
	}

	static void on_browse(AvahiServiceBrowser *browser, AvahiIfIndex interface, AvahiProtocol protocol,
			      AvahiBrowserEvent event, const char *name, const char *type, const char *domain,
			      AvahiLookupResultFlags, void *userdata)
	{
		auto self = static_cast<AvahiBrowser *>(userdata);
		if (event == AVAHI_BROWSER_NEW)
			avahi_service_resolver_new(avahi_service_browser_get_client(browser), interface, protocol, name,
						   type, domain, AVAHI_PROTO_INET, static_cast<AvahiLookupFlags>(0),
						   on_resolve, self);
		else if (event == AVAHI_BROWSER_REMOVE)
			self->phone_lost(name);
	}

	static void on_resolve(AvahiServiceResolver *resolver, AvahiIfIndex, AvahiProtocol, AvahiResolverEvent event,
			       const char *name, const char *, const char *, const char *, const AvahiAddress *address,
			       uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags, void *userdata)
	{
		auto self = static_cast<AvahiBrowser *>(userdata);
		if (event == AVAHI_RESOLVER_FOUND && address) {
			std::map<std::string, std::string> values;
			for (AvahiStringList *item = txt; item; item = avahi_string_list_get_next(item)) {
				char *key = nullptr;
				char *value = nullptr;
				size_t size = 0;
				if (avahi_string_list_get_pair(item, &key, &value, &size) == 0) {
					values[key] = value ? std::string(value, size) : std::string();
					avahi_free(key);
					avahi_free(value);
				}
			}

			// Other web servers announce _http._tcp too; only Blackmagic Camera is kept
			FoundPhone phone = phone_from_txt(values);
			if (!phone.id.empty()) {
				char text[AVAHI_ADDRESS_STR_MAX] = {};
				avahi_address_snprint(text, sizeof(text), address);
				phone.address = text;
				phone.port = port;
				self->phone_found(name, phone);
			}
		}
		avahi_service_resolver_free(resolver);
	}

	AvahiThreadedPoll *poll_ = nullptr;
	AvahiClient *client_ = nullptr;
	AvahiServiceBrowser *browser_ = nullptr;
};

} // namespace

std::unique_ptr<PhoneBrowser> make_platform_browser()
{
	return std::make_unique<AvahiBrowser>();
}

} // namespace bmagicam
