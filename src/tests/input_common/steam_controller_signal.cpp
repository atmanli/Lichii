// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <complex>
#include <numbers>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "input_common/helpers/steam_controller_protocol.h"

namespace {
f32 DecodeMuLaw(u8 value) {
    const int bits = static_cast<u8>(~value);
    const int magnitude = ((((bits & 15) << 3) + 132) << ((bits >> 4) & 7)) - 132;
    return static_cast<f32>(bits & 128 ? -magnitude : magnitude) / 32768.0f;
}

f64 Magnitude(const std::vector<f32>& samples, u32 rate, f64 frequency) {
    std::complex<f64> sum{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const f64 phase = -2.0 * std::numbers::pi * frequency * static_cast<f64>(i) / rate;
        sum += static_cast<f64>(samples[i]) * std::polar(1.0, phase);
    }
    return std::abs(sum) * 2.0 / static_cast<f64>(samples.size());
}
} // namespace

TEST_CASE("Steam grip PCM preserves independent simultaneous frequencies", "[input][rumble]") {
    using namespace InputCommon::SteamHaptics;
    for (const u32 rate : {4000U, 8000U}) {
        GripPcm pcm{rate};
        const std::array<Target, 2> targets{
            {{0.25f, 220.0f, 0.25f, 660.0f}, {0.25f, 330.0f, 0.0f, 320.0f}}};
        for (int i = 0; i < 32; ++i) {
            pcm.Build(targets);
        }
        std::array<std::vector<f32>, 2> samples;
        while (samples[0].size() < rate) {
            const auto report = pcm.Build(targets);
            REQUIRE(report[0] == 0x88);
            REQUIRE(report[1] == 31);
            for (std::size_t side = 0; side < 2; ++side) {
                for (std::size_t i = 0; i < 31; ++i) {
                    samples[side].push_back(DecodeMuLaw(report[2 + side * 31 + i]));
                }
            }
        }
        for (auto& side : samples) {
            side.resize(rate);
        }
        REQUIRE(Magnitude(samples[0], rate, 220.0) > 0.35);
        REQUIRE(Magnitude(samples[0], rate, 660.0) > 0.35);
        REQUIRE(Magnitude(samples[0], rate, 330.0) < 0.01);
        REQUIRE(Magnitude(samples[1], rate, 330.0) > 0.35);
        REQUIRE(Magnitude(samples[1], rate, 220.0) < 0.01);

        Report stopped;
        for (int i = 0; i < 32; ++i) {
            stopped = pcm.Build({});
        }
        REQUIRE(std::all_of(stopped.begin() + 2, stopped.end(),
                            [](u8 sample) { return sample == 0xff || sample == 0x7f; }));
    }
}

TEST_CASE("Steam grip PCM keeps note pitch across both transport rates", "[input][rumble]") {
    using namespace InputCommon::SteamHaptics;
    for (const u32 rate : {4000U, 8000U}) {
        for (const f32 frequency : {220.0f, 330.0f, 440.0f, 660.0f, 880.0f, 1046.0f, 1250.0f}) {
            GripPcm pcm{rate};
            const std::array<Target, 2> targets{{{0.0f, 160.0f, 0.25f, frequency}, {}}};
            for (int i = 0; i < 32; ++i) {
                pcm.Build(targets);
            }
            std::vector<f32> samples;
            bool right_silent = true;
            while (samples.size() < rate) {
                const auto report = pcm.Build(targets);
                for (std::size_t i = 0; i < 31; ++i) {
                    samples.push_back(DecodeMuLaw(report[2 + i]));
                    right_silent &= report[33 + i] == 0xff;
                }
            }
            samples.resize(rate);
            INFO("rate=" << rate << " frequency=" << frequency);
            REQUIRE(Magnitude(samples, rate, frequency) > 0.35);
            REQUIRE(Magnitude(samples, rate, 160.0) < 0.01);
            REQUIRE(right_silent);
        }
    }
}

TEST_CASE("Steam grip PCM uses standard mu-law reference encodings", "[input][rumble]") {
    using InputCommon::SteamHaptics::EncodeMuLaw;
    REQUIRE(EncodeMuLaw(0.0f) == 0xff);
    REQUIRE(EncodeMuLaw(1.0f) == 0x80);
    REQUIRE(EncodeMuLaw(-1.0f) == 0x00);
}

TEST_CASE("Steam Controller report writes accept Windows padding", "[input][rumble]") {
    using InputCommon::SteamHaptics::CompleteReportWrite;
    REQUIRE(CompleteReportWrite(64, 64));
    REQUIRE(CompleteReportWrite(65, 64));
    REQUIRE_FALSE(CompleteReportWrite(63, 64));
    REQUIRE_FALSE(CompleteReportWrite(-1, 64));
}

TEST_CASE("Steam Controller product IDs select transport rate", "[input][rumble]") {
    using namespace InputCommon::SteamHaptics;
    REQUIRE(IsSupportedProduct(UsbProductId));
    REQUIRE(IsSupportedProduct(BluetoothProductId));
    REQUIRE(IsSupportedProduct(PuckProductId));
    REQUIRE(IsSupportedProduct(InternalReceiverProductId));
    REQUIRE_FALSE(IsSupportedProduct(0x1306));
    REQUIRE(FormatForProduct(UsbProductId) == UsbPcmFormat);
    REQUIRE(SampleRateForProduct(UsbProductId) == UsbSampleRate);
    REQUIRE(FormatForProduct(PuckProductId) == WirelessPcmFormat);
    REQUIRE(SampleRateForProduct(PuckProductId) == WirelessSampleRate);
}
