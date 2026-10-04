// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "camera/looks.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace bmagicam;

TEST_CASE("a look lists only what differs from neutral")
{
	const nlohmann::json complete = complete_color({{"color", {{"saturation", 1.12}}}});
	CHECK(complete["color"]["saturation"] == doctest::Approx(1.12));
	CHECK(complete["color"]["hue"] == 0.0);
	CHECK(complete["gain"]["red"] == 1.0);
	CHECK(complete["lumaContribution"]["lumaContribution"] == 1.0);
	CHECK(same_color(nlohmann::json::object(), neutral_color()));
}

TEST_CASE("colors match within what the phone rounds to")
{
	const nlohmann::json studio = {{"contrast", {{"pivot", 0.43}, {"adjust", 1.08}}},
				       {"color", {{"saturation", 1.12}}}};
	const nlohmann::json rounded = {{"contrast", {{"pivot", 0.4300000071525574}, {"adjust", 1.0800000429153442}}},
					{"color", {{"saturation", 1.1200000047683716}}}};
	CHECK(same_color(studio, rounded));
	CHECK_FALSE(same_color(studio, neutral_color()));
}

TEST_CASE("the built-in looks file has the six looks")
{
	std::ifstream file(LOOKS_FILE);
	std::stringstream text;
	text << file.rdbuf();
	const std::vector<Look> looks = parse_looks(text.str(), true);
	REQUIRE(looks.size() == 6);
	CHECK(looks[0].id == "natural");
	CHECK(same_color(looks[0].values, neutral_color()));
	CHECK(looks[1].id == "studio");
	CHECK(looks[1].values["color"]["saturation"] == doctest::Approx(1.12));
}

TEST_CASE("the user's looks are written by name and read back")
{
	const std::vector<Look> looks = {{"Evening", false, complete_color({{"gain", {{"red", 1.05}}}})}};
	const std::vector<Look> read = parse_looks(write_looks(looks), false);
	REQUIRE(read.size() == 1);
	CHECK(read[0].id == "Evening");
	CHECK(read[0].values["gain"]["red"] == doctest::Approx(1.05));
}
