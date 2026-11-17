// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "core/nvn_direct_choice.h"

namespace {
using Core::NvnHook::ChooseRenderer;
using Core::NvnHook::RendererChoice;
using Core::NvnHook::RendererInputs;

/// A 64-bit NVN game on Vulkan, in a build with the direct renderer, first boot of the process.
constexpr RendererInputs Runnable{
    .built = true,
    .override_ = std::nullopt,
    .setting = true,
    .vulkan = true,
    .is_64bit = true,
    .has_nvn = true,
    .direct_ran_before = false,
    .replay_failed = false,
};
} // namespace

TEST_CASE("The setting and the override choose the renderer", "[nvn_direct_choice]") {
    REQUIRE(ChooseRenderer(Runnable) == RendererChoice::Setting);

    RendererInputs off = Runnable;
    off.setting = false;
    REQUIRE(ChooseRenderer(off) == RendererChoice::SettingOff);

    // EDEN_NX2_NVN_DIRECT wins over the setting both ways.
    off.override_ = true;
    REQUIRE(ChooseRenderer(off) == RendererChoice::Override);
    RendererInputs on = Runnable;
    on.override_ = false;
    REQUIRE(ChooseRenderer(on) == RendererChoice::OverrideOff);
}

TEST_CASE("A game the direct renderer cannot run falls back", "[nvn_direct_choice]") {
    const auto with = [](auto change) {
        RendererInputs inputs = Runnable;
        change(inputs);
        return ChooseRenderer(inputs);
    };
    REQUIRE(with([](RendererInputs& in) { in.built = false; }) == RendererChoice::NotBuilt);
    REQUIRE(with([](RendererInputs& in) { in.vulkan = false; }) == RendererChoice::NotVulkan);
    REQUIRE(with([](RendererInputs& in) { in.is_64bit = false; }) == RendererChoice::Not64Bit);
    REQUIRE(with([](RendererInputs& in) { in.has_nvn = false; }) == RendererChoice::NoNvn);
    REQUIRE(with([](RendererInputs& in) { in.direct_ran_before = true; }) ==
            RendererChoice::EarlierBoot);
    // A start that failed on the device stays the reason for the later boots of the process.
    REQUIRE(with([](RendererInputs& in) {
                in.direct_ran_before = true;
                in.replay_failed = true;
            }) == RendererChoice::ReplayFailed);
    // The override cannot force a game the direct renderer cannot run.
    REQUIRE(with([](RendererInputs& in) {
                in.override_ = true;
                in.is_64bit = false;
            }) == RendererChoice::Not64Bit);
    // Nothing asked for: no fallback to report, whatever the game.
    REQUIRE(with([](RendererInputs& in) {
                in.setting = false;
                in.has_nvn = false;
            }) == RendererChoice::SettingOff);
}

TEST_CASE("Only refusals of an asked-for direct renderer are fallbacks", "[nvn_direct_choice]") {
    using Core::NvnHook::IsDirect;
    using Core::NvnHook::IsFallback;
    REQUIRE(IsDirect(RendererChoice::Setting));
    REQUIRE(IsDirect(RendererChoice::Override));
    REQUIRE_FALSE(IsFallback(RendererChoice::Setting));
    REQUIRE_FALSE(IsFallback(RendererChoice::SettingOff));
    REQUIRE_FALSE(IsFallback(RendererChoice::OverrideOff));
    REQUIRE(IsFallback(RendererChoice::EarlierBoot));
    REQUIRE(IsFallback(RendererChoice::ReplayFailed));
    REQUIRE_FALSE(IsDirect(RendererChoice::EarlierBoot));
}
