// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#pragma once

#include <obs.hpp>

#include <QObject>

#include <functional>
#include <memory>

namespace bmagicam::ui {

// Follows the Beautify filter of one source on the Qt thread: when it is added, removed, changed, or turned on or
// off, from the dock, its properties, the API or anywhere else.
class BeautyLink : public QObject {
public:
	explicit BeautyLink(OBSWeakSource source, QObject *parent = nullptr);
	~BeautyLink() override;

	// The source's Beautify filter; null when it has none
	obs_source_t *filter() const { return filter_; }
	bool enabled() const;
	void set_enabled(bool enabled);
	// Changes the filter's settings; values left out keep theirs
	void update(obs_data_t *changes);
	// Adds a Beautify filter with the default style and strength
	void add();

	// Hears any change, once per pass of the event loop
	std::function<void()> changed;

private:
	struct Relay;

	static void filters_changed(void *data, calldata_t *calldata);
	static void filter_changed(void *data, calldata_t *calldata);
	void refresh();

	OBSWeakSource source_;
	std::shared_ptr<Relay> relay_;
	// The filter shown, held as OBS's own list of filters holds its filters
	OBSSource filter_;
	OBSSignal update_signal_;
	OBSSignal enable_signal_;
};

} // namespace bmagicam::ui
