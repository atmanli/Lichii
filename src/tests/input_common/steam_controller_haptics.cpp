// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <complex>
#include <limits>
#include <numbers>
#include <vector>

#include <SDL3/SDL_hidapi.h>
#include <catch2/catch_test_macros.hpp>

#include "input_common/helpers/steam_controller_haptics.h"

namespace {
using namespace std::chrono_literals;
using Common::Input::VibrationSide;
using Common::Input::VibrationStatus;
using InputCommon::SteamControllerHaptics;
using namespace InputCommon::SteamHaptics;

struct Device {
    std::mutex mutex;
    std::condition_variable wake;
    Report last{};
    int writes{}, opens{}, closes{}, modes{};
    bool open_ok{true}, write_ok{true};

    template <typename Predicate>
    bool Wait(Predicate predicate) {
        std::unique_lock lock{mutex};
        return wake.wait_for(lock, 2s, predicate);
    }
};
Device devices[2];

void Reset() {
    for (auto& d : devices) {
        std::scoped_lock lock{d.mutex};
        d.last = {};
        d.writes = d.opens = d.closes = d.modes = 0;
        d.open_ok = d.write_ok = true;
    }
}

VibrationStatus Tone(VibrationSide side = VibrationSide::Both) {
    return {.low_amplitude = 0.25f,
            .low_frequency = 220,
            .high_amplitude = 0.25f,
            .high_frequency = 660,
            .side = side};
}

f64 Decode(u8 byte) {
    const u8 value = static_cast<u8>(~byte);
    const int magnitude = (((value & 15) * 8 + 132) << ((value >> 4) & 7)) - 132;
    return (value & 128 ? -magnitude : magnitude) / 32768.0;
}

f64 Magnitude(const std::vector<f64>& samples, u32 rate, f64 frequency) {
    std::complex<f64> sum{};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        sum += samples[i] *
               std::polar(1.0, -2.0 * std::numbers::pi * frequency * static_cast<f64>(i) / rate);
    }
    return std::abs(sum) * 2 / static_cast<f64>(samples.size());
}
} // namespace

// This dedicated test executable replaces only HID I/O, keeping SDL's real timers.
extern "C" SDL_hid_device* SDL_hid_open_path(const char* path) {
    auto& d = devices[path[0] == '2' ? 1 : 0];
    std::scoped_lock lock{d.mutex};
    ++d.opens;
    d.writes = 0;
    d.last = {};
    return d.open_ok ? reinterpret_cast<SDL_hid_device*>(&d) : nullptr;
}

extern "C" int SDL_hid_write(SDL_hid_device* handle, const unsigned char* data, size_t length) {
    auto& d = *reinterpret_cast<Device*>(handle);
    std::scoped_lock lock{d.mutex};
    if (!d.write_ok) {
        return static_cast<int>(length) - 1;
    }
    if (data[0] == 0x88) {
        std::copy_n(data, d.last.size(), d.last.begin());
        ++d.writes;
    } else if (data[0] == 0x86) {
        ++d.modes;
    }
    d.wake.notify_all();
    return static_cast<int>(length) + 1;
}

extern "C" int SDL_hid_close(SDL_hid_device* handle) {
    auto& d = *reinterpret_cast<Device*>(handle);
    std::scoped_lock lock{d.mutex};
    ++d.closes;
    d.wake.notify_all();
    return 0;
}

TEST_CASE("Steam PCM preserves pitch and stereo at both transport rates", "[input][rumble]") {
    for (const u32 rate : {4000U, 8000U}) {
        for (const f32 frequency : {220.0f, 440.0f, 660.0f, 880.0f, 1046.0f, 1253.0f}) {
            Pcm pcm{rate};
            auto tone = Tone();
            tone.high_frequency = frequency;
            auto right = Tone();
            right.low_frequency = 330;
            right.high_amplitude = 0;
            const std::array targets{tone, right};
            for (int i = 0; i < 32; ++i) {
                pcm.Render(targets);
            }
            std::array<std::vector<f64>, 2> samples;
            while (samples[0].size() < rate) {
                const auto report = pcm.Render(targets);
                REQUIRE(report[0] == 0x88);
                REQUIRE(report[1] == SamplesPerGrip);
                for (std::size_t side = 0; side < 2; ++side) {
                    for (std::size_t i = 0; i < SamplesPerGrip; ++i) {
                        samples[side].push_back(Decode(report[2 + side * SamplesPerGrip + i]));
                    }
                }
            }
            for (auto& side : samples) {
                side.resize(rate);
            }
            REQUIRE(Magnitude(samples[0], rate, frequency) > 0.25);
            REQUIRE(Magnitude(samples[0], rate, 330) < 0.01);
            REQUIRE(Magnitude(samples[1], rate, 330) > 0.35);
            REQUIRE(Magnitude(samples[1], rate, frequency) < 0.01);
        }
    }
}

TEST_CASE("Steam PCM keeps strength changes and sanitizes invalid input", "[input][rumble]") {
    Pcm quiet{8000}, loud{8000}, invalid{8000};
    auto strong = Tone();
    strong.low_amplitude = strong.high_amplitude = 1;
    REQUIRE(quiet.Render({Tone(), {}}) != loud.Render({strong, {}}));
    auto bad = Tone();
    bad.low_amplitude = std::numeric_limits<f32>::quiet_NaN();
    bad.high_frequency = std::numeric_limits<f32>::infinity();
    const auto silence = invalid.Render({bad, {}});
    REQUIRE(std::all_of(silence.begin() + 2, silence.end(), [](u8 b) { return b == 0xff; }));
}

TEST_CASE("Steam HID streams separately and sleeps when idle", "[input][rumble]") {
    Reset();
    SteamControllerHaptics first{"1", 0x1302}, second{"2", 0x1304};
    REQUIRE(first.SetVibration(Tone(VibrationSide::Left), true));
    REQUIRE(second.SetVibration(Tone(VibrationSide::Right), true));
    REQUIRE(devices[0].Wait([] { return devices[0].writes > 0; }));
    REQUIRE(devices[1].Wait([] { return devices[1].writes > 0; }));
    {
        std::scoped_lock lock{devices[0].mutex, devices[1].mutex};
        REQUIRE(std::all_of(devices[0].last.begin() + 33, devices[0].last.end(),
                            [](u8 value) { return value == 0xff; }));
        REQUIRE(std::all_of(devices[1].last.begin() + 2, devices[1].last.begin() + 33,
                            [](u8 value) { return value == 0xff; }));
    }
    REQUIRE(first.SetVibration({}, false) == false);
    REQUIRE(devices[0].closes == 1);
    REQUIRE(first.SetVibration(Tone(VibrationSide::Right), true));
    REQUIRE(devices[0].Wait([] { return devices[0].opens == 2 && devices[0].writes > 0; }));
    REQUIRE(first.SetVibration({}, true));
    std::this_thread::sleep_for(200ms);
    int writes;
    {
        std::scoped_lock lock{devices[0].mutex};
        writes = devices[0].writes;
    }
    std::this_thread::sleep_for(50ms);
    {
        std::scoped_lock lock{devices[0].mutex};
        REQUIRE(devices[0].writes == writes);
    }
}

TEST_CASE("Steam HID closes during setup and falls back after failure", "[input][rumble]") {
    Reset();
    SteamControllerHaptics haptics{"1", 0x1302};
    REQUIRE(haptics.SetVibration(Tone(), true));
    REQUIRE(devices[0].Wait([] { return devices[0].modes == 6; }));
    REQUIRE_FALSE(haptics.SetVibration({}, false));
    REQUIRE(devices[0].closes == 1);
    devices[0].write_ok = false;
    REQUIRE(haptics.SetVibration(Tone(), true));
    REQUIRE(devices[0].Wait([] { return devices[0].closes == 2; }));
    bool fallback = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!fallback && std::chrono::steady_clock::now() < deadline) {
        fallback = !haptics.SetVibration(Tone(), true);
        std::this_thread::sleep_for(1ms);
    }
    REQUIRE(fallback);
    REQUIRE_FALSE(haptics.SetVibration({}, false));
    devices[0].write_ok = true;
    REQUIRE(haptics.SetVibration(Tone(), true));
    REQUIRE(devices[0].Wait([] { return devices[0].writes > 0; }));
}

TEST_CASE("Steam HID expires each grip independently and resumes from idle", "[input][rumble]") {
    Reset();
    SteamControllerHaptics haptics{"1", 0x1302};
    REQUIRE(haptics.SetVibration(Tone(), true));
    REQUIRE(devices[0].Wait([] { return devices[0].writes > 0; }));
    const auto deadline = std::chrono::steady_clock::now() + 2300ms;
    while (std::chrono::steady_clock::now() < deadline) {
        haptics.SetVibration(Tone(VibrationSide::Right), true);
        std::this_thread::sleep_for(50ms);
    }
    REQUIRE(devices[0].Wait([] {
        return std::all_of(devices[0].last.begin() + 2, devices[0].last.begin() + 33,
                           [](u8 value) { return value == 0xff || value == 0x7f; }) &&
               std::any_of(devices[0].last.begin() + 33, devices[0].last.end(),
                           [](u8 value) { return value != 0xff && value != 0x7f; });
    }));
    haptics.SetVibration({}, true);
    std::this_thread::sleep_for(150ms);
    REQUIRE(haptics.SetVibration(Tone(VibrationSide::Left), true));
    REQUIRE(devices[0].Wait([] {
        return std::any_of(devices[0].last.begin() + 2, devices[0].last.begin() + 33,
                           [](u8 value) { return value != 0xff && value != 0x7f; });
    }));
}
