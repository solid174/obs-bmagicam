// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera/stream-presets.hpp"
#include "camera/streaming-xml.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace bmagicam;

TEST_CASE("The destination points the phone at the listener, with every preset as a profile")
{
	const std::string xml = streaming_xml("OBS on STUDIO-PC", "srt://192.168.1.20:9710");

	CHECK(xml.find("<name>OBS on STUDIO-PC</name>") != std::string::npos);
	CHECK(xml.find("<name>OBS</name>") != std::string::npos);
	CHECK(xml.find("<url>srt://192.168.1.20:9710</url>") != std::string::npos);
	CHECK(xml.find("<profiles default=\"1080p60 High\">") != std::string::npos);
	for (const StreamPreset &preset : stream_presets()) {
		CAPTURE(preset.id);
		CHECK(xml.find("<name>" + std::string(preset.profile) + "</name>") != std::string::npos);
		CHECK(xml.find("<bitrate>" + std::to_string(preset.bitrate) + "</bitrate>") != std::string::npos);
	}
}

TEST_CASE("Names in the destination are escaped")
{
	const std::string xml = streaming_xml("OBS on Tom & Jerry's <PC>", "srt://10.0.0.2:9711");
	CHECK(xml.find("<name>OBS on Tom &amp; Jerry&apos;s &lt;PC&gt;</name>") != std::string::npos);
}

TEST_CASE("The phone stores the destination under the service name plus SRT")
{
	CHECK(streaming_platform("OBS on STUDIO-PC") == "OBS on STUDIO-PC SRT");
}

TEST_CASE("Stream presets have unique IDs and an unknown ID gives the default")
{
	std::set<std::string> ids;
	for (const StreamPreset &preset : stream_presets())
		CHECK(ids.insert(preset.id).second);

	CHECK(std::string(stream_preset("4k60").video_format) == "3840x2160p60");
	CHECK(std::string(stream_preset("no-such-preset").id) == "1080p60-high");
	CHECK(std::string(stream_preset("").id) == "1080p60-high");
}
