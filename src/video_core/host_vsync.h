// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include "common/common_types.h"

/// Starts the frames of a game whose frame rate is unlocked from the host's display instead of a
/// fixed timer: the presenter reports each frame that reached the display (present wait), and the
/// nx2:vi2 sessions that asked for it signal their vsync events then. The fixed timer only steps
/// in while the display reports nothing (Service::TimerVsyncSignals). EDEN_NX2_HOST_VSYNC=0 keeps
/// the timer.
namespace VideoCore::HostVsync {

using Clock = std::chrono::steady_clock;

/// What the fallback timer of a listener needs.
struct ListenerStart {
    std::optional<Clock::time_point> last; ///< When the display last started its frame, if paced
    u32 rate{};                            ///< SetPacing
};

/// Display-driven vsync is not switched off.
[[nodiscard]] bool Enabled();

/// The display starts the frames of a listener now (SetPacing).
[[nodiscard]] bool Active();

/// Adds a frame starter (the vsync events of an nx2:vi2 session), not paced by the display until
/// SetPacing says so. `owner` names it in the other calls.
void AddListener(const void* owner, std::function<void()> start);

/// Removes the listener of `owner`, if any. Its function is not running once this returns.
void RemoveListener(const void* owner);

/// Whether the display starts the frames of `owner` (only for a game whose frame rate is
/// unlocked: a locked one counts vsync events), and at most how many per second (the FPS limit,
/// 0 for none).
void SetPacing(const void* owner, bool active, u32 rate);

[[nodiscard]] ListenerStart LastStart(const void* owner);

/// The presenter, when a frame reached the display at `displayed`: when to start the next frame
/// (Service::DisplayVsyncDeadline, the latest of the listeners), or nothing without a listener to
/// start. The presenter waits until then and calls StartFrame.
[[nodiscard]] std::optional<Clock::time_point> NextStart(Clock::time_point displayed);

/// Starts the next frame of every listener the display paces.
///
/// The functions signal guest kernel events from the calling host thread (the presenter's), as
/// the host1x syncpoint actions of nvhost_ctrl do from the GPU thread: KEvent::Signal takes the
/// scheduler lock and is not bound to an emulated thread.
void StartFrame();

/// Forgets every listener: a new presenter starts with none left over from an earlier boot.
void Reset();

} // namespace VideoCore::HostVsync
