// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 solid174

#include "filter/skin-model.hpp"

#include <doctest/doctest.h>

#include <cmath>

using namespace bmagicam;

TEST_CASE("half floats from the GPU read back exactly")
{
	CHECK(from_half(0x0000) == 0.0f);
	CHECK(from_half(0x3c00) == 1.0f);
	CHECK(from_half(0xbc00) == -1.0f);
	CHECK(from_half(0x3800) == 0.5f);
	CHECK(from_half(0x2e66) == doctest::Approx(0.1).epsilon(0.001));
	CHECK(from_half(0x0001) == doctest::Approx(5.9604645e-8));
	CHECK(from_half(0x7bff) == 65504.0f);
	CHECK(std::isinf(from_half(0x7c00)));
}

TEST_CASE("the skin color follows the skin in the picture")
{
	SkinModel model;
	std::vector<BlockStats> blocks(20, {-0.04f, 0.09f, 0.9f, -1.0f});
	blocks.resize(120, {0.0f, 0.0f, 0.0f, -1.0f});
	// The first estimate is taken whole
	model.update(blocks, 0.1f);
	CHECK(model.cb() == doctest::Approx(-0.04).epsilon(0.01));
	CHECK(model.cr() == doctest::Approx(0.09).epsilon(0.01));
}

TEST_CASE("one area of skin wins over a few skin-like blocks elsewhere")
{
	SkinModel model;
	std::vector<BlockStats> blocks(30, {-0.055f, 0.085f, 1.0f, -1.0f});
	blocks.resize(36, {-0.02f, 0.03f, 0.8f, -1.0f});
	model.update(blocks, 1.0f);
	CHECK(model.cb() == doctest::Approx(-0.055).epsilon(0.05));
	CHECK(model.cr() == doctest::Approx(0.085).epsilon(0.05));
}

TEST_CASE("without enough skin the skin color stays where it was")
{
	SkinModel model;
	const float cb = model.cb();
	const float cr = model.cr();
	model.update({{0.1f, -0.1f, 0.5f, -1.0f}}, 1.0f);
	CHECK(model.cb() == cb);
	CHECK(model.cr() == cr);
}

TEST_CASE("the skin color stays plausible whatever the picture holds")
{
	SkinModel model;
	model.update(std::vector<BlockStats>(10, {0.1f, 0.1f, 1.0f, -1.0f}), 1.0f);
	CHECK(std::atan2(model.cr(), model.cb()) == doctest::Approx(1.66));
}

TEST_CASE("the noise is the detail of the flattest mid-tone blocks")
{
	SkinModel model;
	std::vector<BlockStats> blocks(40, {0.0f, 0.0f, 0.0f, 0.002f});
	blocks.resize(100, {0.0f, 0.0f, 0.0f, 0.03f});
	model.update(blocks, 1.0f);
	CHECK(model.noise() == doctest::Approx(0.005));
}

TEST_CASE("later estimates move the model only by the weight")
{
	SkinModel model;
	model.update(std::vector<BlockStats>(20, {0.0f, 0.0f, 0.0f, 0.002f}), 1.0f);
	model.update(std::vector<BlockStats>(20, {0.0f, 0.0f, 0.0f, 0.004f}), 0.25f);
	CHECK(model.noise() == doctest::Approx(0.00625));
}

TEST_CASE("a picture without mid-tones leaves the noise as it was")
{
	SkinModel model;
	const float noise = model.noise();
	model.update(std::vector<BlockStats>(50, {0.0f, 0.0f, 0.0f, -1.0f}), 1.0f);
	CHECK(model.noise() == noise);
}
