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

// Our destination as Blackmagic Camera 3.5 gives it back: in its own layout, with fields added and others dropped
constexpr const char *kStoredDestination = R"(<?xml version="1.0" encoding="UTF-8"?>
<streaming>
    <service>
        <name>OBS on STUDIO-PC</name>
        <servers>
    <server>
        <name>OBS</name>
        <url>srt://192.168.1.20:9710</url>
    </server>
        </servers>
        <profiles>
    <profile>
        <name>1080p60 High</name>
        <low-latency>false</low-latency>
            <config resolution="1080p" fps="60">
        <bitrate>12000000</bitrate>
    </config>
    </profile>
    <profile>
        <name>1080p60 Balanced</name>
        <low-latency>false</low-latency>
            <config resolution="1080p" fps="60">
        <bitrate>8000000</bitrate>
    </config>
    </profile>
    <profile>
        <name>1080p30</name>
        <low-latency>false</low-latency>
            <config resolution="1080p" fps="30">
        <bitrate>6000000</bitrate>
    </config>
    </profile>
    <profile>
        <name>720p60</name>
        <low-latency>false</low-latency>
            <config resolution="720p" fps="60">
        <bitrate>5000000</bitrate>
    </config>
    </profile>
    <profile>
        <name>4K30</name>
        <low-latency>false</low-latency>
            <config resolution="2160p" fps="30">
        <bitrate>20000000</bitrate>
    </config>
    </profile>
    <profile>
        <name>4K60</name>
        <low-latency>false</low-latency>
            <config resolution="2160p" fps="60">
        <bitrate>30000000</bitrate>
    </config>
    </profile>
        </profiles>
        <key></key>
        <passphrase></passphrase>
    </service>
</streaming>)";

TEST_CASE("A destination the phone already has is recognized in its own layout")
{
	CHECK(streaming_xml_matches(kStoredDestination, "srt://192.168.1.20:9710"));
	CHECK(streaming_xml_matches(streaming_xml("OBS on STUDIO-PC", "srt://192.168.1.20:9710"),
				    "srt://192.168.1.20:9710"));
}

TEST_CASE("A destination with another address, bitrate or profile set is out of date")
{
	const std::string stored = kStoredDestination;
	CHECK_FALSE(streaming_xml_matches(stored, "srt://192.168.1.21:9710"));
	CHECK_FALSE(streaming_xml_matches(stored, "srt://192.168.1.20:9711"));

	std::string other_bitrate = stored;
	other_bitrate.replace(other_bitrate.find("<bitrate>8000000</bitrate>"), 26, "<bitrate>7000000</bitrate>");
	CHECK_FALSE(streaming_xml_matches(other_bitrate, "srt://192.168.1.20:9710"));

	std::string missing_profile = stored;
	missing_profile.replace(missing_profile.find("<name>4K60</name>"), 17, "<name>4K50</name>");
	CHECK_FALSE(streaming_xml_matches(missing_profile, "srt://192.168.1.20:9710"));

	CHECK_FALSE(streaming_xml_matches("", "srt://192.168.1.20:9710"));
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
