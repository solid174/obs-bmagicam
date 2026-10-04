// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "phone-browser.hpp"

#include <plugin-support.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>

#include <util/bmem.h>
#include <util/platform.h>

#include <chrono>
#include <condition_variable>
#include <set>

namespace bmagicam {

namespace {

std::string utf8(const wchar_t *text)
{
	if (!text)
		return {};
	char *converted = nullptr;
	os_wcs_to_utf8_ptr(text, 0, &converted);
	std::string result = converted ? converted : "";
	bfree(converted);
	return result;
}

// DNS-SD through DnsServiceBrowse and DnsServiceResolve, Windows 10 1903 and later. Their callbacks run on the
// system's threads.
class WindowsBrowser final : public PhoneBrowser {
public:
	WindowsBrowser()
	{
		DNS_SERVICE_BROWSE_REQUEST request = {};
		request.Version = DNS_QUERY_REQUEST_VERSION1;
		request.InterfaceIndex = 0;
		request.QueryName = L"_http._tcp.local";
		request.pBrowseCallback = on_browse;
		request.pQueryContext = this;
		if (DnsServiceBrowse(&request, &browse_) != DNS_REQUEST_PENDING) {
			obs_log(LOG_WARNING, "cannot look for phones: the DNS-SD service refused the search");
			browsing_ = false;
		}
	}

	~WindowsBrowser() override
	{
		if (browsing_)
			DnsServiceBrowseCancel(&browse_);

		std::unique_lock lock(mutex_);
		for (Resolution *resolution : resolutions_)
			DnsServiceResolveCancel(&resolution->cancel);
		// Cancelled requests still call back once; wait for them
		done_.wait_for(lock, std::chrono::seconds(2), [&] { return resolutions_.empty(); });
		stopping_ = true;
	}

private:
	struct Resolution {
		WindowsBrowser *browser;
		std::wstring name;
		DNS_SERVICE_CANCEL cancel = {};
	};

	static VOID WINAPI on_browse(DWORD status, PVOID context, PDNS_RECORD records)
	{
		auto self = static_cast<WindowsBrowser *>(context);
		if (status == ERROR_SUCCESS) {
			for (PDNS_RECORD record = records; record; record = record->pNext) {
				if (record->wType != DNS_TYPE_PTR || !record->Data.PTR.pNameHost)
					continue;
				const std::wstring name = record->Data.PTR.pNameHost;
				if (record->dwTtl == 0)
					self->phone_lost(utf8(name.c_str()));
				else
					self->resolve(name);
			}
		}
		if (records)
			DnsRecordListFree(records, DnsFreeRecordList);
	}

	void resolve(const std::wstring &name)
	{
		std::lock_guard lock(mutex_);
		if (stopping_)
			return;

		auto resolution = new Resolution{this, name};
		DNS_SERVICE_RESOLVE_REQUEST request = {};
		request.Version = DNS_QUERY_REQUEST_VERSION1;
		request.InterfaceIndex = 0;
		request.QueryName = const_cast<PWSTR>(resolution->name.c_str());
		request.pResolveCompletionCallback = on_resolve;
		request.pQueryContext = resolution;
		if (DnsServiceResolve(&request, &resolution->cancel) != DNS_REQUEST_PENDING) {
			delete resolution;
			return;
		}
		resolutions_.insert(resolution);
	}

	static VOID WINAPI on_resolve(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
	{
		auto resolution = static_cast<Resolution *>(context);
		WindowsBrowser *self = resolution->browser;

		if (status == ERROR_SUCCESS && instance) {
			std::map<std::string, std::string> values;
			for (DWORD i = 0; i < instance->dwPropertyCount; i++)
				values[utf8(instance->keys[i])] = utf8(instance->values[i]);

			// Other web servers announce _http._tcp too; only Blackmagic Camera is kept
			FoundPhone phone = phone_from_txt(values);
			if (!phone.id.empty() && instance->ip4Address) {
				in_addr address = {};
				address.S_un.S_addr = *instance->ip4Address;
				char text[INET_ADDRSTRLEN] = {};
				inet_ntop(AF_INET, &address, text, sizeof(text));
				phone.address = text;
				phone.port = instance->wPort;
				self->phone_found(utf8(resolution->name.c_str()), phone);
			}
		}
		if (instance)
			DnsServiceFreeInstance(instance);

		{
			std::lock_guard lock(self->mutex_);
			self->resolutions_.erase(resolution);
		}
		self->done_.notify_all();
		delete resolution;
	}

	DNS_SERVICE_CANCEL browse_ = {};
	bool browsing_ = true;
	std::mutex mutex_;
	std::condition_variable done_;
	std::set<Resolution *> resolutions_;
	bool stopping_ = false;
};

} // namespace

std::unique_ptr<PhoneBrowser> make_platform_browser()
{
	return std::make_unique<WindowsBrowser>();
}

} // namespace bmagicam
