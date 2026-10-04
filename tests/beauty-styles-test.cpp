// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "filter/beauty-styles.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

using namespace bmagicam;

TEST_CASE("the built-in styles file has Natural, Soft and Glam, each stronger than the one before")
{
	std::ifstream file(STYLES_FILE);
	std::stringstream text;
	text << file.rdbuf();
	const std::vector<BeautyStyle> styles = parse_styles(text.str(), true);
	REQUIRE(styles.size() == 3);
	CHECK(styles[0].id == "natural");
	CHECK(styles[1].id == "soft");
	CHECK(styles[2].id == "glam");
	CHECK(styles[0].builtin);
	CHECK(styles[0].values.smoothing < styles[1].values.smoothing);
	CHECK(styles[1].values.smoothing < styles[2].values.smoothing);
	CHECK(styles[0].values.texture > styles[2].values.texture);
}

TEST_CASE("a user style survives writing and reading")
{
	const BeautyStyle style{"Evening", false, {40, 50, 30, 20, 10, 60, 70}};
	const std::vector<BeautyStyle> read = parse_styles(write_styles({style}), false);
	REQUIRE(read.size() == 1);
	CHECK(read[0].id == "Evening");
	CHECK_FALSE(read[0].builtin);
	CHECK(read[0].values == style.values);
}

TEST_CASE("broken style entries are skipped and values are clamped")
{
	const std::vector<BeautyStyle> styles = parse_styles(
		R"({"styles": [{"name": "A", "values": {"smoothing": 150, "glow": -5}}, {"values": {}}, 3, {"name": "B"}]})",
		false);
	REQUIRE(styles.size() == 1);
	CHECK(styles[0].values.smoothing == 100);
	CHECK(styles[0].values.glow == 0);
	CHECK(parse_styles("not json", false).empty());
}

TEST_CASE("zero strength applies nothing, and full strength the whole style")
{
	const BeautyValues values{80, 30, 60, 40, 50, 55, 45};
	const BeautyAmounts none = beauty_amounts(values, 0);
	CHECK(none.smoothing == 0.0f);
	CHECK(none.evening == 0.0f);
	CHECK(none.sharpen == 0.0f);
	CHECK(none.glow == 0.0f);
	CHECK(none.texture == 1.0f);
	const BeautyAmounts full = beauty_amounts(values, 100);
	CHECK(full.smoothing == doctest::Approx(0.8));
	CHECK(full.texture == doctest::Approx(0.3));
	CHECK(full.evening == doctest::Approx(0.6));
	CHECK(full.sharpen == doctest::Approx(0.4));
	CHECK(full.glow == doctest::Approx(0.5));
	// Mask softness and detail size shape the effect; they do not grow with strength
	CHECK(beauty_amounts(values, 10).mask_softness == doctest::Approx(0.55));
	CHECK(beauty_amounts(values, 10).detail_size == doctest::Approx(0.45));
}

TEST_CASE("the first half of the Beauty slider stays natural")
{
	const BeautyValues values{100, 0, 100, 100, 100, 50, 50};
	const BeautyAmounts half = beauty_amounts(values, 50);
	CHECK(half.glow == doctest::Approx(0.25));
	CHECK(half.smoothing < 0.6f);
	CHECK(half.texture > 0.3f);
}
