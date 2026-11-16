// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "input_common/helpers/steam_controller_haptics.h"

namespace {
class FakeTransport final : public InputCommon::SteamControllerHaptics::Transport {
public:
    bool Open(const std::string&) override {
        std::scoped_lock lock{mutex};
        ++opens;
        return true;
    }

    bool Configure(u8 format) override {
        std::scoped_lock lock{mutex};
        ++configures;
        last_format = format;
        return configure_success;
    }

    bool Write(const InputCommon::SteamHaptics::Report& report) override {
        std::scoped_lock lock{mutex};
        const bool success = !fail_writes;
        if (success && report[0] == 0x88) {
            ++pcm_writes;
            last_report = report;
        }
        wake.notify_all();
        return success;
    }

    void Silence(u8) override {
        std::scoped_lock lock{mutex};
        ++silences;
        wake.notify_all();
    }

    void Close() override {
        std::scoped_lock lock{mutex};
        ++closes;
        wake.notify_all();
    }

    bool WaitForPcmWrites(int count) {
        std::unique_lock lock{mutex};
        return wake.wait_for(lock, std::chrono::seconds{2},
                             [this, count] { return pcm_writes >= count; });
    }

    bool WaitForSilence() {
        std::unique_lock lock{mutex};
        return wake.wait_for(lock, std::chrono::seconds{3}, [this] { return silences > 0; });
    }

    void SetConfigureSuccess(bool success) {
        std::scoped_lock lock{mutex};
        configure_success = success;
    }

    void SetWriteFailures(bool fail) {
        std::scoped_lock lock{mutex};
        fail_writes = fail;
    }

    int ConfigureCount() {
        std::scoped_lock lock{mutex};
        return configures;
    }

    int CloseCount() {
        std::scoped_lock lock{mutex};
        return closes;
    }

    int OpenCount() {
        std::scoped_lock lock{mutex};
        return opens;
    }

    u8 LastFormat() {
        std::scoped_lock lock{mutex};
        return last_format;
    }

    InputCommon::SteamHaptics::Report LastReport() {
        std::scoped_lock lock{mutex};
        return last_report;
    }

private:
    std::mutex mutex;
    std::condition_variable wake;
    int opens{};
    int configures{};
    int pcm_writes{};
    int silences{};
    int closes{};
    u8 last_format{};
    InputCommon::SteamHaptics::Report last_report{};
    bool configure_success{true};
    bool fail_writes{};
};

Common::Input::VibrationStatus Vibration(Common::Input::VibrationSide side) {
    return {
        .low_amplitude = 0.4f,
        .low_frequency = 220.0f,
        .high_amplitude = 0.3f,
        .high_frequency = 660.0f,
        .type = Common::Input::VibrationAmplificationType::Linear,
        .side = side,
    };
}
} // namespace

TEST_CASE("Steam Controller haptics close and reopen cleanly", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* const fake = transport.get();
    InputCommon::SteamControllerHaptics haptics{0x1302, std::move(transport)};

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), true));
    REQUIRE(fake->WaitForPcmWrites(1));
    CHECK(haptics.IsStreaming());
    REQUIRE(fake->LastFormat() == 8);
    const auto left_report = fake->LastReport();
    REQUIRE(left_report[1] == 31);
    REQUIRE(left_report[33] == 0xff);
    REQUIRE(std::any_of(left_report.begin() + 2, left_report.begin() + 33,
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Right), true));
    REQUIRE(fake->WaitForPcmWrites(2));
    const auto both_report = fake->LastReport();
    REQUIRE(std::any_of(both_report.begin() + 2, both_report.begin() + 33,
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE(std::any_of(both_report.begin() + 33, both_report.end(),
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), false));
    REQUIRE(fake->CloseCount() == 1);

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Left), true));
    REQUIRE(fake->OpenCount() == 2);
    REQUIRE(fake->WaitForPcmWrites(3));
    REQUIRE(fake->LastFormat() == 8);
    const auto left_only_report = fake->LastReport();
    REQUIRE(std::any_of(left_only_report.begin() + 2, left_only_report.begin() + 33,
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE(std::all_of(left_only_report.begin() + 33, left_only_report.end(),
                        [](u8 sample) { return sample == 0xff; }));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    REQUIRE(fake->CloseCount() == 2);

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Right), true));
    REQUIRE(fake->OpenCount() == 3);
    REQUIRE(fake->WaitForPcmWrites(4));
    const auto reopened_report = fake->LastReport();
    REQUIRE(reopened_report[2] == 0xff);
    REQUIRE(std::any_of(reopened_report.begin() + 33, reopened_report.end(),
                        [](u8 sample) { return sample != 0xff && sample != 0x7f; }));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    REQUIRE(fake->CloseCount() == 3);
    CHECK_FALSE(haptics.IsStreaming());
    haptics.Disconnect();
    CHECK_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    CHECK(fake->OpenCount() == 3);
}

TEST_CASE("Steam Controller haptics recover once before falling back", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* const fake = transport.get();
    fake->SetWriteFailures(true);
    InputCommon::SteamControllerHaptics haptics{0x1303, std::move(transport)};

    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->WaitForSilence());
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->ConfigureCount() == 2);
    REQUIRE(fake->LastFormat() == 9);
    REQUIRE(fake->CloseCount() == 1);
}

TEST_CASE("Steam Controller haptics fall back after setup failure", "[input][rumble]") {
    auto transport = std::make_unique<FakeTransport>();
    auto* const fake = transport.get();
    fake->SetConfigureSuccess(false);
    InputCommon::SteamControllerHaptics haptics{0x1304, std::move(transport)};

    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->CloseCount() == 1);
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    fake->SetConfigureSuccess(true);
    REQUIRE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), true));
    REQUIRE(fake->WaitForPcmWrites(1));
    REQUIRE_FALSE(haptics.SetVibration(Vibration(Common::Input::VibrationSide::Both), false));
    REQUIRE(fake->CloseCount() == 2);
}
