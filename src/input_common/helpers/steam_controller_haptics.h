// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>

#include "common/common_types.h"
#include "common/input.h"
#include "input_common/helpers/steam_controller_protocol.h"

namespace InputCommon {

class SteamControllerHaptics {
public:
    // Keeps transport lifecycle and failure handling testable without controller hardware.
    class Transport {
    public:
        virtual ~Transport() = default;
        virtual bool Open(const std::string& path) = 0;
        virtual bool Configure(u8 format) = 0;
        virtual bool Write(const SteamHaptics::Report& report) = 0;
        virtual void Silence(u8 format) = 0;
        virtual void Close() = 0;
    };

    SteamControllerHaptics(std::string path, u16 product_id);
    SteamControllerHaptics(u16 product_id, std::unique_ptr<Transport> transport);
    ~SteamControllerHaptics();

    SteamControllerHaptics(const SteamControllerHaptics&) = delete;
    SteamControllerHaptics& operator=(const SteamControllerHaptics&) = delete;

    /// Updates the requested grip(s). Returns false when standard rumble should be used.
    bool SetVibration(const Common::Input::VibrationStatus& vibration, bool enabled);

    bool IsStreaming() const;
    void Disconnect();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace InputCommon
