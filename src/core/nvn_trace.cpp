// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/logging.h"
#include "common/zstd_compression.h"
#include "core/hle/kernel/k_process.h"
#include "core/memory.h"
#include "core/nvn_trace.h"
#include "dynarmic/interface/A64/a64.h"

namespace Core::NvnTrace {
namespace {

constexpr u32 MaxSlots = 4096;
constexpr u64 PageSize = 0x1000;
constexpr size_t BlockBytes = 32u << 20;
constexpr size_t MaxQueuedBytes = 1u << 30;
constexpr u64 MaxItemBytes = 64u << 20;

/// From BananaNative's NVN prototypes (gen.py): AArch64 argument counts and the by-value NVN
/// objects whose bytes are hashed (argument, bytes, count argument or -1).
struct Proto {
    const char* name;
    u8 nint;
    u8 nflt;
    struct Hashed {
        s8 arg;
        u16 length;
        s8 count;
    } hashed[6];
};
constexpr Proto Protos[] = {
#include "core/nvn_trace_protos.inc"
};

/// The functions with a payload or a return rule.
enum class F : u16 {
    Other,
    QueueSubmitCommands,
    CallCommands,
    CopyCommands,
    SetRenderTargets,
    ClearColor,
    ClearColori,
    ClearColorui,
    SetBlendColor,
    SetViewports,
    SetScissors,
    SetDepthRanges,
    SetViewportSwizzles,
    BindVertexBuffers,
    BindUniformBuffers,
    BindStorageBuffers,
    BindTextures,
    BindImages,
    BindSeparateTextures,
    BindSeparateSamplers,
    BindTexturesDeferred,
    UpdateUniformBuffer,
    PushGraphicsConstants,
    PushComputeConstants,
    ProgramSetShaders,
    ProgramSetShadersExt,
    ProgramBuilderSetDefaults,
    ProgramBuilderSetShaders,
    ProgramInitializeFromBuilder,
    TextureBuilderSetPackagedTextureLayout,
    TextureWriteTexels,
    TextureWriteTexelsStrided,
    TextureFlushTexels,
    TextureInvalidateTexels,
    TextureReadTexels,
    TextureReadTexelsStrided,
    CopyBufferToTexture,
    CopyTextureToBuffer,
    CopyTextureToTexture,
    ClearTexture,
    ClearTexturei,
    ClearTextureui,
    DrawTexture,
    MemoryPoolMapVirtual,
    WindowBuilderSetTextures,
    SamplerBuilderSetBorderColor,
    SamplerBuilderSetBorderColori,
    SamplerBuilderSetBorderColorui,
    DeviceRegisterFastClearColor,
    DeviceRegisterFastClearColori,
    DeviceRegisterFastClearColorui,
    MultisampleStateSetSampleLocations,
    MemoryPoolBuilderSetDefaults,
    MemoryPoolBuilderSetStorage,
    MemoryPoolBuilderSetFlags,
    MemoryPoolInitialize,
    MemoryPoolFinalize,
    // Returns
    EndRecording,
    GetTextureHandle,
    GetTexelFetchHandle,
    GetImageHandle,
    GetSeparateTextureHandle,
    GetSeparateSamplerHandle,
    WindowAcquireTexture,
    QueueAcquireTexture,
    MemoryPoolGetBufferAddress,
    BufferGetAddress,
    MemoryPoolMap,
    BufferMap,
};

struct Named {
    std::string_view name;
    F f;
};
constexpr Named Rules[] = {
    {"nvnQueueSubmitCommands", F::QueueSubmitCommands},
    {"nvnCommandBufferCallCommands", F::CallCommands},
    {"nvnCommandBufferCopyCommands", F::CopyCommands},
    {"nvnCommandBufferSetRenderTargets", F::SetRenderTargets},
    {"nvnCommandBufferClearColor", F::ClearColor},
    {"nvnCommandBufferClearColori", F::ClearColori},
    {"nvnCommandBufferClearColorui", F::ClearColorui},
    {"nvnCommandBufferSetBlendColor", F::SetBlendColor},
    {"nvnCommandBufferSetViewports", F::SetViewports},
    {"nvnCommandBufferSetScissors", F::SetScissors},
    {"nvnCommandBufferSetDepthRanges", F::SetDepthRanges},
    {"nvnCommandBufferSetViewportSwizzles", F::SetViewportSwizzles},
    {"nvnCommandBufferBindVertexBuffers", F::BindVertexBuffers},
    {"nvnCommandBufferBindUniformBuffers", F::BindUniformBuffers},
    {"nvnCommandBufferBindStorageBuffers", F::BindStorageBuffers},
    {"nvnCommandBufferBindTextures", F::BindTextures},
    {"nvnCommandBufferBindImages", F::BindImages},
    {"nvnCommandBufferBindSeparateTextures", F::BindSeparateTextures},
    {"nvnCommandBufferBindSeparateSamplers", F::BindSeparateSamplers},
    {"nvnCommandBufferBindTexturesDeferred", F::BindTexturesDeferred},
    {"nvnCommandBufferUpdateUniformBuffer", F::UpdateUniformBuffer},
    {"nvnCommandBufferPushGraphicsConstants", F::PushGraphicsConstants},
    {"nvnCommandBufferPushComputeConstants", F::PushComputeConstants},
    {"nvnProgramSetShaders", F::ProgramSetShaders},
    {"nvnProgramSetShadersExt", F::ProgramSetShadersExt},
    {"nvnProgramBuilderSetDefaults", F::ProgramBuilderSetDefaults},
    {"nvnProgramBuilderSetShaders", F::ProgramBuilderSetShaders},
    {"nvnProgramInitializeFromBuilder", F::ProgramInitializeFromBuilder},
    {"nvnTextureBuilderSetPackagedTextureLayout", F::TextureBuilderSetPackagedTextureLayout},
    {"nvnTextureWriteTexels", F::TextureWriteTexels},
    {"nvnTextureWriteTexelsStrided", F::TextureWriteTexelsStrided},
    {"nvnTextureFlushTexels", F::TextureFlushTexels},
    {"nvnTextureInvalidateTexels", F::TextureInvalidateTexels},
    {"nvnTextureReadTexels", F::TextureReadTexels},
    {"nvnTextureReadTexelsStrided", F::TextureReadTexelsStrided},
    {"nvnCommandBufferCopyBufferToTexture", F::CopyBufferToTexture},
    {"nvnCommandBufferCopyTextureToBuffer", F::CopyTextureToBuffer},
    {"nvnCommandBufferCopyTextureToTexture", F::CopyTextureToTexture},
    {"nvnCommandBufferClearTexture", F::ClearTexture},
    {"nvnCommandBufferClearTexturei", F::ClearTexturei},
    {"nvnCommandBufferClearTextureui", F::ClearTextureui},
    {"nvnCommandBufferDrawTexture", F::DrawTexture},
    {"nvnMemoryPoolMapVirtual", F::MemoryPoolMapVirtual},
    {"nvnWindowBuilderSetTextures", F::WindowBuilderSetTextures},
    {"nvnSamplerBuilderSetBorderColor", F::SamplerBuilderSetBorderColor},
    {"nvnSamplerBuilderSetBorderColori", F::SamplerBuilderSetBorderColori},
    {"nvnSamplerBuilderSetBorderColorui", F::SamplerBuilderSetBorderColorui},
    {"nvnDeviceRegisterFastClearColor", F::DeviceRegisterFastClearColor},
    {"nvnDeviceRegisterFastClearColori", F::DeviceRegisterFastClearColori},
    {"nvnDeviceRegisterFastClearColorui", F::DeviceRegisterFastClearColorui},
    {"nvnMultisampleStateSetSampleLocations", F::MultisampleStateSetSampleLocations},
    {"nvnMemoryPoolBuilderSetDefaults", F::MemoryPoolBuilderSetDefaults},
    {"nvnMemoryPoolBuilderSetStorage", F::MemoryPoolBuilderSetStorage},
    {"nvnMemoryPoolBuilderSetFlags", F::MemoryPoolBuilderSetFlags},
    {"nvnMemoryPoolInitialize", F::MemoryPoolInitialize},
    {"nvnMemoryPoolFinalize", F::MemoryPoolFinalize},
    {"nvnCommandBufferEndRecording", F::EndRecording},
    {"nvnDeviceGetTextureHandle", F::GetTextureHandle},
    {"nvnDeviceGetTexelFetchHandle", F::GetTexelFetchHandle},
    {"nvnDeviceGetImageHandle", F::GetImageHandle},
    {"nvnDeviceGetSeparateTextureHandle", F::GetSeparateTextureHandle},
    {"nvnDeviceGetSeparateSamplerHandle", F::GetSeparateSamplerHandle},
    {"nvnWindowAcquireTexture", F::WindowAcquireTexture},
    {"nvnQueueAcquireTexture", F::QueueAcquireTexture},
    {"nvnMemoryPoolGetBufferAddress", F::MemoryPoolGetBufferAddress},
    {"nvnBufferGetAddress", F::BufferGetAddress},
    {"nvnMemoryPoolMap", F::MemoryPoolMap},
    {"nvnBufferMap", F::BufferMap},
};

bool WantsReturn(F f) {
    return f >= F::EndRecording;
}

struct SlotInfo {
    const Proto* proto{};
    F f{F::Other};
};

struct Pool {
    u64 storage{};
    u64 size{};
    u32 flags{};
};

struct ProgramBuilder {
    u64 count{};
    u64 shaders{};
};

struct State {
    std::mutex mutex;
    std::FILE* file{};
    std::vector<u8> buf;
    u64 seq{};
    std::array<SlotInfo, MaxSlots> slots{};

    u64 from{};
    u64 frames{};
    bool window{};
    bool done{};

    std::unordered_map<u64, Pool> pool_builders;
    std::unordered_map<u64, Pool> pools;
    std::unordered_map<u64, ProgramBuilder> program_builders;

    Kernel::KProcess* process{};
    /// Window: each pool page's hash at the last 'S'/'D'
    std::unordered_map<u64, u64> page_hashes;

    std::thread writer;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::vector<u8>> queue;
    size_t queued_bytes{};
    bool stop{};
    u64 file_bytes{};
    u64 raw_bytes{};
};

State* Create() {
    const char* hook = std::getenv("EDEN_NX2_NVN_HOOK");
    const char* path = std::getenv("EDEN_NX2_NVN_TRACE");
    if (hook == nullptr || std::strcmp(hook, "1") != 0 || path == nullptr || *path == 0) {
        return nullptr;
    }
    auto* state = new State;
    state->file = std::fopen(path, "wb");
    if (state->file == nullptr) {
        LOG_CRITICAL(Core, "NVN trace: cannot write {}", path);
        delete state;
        return nullptr;
    }
    std::fwrite("NVNTRC01", 1, 8, state->file);
    const char* from = std::getenv("EDEN_NX2_NVN_TRACE_FROM");
    const char* frames = std::getenv("EDEN_NX2_NVN_TRACE_FRAMES");
    state->from = from ? std::strtoull(from, nullptr, 10) : 0;
    state->frames = frames ? std::strtoull(frames, nullptr, 10) : 120;
    state->buf.reserve(BlockBytes + (1u << 20));
    state->writer = std::thread([state] {
        for (;;) {
            std::vector<u8> block;
            {
                std::unique_lock lock{state->queue_mutex};
                state->queue_cv.wait(lock, [&] { return state->stop || !state->queue.empty(); });
                if (state->queue.empty()) {
                    return;
                }
                block = std::move(state->queue.front());
                state->queue.pop_front();
            }
            const auto packed = Common::Compression::CompressDataZSTD(block.data(), block.size(), 1);
            const u32 header[2]{static_cast<u32>(packed.size()), static_cast<u32>(block.size())};
            std::fwrite(header, sizeof(header), 1, state->file);
            std::fwrite(packed.data(), 1, packed.size(), state->file);
            std::fflush(state->file); // a run that quits mid-window keeps what was written
            {
                std::scoped_lock lock{state->queue_mutex};
                state->queued_bytes -= block.size();
                state->file_bytes += sizeof(header) + packed.size();
                state->raw_bytes += block.size();
            }
            state->queue_cv.notify_all();
        }
    });
    LOG_INFO(Core, "NVN trace: {} (window from present {}, {} presents)", path, state->from,
             state->frames);
    return state;
}

State* Get() {
    static State* const state = Create();
    return state;
}

template <typename T>
void Put(State& s, const T& value) {
    const auto* p = reinterpret_cast<const u8*>(&value);
    s.buf.insert(s.buf.end(), p, p + sizeof(T));
}

void PutBytes(State& s, const void* data, size_t size) {
    const auto* p = static_cast<const u8*>(data);
    s.buf.insert(s.buf.end(), p, p + size);
}

/// Hands the buffer to the writer once it holds a block (waits while too much is queued).
void Flush(State& s, bool force) {
    if (s.buf.empty() || (!force && s.buf.size() < BlockBytes)) {
        return;
    }
    std::unique_lock lock{s.queue_mutex};
    s.queue_cv.wait(lock, [&] { return s.queued_bytes < MaxQueuedBytes; });
    s.queued_bytes += s.buf.size();
    s.queue.push_back(std::move(s.buf));
    s.buf = {};
    s.buf.reserve(BlockBytes + (1u << 20));
    lock.unlock();
    s.queue_cv.notify_all();
}

bool ReadGuest(Core::Memory::Memory& memory, u64 address, void* out, size_t size) {
    auto* bytes = static_cast<u8*>(out);
    while (size != 0) {
        const size_t chunk = std::min<size_t>(size, PageSize - (address & (PageSize - 1)));
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

u64 Hash(const u8* data, size_t size) {
    u64 h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i) {
        h = (h ^ data[i]) * 0x100000001b3ull;
    }
    return h;
}

/// The call record being written: its item count is patched at the end.
struct Call {
    State& s;
    Core::Memory::Memory& memory;
    u32 items{};

    void Mem(u64 address, u64 size) {
        if (address == 0 || size == 0 || size > MaxItemBytes) {
            return;
        }
        const size_t at = s.buf.size();
        s.buf.resize(at + 13 + size);
        s.buf[at] = 'M';
        std::memcpy(&s.buf[at + 1], &address, 8);
        const u32 length = static_cast<u32>(size);
        std::memcpy(&s.buf[at + 9], &length, 4);
        if (!ReadGuest(memory, address, &s.buf[at + 13], size)) {
            s.buf.resize(at);
            return;
        }
        ++items;
    }

    void Hashed(u64 address, u32 size) {
        if (address == 0) {
            return;
        }
        u8 bytes[0x100];
        if (size > sizeof(bytes) || !ReadGuest(memory, address, bytes, size)) {
            return;
        }
        Put<u8>(s, 'H');
        Put(s, address);
        Put(s, size);
        Put(s, Hash(bytes, size));
        ++items;
    }

    u64 Read64(u64 address) {
        u64 value = 0;
        ReadGuest(memory, address, &value, 8);
        return value;
    }

    u32 Read32(u64 address) {
        u32 value = 0;
        ReadGuest(memory, address, &value, 4);
        return value;
    }

    /// NVNshaderData[count] (stride 16, or 24 for the Ext form) with each stage's control
    /// section and code area (hle_nvn.c nvn_ctl_lens: NVN2 control 0x98761234 version 0xc9 is
    /// 0xd8 bytes, its code length at +0x44; NVN's is up to 0x880, its code length at +0x704).
    void Shaders(u64 count, u64 shaders, u64 stride) {
        if (count > 6 || shaders == 0) {
            return;
        }
        Mem(shaders, count * stride);
        for (u64 i = 0; i < count; ++i) {
            const u64 data = Read64(shaders + i * stride);
            const u64 control = Read64(shaders + i * stride + 8);
            if (control == 0) {
                continue;
            }
            u32 control_size, code_size;
            if (Read32(control) == 0x98761234u && Read32(control + 4) == 0xc9) {
                control_size = 0xd8;
                code_size = Read32(control + 0x44);
            } else {
                const u32 own = Read32(control + 0x20);
                control_size = own >= 0x760 && own < 0x880 ? own : 0x880;
                code_size = Read32(control + 0x704);
            }
            Mem(control, control_size);
            if (code_size < (16u << 20)) {
                Mem(data, code_size);
            }
        }
    }
};

/// Live pools' storage ranges, merged.
std::vector<std::pair<u64, u64>> PoolRanges(const State& s) {
    std::vector<std::pair<u64, u64>> ranges;
    for (const auto& [pool, p] : s.pools) {
        if (p.storage != 0 && p.size != 0) {
            const u64 begin = p.storage & ~(PageSize - 1);
            const u64 end = (p.storage + p.size + PageSize - 1) & ~(PageSize - 1);
            ranges.emplace_back(begin, end);
        }
    }
    std::ranges::sort(ranges);
    std::vector<std::pair<u64, u64>> merged;
    for (const auto& r : ranges) {
        if (!merged.empty() && r.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, r.second);
        } else {
            merged.push_back(r);
        }
    }
    return merged;
}

u64 PageHash(const u8* page) {
    u64 h = 0x9e3779b97f4a7c15ull;
    for (size_t i = 0; i < PageSize; i += 8) {
        u64 v;
        std::memcpy(&v, page + i, 8);
        h = (h ^ v) * 0xff51afd7ed558ccdull;
        h ^= h >> 29;
    }
    return h;
}

/// Emits pages [first, last) of guest memory as `type` records, runs of up to 1 MiB. Each page's
/// hash in s.page_hashes becomes that of the bytes read for it, so it describes the file even when
/// the guest writes the page meanwhile.
void EmitPages(State& s, Core::Memory::Memory& memory, u8 type, const std::vector<u64>& pages,
               bool skip_zero) {
    static const std::array<u8, PageSize> zero{};
    size_t i = 0;
    std::array<u8, PageSize> page;
    while (i < pages.size()) {
        size_t j = i;
        const size_t at = s.buf.size();
        Put<u8>(s, type);
        Put(s, pages[i] * PageSize);
        const size_t length_at = s.buf.size();
        Put<u32>(s, 0);
        u32 length = 0;
        while (j < pages.size() && (j == i || pages[j] == pages[j - 1] + 1) && length < (1u << 20)) {
            if (!ReadGuest(memory, pages[j] * PageSize, page.data(), PageSize)) {
                page.fill(0);
            }
            s.page_hashes[pages[j]] = PageHash(page.data());
            if (skip_zero && page == zero) {
                break;
            }
            PutBytes(s, page.data(), PageSize);
            length += PageSize;
            ++j;
        }
        if (length == 0) {
            s.buf.resize(at);
            ++j; // a zero page: left out
        } else {
            std::memcpy(&s.buf[length_at], &length, 4);
        }
        i = j;
        Flush(s, false);
    }
}

/// The pages of the live pools.
std::vector<u64> PoolPages(const State& s) {
    std::vector<u64> pages;
    for (const auto& [begin, end] : PoolRanges(s)) {
        for (u64 a = begin; a < end; a += PageSize) {
            pages.push_back(a / PageSize);
        }
    }
    return pages;
}

/// The pages of the live pools, and their hashes (on 16 threads).
std::pair<std::vector<u64>, std::vector<u64>> HashPoolPages(State& s, Core::Memory::Memory& memory) {
    std::vector<u64> pages = PoolPages(s);
    std::vector<u64> hashes(pages.size());
    constexpr u64 Threads = 16;
    const u64 per = (pages.size() + Threads - 1) / Threads;
    std::vector<std::thread> threads;
    for (u64 t = 0; t < Threads && t * per < pages.size(); ++t) {
        threads.emplace_back([&, t] {
            std::array<u8, PageSize> page;
            for (u64 i = t * per; i < std::min<u64>(pages.size(), (t + 1) * per); ++i) {
                if (!ReadGuest(memory, pages[i] * PageSize, page.data(), PageSize)) {
                    page.fill(0);
                }
                hashes[i] = PageHash(page.data());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    return {std::move(pages), std::move(hashes)};
}

/// The pool pages whose bytes changed since the last call (by page hash, whatever wrote them: the
/// guest, services, file loads), as 'D' records; pages of a pool new since then are emitted.
/// (The kernel's soft-dirty bits were tried first: clearing them hung the emulator.)
void Dirty(State& s, Core::Memory::Memory& memory) {
    auto [pages, hashes] = HashPoolPages(s, memory);
    std::vector<u64> changed;
    std::unordered_map<u64, u64> next;
    next.reserve(pages.size());
    for (size_t i = 0; i < pages.size(); ++i) {
        const auto it = s.page_hashes.find(pages[i]);
        if (it == s.page_hashes.end() || it->second != hashes[i]) {
            changed.push_back(pages[i]);
        } else {
            next.emplace(pages[i], it->second);
        }
    }
    s.page_hashes = std::move(next); // (EmitPages adds the changed pages' hashes)
    EmitPages(s, memory, 'D', changed, false);
}

void Snapshot(State& s, Core::Memory::Memory& memory) {
    Put<u8>(s, 'L');
    Put(s, static_cast<u32>(s.pools.size()));
    for (const auto& [pool, p] : s.pools) {
        Put(s, pool);
        Put(s, p.storage);
        Put(s, p.size);
        Put(s, p.flags);
        Put<u32>(s, 0);
    }
    const std::vector<u64> pages = PoolPages(s);
    s.page_hashes.clear(); // (EmitPages adds every page's hash)
    const u64 bytes = pages.size() * PageSize;
    EmitPages(s, memory, 'S', pages, true);
    Put<u8>(s, 'W');
    Put<u8>(s, 0);
    LOG_INFO(Core, "NVN trace: window starts: {} pools, {} MiB of storage", s.pools.size(),
             bytes >> 20);
}

} // namespace

bool Enabled() {
    return Get() != nullptr;
}

void Install(Kernel::KProcess& process) {
    State* const state = Get();
    if (state == nullptr) {
        return;
    }
    state->process = &process;
}

void OnName(u32 slot, const char* name) {
    State* const state = Get();
    if (state == nullptr || slot >= MaxSlots) {
        return;
    }
    State& s = *state;
    std::scoped_lock lock{s.mutex};
    const std::string_view view{name};
    for (const Proto& proto : Protos) {
        if (view == proto.name) {
            s.slots[slot].proto = &proto;
        }
    }
    for (const Named& rule : Rules) {
        if (view == rule.name) {
            s.slots[slot].f = rule.f;
        }
    }
    if (s.done) {
        return;
    }
    Put<u8>(s, 'N');
    Put(s, static_cast<u16>(slot));
    Put(s, static_cast<u16>(view.size()));
    PutBytes(s, view.data(), view.size());
}

u32 OutputArgument(u32 slot) {
    const State* const state = Get();
    if (state == nullptr || slot >= MaxSlots) {
        return ~0u;
    }
    switch (state->slots[slot].f) {
    case F::WindowAcquireTexture:
    case F::QueueAcquireTexture:
        return 2;
    default:
        return ~0u;
    }
}

bool OnCall(u32 slot, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory, u32 thread,
            u64& seq) {
    State* const state = Get();
    if (state == nullptr || slot >= MaxSlots) {
        return false;
    }
    State& s = *state;
    std::scoped_lock lock{s.mutex};
    if (s.done) {
        return false;
    }
    const SlotInfo& info = s.slots[slot];
    std::array<u64, 8> x;
    for (u32 i = 0; i < 8; ++i) {
        x[i] = jit.GetRegister(i);
    }
    const u8 nx = info.proto ? std::min<u8>(info.proto->nint, 8) : 8;
    const u8 nv = info.proto ? std::min<u8>(info.proto->nflt, 8) : 0;
    seq = ++s.seq;
    Put<u8>(s, 'C');
    Put(s, seq);
    Put(s, thread);
    Put(s, static_cast<u16>(slot));
    Put(s, nx);
    Put(s, nv);
    const size_t items_at = s.buf.size();
    Put<u32>(s, 0);
    for (u32 i = 0; i < nx; ++i) {
        Put(s, x[i]);
    }
    for (u32 i = 0; i < nv; ++i) {
        Put(s, jit.GetVector(i)[0]);
    }
    Call call{s, memory};
    if (info.proto != nullptr) {
        for (const auto& h : info.proto->hashed) {
            if (h.length == 0) {
                break;
            }
            const u64 count = h.count >= 0 ? std::min<u64>(x[h.count], 32) : 1;
            for (u64 i = 0; i < count; ++i) {
                call.Hashed(x[h.arg] + i * h.length, h.length);
            }
        }
    }
    switch (info.f) {
    case F::QueueSubmitCommands:
    case F::CallCommands:
    case F::CopyCommands:
        call.Mem(x[2], std::min<u64>(x[1], 4096) * 8);
        break;
    case F::SetRenderTargets: {
        const u64 count = std::min<u64>(x[1], 8);
        call.Mem(x[2], count * 8);
        call.Mem(x[3], count * 8);
        for (u64 i = 0; i < count; ++i) {
            if (x[2] != 0) {
                call.Hashed(call.Read64(x[2] + i * 8), 0x80);
            }
            if (x[3] != 0) {
                call.Hashed(call.Read64(x[3] + i * 8), 16);
            }
        }
        break;
    }
    case F::ClearColor:
    case F::ClearColori:
    case F::ClearColorui:
        call.Mem(x[2], 16);
        break;
    case F::SetBlendColor:
    case F::SamplerBuilderSetBorderColor:
    case F::SamplerBuilderSetBorderColori:
    case F::SamplerBuilderSetBorderColorui:
    case F::DeviceRegisterFastClearColor:
    case F::DeviceRegisterFastClearColori:
    case F::DeviceRegisterFastClearColorui:
        call.Mem(x[1], 16);
        break;
    case F::SetViewports:
    case F::SetScissors:
    case F::SetViewportSwizzles:
        call.Mem(x[3], std::min<u64>(x[2], 16) * 16);
        break;
    case F::SetDepthRanges:
    case F::MultisampleStateSetSampleLocations:
        call.Mem(x[3], std::min<u64>(x[2], 64) * 8);
        break;
    case F::BindVertexBuffers:
        call.Mem(x[3], std::min<u64>(x[2], 32) * 16);
        break;
    case F::BindUniformBuffers:
    case F::BindStorageBuffers:
        call.Mem(x[4], std::min<u64>(x[3], 32) * 16);
        break;
    case F::BindTextures:
    case F::BindImages:
    case F::BindSeparateTextures:
    case F::BindSeparateSamplers:
    case F::BindTexturesDeferred:
        call.Mem(x[4], std::min<u64>(x[3], 256) * 8);
        break;
    case F::UpdateUniformBuffer:
        call.Mem(x[5], x[4]);
        break;
    case F::PushGraphicsConstants:
    case F::PushComputeConstants:
        call.Mem(x[4], x[3]);
        break;
    case F::ProgramSetShaders:
        call.Shaders(x[1], x[2], 16);
        break;
    case F::ProgramSetShadersExt:
        call.Shaders(x[1], x[2], 24);
        break;
    case F::ProgramBuilderSetDefaults:
        s.program_builders.erase(x[0]);
        break;
    case F::ProgramBuilderSetShaders:
        s.program_builders[x[0]] = {x[1], x[2]};
        call.Shaders(x[1], x[2], 16);
        break;
    case F::ProgramInitializeFromBuilder:
        if (const auto it = s.program_builders.find(x[1]); it != s.program_builders.end()) {
            call.Shaders(it->second.count, it->second.shaders, 16);
        }
        break;
    case F::TextureBuilderSetPackagedTextureLayout:
        call.Mem(x[1], 8);
        break;
    case F::TextureWriteTexels:
    case F::TextureWriteTexelsStrided:
    case F::TextureFlushTexels:
    case F::TextureInvalidateTexels:
    case F::TextureReadTexels:
    case F::TextureReadTexelsStrided:
        call.Mem(x[2], 24);
        break;
    case F::CopyBufferToTexture:
        call.Mem(x[4], 24);
        break;
    case F::CopyTextureToBuffer:
        call.Mem(x[3], 24);
        break;
    case F::CopyTextureToTexture:
        call.Mem(x[3], 24);
        call.Mem(x[6], 24);
        break;
    case F::ClearTexture:
    case F::ClearTexturei:
    case F::ClearTextureui:
        call.Mem(x[3], 24);
        call.Mem(x[4], 16);
        break;
    case F::DrawTexture:
        call.Mem(x[2], 16);
        call.Mem(x[3], 16);
        break;
    case F::MemoryPoolMapVirtual:
        call.Mem(x[2], std::min<u64>(x[1], 65536) * 0x28);
        break;
    case F::WindowBuilderSetTextures:
        call.Mem(x[2], std::min<u64>(x[1], 8) * 8);
        break;
    case F::MemoryPoolBuilderSetDefaults:
        s.pool_builders[x[0]] = {};
        break;
    case F::MemoryPoolBuilderSetStorage:
        s.pool_builders[x[0]].storage = x[1];
        s.pool_builders[x[0]].size = x[2];
        break;
    case F::MemoryPoolBuilderSetFlags:
        s.pool_builders[x[0]].flags = static_cast<u32>(x[1]);
        break;
    case F::MemoryPoolInitialize:
        s.pools[x[0]] = s.pool_builders[x[1]];
        break;
    case F::MemoryPoolFinalize:
        s.pools.erase(x[0]);
        break;
    default:
        break;
    }
    std::memcpy(&s.buf[items_at], &call.items, 4);
    Flush(s, false);
    return WantsReturn(info.f);
}

void OnReturn(u64 seq, u32 slot, u64 arg, Dynarmic::A64::Jit& jit, Core::Memory::Memory& memory) {
    State* const state = Get();
    if (state == nullptr) {
        return;
    }
    State& s = *state;
    std::scoped_lock lock{s.mutex};
    if (s.done) {
        return;
    }
    Put<u8>(s, 'R');
    Put(s, seq);
    Put(s, jit.GetRegister(0));
    u32 output = 0;
    if (arg != 0 && ReadGuest(memory, arg, &output, 4)) {
        Put<u32>(s, 4);
        Put(s, output);
    } else {
        Put<u32>(s, 0);
    }
    Flush(s, false);
}

void OnPresent(u64 present) {
    State* const state = Get();
    if (state == nullptr || state->process == nullptr) {
        return;
    }
    State& s = *state;
    std::scoped_lock lock{s.mutex};
    if (s.done) {
        return;
    }
    auto& memory = s.process->GetMemory();
    if (s.window) {
        // The pages the frame changed. They come after its calls; the replay applies them first
        // (a page rewritten between two submits of the frame keeps only its last bytes). Hashing
        // the pools at every submit made a frame take ~15 s.
        Dirty(s, memory);
    }
    Put<u8>(s, 'P');
    Put(s, present);
    Put(s, static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count()));
    if (!s.window && s.from != 0 && present == s.from) {
        s.window = true;
        Snapshot(s, memory);
    } else if (s.window && present >= s.from + s.frames) {
        Put<u8>(s, 'W');
        Put<u8>(s, 1);
        Put<u8>(s, 'E');
        Flush(s, true);
        {
            std::unique_lock qlock{s.queue_mutex};
            s.stop = true;
        }
        s.queue_cv.notify_all();
        s.writer.join();
        std::fclose(s.file);
        s.done = true;
        s.window = false;
        LOG_INFO(Core, "NVN trace: done at present {}: {} calls, {} MiB raw, {} MiB written",
                 present, s.seq, s.raw_bytes >> 20, s.file_bytes >> 20);
    }
    // In the window each frame goes to the writer at its present (see the writer's fflush)
    Flush(s, s.window);
}

} // namespace Core::NvnTrace
