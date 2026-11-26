// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "core/hle/service/nx2_gsv.h"

namespace {

using Service::Nx2CheckedDecoderPool;

// A preference with layout code `code` and the given dimensions (word 2: (w - 1) | (h - 1) << 16).
std::array<u32, 16> Preference(u32 code, u32 width, u32 height) {
    std::array<u32, 16> preference{};
    preference[1] = code;
    preference[2] = (width - 1) | ((height - 1) << 16);
    return preference;
}

} // Anonymous namespace

TEST_CASE("A video's decoder image pool keeps its layout", "[nx2_gsv]") {
    const auto preference = Preference(4, 1920, 1080);
    const auto checked = Nx2CheckedDecoderPool(preference.data(), 8);
    REQUIRE(checked);
    CHECK(checked->width == 1920);
    CHECK(checked->height == 1080);
    // NV12 in 128-row blocks: 1920 x 1152 luma, then 1920 x 640 chroma.
    CHECK(checked->size == 1920 * 1152 + 1920 * 640);
}

TEST_CASE("A decoder image pool past its limits is refused", "[nx2_gsv]") {
    constexpr u32 MaxDimension = Service::Nx2MaxDecoderImageDimension;
    using Service::Nx2MaxDecoderImages;
    // The largest dimensions and count the 16-bit fields and the count word can carry.
    CHECK_FALSE(Nx2CheckedDecoderPool(Preference(2, 65536, 65536).data(), 1));
    CHECK_FALSE(Nx2CheckedDecoderPool(Preference(2, MaxDimension + 1, 16).data(), 1));
    CHECK_FALSE(Nx2CheckedDecoderPool(Preference(4, 16, MaxDimension + 1).data(), 1));
    CHECK(Nx2CheckedDecoderPool(Preference(4, 1920, 1080).data(), Nx2MaxDecoderImages));
    CHECK_FALSE(Nx2CheckedDecoderPool(Preference(4, 1920, 1080).data(), Nx2MaxDecoderImages + 1));
    // The largest count the word carries, refused by the count limit.
    CHECK_FALSE(Nx2CheckedDecoderPool(Preference(4, 1920, 1080).data(), 0xFFFFFFFF));
    // Within each limit alone, but over the pool's total: 9 images of 256 MiB.
    const auto largest = Preference(2, MaxDimension, MaxDimension);
    CHECK(Nx2CheckedDecoderPool(largest.data(), 8));
    CHECK_FALSE(Nx2CheckedDecoderPool(largest.data(), 9));
}

TEST_CASE("Image ids are taken without wrapping", "[nx2_gsv]") {
    Service::Nx2NativeObjects objects;
    CHECK(objects.TakeImageIds(0) == std::nullopt);
    CHECK(objects.TakeImageIds(3) == 1U);
    CHECK(objects.next_image == 4);
    // Up to the top of the range: the last id taken is 0xFFFFFFFE, next_image ends at the top.
    objects.next_image = 0xFFFFFFF0;
    CHECK(objects.TakeImageIds(0x10) == std::nullopt);
    CHECK(objects.next_image == 0xFFFFFFF0);
    CHECK(objects.TakeImageIds(0xF) == 0xFFFFFFF0U);
    CHECK(objects.next_image == 0xFFFFFFFF);
    CHECK(objects.TakeImageIds(1) == std::nullopt);
}

TEST_CASE("Memory ids are taken without wrapping", "[nx2_gsv]") {
    Service::Nx2NativeObjects objects;
    CHECK(objects.TakeMemoryId() == 1U);
    CHECK(objects.TakeMemoryId() == 2U);
    objects.next_memory = 0xFFFFFFFE;
    CHECK(objects.TakeMemoryId() == 0xFFFFFFFEU);
    CHECK(objects.TakeMemoryId() == std::nullopt);
    CHECK(objects.next_memory == 0xFFFFFFFF);
}
