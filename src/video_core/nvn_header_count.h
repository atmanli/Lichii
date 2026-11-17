// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "common/common_types.h"

namespace VideoCommon::NvnHeaderCount {

/// EDEN_NX2_NVN_EMITTED (Core::NvnHook): the front counts the command headers it parses by
/// (subchannel << 13 | method), to compare with the headers NVN's proc-table calls wrote.
inline bool g_enabled{};
/// Written by the GPU (front) thread only; read racily by the NVN hook's periodic dump.
inline std::array<u64, 1u << 16> g_headers{};

/// Headers the front parsed from words a proc-table call wrote (the same word, at the same host
/// address), by key: what a table hook would see, replays of recorded memory included.
inline std::array<u64, 1u << 16> g_table_headers{};

/// Header words the hook decoded from table calls, by host address: open addressing, a word
/// written again overwrites (command memory is reused every frame). Lock-free: the game's threads
/// insert, the front looks up.
constexpr u32 WrittenBits = 22;
inline std::array<std::atomic<u64>, 1u << WrittenBits> g_written_keys{};
inline std::array<std::atomic<u32>, 1u << WrittenBits> g_written_values{};

inline u32 WrittenSlot(u64 key) {
    return static_cast<u32>((key * 0x9E3779B97F4A7C15ULL) >> (64 - WrittenBits));
}

inline void NoteWritten(const void* host, u32 header) {
    const u64 key = reinterpret_cast<std::uintptr_t>(host);
    for (u32 i = WrittenSlot(key), probes = 0; probes < 64;
         i = (i + 1) & ((1u << WrittenBits) - 1), ++probes) {
        u64 current = g_written_keys[i].load(std::memory_order_acquire);
        if (current == 0 &&
            g_written_keys[i].compare_exchange_strong(current, key, std::memory_order_acq_rel)) {
            current = key;
        }
        if (current == key) {
            g_written_values[i].store(header, std::memory_order_release);
            return;
        }
    }
}

[[nodiscard]] inline bool WasWritten(const void* host, u32 header) {
    const u64 key = reinterpret_cast<std::uintptr_t>(host);
    for (u32 i = WrittenSlot(key), probes = 0; probes < 64;
         i = (i + 1) & ((1u << WrittenBits) - 1), ++probes) {
        const u64 current = g_written_keys[i].load(std::memory_order_acquire);
        if (current == key) {
            return g_written_values[i].load(std::memory_order_acquire) == header;
        }
        if (current == 0) {
            return false;
        }
    }
    return false;
}

/// `host`: where the header word is in guest memory (null if unknown).
inline void Count(u32 subchannel, u32 method, u32 header, const void* host) {
    const u32 key = ((subchannel & 7) << 13) | (method & 0x1fff);
    ++g_headers[key];
    if (host != nullptr && WasWritten(host, header)) {
        ++g_table_headers[key];
    }
}

} // namespace VideoCommon::NvnHeaderCount
