// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "core/hle/service/ns/language.h"

TEST_CASE("Every application language priority list falls back to American English",
          "[core][service][ns]") {
    // GetDesiredLanguage reports American English when a title's control data is missing
    // (#2150); that answer is only safe while every priority list contains it.
    for (u32 i = 0; i < static_cast<u32>(Service::NS::ApplicationLanguage::Count); ++i) {
        const auto lang = static_cast<Service::NS::ApplicationLanguage>(i);
        const auto* const list = Service::NS::GetApplicationLanguagePriorityList(lang);
        REQUIRE(list != nullptr);
        CHECK(std::ranges::find(*list, Service::NS::ApplicationLanguage::AmericanEnglish) !=
              list->end());
    }
}
