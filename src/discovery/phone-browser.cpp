// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "phone-browser.hpp"

#include <cstdlib>

namespace bmagicam {

namespace {

std::mutex instance_mutex;
std::unique_ptr<PhoneBrowser> browser;

std::string value(const std::map<std::string, std::string> &txt, const char *key)
{
	const auto found = txt.find(key);
	return found == txt.end() ? std::string() : found->second;
}

} // namespace

PhoneBrowser &PhoneBrowser::instance()
{
	std::lock_guard lock(instance_mutex);
	if (!browser)
		browser = make_platform_browser();
	return *browser;
}

void PhoneBrowser::shutdown()
{
	std::lock_guard lock(instance_mutex);
	browser.reset();
}

std::vector<FoundPhone> PhoneBrowser::phones() const
{
	std::lock_guard lock(mutex_);
	std::vector<FoundPhone> list;
	for (const auto &[instance, phone] : phones_)
		list.push_back(phone);
	return list;
}

bool PhoneBrowser::find(const std::string &id, FoundPhone &phone) const
{
	std::lock_guard lock(mutex_);
	for (const auto &[instance, found] : phones_) {
		if (found.id == id) {
			phone = found;
			return true;
		}
	}
	return false;
}

int PhoneBrowser::listen(Listener listener)
{
	std::lock_guard lock(mutex_);
	listeners_[next_listener_] = std::move(listener);
	return next_listener_++;
}

void PhoneBrowser::unlisten(int id)
{
	std::lock_guard lock(mutex_);
	listeners_.erase(id);
}

FoundPhone PhoneBrowser::phone_from_txt(const std::map<std::string, std::string> &txt)
{
	FoundPhone phone;
	if (value(txt, "capabilities").find("cameraControl") == std::string::npos)
		return phone;

	phone.id = value(txt, "device_id");
	if (phone.id.empty())
		phone.id = value(txt, "unique id");

	std::string model = value(txt, "device name");
	if (model.empty())
		model = value(txt, "model");
	const std::string letter = value(txt, "camera name");
	phone.name = letter.empty() ? model : model + " (" + letter + ")";
	phone.version = value(txt, "version");

	const std::string port = value(txt, "port");
	if (!port.empty())
		phone.port = std::atoi(port.c_str());
	return phone;
}

void PhoneBrowser::phone_found(const std::string &instance, const FoundPhone &phone)
{
	{
		std::lock_guard lock(mutex_);
		const auto known = phones_.find(instance);
		if (known != phones_.end() && known->second.address == phone.address &&
		    known->second.name == phone.name && known->second.version == phone.version)
			return;
		phones_[instance] = phone;
	}
	notify();
}

void PhoneBrowser::phone_lost(const std::string &instance)
{
	{
		std::lock_guard lock(mutex_);
		if (!phones_.erase(instance))
			return;
	}
	notify();
}

void PhoneBrowser::notify()
{
	std::map<int, Listener> listeners;
	{
		std::lock_guard lock(mutex_);
		listeners = listeners_;
	}
	for (const auto &[id, listener] : listeners)
		listener();
}

} // namespace bmagicam
