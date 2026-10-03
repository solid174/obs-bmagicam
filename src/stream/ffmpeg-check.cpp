// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "ffmpeg-check.hpp"

#include <plugin-support.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

#include <cstring>

namespace bmagicam {

namespace {

struct Library {
	const char *name;
	unsigned built;
	unsigned (*loaded)();
};

const Library kLibraries[] = {
	{"libavcodec", LIBAVCODEC_VERSION_MAJOR, avcodec_version},
	{"libavformat", LIBAVFORMAT_VERSION_MAJOR, avformat_version},
	{"libavutil", LIBAVUTIL_VERSION_MAJOR, avutil_version},
	{"libswresample", LIBSWRESAMPLE_VERSION_MAJOR, swresample_version},
};

bool receives_srt()
{
	void *opaque = nullptr;
	while (const char *protocol = avio_enum_protocols(&opaque, 0)) {
		if (std::strcmp(protocol, "srt") == 0)
			return true;
	}
	return false;
}

bool check()
{
	for (const Library &library : kLibraries) {
		const unsigned loaded = AV_VERSION_MAJOR(library.loaded());
		if (loaded != library.built) {
			obs_log(LOG_ERROR, "this build is for %s %u, but OBS has %s %u", library.name, library.built,
				library.name, loaded);
			return false;
		}
	}

	if (!receives_srt()) {
		obs_log(LOG_ERROR, "cannot receive SRT: the FFmpeg that OBS uses was built without it");
		return false;
	}
	return true;
}

} // namespace

bool ffmpeg_usable()
{
	static const bool usable = check();
	return usable;
}

} // namespace bmagicam
