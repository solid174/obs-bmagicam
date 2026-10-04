// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "beauty-link.hpp"

#include "../filter/beautify-filter.hpp"

#include <obs-module.h>

#include <mutex>

namespace bmagicam::ui {

namespace {

// Filters are private sources, so their changes come from their own signals, not global ones
constexpr const char *kGlobalSignals[] = {"source_filter_add", "source_filter_remove"};

} // namespace

// Shared with the signal callbacks, which run on any thread
struct BeautyLink::Relay {
	std::mutex mutex;
	BeautyLink *link = nullptr;
	bool scheduled = false;

	void post()
	{
		std::lock_guard lock(mutex);
		if (!link || scheduled)
			return;
		scheduled = true;
		// Posted while the lock is held, so the link cannot go away in between
		QMetaObject::invokeMethod(link, [target = link] { target->refresh(); }, Qt::QueuedConnection);
	}
};

BeautyLink::BeautyLink(OBSWeakSource source, QObject *parent)
	: QObject(parent),
	  source_(std::move(source)),
	  relay_(std::make_shared<Relay>())
{
	relay_->link = this;
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : kGlobalSignals)
		signal_handler_connect(handler, signal, filters_changed, relay_.get());
	refresh();
}

BeautyLink::~BeautyLink()
{
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : kGlobalSignals)
		signal_handler_disconnect(handler, signal, filters_changed, relay_.get());
	std::lock_guard lock(relay_->mutex);
	relay_->link = nullptr;
}

void BeautyLink::filters_changed(void *data, calldata_t *calldata)
{
	// Only Beautify filters matter
	if (is_beautify_filter(static_cast<obs_source_t *>(calldata_ptr(calldata, "filter"))))
		static_cast<Relay *>(data)->post();
}

void BeautyLink::filter_changed(void *data, calldata_t *)
{
	static_cast<Relay *>(data)->post();
}

bool BeautyLink::enabled() const
{
	return filter_ && obs_source_enabled(filter_);
}

void BeautyLink::set_enabled(bool enabled)
{
	if (filter_)
		obs_source_set_enabled(filter_, enabled);
}

void BeautyLink::update(obs_data_t *changes)
{
	if (filter_)
		obs_source_update(filter_, changes);
}

void BeautyLink::add()
{
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	if (source && !filter_) {
		OBSSourceAutoRelease added = add_beautify_filter(source, "natural", kDefaultBeautyStrength);
	}
}

void BeautyLink::refresh()
{
	{
		std::lock_guard lock(relay_->mutex);
		relay_->scheduled = false;
	}
	OBSSourceAutoRelease source = obs_weak_source_get_source(source_);
	OBSSourceAutoRelease filter = source ? find_beautify_filter(source) : nullptr;
	if (filter.Get() != filter_.Get()) {
		update_signal_.Disconnect();
		enable_signal_.Disconnect();
		filter_ = filter.Get();
		if (filter_) {
			signal_handler_t *handler = obs_source_get_signal_handler(filter_);
			update_signal_.Connect(handler, "update", filter_changed, relay_.get());
			enable_signal_.Connect(handler, "enable", filter_changed, relay_.get());
		}
	}
	if (changed)
		changed();
}

} // namespace bmagicam::ui
