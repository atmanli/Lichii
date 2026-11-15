// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace VideoCore { class RendererBase; }

namespace Core::GameSettings {

enum class OS {
    Windows,
    Linux,
    MacOS,
    IOS,
    Android,
    FireOS,
    HarmonyOS,
    FreeBSD,
    DragonFlyBSD,
    NetBSD,
    OpenBSD,
    HaikuOS,
    AIX,
    Managarm,
    RedoxOS,
    Solaris,
    Unknown,
};

enum class GPUVendor {
    Nvidia,
    AMD,
    Intel,
    Apple,
    Qualcomm,
    ARM,
    Imagination,
    Microsoft,
    Unknown,
};

enum class TitleID : std::uint64_t {
    NinjaGaidenRagebound = 0x0100781020710000ULL,
    DuskbloodsNetworkTest = 0x0400D1D02399A000ULL,
    XenobladeChroniclesDENintendoSwitch2Edition = 0x0400FF500E34A001ULL,
    XenobladeChronicles2NintendoSwitch2Edition = 0x0400E95004038001ULL,
    XenobladeChroniclesXDENintendoSwitch2Edition = 0x0400453019AA8001ULL,
};

struct EnvironmentInfo {
    OS os{OS::Unknown};
    GPUVendor vendor{GPUVendor::Unknown};
    std::string vendor_string; // raw string from driver
};

EnvironmentInfo DetectEnvironment(const VideoCore::RendererBase& renderer);

void LoadOverrides(std::uint64_t program_id, const VideoCore::RendererBase& renderer);

} // namespace Core::GameSettings
