// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// NVN2 proc-table hook (dotfiles-o72, lab branch nvn-hle). Games get every NVN function but
// nvnBootstrapLoader by name, through nvnBootstrapLoader (exported by the NVN driver module,
// `sdk`) or nvnDeviceGetProcAddress (which they get from nvnBootstrapLoader). The hook stands at
// nvnBootstrapLoader (found by its export name, in any game) and at what it returns for
// nvnDeviceGetProcAddress, and hands the game a synthetic address per name instead of
// the driver's; the JIT translates a block at a synthetic address as a host call that counts
// the call and continues in the driver's function (pass-through), or runs a host implementation
// and returns to the caller. The same boundary nxnative (seedds, MIT) and Switch2Recomp's census
// hook stand at; this code is Lichii's own.
//
// Off unless EDEN_NX2_NVN_HOOK=1 or the boot runs the direct renderer. Output to the file
// EDEN_NX2_NVN_OUT names:
//   "n <slot> <driver address> <synthetic address> <name>" once per name the game resolved;
//   every EDEN_NX2_NVN_EVERY presents (default 300): "t <present> <steady ns>", then
//   cumulative "c <present> <slot> <calls>" for each slot called, and with
//   EDEN_NX2_NVN_EMITTED=1 "e <present> <key> <headers>" (headers the game's top-level
//   command-buffer calls wrote, key = subchannel << 13 | method), "ew <present> <calls>
//   <words> <skipped>" and "f <present> <key> <headers>" (headers the GPU front parsed);
//   "p0 <present> <value>" (nvnPrivate0).
// EDEN_NX2_NVN_PRIVATE0=<n>: nvnPrivate0 is set to n after every proc-address lookup
// (dotfiles-o72.2: 0 makes the game's inlined NVN encoding call the proc table instead).
// EDEN_NX2_NVN_HLE=BindTexture (dotfiles-o72.3): nvnCommandBufferBindTexture on graphics stages
// runs here and writes a 3-word marker (video_core/nvn_hle_marker.h) instead of the driver's
// 4-5 words; the front replays the driver's method calls from it without parsing them. Macro
// 0xe1c's argument per stage is taken from the driver's first call on that stage. With
// EDEN_NX2_NVN_HLE_VERIFY=1 the driver writes every call and its words are compared with what
// the marker would have stood for. Counts: "h <present> <hle> <fallbacks> <checked> <mismatches>".
// EDEN_NX2_NVN_DRIVER_FILE=<path>: in pass-through the driver's entry point per name ("e <name>
// <module> <offset>") and its nvnDeviceGetInteger answers ("i <item> <value>") are written there;
// direct mode reads them back (dotfiles-25x.3).
// NVN direct mode (dotfiles-25x), chosen per boot by Install from the nvn_direct_renderer
// setting (EDEN_NX2_NVN_DIRECT=0/1 overrides it; see nvn_direct_choice.h). Every NVN name the
// game looks up resolves to the direct renderer's front end (video_core/nvn_direct) and the
// driver never runs:
// nvnBootstrapLoader is answered here, the driver's entry points from the capture are watched
// (logged if entered), names the front end lacks get a stub that answers 0. Calls that may block
// halt the JIT and run from the kernel (RunHaltedCall). Every 600 presents a line counts the
// driver entries run, the halted calls and the stubs called.

#pragma once

#include <array>

#include "common/common_types.h"
#include "core/nvn_direct_choice.h"

namespace Dynarmic::A64 {
class Jit;
enum class BlockEntryHook : std::uint8_t;
} // namespace Dynarmic::A64

namespace Core {
class System;
}

namespace Core::Memory {
class Memory;
}

namespace Common {
struct PageTable;
}

namespace Kernel {
class KProcess;
}

namespace Core::NvnHook {

[[nodiscard]] bool Enabled();

/// NVN direct mode (dotfiles-25x): the game's NVN calls run in the direct renderer
/// (video_core/nvn_direct), not in its NVN driver. Decided once per boot by Install; read by
/// the Vulkan device, created after Install.
[[nodiscard]] bool DirectMode();

/// Direct mode's title (0 otherwise): the direct renderer keeps its caches per title
/// (renderer_vulkan.cpp, DM_TEMP).
[[nodiscard]] u64 DirectProgramId();

/// Direct mode, the emulator shutting down (dotfiles-25x.18): the game's threads waiting on the
/// host inside a halted NVN call return, so the kernel can suspend them. Before the cores are
/// suspended (System::Pause, ShutdownMainProcess). Nothing otherwise.
void StopDirect();

/// Decides the boot's renderer (Choice) and, when the hook is on for it, finds the NVN driver's
/// exports in the application's modules (after they are loaded). Logs the decision.
void Install(Core::System& system, Kernel::KProcess& process);

/// Direct mode, the Vulkan renderer being made (before the game runs): the direct renderer could
/// not start on the device. The boot falls back to GPU emulation, and so does every later boot
/// in this process that asks for the direct renderer. Logged.
void DirectFailed();

/// The boot is over (the end of System::ShutdownMainProcess): its hook state goes.
void Uninstall();

/// The renderer Install chose for the current (or last) boot.
[[nodiscard]] RendererChoice Choice();

/// UserConfig::block_entry_hook (null when the hook is off).
Dynarmic::A64::BlockEntryHook (*BlockEntryHookFunction())(u64 pc);

/// InstructionCacheOperationRaised(BlockEntryHook, pc) from a core's JIT. Returns whether the
/// JIT must halt for a call that may block: it then runs from the kernel as SVC HaltedCallSvc.
[[nodiscard]] bool OnBlockEntry(u64 pc, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory,
                                Kernel::KProcess* process, u32 thread_id);

/// The SVC number a halted NVN call takes (no real SVC is above 0xffff).
constexpr u32 HaltedCallSvc = 0x10000;

/// Direct mode: runs the NVN call that halted the JIT on this host thread, from Svc::Call (the
/// kernel may wait or switch threads here). args: x0-x7, x0 the result.
void RunHaltedCall(Core::System& system, std::array<u64, 8>& args);

/// NVN2 direct mode: a guest write reached write-tracked pool memory [address, address + size)
/// (Core::Memory's rasterizer write path): the replay's textures over it are uploaded again
/// (dotfiles-25x.5).
void OnDirectCpuWrite(u64 address, u64 size);

/// The guest unmaps [address, address + size) of a page table (Core::Memory::UnmapRegion): in
/// direct mode the application's pool memory imported there is given back first, and the
/// replay's readers of guest memory wait until OnGuestUnmapDone. Returns whether the caller
/// must call OnGuestUnmapDone once the pages are unmapped.
bool OnGuestUnmap(const Common::PageTable& page_table, u64 address, u64 size);
void OnGuestUnmapDone();

/// A present (nx2:vi2), numbered from 1.
void OnPresent(u64 present);

} // namespace Core::NvnHook
