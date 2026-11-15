// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <limits>
#include <catch2/catch_test_macros.hpp>

#include "video_core/renderer_vulkan/async_draw_policy.h"

namespace {
struct BudgetClock {
    inline static std::chrono::steady_clock::time_point current{};
    static auto now() {
        return current;
    }
};
} // namespace

TEST_CASE("Translation and driver waits share the remaining native frame budget",
          "[video_core][async_draw][native_pending]") {
    using namespace std::chrono_literals;
    BudgetClock::current = {};
    s64 remaining = 8000;
    s64 used = 0;
    CHECK(Vulkan::WaitWithinNativeBudget<BudgetClock>(remaining, used, [&](auto timeout) {
        CHECK(timeout == 8ms);
        BudgetClock::current += 3ms;
        return true;
    }));
    CHECK_FALSE(Vulkan::WaitWithinNativeBudget<BudgetClock>(remaining, used, [&](auto timeout) {
        CHECK(timeout == 5ms);
        BudgetClock::current += 6ms;
        return false;
    }));
    CHECK(remaining == -1000);
    CHECK(used == 9000);
    bool called = false;
    CHECK_FALSE(Vulkan::WaitWithinNativeBudget<BudgetClock>(remaining, used, [&](auto) {
        called = true;
        return true;
    }));
    CHECK_FALSE(called);
    CHECK(Vulkan::DecideNativePending(Vulkan::NativePendingMode::Bounded, true, remaining) ==
          Vulkan::NativeDrawAction::Skip);
}

TEST_CASE("A pending translation uses a renewed frame budget and rounds partial microseconds up",
          "[video_core][async_draw][native_pending]") {
    using namespace std::chrono_literals;
    BudgetClock::current = {};
    s64 remaining = 0;
    s64 used = 100;
    CHECK(Vulkan::DecideNativePending(Vulkan::NativePendingMode::Bounded, true, remaining) ==
          Vulkan::NativeDrawAction::Skip);
    remaining = 8000; // TickFrame renews the budget while the same translation remains pending.
    CHECK(Vulkan::DecideNativePending(Vulkan::NativePendingMode::Bounded, true, remaining) ==
          Vulkan::NativeDrawAction::WaitTimed);
    CHECK_FALSE(Vulkan::WaitWithinNativeBudget<BudgetClock>(remaining, used, [&](auto timeout) {
        CHECK(timeout == 8ms);
        BudgetClock::current += 2500ns;
        return false;
    }));
    CHECK(remaining == 7997);
    CHECK(used == 103);
}

TEST_CASE("Native pending draws are never discarded", "[video_core][async_draw]") {
    constexpr std::array<u32, 6> counts{0, 1, 6, 7, 4096, std::numeric_limits<u32>::max()};
    for (const bool depth : {false, true}) {
        for (const u32 indices : counts) {
            for (const u32 vertices : counts) {
                REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(true, depth, indices, vertices));
            }
        }
    }
}

TEST_CASE("Native pending draws with depth can be discarded when opted in",
          "[video_core][async_draw]") {
    REQUIRE(Vulkan::CanDiscardPendingDraw(true, true, 0, 0, true));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(true, false, 7, 7, true));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(true, false, 6, 100, true));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(true, false, 0, 0, true));
    // The opt-in never changes non-native draws.
    REQUIRE(Vulkan::CanDiscardPendingDraw(false, true, 0, 0, true) ==
            Vulkan::CanDiscardPendingDraw(false, true, 0, 0));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 0, 0, true));
}

TEST_CASE("Star Fox font atlas draws wait even when the native wait budget is exhausted",
          "[video_core][async_draw][native_pending]") {
    constexpr std::array<u32, 4> glyph_counts{30, 96, 384, 672};
    for (const u32 count : glyph_counts) {
        const bool can_discard = Vulkan::CanDiscardPendingDraw(true, false, count, count, true);
        for (const auto mode : {Vulkan::NativePendingMode::Wait, Vulkan::NativePendingMode::Bounded,
                                Vulkan::NativePendingMode::Skip}) {
            for (const s64 budget : {8000, 0, -500}) {
                REQUIRE(Vulkan::DecideNativePending(mode, can_discard, budget) ==
                        Vulkan::NativeDrawAction::Wait);
            }
        }
    }
}

TEST_CASE("Legacy pending draws retain the async heuristic", "[video_core][async_draw]") {
    REQUIRE(Vulkan::CanDiscardPendingDraw(false, true, 0, 0));
    REQUIRE(Vulkan::CanDiscardPendingDraw(false, false, 7, 7));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 6, 100));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 100, 6));
    REQUIRE_FALSE(Vulkan::CanDiscardPendingDraw(false, false, 0, 0));
}
