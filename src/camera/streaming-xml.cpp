// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "streaming-xml.hpp"

#include "stream-presets.hpp"

namespace bmagicam {

namespace {

std::string escape(const std::string &text)
{
	std::string escaped;
	for (const char c : text) {
		switch (c) {
		case '&':
			escaped += "&amp;";
			break;
		case '<':
			escaped += "&lt;";
			break;
		case '>':
			escaped += "&gt;";
			break;
		case '"':
			escaped += "&quot;";
			break;
		case '\'':
			escaped += "&apos;";
			break;
		default:
			escaped += c;
		}
	}
	return escaped;
}

} // namespace

std::string streaming_xml(const std::string &service, const std::string &srt_url)
{
	const StreamPreset &default_preset = stream_presets().front();

	// The phone streams in the camera's own codec and format; the profile sets the bitrate (camera-api.md)
	std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			  "<streaming>\n"
			  " <service>\n"
			  "  <name>" +
			  escape(service) +
			  "</name>\n"
			  "  <servers>\n"
			  "   <server>\n"
			  "    <name>" +
			  std::string(kStreamingServer) +
			  "</name>\n"
			  "    <url>" +
			  escape(srt_url) +
			  "</url>\n"
			  "   </server>\n"
			  "  </servers>\n"
			  "  <profiles default=\"" +
			  escape(default_preset.profile) + "\">\n";
	for (const StreamPreset &preset : stream_presets()) {
		xml += "   <profile>\n"
		       "    <name>" +
		       escape(preset.profile) +
		       "</name>\n"
		       "    <config resolution=\"" +
		       std::to_string(preset.height) + "p\" fps=\"" + std::to_string(preset.fps) +
		       "\" codec=\"H264\">\n"
		       "     <bitrate>" +
		       std::to_string(preset.bitrate) +
		       "</bitrate>\n"
		       "     <audio-bitrate>128000</audio-bitrate>\n"
		       "    </config>\n"
		       "   </profile>\n";
	}
	xml += "  </profiles>\n"
	       " </service>\n"
	       "</streaming>\n";
	return xml;
}

std::string streaming_platform(const std::string &service)
{
	return service + " SRT";
}

bool streaming_xml_matches(const std::string &stored, const std::string &srt_url)
{
	if (stored.find("<url>" + escape(srt_url) + "</url>") == std::string::npos)
		return false;

	for (const StreamPreset &preset : stream_presets()) {
		const size_t name = stored.find("<name>" + escape(preset.profile) + "</name>");
		if (name == std::string::npos)
			return false;
		// The profile's bitrate comes before the next name
		const std::string bitrate = "<bitrate>" + std::to_string(preset.bitrate) + "</bitrate>";
		const size_t found = stored.find("<bitrate>", name);
		if (found == std::string::npos || found > stored.find("<name>", name + 1) ||
		    stored.compare(found, bitrate.size(), bitrate) != 0)
			return false;
	}
	return true;
}

} // namespace bmagicam
