// SPDX-FileCopyrightText: Copyright 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/hle/service/am/am_types.h"
#include "tests/core/system_test_context.h"

using Service::AM::AppletProgramId;
using Service::AM::IsAppletProgramId;

TEST_CASE("Applet program ids cover the HOME Menu, overlayDisp and the LLE applets only",
          "[core][am][home_menu]") {
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::QLaunch)));
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::OverlayDisplay)));
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::Starter)));
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::MaxProgramId)));
    CHECK_FALSE(IsAppletProgramId(0x0100000000000FFFULL));
    CHECK_FALSE(IsAppletProgramId(0x0100000000002000ULL));
    CHECK_FALSE(IsAppletProgramId(0x0100152000022000ULL));
    CHECK_FALSE(IsAppletProgramId(0x0400C3F00006E000ULL));
}

TEST_CASE("A HOME Menu launch in a new session reaches the frontend that can boot it",
          "[core][am][home_menu]") {
    ScopedSystemTestContext environment;
    Core::System system;
    constexpr u64 Switch2Title = 0x0400C3F00006E000ULL;

    CHECK_FALSE(system.LaunchInNewSession(Switch2Title));

    std::optional<u64> launched;
    system.RegisterLaunchInNewSessionCallback([&](u64 program_id) { launched = program_id; });
    CHECK(system.LaunchInNewSession(Switch2Title));
    CHECK(launched == Switch2Title);
}
