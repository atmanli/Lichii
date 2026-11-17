// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_hardware_timer.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/nx2_thread_stats.h"
#include "core/hle/kernel/svc.h"
#include "core/arm/debug.h"
#include "core/arm/symbols.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "core/nvn_direct_choice.h"
#include "core/nvn_hook.h"
#include "core/nvn_trace.h"
#include "core/hle/service/nx2_image_layout.h"
#include "core/hle/service/nx2_present.h"
#include "video_core/framebuffer_config.h"
#include "dynarmic/interface/A64/a64.h"
#include "dynarmic/interface/A64/config.h"
#include "video_core/nvn_header_count.h"
#include "video_core/nvn_hle_marker.h"
#ifdef LICHII_NVN_DIRECT
#include "video_core/nvn_direct/nvn_direct.h"
#endif

namespace Core::NvnHook {
namespace {

using Dynarmic::A64::BlockEntryHook;

constexpr u32 MaxSlots = 4096;
/// Synthetic address the proc-address lookups return to (their caller's LR is kept per thread).
constexpr u32 LookupReturnSlot = 0;
/// Synthetic address command-buffer calls return to under EDEN_NX2_NVN_EMITTED.
constexpr u32 EmittedReturnSlot = 1;
/// What the game gets for nvnDeviceGetProcAddress (not exported: games get it from
/// nvnBootstrapLoader): a lookup like nvnBootstrapLoader's.
constexpr u32 LookupSlot = 2;
/// Synthetic address calls whose return the trace wants (nvn_trace.h) return to.
constexpr u32 TraceReturnSlot = 3;
/// Synthetic address nvnDeviceGetInteger returns to when its answers are captured.
constexpr u32 DeviceIntegerReturnSlot = 4;
constexpr u32 FirstNameSlot = 5;
constexpr u32 GuestThreadSlots = 4096;

struct Slot {
    u64 driver{};
    bool command_buffer{};
    /// Direct mode: the front end's slot (-1: no implementation, the stub answers 0) and whether
    /// the call leaves the JIT first (it may block: HaltedCallSvc).
    s32 front = -1;
    bool halts{};
};

/// Direct mode: the calls that may block. They run from the kernel (RunHaltedCall), not inside
/// the JIT's callback: a kernel wait or signal there would switch fibers under a live Jit::Run.
constexpr std::array HaltingCalls{
    std::string_view{"nvnQueueSubmitCommands"}, std::string_view{"nvnQueuePresentTexture"},
    std::string_view{"nvnQueueFinish"},         std::string_view{"nvnSyncWait"},
    std::string_view{"nvnWindowAcquireTexture"}, std::string_view{"nvnQueueAcquireTexture"},
    std::string_view{"nvnMemoryPoolFinalize"},  std::string_view{"nvnTextureReadTexels"},
    std::string_view{"nvnTextureReadTexelsStrided"},
};

struct DeviceIntegerFrame {
    u64 lr;
    u32 item;
    u64 out;
};

/// One host thread's counters. Only that thread increments them, but OnPresent sums them from
/// another: relaxed atomics.
struct Counters {
    std::array<std::atomic<u64>, MaxSlots> calls{};
    std::vector<std::atomic<u64>> emitted_headers; ///< by key, EDEN_NX2_NVN_EMITTED only
    std::atomic<u64> decoded_calls{};
    std::atomic<u64> emitted_words{};
    std::atomic<u64> skipped_calls{}; ///< the write pointer moved to another chunk
    // EDEN_NX2_NVN_HLE=BindTexture
    std::atomic<u64> hle_calls{};     ///< written as a marker
    std::atomic<u64> hle_fallbacks{}; ///< left to the driver: compute/task stage, unlearned, no room
    /// EDEN_NX2_NVN_HLE_VERIFY: driver output compared with the marker's
    std::atomic<u64> verify_checked{};
    std::atomic<u64> verify_mismatches{};
};

struct LookupFrame {
    std::string name;
    u64 lr;
};

/// The words the driver's nvnCommandBufferBindTexture writes for a graphics stage, at most.
using BindTextureWords = u32[5];

struct TraceFrame {
    u64 lr;
    u64 seq;
    u32 slot;
    u64 output; ///< the guest address of the output the trace records at the return
};

struct CommandFrame {
    u64 lr;
    u64 command_buffer;
    u64 write_pointer;
    bool decode;               ///< EDEN_NX2_NVN_EMITTED, top level
    u32 learn_stage = ~0u;     ///< BindTexture: take macro 0xe1c's argument for this stage
    u32 expected_count{};      ///< BindTexture verify: the words the marker stands for
    BindTextureWords expected{};
};

constexpr u32 MaxStages = 16;
constexpr u32 ComputeStage = 5;
constexpr u32 TaskStage = 8; ///< the driver's other special case besides compute

struct State {
    ~State() {
        for (Counters* thread_counters : counters) {
            delete thread_counters;
        }
        if (out != nullptr) {
            std::fclose(out);
        }
        if (driver_file != nullptr) {
            std::fclose(driver_file);
        }
    }

    u64 boot{}; ///< which boot of the process this state is (LocalCounters)
    bool direct{}; ///< DirectMode()
    u64 program_id{}; ///< DirectProgramId()
    bool emitted{};
    bool hle_bind_texture{};
    bool hle_verify{};
    u32 bind_texture_slot = ~0u;
    /// Macro 0xe1c's argument per stage, as the driver wrote it (0 until seen; bit 31 set)
    std::array<std::atomic<u32>, MaxStages> bind_texture_argument{};
    std::optional<u64> private0_value;
    u64 every = 300;
    std::FILE* out{};
    /// EDEN_NX2_NVN_DRIVER_FILE: the NVN driver's entry points and device integers, written in
    /// pass-through and read in direct mode.
    std::string driver_file_path;
    std::FILE* driver_file{};
    std::map<u64, std::string> modules; ///< by base
    u32 device_integer_slot = ~0u;
    std::unordered_map<u32, s32> device_integers; ///< captured (pass-through) or read (direct)
    std::array<std::vector<DeviceIntegerFrame>, GuestThreadSlots> device_integer_frames;
    // Direct mode
    bool direct_ready{};                     ///< nvn_direct_init ran
    std::vector<std::pair<u64, std::string>> driver_entries; ///< hooked to prove none runs
    std::atomic<u64> driver_entry_calls{};
    std::atomic<u64> halted_calls{};
    std::optional<Service::Nx2PresentBookkeeping> present; ///< direct mode's presents
    u64 presents{};                                         ///< direct mode's presents so far

    Kernel::KProcess* process{};
    /// Lichii's guest memory (its memfd and backing views, for the replay's pool imports)
    Common::HostMemory* host_memory{};
    u64 synthetic_base{};
    /// nvnBootstrapLoader, every module's export: LM3 (Switch 1) exports it from a subsdk
    /// (nnSdkEn) and from nnSdk (dotfiles-1gi).
    std::vector<u64> bootstrap_entries;
    int sdk_major{-1}; ///< NintendoSDK major of the game's sdk (-1 unknown)
    u64 lookup_driver{};   ///< nvnDeviceGetProcAddress, as nvnBootstrapLoader returned it
    u64 private0_address{};
    u64 private0_size{};

    std::array<Slot, MaxSlots> slots{};
    std::array<std::atomic<u64>, MaxSlots> stub_calls{}; ///< direct mode
    std::atomic<u32> slot_count{FirstNameSlot};

    std::mutex mutex; ///< slots' creation, lookups, the counters' list, the output
    std::unordered_map<std::string, u32> slot_by_name;
    std::unordered_map<u32, std::vector<LookupFrame>> lookups; ///< by guest thread
    std::vector<Counters*> counters;
    /// EDEN_NX2_NVN_EMITTED: open command-buffer calls by guest thread (id modulo the size)
    std::array<std::vector<CommandFrame>, GuestThreadSlots> command_frames;
    /// EDEN_NX2_NVN_TRACE: calls whose return the trace wants, by guest thread
    std::array<std::vector<TraceFrame>, GuestThreadSlots> trace_frames;
};

bool EnvIsOne(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::strcmp(value, "1") == 0;
}

/// EDEN_NX2_NVN_DIRECT=0/1: the lab's override of the nvn_direct_renderer setting.
std::optional<bool> DirectOverride() {
    const char* const value = std::getenv("EDEN_NX2_NVN_DIRECT");
    if (value == nullptr || (std::strcmp(value, "0") != 0 && std::strcmp(value, "1") != 0)) {
        return std::nullopt;
    }
    return value[0] == '1';
}

/// Whether this boot asks for the direct renderer (it may still fall back: Install).
bool DirectAsked() {
    return DirectOverride().value_or(Settings::values.nvn_direct_renderer.GetValue());
}

/// The boot's state, from Install to Uninstall (none while the hook is off for the boot).
std::unique_ptr<State> g_state;
u64 g_boots{};
/// A boot of this process ran the direct renderer: its replay starts once per process.
bool g_direct_ran{};
/// The direct renderer failed to start on the device in this process (DirectFailed).
bool g_replay_failed{};
RendererChoice g_choice{RendererChoice::SettingOff};

std::unique_ptr<State> Create() {
    auto state = std::make_unique<State>();
    state->boot = ++g_boots;
    if (const char* path = std::getenv("EDEN_NX2_NVN_OUT")) {
        state->out = std::fopen(path, "w");
    }
    if (const char* every = std::getenv("EDEN_NX2_NVN_EVERY")) {
        state->every = std::max<u64>(std::strtoull(every, nullptr, 10), 1);
    }
    state->emitted = std::getenv("EDEN_NX2_NVN_EMITTED") != nullptr;
    VideoCommon::NvnHeaderCount::g_enabled = state->emitted;
    if (const char* hle = std::getenv("EDEN_NX2_NVN_HLE")) {
        state->hle_bind_texture = std::strstr(hle, "BindTexture") != nullptr;
    }
    state->hle_verify = std::getenv("EDEN_NX2_NVN_HLE_VERIFY") != nullptr;
    if (const char* path = std::getenv("EDEN_NX2_NVN_DRIVER_FILE")) {
        state->driver_file_path = path;
    }
    // Verify keeps the driver's words: no marker reaches the front.
    VideoCommon::NvnHleMarker::g_enabled = state->hle_bind_texture && !state->hle_verify;
    if (const char* value = std::getenv("EDEN_NX2_NVN_PRIVATE0")) {
        state->private0_value = std::strtoull(value, nullptr, 0);
    }
    return state;
}

State* Get() {
    return g_state.get();
}

Counters& LocalCounters(State& state) {
    static thread_local Counters* counters = nullptr;
    static thread_local u64 counters_boot = 0;
    if (counters == nullptr || counters_boot != state.boot) {
        counters = new Counters;
        counters_boot = state.boot;
        if (state.emitted) {
            counters->emitted_headers = std::vector<std::atomic<u64>>(1u << 16);
        }
        std::scoped_lock lock{state.mutex};
        state.counters.push_back(counters);
    }
    return *counters;
}

u64 SyntheticAddress(const State& state, u32 slot) {
    return state.synthetic_base + u64{slot} * 4;
}

bool IsBootstrap(const State& state, u64 address) {
    return std::ranges::find(state.bootstrap_entries, address) != state.bootstrap_entries.end();
}

bool IsDriverEntry(const State& state, u64 address) {
    return std::ranges::binary_search(state.driver_entries, std::pair{address, std::string{}},
                                      [](const auto& a, const auto& b) { return a.first < b.first; });
}

BlockEntryHook HookOf(u64 pc) {
    const State* const state_pointer = Get();
    if (state_pointer == nullptr) {
        return BlockEntryHook::None;
    }
    const State& state = *state_pointer;
    if (IsBootstrap(state, pc)) {
        // Direct mode: nvnBootstrapLoader is answered here and the driver's never runs.
        return state.direct ? BlockEntryHook::Thunk : BlockEntryHook::Call;
    }
    if (state.direct && IsDriverEntry(state, pc)) {
        return BlockEntryHook::Call;
    }
    if (pc >= state.synthetic_base && (pc & 3) == 0 && state.synthetic_base != 0 &&
        (pc - state.synthetic_base) / 4 < state.slot_count.load(std::memory_order_acquire)) {
        return BlockEntryHook::Thunk;
    }
    return BlockEntryHook::None;
}

/// The slot of `name`, made on first sight (under state.mutex).
u32 SlotOf(State& state, const std::string& name, u64 driver) {
    if (const auto it = state.slot_by_name.find(name); it != state.slot_by_name.end()) {
        if (state.slots[it->second].driver != driver) {
            LOG_WARNING(Core, "NVN hook: {} resolved to {:#x}, earlier {:#x}", name, driver,
                        state.slots[it->second].driver);
        }
        return it->second;
    }
    const u32 slot = state.slot_count.load(std::memory_order_relaxed);
    if (slot == MaxSlots) {
        return MaxSlots;
    }
    state.slots[slot] = {.driver = driver,
                         .command_buffer = name.starts_with("nvnCommandBuffer")};
    state.slot_by_name.emplace(name, slot);
    if (name == "nvnCommandBufferBindTexture") {
        state.bind_texture_slot = slot;
    }
    if (name == "nvnDeviceGetInteger") {
        state.device_integer_slot = slot;
    }
    state.slot_count.store(slot + 1, std::memory_order_release);
    if (state.driver_file != nullptr && driver != 0) {
        // The entry as module + offset: the driver's addresses in a later run of the same game.
        const auto module = state.modules.upper_bound(driver);
        if (module != state.modules.begin()) {
            const auto& [base, module_name] = *std::prev(module);
            fmt::print(state.driver_file, "e {} {} {:#x}\n", name, module_name, driver - base);
            std::fflush(state.driver_file);
        }
    }
    NvnTrace::OnName(slot, name.c_str());
    if (state.out != nullptr) {
        fmt::print(state.out, "n {} {:#x} {:#x} {}\n", slot, driver, SyntheticAddress(state, slot),
                   name);
        std::fflush(state.out);
    }
    return slot;
}

void WritePrivate0(const State& state, Core::Memory::Memory& memory) {
    if (!state.private0_value || state.private0_address == 0) {
        return;
    }
    const u64 value = *state.private0_value;
    switch (state.private0_size) {
    case 1:
        memory.Write8(state.private0_address, static_cast<u8>(value));
        break;
    case 2:
        memory.Write16(state.private0_address, static_cast<u16>(value));
        break;
    case 8:
        memory.Write64(state.private0_address, value);
        break;
    default:
        memory.Write32(state.private0_address, static_cast<u32>(value));
        break;
    }
}

u64 ReadPrivate0(const State& state, Core::Memory::Memory& memory) {
    switch (state.private0_size) {
    case 1:
        return memory.Read8(state.private0_address);
    case 2:
        return memory.Read16(state.private0_address);
    case 8:
        return memory.Read64(state.private0_address);
    default:
        return memory.Read32(state.private0_address);
    }
}

/// The NintendoSDK major [begin, end) was built with, from its version string
/// ("NintendoSdk_nnSdk-7_3_2-Release", a variant name such as "Generic_" may precede the version):
/// -1 when it has none. Read as upstream's loader.c dm_sdk_major does, but a prerelease SDK 0.x
/// (Breath of the Wild 1.6.0: "nnSdk-0_12_14") is 0, not unknown.
int SdkMajor(Core::Memory::Memory& memory, u64 begin, u64 end) {
    constexpr std::string_view Tag{"_nnSdk-"};
    constexpr std::string_view Sdk{"nintendosdk"};
    std::string bytes;
    for (u64 address = begin; address < end; address += Core::Memory::LICHII_PAGESIZE) {
        const u8* const page = memory.GetPointerSilent(Common::ProcessAddress{address});
        if (page == nullptr) {
            break;
        }
        bytes.append(reinterpret_cast<const char*>(page), Core::Memory::LICHII_PAGESIZE);
    }
    for (size_t at = bytes.find(Tag); at != std::string::npos; at = bytes.find(Tag, at + 1)) {
        if (at < Sdk.size() ||
            !std::ranges::equal(std::string_view{bytes}.substr(at - Sdk.size(), Sdk.size()), Sdk,
                                [](char a, char b) { return std::tolower(a) == b; })) {
            continue;
        }
        size_t version = at + Tag.size();
        while (version < bytes.size() && std::isalpha(static_cast<unsigned char>(bytes[version]))) {
            ++version;
        }
        if (version > at + Tag.size() && version < bytes.size() && bytes[version] == '_') {
            ++version;
        }
        if (version < bytes.size() && std::isdigit(static_cast<unsigned char>(bytes[version]))) {
            return std::atoi(bytes.c_str() + version);
        }
    }
    return -1;
}

bool ReadGuest(Core::Memory::Memory& memory, u64 address, void* out, size_t size) {
    auto* bytes = static_cast<u8*>(out);
    while (size != 0) {
        const size_t chunk = std::min<size_t>(size, 0x1000 - (address & 0xfff));
        const u8* const pointer = memory.GetPointerSilent(Common::ProcessAddress{address});
        if (pointer == nullptr) {
            return false;
        }
        std::memcpy(bytes, pointer, chunk);
        bytes += chunk;
        address += chunk;
        size -= chunk;
    }
    return true;
}

/// The method headers a top-level command-buffer call wrote between `begin` and `end`.
void DecodeEmitted(Counters& counters, Core::Memory::Memory& memory, u64 begin, u64 end) {
    if (begin == 0 || end < begin || end - begin > 0x10000) {
        counters.skipped_calls.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    counters.decoded_calls.fetch_add(1, std::memory_order_relaxed);
    const size_t count = static_cast<size_t>((end - begin) / 4);
    counters.emitted_words.fetch_add(count, std::memory_order_relaxed);
    if (count == 0) {
        return;
    }
    static thread_local std::vector<u32> words;
    words.resize(count);
    if (!ReadGuest(memory, begin, words.data(), count * 4)) {
        return;
    }
    for (size_t i = 0; i < count;) {
        const u32 header = words[i];
        const u32 mode = header >> 29;
        u32 data;
        if (mode == 1 || mode == 3 || mode == 5) {
            data = (header >> 16) & 0x1fff;
        } else if (mode == 4) {
            data = 0;
        } else {
            break;
        }
        // The header's method field is in words; subchannel bits 13-15.
        const u32 key = (((header >> 13) & 7) << 13) | (header & 0x1fff);
        counters.emitted_headers[key].fetch_add(1, std::memory_order_relaxed);
        VideoCommon::NvnHeaderCount::NoteWritten(
            memory.GetPointerSilent(Common::ProcessAddress{begin + i * 4}), header);
        i += 1 + data;
    }
}

/// nvnCommandBufferBindTexture(cmdbuf x0, stage w1, slot w2, handle x3). Returns whether the
/// call was done here (a marker written, see nvn_hle_marker.h); otherwise the driver runs it,
/// with `frame` (when set up) checked at its return.
bool HleBindTexture(State& state, Counters& counters, Dynarmic::A64::Jit& jit,
                    Core::Memory::Memory& memory, CommandFrame& frame, bool& want_return) {
    const u64 command_buffer = jit.GetRegister(0);
    const u32 stage = static_cast<u32>(jit.GetRegister(1));
    const u32 slot = static_cast<u32>(jit.GetRegister(2));
    const u32 handle = static_cast<u32>(jit.GetRegister(3));
    if (stage == ComputeStage || stage == TaskStage || stage >= MaxStages || slot > 0xff) {
        counters.hle_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const u32 learned = state.bind_texture_argument[stage].load(std::memory_order_relaxed);
    if (learned == 0) {
        frame.learn_stage = stage;
        want_return = true;
        counters.hle_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    namespace Marker = VideoCommon::NvnHleMarker;
    // pipe_nop: the driver's IMMD 0x68b, written when cmdbuf+0x13 bit 3 is set (sdk BindTexture)
    const bool pipe_nop = (memory.Read8(command_buffer + 0x13) & 8) != 0;
    const u32 word0 = Marker::BindTextureWord0(slot, stage, pipe_nop, learned);
    if (state.hle_verify) {
        // What the front would replay from this very marker, against the driver's words.
        frame.expected_count = Marker::ExpandBindTexture(word0, handle, frame.expected);
        want_return = true;
        return false;
    }
    const u64 write_pointer = memory.Read64(command_buffer);
    const u64 end = memory.Read64(command_buffer + 8);
    if (write_pointer == 0 || write_pointer + 12 > end) {
        // the driver asks the game for more memory
        counters.hle_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const std::array<u32, 3> words{Marker::Header(Marker::BindTexture, 2), word0, handle};
    memory.WriteBlock(write_pointer, words.data(), sizeof(words));
    memory.Write64(command_buffer, write_pointer + sizeof(words));
    counters.hle_calls.fetch_add(1, std::memory_order_relaxed);
    return true;
}

/// At a BindTexture's return: learn the stage's macro argument, or compare with the marker.
void CheckBindTexture(State& state, Counters& counters, Core::Memory::Memory& memory,
                      const CommandFrame& frame) {
    const u64 end = memory.Read64(frame.command_buffer);
    if (frame.write_pointer == 0 || end <= frame.write_pointer ||
        end - frame.write_pointer > 5 * 4) {
        return; // another chunk (the driver asked for memory first): not comparable
    }
    u32 written[5]{};
    const u32 count = static_cast<u32>((end - frame.write_pointer) / 4);
    ReadGuest(memory, frame.write_pointer, written, count * 4);
    if (frame.learn_stage != ~0u) {
        if ((written[0] & 0xe000ffffu) == 0x80000e1cu) {
            state.bind_texture_argument[frame.learn_stage].store(
                0x80000000u | ((written[0] >> 16) & 0x1fff), std::memory_order_relaxed);
        }
        return;
    }
    counters.verify_checked.fetch_add(1, std::memory_order_relaxed);
    if (count != frame.expected_count ||
        !std::equal(written, written + count, frame.expected)) {
        if (counters.verify_mismatches.fetch_add(1, std::memory_order_relaxed) < 16) {
            LOG_ERROR(Core, "NVN HLE verify: BindTexture wrote {} words {:08x} {:08x} {:08x} {:08x} "
                            "{:08x}, the marker stands for {} words {:08x} {:08x} {:08x} {:08x} {:08x}",
                      count, written[0], written[1], written[2], written[3], written[4],
                      frame.expected_count, frame.expected[0], frame.expected[1],
                      frame.expected[2], frame.expected[3], frame.expected[4]);
        }
    }
}

#ifdef LICHII_NVN_DIRECT
/// The answers of NVN2's own nvnDeviceGetInteger that are constants in its code, given to the front
/// end as device integers (dm_sdk_nvn2_item), read from the module at `base` that holds the driver
/// as upstream's loader.c nvn2_find_switch reads them. Its API and GPU code versions move with the
/// SDK and games check them: Ocarina of Time (SDK 22.2.8) requires API 231.40 (main+0x346c118)
/// and otherwise makes no graphics system. The driver's switch over the items loads w0 with the
/// major first, then jumps through a byte table: mov w0, #231 / adr (the cases) / adrp + add (the
/// table) / ldrb / br, with cmp wN, #<last item> before it; a constant case is mov w0, #v; b.
/// Returns how many items it found.
u32 ReadNvn2Constants(Kernel::KProcess& process, u64 base) {
    Kernel::KMemoryInfo info{};
    Kernel::Svc::PageInfo page{};
    if (process.GetPageTable()
            .QueryInfo(std::addressof(info), std::addressof(page), base)
            .IsError()) {
        return 0;
    }
    const u64 text_end = info.GetSvcMemoryInfo().base_address + info.GetSvcMemoryInfo().size;
    const u64 end = GetInteger(Core::GetModuleEnd(&process, base)) + 1;
    std::vector<u32> words((end - base) / 4);
    if (text_end <= base || !ReadGuest(process.GetMemory(), base, words.data(), words.size() * 4)) {
        return 0;
    }
    const auto* const bytes = reinterpret_cast<const u8*>(words.data());
    const s64 text_size = static_cast<s64>(text_end - base);
    const s64 size = static_cast<s64>(words.size() * 4);
    // adr and adrp's signed 21-bit immediate
    const auto immediate = [](u32 word) {
        const s64 value = ((word >> 3) & 0x1ffffc) | ((word >> 29) & 3);
        return (value & 0x100000) != 0 ? value - 0x200000 : value;
    };
    for (s64 p = 8; p + 9 < text_size / 4; ++p) {
        if (words[p] != 0x52801ce0) { // mov w0, #231
            continue;
        }
        s64 table = -1, cases = -1, last = -1;
        bool ldrb = false, br = false;
        for (s64 q = p - 8; q <= p + 8; ++q) {
            const u32 word = words[q];
            if ((word & 0xffc0001f) == 0x7100001f) { // cmp wN, #imm
                last = (word >> 10) & 0xfff;
            } else if ((word & 0x9f000000) == 0x90000000 &&
                       (words[q + 1] & 0xffc00000) == 0x91000000) { // adrp + add
                table = ((q * 4) & ~s64{0xfff}) + immediate(word) * 4096 +
                        ((words[q + 1] >> 10) & 0xfff);
            } else if ((word & 0x9f000000) == 0x10000000) { // adr
                cases = q * 4 + immediate(word);
            } else if ((word & 0xffe0fc00) == 0x38606800) { // ldrb wK, [xT, xI]
                ldrb = true;
            } else if ((word & 0xfffffc1f) == 0xd61f0000) { // br
                br = true;
            }
        }
        if (last <= 1 || !ldrb || !br || table < text_size || table + last >= size || cases < 0 ||
            cases % 4 != 0 || cases + 255 * 4 + 8 > text_size) {
            continue;
        }
        u32 found = 0;
        for (s64 item = 0; item <= last; ++item) {
            const s64 at = cases / 4 + bytes[table + item];
            if ((words[at] & 0xffe0001f) == 0x52800000 &&     // mov w0, #imm16
                (words[at + 1] & 0xfc000000) == 0x14000000) { // b
                nvn_direct_set_device_integer(static_cast<unsigned>(item),
                                              static_cast<int>((words[at] >> 5) & 0xffff));
                ++found;
            }
        }
        return found;
    }
    return 0;
}
#endif

/// Direct mode, at Install: a slot per name the front end knows (upstream's names.txt), the
/// driver's entry points from a pass-through run's capture, hooked only to count, and its
/// device integers (the driver code's constants, then the capture's answers).
void InstallDirect(State& state) {
#ifdef LICHII_NVN_DIRECT
    for (int front = 0; front < nvn_direct_name_count(); ++front) {
        const std::string name = nvn_direct_name(front);
        const u32 slot = state.slot_count.load(std::memory_order_relaxed);
        state.slots[slot] = {.command_buffer = name.starts_with("nvnCommandBuffer"), .front = front,
                             .halts = std::ranges::find(HaltingCalls, name) != HaltingCalls.end()};
        if (state.slots[slot].halts && nvn_direct_float_args(front)) {
            LOG_CRITICAL(Core, "NVN direct: {} takes floats, which a halted call loses", name);
        }
        state.slot_by_name.emplace(name, slot);
        state.slot_count.store(slot + 1, std::memory_order_release);
    }
    // The game's inline NVN encoding stays off: its commands come through the table (nvnPrivate0
    // is 1 only once the driver's lookup ran, which it never does now; written at each lookup).
    state.private0_value = state.private0_value.value_or(0);
    // NVN2 is compiled into the module that exports nvnBootstrapLoader.
    u32 constants = 0;
    if (state.process->IsSwitch2()) {
        for (const u64 entry : state.bootstrap_entries) {
            constants = ReadNvn2Constants(*state.process,
                                          std::prev(state.modules.upper_bound(entry))->first);
            if (constants != 0) {
                break;
            }
        }
        if (constants == 0) {
            LOG_WARNING(Core, "NVN direct: NVN2's nvnDeviceGetInteger switch is not in the game's "
                              "driver: the device answers the newest versions known");
        }
    }
    std::FILE* file = state.driver_file_path.empty()
                          ? nullptr
                          : std::fopen(state.driver_file_path.c_str(), "r");
    if (file == nullptr) {
        LOG_WARNING(Core, "NVN direct: no driver capture (EDEN_NX2_NVN_DRIVER_FILE): driver entries "
                          "are not watched, device integers are the front end's own");
    } else {
        char line[512];
        while (std::fgets(line, sizeof line, file) != nullptr) {
            char name[256], module[64];
            unsigned long long offset{};
            unsigned item{};
            int value{};
            if (std::sscanf(line, "e %255s %63s %llx", name, module, &offset) == 3) {
                for (const auto& [base, module_name] : state.modules) {
                    if (module_name == module) {
                        state.driver_entries.emplace_back(base + offset, name);
                    }
                }
            } else if (std::sscanf(line, "i %u %d", &item, &value) == 2) {
                state.device_integers[item] = value;
                nvn_direct_set_device_integer(item, value);
            }
        }
        std::fclose(file);
        std::ranges::sort(state.driver_entries);
    }
    LOG_INFO(Core,
             "NVN direct: the game's NVN driver is replaced: {} names in the front end, {} driver "
             "entries watched, {} device integers from the driver, {} from its code",
             state.slot_count.load() - FirstNameSlot, state.driver_entries.size(),
             state.device_integers.size(), constants);
#endif
}

#ifdef LICHII_NVN_DIRECT
/// Direct mode: the slot answering `name` (a stub slot for a name the front end lacks).
u32 DirectSlotOf(State& state, const std::string& name) {
    if (name == "nvnDeviceGetProcAddress") {
        return LookupSlot;
    }
    if (const auto it = state.slot_by_name.find(name); it != state.slot_by_name.end()) {
        return it->second;
    }
    const u32 slot = state.slot_count.load(std::memory_order_relaxed);
    if (slot == MaxSlots) {
        return MaxSlots;
    }
    state.slots[slot] = {.command_buffer = name.starts_with("nvnCommandBuffer")};
    state.slot_by_name.emplace(name, slot);
    state.slot_count.store(slot + 1, std::memory_order_release);
    LOG_WARNING(Core, "NVN direct: the game asks for {}, which the front end lacks: a stub", name);
    return slot;
}

/// nvn_direct_memory.map_view (dotfiles-25x.5): the pages behind guest [address, address + size)
/// mapped once more at a range of their own, for the replay to import: each run of pages that lie
/// one after the other in the backing memory is one mapping of it (HostMemory::MapBackingView);
/// pages the guest has not mapped are zero pages of the view.
void* MapPoolView(void* context, u64 address, u64 size, u64* unmapped) {
    State& state = *static_cast<State*>(context);
    auto& memory = state.process->GetMemory();
    const u8* const backing_base = state.host_memory->BackingBasePointer();
    constexpr u64 PageSize = Core::Memory::LICHII_PAGESIZE;
    std::vector<Common::HostMemory::BackingRun> runs;
    for (u64 offset = 0; offset < size; offset += PageSize) {
        const u8* const backing = memory.GetPointerSilent(address + offset);
        if (backing == nullptr) {
            *unmapped += PageSize;
            continue;
        }
        if (backing < backing_base ||
            static_cast<size_t>(backing - backing_base) >= state.host_memory->BackingSize()) {
            LOG_ERROR(Core, "NVN direct: view of guest {:#x}+{:#x}: page {:#x} is outside the "
                      "backing memory", address, size, address + offset);
            return nullptr;
        }
        const size_t host_offset = static_cast<size_t>(backing - backing_base);
        if (!runs.empty() && runs.back().view_offset + runs.back().length == offset &&
            runs.back().host_offset + runs.back().length == host_offset) {
            runs.back().length += PageSize;
            continue;
        }
        runs.push_back({.view_offset = offset, .host_offset = host_offset, .length = PageSize});
    }
    u8* const view = state.host_memory->MapBackingView(size, runs);
    if (view == nullptr) {
        LOG_ERROR(Core, "NVN direct: no view of guest {:#x}+{:#x} ({} runs)", address, size,
                  runs.size());
    }
    return view;
}

/// nvn_direct_memory.unmap_view: takes down a view MapPoolView made.
void UnmapPoolView(void* context, void* view, u64 size) {
    static_cast<State*>(context)->host_memory->UnmapBackingView(static_cast<u8*>(view), size);
}

/// nvn_direct_memory.track_pages: arms or disarms the write tracking of pool pages by guest
/// address: rasterizer-cached pages, which stay readable in direct mode (Core::Memory); a write
/// to one reaches OnDirectCpuWrite. The pools stay out of the SMMU: with them mapped there MKW
/// stalls (dotfiles-25x.5).
void TrackPages(void* context, u64 address, u64 size, int armed) {
    static_cast<State*>(context)->process->GetMemory().RasterizerMarkRegionCached(address, size,
                                                                                  armed != 0);
}

/// nvn_direct_memory.backing: the backing memory of a guest page (never write-protected).
void* Backing(void* context, u64 address) {
    State& state = *static_cast<State*>(context);
    return state.process->GetMemory().GetPointerSilent(address);
}

#ifdef _WIN32
/// nvn_direct_memory's alias calls: HostMemory's alias regions.
void* AliasReserve(void* context, u64 size) {
    return static_cast<State*>(context)->host_memory->ReserveAliasRegion(size);
}
int AliasMap(void* context, void* address, u64 backing_offset, u64 size) {
    return static_cast<State*>(context)->host_memory->MapAlias(static_cast<u8*>(address),
                                                               backing_offset, size);
}
void AliasUnmap(void* context, void* address, u64 size) {
    static_cast<State*>(context)->host_memory->UnmapAlias(static_cast<u8*>(address), size);
}
void AliasRelease(void* context, void* region) {
    static_cast<State*>(context)->host_memory->ReleaseAliasRegion(static_cast<u8*>(region));
}
#endif

/// The guest's x0-x7 and the low halves of v0-v7.
void ReadArguments(Dynarmic::A64::Jit& jit, std::array<u64, 8>& x, std::array<u64, 8>& v) {
    for (size_t i = 0; i < 8; ++i) {
        x[i] = jit.GetRegister(i);
        v[i] = jit.GetVector(i)[0];
    }
}

/// The slot of a call that halted the JIT (HaltedCallSvc), on the host thread that runs it: the
/// kernel runs it (RunHaltedCall) right after the JIT returns, on the same host thread.
thread_local u32 t_halted_slot = MaxSlots;
thread_local u64 t_halted_lr = 0;

/// Direct mode's block entries. Returns whether the JIT halts for the call (HaltedCallSvc).
bool DirectEntry(State& state, u64 pc, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory) {
    const u64 lr = jit.GetRegister(30);
    if (IsDriverEntry(state, pc)) {
        const u64 calls = state.driver_entry_calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (calls <= 8) {
            const auto it = std::ranges::lower_bound(state.driver_entries,
                                                     std::pair{pc, std::string{}});
            LOG_CRITICAL(Core, "NVN direct: the game's NVN driver ran: {} at {:#x} (from {:#x})",
                         it != state.driver_entries.end() ? it->second : "?", pc, lr);
        }
        return false;
    }
    const bool bootstrap = IsBootstrap(state, pc);
    const u32 slot = bootstrap ? MaxSlots : static_cast<u32>((pc - state.synthetic_base) / 4);
    if (bootstrap || slot == LookupSlot) {
        // nvnBootstrapLoader(name) or nvnDeviceGetProcAddress(device, name): the name's
        // synthetic address, without the driver.
        const std::string name = memory.ReadCString(jit.GetRegister(bootstrap ? 0 : 1), 128);
        std::scoped_lock lock{state.mutex};
        if (!state.direct_ready) {
            auto& page_table = state.process->GetPageTable();
            const nvn_direct_memory views{.map_view = MapPoolView,
                                          .unmap_view = UnmapPoolView,
                                          .track_pages = TrackPages,
                                          .backing = Backing,
                                          .context = &state,
                                          .backing_fd = state.host_memory->BackingFd(),
                                          .backing_base = state.host_memory->BackingBasePointer(),
#ifdef _WIN32
                                          .alias_reserve = AliasReserve,
                                          .alias_map = AliasMap,
                                          .alias_unmap = AliasUnmap,
                                          .alias_release = AliasRelease,
#endif
            };
            nvn_direct_init(page_table.GetImpl().fastmem_arena,
                            u64{1} << page_table.GetAddressSpaceWidth(), &views,
                            state.process->IsSwitch2(), state.sdk_major);
            state.direct_ready = true;
        }
        const u32 found = DirectSlotOf(state, name);
        jit.SetRegister(0, found == MaxSlots ? 0 : SyntheticAddress(state, found));
        WritePrivate0(state, memory);
        jit.SetRegister(16, lr);
        return false;
    }
    LocalCounters(state).calls[slot].fetch_add(1, std::memory_order_relaxed);
    const Slot& target = state.slots[slot];
    jit.SetRegister(16, lr);
    if (target.front < 0 || !nvn_direct_implemented(target.front)) {
        if (state.stub_calls[slot].fetch_add(1, std::memory_order_relaxed) == 0) {
            std::scoped_lock lock{state.mutex};
            for (const auto& [name, index] : state.slot_by_name) {
                if (index == slot) {
                    LOG_WARNING(Core, "NVN direct: {} has no implementation: answered 0", name);
                }
            }
        }
        jit.SetRegister(0, 0);
        return false;
    }
    if (target.halts) {
        t_halted_slot = slot;
        t_halted_lr = lr;
        return true;
    }
    std::array<u64, 8> x, v;
    ReadArguments(jit, x, v);
    u64 v0 = 0;
    jit.SetRegister(0, nvn_direct_call(target.front, x.data(), v.data(), jit.GetSP(), lr, &v0));
    if (nvn_direct_float_args(target.front)) {
        jit.SetVector(0, {v0, 0});
    }
    return false;
}
#endif

} // namespace

bool Enabled() {
    return Get() != nullptr;
}

bool DirectMode() {
    const State* const state = Get();
    return state != nullptr && state->direct;
}

u64 DirectProgramId() {
    const State* const state = Get();
    return state != nullptr && state->direct ? state->program_id : 0;
}

void StopDirect() {
#ifdef LICHII_NVN_DIRECT
    if (DirectMode()) {
        nvn_direct_stop();
    }
#endif
}

namespace {

/// The NVN driver's exports in the process's modules, the game's SDK and the synthetic
/// addresses' range.
void ScanModules(Core::System& system, Kernel::KProcess& process, State& state) {
    auto& memory = process.GetMemory();
    const auto modules = Core::FindModules(&process);
    for (const auto& [base, module] : modules) {
        state.modules.emplace(base, module);
        const auto symbols = Core::Symbols::GetSymbols(base, memory, true);
        const auto find = [&](std::string_view name) -> std::pair<u64, u64> {
            const auto it = symbols.find(name);
            if (it == symbols.end() || it->second.first == 0) {
                return {0, 0};
            }
            return {base + it->second.first, it->second.second};
        };
        if (const auto [address, size] = find("nvnBootstrapLoader"); address != 0) {
            state.bootstrap_entries.push_back(address);
            if (state.sdk_major < 0) {
                state.sdk_major =
                    SdkMajor(memory, base, GetInteger(Core::GetModuleEnd(&process, base)));
            }
            LOG_INFO(Core, "NVN hook: nvnBootstrapLoader in module {} at {:#x}: {:#x}", module,
                     base, address);
        }
        if (const auto [address, size] = find("nvnPrivate0"); address != 0) {
            state.private0_address = address;
            state.private0_size = size;
        }
    }
    // Older SDKs ship the driver as a module of its own (Breath of the Wild 1.6.0: "nvn", which
    // exports nvnBootstrapLoader); the version string is then in nnSdk's module.
    for (auto it = modules.begin(); state.sdk_major < 0 && !state.bootstrap_entries.empty() &&
                                    it != modules.end();
         ++it) {
        state.sdk_major =
            SdkMajor(memory, it->first, GetInteger(Core::GetModuleEnd(&process, it->first)));
    }
    // The synthetic addresses: in the first unmapped range from 64 KiB that holds every slot.
    // Nothing is ever mapped executable there, and the JIT reads no guest code for them.
    auto& page_table = process.GetPageTable();
    for (u64 address = 0x10000;;) {
        Kernel::KMemoryInfo info{};
        Kernel::Svc::PageInfo page{};
        if (page_table.QueryInfo(std::addressof(info), std::addressof(page), address).IsError()) {
            break;
        }
        const auto svc = info.GetSvcMemoryInfo();
        const u64 end = svc.base_address + svc.size;
        if (svc.state == Kernel::Svc::MemoryState::Free && end - address >= u64{MaxSlots} * 4) {
            state.synthetic_base = address;
            break;
        }
        if (end <= address) {
            break;
        }
        address = end;
    }
    state.process = &process;
    state.host_memory = &system.DeviceMemory().buffer;
    state.program_id = process.GetProgramId();
}

} // namespace

void Install(Core::System& system, Kernel::KProcess& process) {
#ifdef LICHII_NVN_DIRECT
    constexpr bool built = true;
#else
    constexpr bool built = false;
#endif
    g_state.reset();
    const bool hook = EnvIsOne("EDEN_NX2_NVN_HOOK");
    const bool asked = DirectAsked();
    // The hook's block entries are A64 only (ArmDynarmic64): a 32-bit game gets no state.
    std::unique_ptr<State> state_owner =
        (hook || (built && asked)) && process.Is64Bit() ? Create() : nullptr;
    if (state_owner != nullptr) {
        ScanModules(system, process, *state_owner);
    }
    g_choice = ChooseRenderer({
        .built = built,
        .override_ = DirectOverride(),
        .setting = Settings::values.nvn_direct_renderer.GetValue(),
        .vulkan = Settings::values.renderer_backend.GetValue() == Settings::RendererBackend::Vulkan,
        .is_64bit = process.Is64Bit(),
        .has_nvn = state_owner != nullptr && !state_owner->bootstrap_entries.empty(),
        .direct_ran_before = g_direct_ran,
        .replay_failed = g_replay_failed,
    });
    if (built || IsFallback(g_choice)) {
        LOG_INFO(Core, "Renderer: {} ({})",
                 IsDirect(g_choice) ? "NVN direct renderer" : "GPU emulation",
                 Describe(g_choice));
    }
    if (state_owner == nullptr || (!hook && !IsDirect(g_choice))) {
        return;
    }
    State& state = *state_owner;
    state.direct = IsDirect(g_choice);
    g_direct_ran |= state.direct;
    if (state.direct) {
        InstallDirect(state);
    } else if (!state.driver_file_path.empty()) {
        state.driver_file = std::fopen(state.driver_file_path.c_str(), "w");
    }
    NvnTrace::Install(process);
    LOG_INFO(Core,
             "NVN hook: {} nvnBootstrapLoader exports (SDK {}), nvnPrivate0 {:#x} ({} bytes), "
             "synthetic addresses from {:#x}",
             state.bootstrap_entries.size(), state.sdk_major, state.private0_address,
             state.private0_size, state.synthetic_base);
    g_state = std::move(state_owner);
}

void DirectFailed() {
    g_replay_failed = true;
    g_choice = RendererChoice::ReplayFailed;
    LOG_WARNING(Core, "Renderer: GPU emulation ({})", Describe(g_choice));
    // The game has not run yet: without a state its NVN driver runs, on GPU emulation.
    g_state.reset();
}

void Uninstall() {
    g_state.reset();
}

RendererChoice Choice() {
    return g_choice;
}

Dynarmic::A64::BlockEntryHook (*BlockEntryHookFunction())(u64 pc) {
    // Asked for when the process's JIT is made, before Install decides: HookOf answers nothing
    // while the boot has no state.
    return EnvIsOne("EDEN_NX2_NVN_HOOK") || DirectAsked() ? &HookOf : nullptr;
}

bool OnBlockEntry(u64 pc, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory,
                  Kernel::KProcess* process, u32 thread_id) {
    State& state = *Get();
    if (process != state.process) {
        return false;
    }
#ifdef LICHII_NVN_DIRECT
    if (state.direct) {
        return DirectEntry(state, pc, jit, memory);
    }
#endif
    const u32 slot = IsBootstrap(state, pc)
                         ? MaxSlots
                         : static_cast<u32>((pc - state.synthetic_base) / 4);
    if (slot == LookupSlot) {
        LocalCounters(state).calls[LookupSlot].fetch_add(1, std::memory_order_relaxed);
    }
    if (slot == MaxSlots || slot == LookupSlot) {
        // nvnBootstrapLoader(name) or nvnDeviceGetProcAddress(device, name): return through
        // LookupReturnSlot, which swaps the driver's address for the name's synthetic one.
        const u64 name_address = jit.GetRegister(slot == LookupSlot ? 1 : 0);
        std::scoped_lock lock{state.mutex};
        state.lookups[thread_id].push_back(
            {.name = memory.ReadCString(name_address, 128), .lr = jit.GetRegister(30)});
        jit.SetRegister(30, SyntheticAddress(state, LookupReturnSlot));
        if (slot == LookupSlot) {
            jit.SetRegister(16, state.lookup_driver);
        }
        return false;
    }
    if (slot == LookupReturnSlot) {
        std::scoped_lock lock{state.mutex};
        auto& frames = state.lookups[thread_id];
        if (frames.empty()) {
            LOG_CRITICAL(Core, "NVN hook: lookup return without a lookup on thread {}", thread_id);
            return false;
        }
        const LookupFrame frame = std::move(frames.back());
        frames.pop_back();
        const u64 driver = jit.GetRegister(0);
        // A loader export that forwards to another hooked one returns its synthetic address.
        const bool synthetic =
            driver >= state.synthetic_base &&
            (driver - state.synthetic_base) / 4 < state.slot_count.load(std::memory_order_relaxed);
        if (!synthetic && frame.name == "nvnDeviceGetProcAddress" && driver != 0) {
            if (state.lookup_driver != 0 && state.lookup_driver != driver) {
                LOG_WARNING(Core, "NVN hook: a second nvnDeviceGetProcAddress {:#x}", driver);
            }
            if (state.lookup_driver == 0 && state.out != nullptr) {
                fmt::print(state.out, "n {} {:#x} {:#x} {}\n", LookupSlot, driver,
                           SyntheticAddress(state, LookupSlot), frame.name);
            }
            state.lookup_driver = driver;
            jit.SetRegister(0, SyntheticAddress(state, LookupSlot));
        } else if (!synthetic && driver != 0 && !IsBootstrap(state, driver)) {
            const u32 name_slot = SlotOf(state, frame.name, driver);
            if (name_slot != MaxSlots) {
                jit.SetRegister(0, SyntheticAddress(state, name_slot));
            }
        }
        WritePrivate0(state, memory);
        jit.SetRegister(16, frame.lr);
        return false;
    }
    if (slot == TraceReturnSlot) {
        auto& frames = state.trace_frames[thread_id % GuestThreadSlots];
        if (frames.empty()) {
            LOG_CRITICAL(Core, "NVN hook: trace return without a call on thread {}", thread_id);
            return false;
        }
        const TraceFrame frame = frames.back();
        frames.pop_back();
        NvnTrace::OnReturn(frame.seq, frame.slot, frame.output, jit, memory);
        jit.SetRegister(16, frame.lr);
        return false;
    }
    if (slot == DeviceIntegerReturnSlot) {
        auto& frames = state.device_integer_frames[thread_id % GuestThreadSlots];
        if (frames.empty()) {
            LOG_CRITICAL(Core, "NVN hook: GetInteger return without a call on thread {}", thread_id);
            return false;
        }
        const DeviceIntegerFrame frame = frames.back();
        frames.pop_back();
        const s32 value = static_cast<s32>(memory.Read32(frame.out));
        std::scoped_lock lock{state.mutex};
        if (state.device_integers.emplace(frame.item, value).second) {
            fmt::print(state.driver_file, "i {} {}\n", frame.item, value);
            std::fflush(state.driver_file);
        }
        jit.SetRegister(16, frame.lr);
        return false;
    }
    if (slot == EmittedReturnSlot) {
        auto& frames = state.command_frames[thread_id % GuestThreadSlots];
        if (frames.empty()) {
            LOG_CRITICAL(Core, "NVN hook: command return without a call on thread {}", thread_id);
            return false;
        }
        const CommandFrame frame = frames.back();
        frames.pop_back();
        Counters& counters = LocalCounters(state);
        if (frame.decode && frames.empty()) {
            DecodeEmitted(counters, memory, frame.write_pointer, memory.Read64(frame.command_buffer));
        }
        if (frame.learn_stage != ~0u || frame.expected_count != 0) {
            CheckBindTexture(state, counters, memory, frame);
        }
        jit.SetRegister(16, frame.lr);
        return false;
    }
    Counters& counters = LocalCounters(state);
    counters.calls[slot].fetch_add(1, std::memory_order_relaxed);
    const Slot& target = state.slots[slot];
    CommandFrame frame{.lr = jit.GetRegister(30), .decode = state.emitted && target.command_buffer};
    bool want_return = frame.decode;
    if (slot == state.bind_texture_slot && state.hle_bind_texture &&
        HleBindTexture(state, counters, jit, memory, frame, want_return)) {
        jit.SetRegister(16, frame.lr); // done: back to the caller
        return false;
    }
    if (slot == state.device_integer_slot && state.driver_file != nullptr) {
        // nvnDeviceGetInteger(device, item, int* out): the answer at the return (pass-through
        // runs only; the trace misses these calls then).
        state.device_integer_frames[thread_id % GuestThreadSlots].push_back(
            {.lr = jit.GetRegister(30),
             .item = static_cast<u32>(jit.GetRegister(1)),
             .out = jit.GetRegister(2)});
        jit.SetRegister(30, SyntheticAddress(state, DeviceIntegerReturnSlot));
        jit.SetRegister(16, target.driver);
        return false;
    }
    if (u64 seq = 0; NvnTrace::OnCall(slot, jit, memory, thread_id, seq)) {
        // The trace's return frame stands alone: a trace run sets neither EDEN_NX2_NVN_EMITTED
        // nor EDEN_NX2_NVN_HLE.
        const u32 output = NvnTrace::OutputArgument(slot);
        state.trace_frames[thread_id % GuestThreadSlots].push_back(
            {.lr = jit.GetRegister(30),
             .seq = seq,
             .slot = slot,
             .output = output != ~0u ? jit.GetRegister(output) : 0});
        jit.SetRegister(30, SyntheticAddress(state, TraceReturnSlot));
        jit.SetRegister(16, target.driver);
        return false;
    }
    if (want_return) {
        frame.command_buffer = jit.GetRegister(0);
        frame.write_pointer = memory.Read64(frame.command_buffer);
        state.command_frames[thread_id % GuestThreadSlots].push_back(frame);
        jit.SetRegister(30, SyntheticAddress(state, EmittedReturnSlot));
    }
    jit.SetRegister(16, target.driver);
    return false;
}

#ifdef LICHII_NVN_DIRECT
namespace {
/// Direct mode's present (nvnQueuePresentTexture, dotfiles-25x.7): the replay's image goes to
/// Lichii's presenter as a layer whose address names the frame (AccelerateDisplay finds it), and
/// the present's bookkeeping is nx2:vi2's (Service::Nx2PresentBookkeeping): counts, capture,
/// speed limiter, perf stats, pacing to the vblank, and as nx2:vi2's Composite the display
/// HDR-change check.
void PresentDirect(Core::System& system, State& state) {
    Service::Nx2NotifyHdrChange(system.Kernel());
    if (!state.present) {
        state.present.emplace(system);
    }
    auto& present = *state.present;
    const u64 own = ++state.presents;
    const u64 number = present.Count(Kernel::GetCurrentThread(system.Kernel()));
    nvn_direct_frame frame{};
    const bool shown = nvn_direct_present_take(&frame) != 0;
    if (own <= 4 || own % 600 == 0) {
        LOG_INFO(Core, "NVN direct: present #{} ({}x{} VkFormat {}, {})", own, frame.width,
                 frame.height, static_cast<int>(frame.format), shown ? "shown" : "nothing to show");
    }
    if (shown) {
        // The game's present waits for the previous frame's composite (as many images as the
        // window has are in flight between the replay and Lichii's presenter).
        system.GPU().WaitForComposite();
        u32 crop_width = frame.crop[2] > 0 ? static_cast<u32>(frame.crop[2]) : frame.width;
        u32 crop_height = frame.crop[3] > 0 ? static_cast<u32>(frame.crop[3]) : frame.height;
        if (frame.crop[2] <= 0 && !Settings::IsDockedMode()) {
            // As nx2:vi2: a handheld frame is the top-left 1920x1080 of the image.
            crop_width = (std::min)(crop_width, 1920U);
            crop_height = (std::min)(crop_height, 1080U);
        }
        // As nx2:vi2, the presenter converts by the image's format and colour space: Mario Kart
        // World told HDR presents PQ in RGB10A2.
        const auto image_format =
            frame.format == VK_FORMAT_R16G16B16A16_SFLOAT ? Service::Nx2NativeImageFormat::Rgba16Float
            : frame.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32
                ? Service::Nx2NativeImageFormat::Rgb10A2
                : Service::Nx2NativeImageFormat::Rgba8;
        std::vector<Tegra::FramebufferConfig> layers{Tegra::FramebufferConfig{
            .address = NVN_DIRECT_FRAME_TAG | frame.id,
            .width = frame.width,
            .height = frame.height,
            .stride = frame.width,
            .pixel_format = image_format == Service::Nx2NativeImageFormat::Rgba16Float
                                ? Service::android::PixelFormat::RgbaFp16
                            : image_format == Service::Nx2NativeImageFormat::Rgb10A2
                                ? Service::android::PixelFormat::Rgba1010102
                                : Service::android::PixelFormat::Rgba8888,
            .transform_flags = Service::android::BufferTransformFlags::Unset,
            .crop_rect = {frame.crop[0], frame.crop[1],
                          frame.crop[0] + static_cast<int>(crop_width),
                          frame.crop[1] + static_cast<int>(crop_height)},
            .blending = Tegra::BlendMode::Opaque,
            .color_space = Service::Nx2NativeImageColorSpace(image_format,
                                                             Service::g_nx2_told_hdr.load()),
        }};
        system.GPU().RequestComposite(std::move(layers), {});
    }
    present.Finish(own);
    present.PaceToVblank((frame.interval << 16) | 60, system.ApplicationProcess(), own);
    OnPresent(number);
}
} // namespace
#endif

void OnDirectCpuWrite(u64 address, u64 size) {
#ifdef LICHII_NVN_DIRECT
    nvn_direct_cpu_written(address, size);
#else
    (void)address;
    (void)size;
#endif
}

bool OnGuestUnmap(const Common::PageTable& page_table, u64 address, u64 size) {
#ifdef LICHII_NVN_DIRECT
    const State* const state = Get();
    if (state != nullptr && state->direct_ready &&
        &page_table == &state->process->GetPageTable().GetImpl()) {
        nvn_direct_guest_unmap(address, size);
        return true;
    }
#else
    (void)page_table;
    (void)address;
    (void)size;
#endif
    return false;
}

void OnGuestUnmapDone() {
#ifdef LICHII_NVN_DIRECT
    nvn_direct_guest_unmap_done();
#endif
}

void RunHaltedCall(Core::System& system, std::array<u64, 8>& args) {
#ifdef LICHII_NVN_DIRECT
    State& state = *Get();
    const u32 slot = std::exchange(t_halted_slot, MaxSlots);
    if (slot == MaxSlots) {
        LOG_CRITICAL(Core, "NVN direct: a halted call without its slot");
        return;
    }
    state.halted_calls.fetch_add(1, std::memory_order_relaxed);
    const Slot& target = state.slots[slot];
    // A wait for the replay's GPU (SyncWait, QueueFinish) sleeps in the guest kernel until it
    // would not wait (dotfiles-25x.4): a host wait here held the guest core, so the game's other
    // threads there stopped (one of them may be the one whose submission the GPU waits for).
    const bool sync_wait = std::string_view{nvn_direct_name(target.front)} == "nvnSyncWait";
    bool waited = false;
    if (nvn_direct_call_blocks(target.front, args.data())) {
        auto& kernel = system.Kernel();
        const s64 start = kernel.HardwareTimer().GetTick();
        const u64 timeout = sync_wait ? args[1] : ~u64{0};
        s64 slice = 50'000;
        while (nvn_direct_call_blocks(target.front, args.data())) {
            const s64 now = kernel.HardwareTimer().GetTick();
            if (timeout != ~u64{0} && static_cast<u64>(now - start) >= timeout) {
                break;
            }
            static_cast<void>(Kernel::GetCurrentThread(kernel).Sleep(kernel, now + slice));
            slice = (std::min)(slice * 2, s64{1'000'000});
        }
        waited = true;
        if (sync_wait) {
            args[1] = 0; // (answered from the state now, without a host wait)
        }
    }
    // No halting call takes or returns floats (checked at the first one), and none of HaltingCalls
    // takes arguments on the stack (sp 0).
    const std::array<u64, 8> v{};
    u64 v0 = 0;
    args[0] = nvn_direct_call(target.front, args.data(), v.data(), 0, t_halted_lr, &v0);
    if (sync_wait && waited && args[0] == 0) {
        args[0] = 1; // NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED: it was signalled while waiting
    }
    if (std::string_view{nvn_direct_name(target.front)} != "nvnQueuePresentTexture") {
        return;
    }
    PresentDirect(system, state);
#else
    (void)system;
    (void)args;
#endif
}

void OnPresent(u64 present) {
    NvnTrace::OnPresent(present);
    State* const state_pointer = Get();
    if (state_pointer != nullptr && state_pointer->direct && present % 600 == 0) {
        // Direct mode: the stub census (names called without an implementation) and proof the
        // driver did not run.
        State& state = *state_pointer;
        std::string census;
        std::scoped_lock lock{state.mutex};
        for (const auto& [name, slot] : state.slot_by_name) {
            if (const u64 calls = state.stub_calls[slot].load(std::memory_order_relaxed); calls) {
                census += fmt::format(" {}={}", name, calls);
            }
        }
        LOG_INFO(Core,
                 "NVN direct: present {}: driver entries run {}, halted calls {}, stubs called:{}",
                 present, state.driver_entry_calls.load(), state.halted_calls.load(),
                 census.empty() ? " none" : census);
    }
    if (state_pointer == nullptr || state_pointer->out == nullptr ||
        present % state_pointer->every != 0) {
        return;
    }
    State& state = *state_pointer;
    std::string text = fmt::format(
        "t {} {}\n", present,
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    std::scoped_lock lock{state.mutex};
    const u32 slots = state.slot_count.load(std::memory_order_acquire);
    for (u32 slot = LookupSlot; slot < slots; ++slot) {
        u64 calls = 0;
        for (const Counters* counters : state.counters) {
            calls += counters->calls[slot].load(std::memory_order_relaxed);
        }
        if (calls != 0) {
            text += fmt::format("c {} {} {}\n", present, slot, calls);
        }
    }
    if (state.emitted) {
        u64 decoded = 0, words = 0, skipped = 0;
        for (const Counters* counters : state.counters) {
            decoded += counters->decoded_calls.load(std::memory_order_relaxed);
            words += counters->emitted_words.load(std::memory_order_relaxed);
            skipped += counters->skipped_calls.load(std::memory_order_relaxed);
        }
        text += fmt::format("ew {} {} {} {}\n", present, decoded, words, skipped);
        for (u32 key = 0; key < (1u << 16); ++key) {
            u64 headers = 0;
            for (const Counters* counters : state.counters) {
                headers += counters->emitted_headers[key].load(std::memory_order_relaxed);
            }
            if (headers != 0) {
                text += fmt::format("e {} {:#x} {}\n", present, key, headers);
            }
            if (const u64 front = VideoCommon::NvnHeaderCount::g_headers[key]; front != 0) {
                text += fmt::format("f {} {:#x} {} {}\n", present, key, front,
                                    VideoCommon::NvnHeaderCount::g_table_headers[key]);
            }
        }
    }
    if (state.hle_bind_texture) {
        u64 hle = 0, fallbacks = 0, checked = 0, mismatches = 0;
        for (const Counters* counters : state.counters) {
            hle += counters->hle_calls.load(std::memory_order_relaxed);
            fallbacks += counters->hle_fallbacks.load(std::memory_order_relaxed);
            checked += counters->verify_checked.load(std::memory_order_relaxed);
            mismatches += counters->verify_mismatches.load(std::memory_order_relaxed);
        }
        text += fmt::format("h {} {} {} {} {}\n", present, hle, fallbacks, checked, mismatches);
    }
    if (state.private0_address != 0 && state.process != nullptr) {
        text += fmt::format("p0 {} {}\n", present,
                            ReadPrivate0(state, state.process->GetMemory()));
    }
    std::fwrite(text.data(), 1, text.size(), state.out);
    std::fflush(state.out);
}

} // namespace Core::NvnHook
