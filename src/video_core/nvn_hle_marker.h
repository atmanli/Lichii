// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"

// Commands a host implementation of an NVN function (Core::NvnHook, EDEN_NX2_NVN_HLE) writes into
// the game's command memory in place of the driver's encoding, so the GPU front meets the call at
// its place in the stream (memory recorded once and called again keeps it too). An incrementing
// method on subchannel 7, which NVN binds to no engine; DmaPusher hands its words to its NVN
// replay instead of an engine.
namespace VideoCommon::NvnHleMarker {

constexpr u32 Subchannel = 7;

/// nvnCommandBufferBindTexture, graphics stages: two words.
///   word 0: slot (bits 0-7), stage (8-11), pipe_nop (12), macro 0xe1c's argument (16-31)
///   word 1: the handle's low 32 bits
/// The front replays what the driver writes: IMMD 0xe1c = argument, IMMD 0x68b = 0 if pipe_nop,
/// then INCR 0x8e3 {0x20 + 8 * slot, handle}.
constexpr u32 BindTexture = 0x1000;

/// Set when a host implementation may write markers (the front checks subchannel 7 only then).
inline bool g_enabled{};

[[nodiscard]] constexpr u32 BindTextureWord0(u32 slot, u32 stage, bool pipe_nop, u32 argument) {
    return (slot & 0xff) | ((stage & 0xf) << 8) | (pipe_nop ? 1u << 12 : 0) | ((argument & 0x1fff) << 16);
}
[[nodiscard]] constexpr u32 BindTextureSlot(u32 word0) {
    return word0 & 0xff;
}
[[nodiscard]] constexpr bool BindTexturePipeNop(u32 word0) {
    return ((word0 >> 12) & 1) != 0;
}
/// Macro 0xe1c's argument (an inline header's 13 bits)
[[nodiscard]] constexpr u32 BindTextureArgument(u32 word0) {
    return (word0 >> 16) & 0x1fff;
}
/// The driver's words a BindTexture marker stands for (what the front replays); returns the count.
constexpr u32 ExpandBindTexture(u32 word0, u32 handle, u32 (&words)[5]) {
    u32 count = 0;
    words[count++] = 0x80000e1cu | (BindTextureArgument(word0) << 16); // IMMD 0xe1c
    if (BindTexturePipeNop(word0)) {
        words[count++] = 0x8000068bu; // IMMD 0x68b = 0
    }
    words[count++] = 0x200208e3u; // INCR 0x8e3, 2 words
    words[count++] = 0x20 + 8 * BindTextureSlot(word0);
    words[count++] = handle;
    return count;
}

[[nodiscard]] constexpr u32 Header(u32 method, u32 count) {
    return (1u << 29) | (count << 16) | (Subchannel << 13) | method; // incrementing
}

} // namespace VideoCommon::NvnHleMarker
