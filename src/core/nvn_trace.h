// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// NVN2 call trace (dotfiles-o72.4, lab branch o72-trace): every call the NVN hook (nvn_hook.h)
// sees, with the guest memory a replay needs, for an offline NVN -> Vulkan replay of real frames.
// Off unless EDEN_NX2_NVN_HOOK=1 and EDEN_NX2_NVN_TRACE=<file>. Records every call from the
// start (objects are made at load); EDEN_NX2_NVN_TRACE_FROM=<present> starts the window: the
// memory pools' contents then, and from then the pool pages each frame changed;
// EDEN_NX2_NVN_TRACE_FRAMES=<n> (default 120) presents later the file is closed and tracing stops.
// Without FROM (or with 0) no window opens: the file holds the calls and presents only (no pool
// snapshot, no changed pages), and FRAMES does not apply: tracing goes on until the emulator stops.
//
// The file: "NVNTRC01", then blocks {u32 compressed, u32 raw, zstd frame}; the raw stream is
// records {u8 type, ...} (little-endian):
//   'N' u16 slot, u16 length, name                  a resolved NVN function
//   'C' u64 seq, u32 thread, u16 slot, u8 nx, u8 nv, u32 items; u64 x[nx]; u64 v[nv]; items
//       item: u8 'M', u64 address, u32 length, bytes  guest memory the call reads (by rule)
//             u8 'H', u64 address, u32 length, u64 hash   a by-value NVN object's bytes, hashed
//   'R' u64 seq, u64 x0, u32 length, bytes          a call's return (x0, and an output it wrote)
//   'P' u64 present, u64 steady ns                  a present (nx2:vi2)
//   'L' u32 count, {u64 pool, u64 storage, u64 size, u32 flags, u32 0}   live pools (window start)
//   'S' u64 address, u32 length, bytes             pool contents (window start; zero pages left out)
//   'D' u64 address, u32 length, bytes             pool pages the frame changed (window, by page hash),
//                                                   written before the frame's 'P': apply before its calls
//                                                   (a page written again between two submits of the
//                                                   frame has only its bytes at the present: the
//                                                   frame's earlier submits replay with them)
//   'W' u8 0 begin / 1 end                         the window
//   'E'                                             the end of the file

#pragma once

#include "common/common_types.h"

namespace Dynarmic::A64 {
class Jit;
}

namespace Core::Memory {
class Memory;
}

namespace Kernel {
class KProcess;
}

namespace Core::NvnTrace {

[[nodiscard]] bool Enabled();

/// The application process (its page table: the fastmem view the dirty pages are read from).
void Install(Kernel::KProcess& process);

/// A name the hook gave a slot to.
void OnName(u32 slot, const char* name);

/// A call entering slot's function. Returns whether its return is wanted (OnReturn with seq).
bool OnCall(u32 slot, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory, u32 thread,
            u64& seq);

/// The return of a call OnCall wanted; arg is the call's x-register OnCall noted (outputs).
void OnReturn(u64 seq, u32 slot, u64 arg, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory);

/// The x-register whose pointed-to output OnReturn records for slot (or ~0).
[[nodiscard]] u32 OutputArgument(u32 slot);

void OnPresent(u64 present);

} // namespace Core::NvnTrace
