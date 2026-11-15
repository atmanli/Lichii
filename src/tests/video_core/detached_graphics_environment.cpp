// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/page_table.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"
#include "core/memory.h"
#include "shader_recompiler/environment.h"
#include "shader_recompiler/program_header.h"
#include "tests/core/system_test_context.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/gpu.h"
#include "video_core/memory_manager.h"
#include "video_core/renderer_null/null_rasterizer.h"
#include "video_core/shader_environment.h"
#include "video_core/textures/texture.h"

namespace {

struct DetachedGraphicsFixture {
    static constexpr GPUVAddr ProgramBase = 0x100000;
    static constexpr GPUVAddr CbufBase = 0x120000;
    static constexpr GPUVAddr TicBase = 0x140000;
    static constexpr size_t BackingSize = 0x80000; // 512 KiB mapped contiguous window
    static constexpr u32 ProgramOffset = 0x0;
    static constexpr u32 CbufOffset = 0x20000;
    static constexpr u32 TicOffset = 0x40000;

    ScopedSystemTestContext context;
    Core::System system;
    Core::DeviceMemory physical;
    Tegra::MaxwellDeviceMemoryManager device{physical};
    Tegra::GPU gpu{system, false, false};
    Null::RasterizerNull rasterizer{gpu};
    std::optional<Tegra::MemoryManager> memory;
    std::optional<Tegra::Engines::Maxwell3D> maxwell3d;
    std::unique_ptr<Kernel::KProcess> process;
    Core::Asid asid{};
    u8* bytes{};

    DetachedGraphicsFixture() {
        system.Initialize();
        process = std::make_unique<Kernel::KProcess>(system.Kernel());
        auto& table = process->GetPageTable().GetImpl();
        table.Resize(24, 12);
        bytes = physical.GetPointerFromRaw<u8>(0x10000);
        std::memset(bytes, 0, BackingSize);
        const u64 num_pages = BackingSize / 0x1000;
        for (u64 page = 0; page < num_pages; ++page) {
            table.entries.GetAndFault(0x10 + page)
                .Store(false, Common::PageType::Memory, 1,
                       reinterpret_cast<uintptr_t>(bytes) - 0x10000);
        }
        process->GetMemory().SetCurrentPageTable(*process);
        asid = device.RegisterProcess(&process->GetMemory());
        device.BindInterface(&rasterizer);
        device.Map(0x10000, 0x10000, BackingSize, asid);
        memory.emplace(system, device, 32, 1ULL << 31, 16);
        memory->BindRasterizer(&rasterizer);
        memory->Map(ProgramBase, 0x10000, BackingSize, Tegra::PTEKind::PITCH, true);

        // Minimal Maxwell vertex program: 80-byte ProgramHeader + sched word + EXIT.
        std::array<u64, 2> minimal_code{0ULL, 0xe30000000007000fULL};
        std::memcpy(bytes + ProgramOffset + sizeof(Shader::ProgramHeader), minimal_code.data(),
                    sizeof(minimal_code));

        maxwell3d.emplace(*memory);
        auto& regs = maxwell3d->regs;
        regs.sampler_binding = Tegra::Engines::Maxwell3D::Regs::SamplerBinding::ViaHeaderBinding;
        regs.tex_header.address_high = static_cast<u32>(TicBase >> 32);
        regs.tex_header.address_low = static_cast<u32>(TicBase);
        regs.tex_header.limit = 31;
    }

    ~DetachedGraphicsFixture() {
        maxwell3d.reset();
        memory.reset();
        device.Unmap(0x10000, BackingSize);
        device.UnregisterProcess(asid);
        system.Kernel().UnregisterKernelObject(process.get());
    }

    VideoCommon::GraphicsEnvironment MakeVertexEnvironment() {
        return VideoCommon::GraphicsEnvironment{
            *maxwell3d, *memory, Tegra::Engines::Maxwell3D::Regs::ShaderType::VertexB,
            ProgramBase, 0};
    }
};

} // namespace

TEST_CASE("Detached GraphicsEnvironment snapshots full constant buffers beyond 4096 bytes",
          "[shader_environment][async_graphics]") {
    DetachedGraphicsFixture fixture;
    constexpr u32 CbufSize = 0x8000; // 32 KiB, well above old 4096-byte inline cutoff

    auto& cbuf0 = fixture.maxwell3d->state.shader_stages[0].const_buffers[0];
    cbuf0.address = DetachedGraphicsFixture::CbufBase;
    cbuf0.size = CbufSize;
    cbuf0.enabled = true;

    constexpr u32 TailOffset = 0x6120;
    constexpr u32 SnapshotValue = 0x12345678U;
    std::memcpy(fixture.bytes + DetachedGraphicsFixture::CbufOffset + TailOffset, &SnapshotValue,
                sizeof(SnapshotValue));

    auto env = fixture.MakeVertexEnvironment();
    REQUIRE(env.DetachFromLiveState());

    // Mutate and even unmap live guest memory after detaching: detached worker reads snapshot only.
    constexpr u32 MutatedValue = 0xdeadbeefU;
    std::memcpy(fixture.bytes + DetachedGraphicsFixture::CbufOffset + TailOffset, &MutatedValue,
                sizeof(MutatedValue));
    cbuf0.enabled = false;
    cbuf0.size = 0;

    REQUIRE(env.ReadCbufValue(0, TailOffset) == SnapshotValue);
    REQUIRE(env.ReadCbufValue(0, CbufSize + 4) == 0U);
}

TEST_CASE("Detached GraphicsEnvironment snapshots texture descriptor pool independently of mutations",
          "[shader_environment][async_graphics]") {
    DetachedGraphicsFixture fixture;

    Tegra::Texture::TICEntry tic{};
    tic.format.Assign(Tegra::Texture::TextureFormat::R32G32B32A32);
    tic.r_type.Assign(Tegra::Texture::ComponentType::FLOAT);
    tic.g_type.Assign(Tegra::Texture::ComponentType::FLOAT);
    tic.b_type.Assign(Tegra::Texture::ComponentType::FLOAT);
    tic.a_type.Assign(Tegra::Texture::ComponentType::FLOAT);
    tic.texture_type.Assign(Tegra::Texture::TextureType::TextureCubemap);
    tic.normalized_coords.Assign(1);

    constexpr u32 Slot = 7;
    std::memcpy(fixture.bytes + DetachedGraphicsFixture::TicOffset +
                    Slot * sizeof(Tegra::Texture::TICEntry),
                &tic, sizeof(tic));

    auto env = fixture.MakeVertexEnvironment();
    REQUIRE(env.DetachFromLiveState());

    // Overwrite live descriptor pool memory and change registers after detach.
    Tegra::Texture::TICEntry overwritten{};
    overwritten.format.Assign(Tegra::Texture::TextureFormat::A8B8G8R8);
    overwritten.r_type.Assign(Tegra::Texture::ComponentType::UNORM);
    overwritten.g_type.Assign(Tegra::Texture::ComponentType::UNORM);
    overwritten.b_type.Assign(Tegra::Texture::ComponentType::UNORM);
    overwritten.a_type.Assign(Tegra::Texture::ComponentType::UNORM);
    overwritten.texture_type.Assign(Tegra::Texture::TextureType::Texture2D);
    std::memcpy(fixture.bytes + DetachedGraphicsFixture::TicOffset +
                    Slot * sizeof(Tegra::Texture::TICEntry),
                &overwritten, sizeof(overwritten));
    fixture.maxwell3d->regs.tex_header.limit = 0;

    REQUIRE(env.ReadTextureType(Slot) == Shader::TextureType::ColorCube);
    REQUIRE(env.ReadTexturePixelFormat(Slot) == Shader::TexturePixelFormat::R32G32B32A32_FLOAT);
}

TEST_CASE("Detached GraphicsEnvironment rejects oversized constant buffer and excessive texture pool",
          "[shader_environment][async_graphics]") {
    DetachedGraphicsFixture fixture;

    auto& cbuf0 = fixture.maxwell3d->state.shader_stages[0].const_buffers[0];
    cbuf0.address = DetachedGraphicsFixture::CbufBase;
    cbuf0.size = static_cast<u32>(Tegra::Engines::Maxwell3D::Regs::MaxConstBufferSize + 16);
    cbuf0.enabled = true;

    auto env_big_cbuf = fixture.MakeVertexEnvironment();
    CHECK_FALSE(env_big_cbuf.DetachFromLiveState());

    // Restore valid cbuf, but set texture header limit above MaxFlushedTexturePoolBytes.
    cbuf0.size = 0x1000;
    fixture.maxwell3d->regs.tex_header.limit = 0x40000;
    auto env_big_pool = fixture.MakeVertexEnvironment();
    CHECK_FALSE(env_big_pool.DetachFromLiveState());
}
