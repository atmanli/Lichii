// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include "video_core/fsr4_support.h"

TEST_CASE("FSR 4 is offered on RDNA 2 and newer Radeons on AMD's driver", "[video_core][fsr4]") {
    using VideoCommon::IsFsr4CapableGpu;
    constexpr u32 amd = 0x1002;
    for (const char* name :
         {"AMD Radeon RX 9070 XT", "AMD Radeon RX 9060 XT", "AMD Radeon RX 7900 XTX",
          "AMD Radeon RX 7600S", "AMD Radeon RX 6800 XT", "AMD Radeon RX 6700M",
          "AMD Radeon PRO W7900", "AMD Radeon(TM) 890M Graphics", "AMD Radeon(TM) 880M",
          "AMD Radeon(TM) 8060S Graphics", "AMD Radeon 780M Graphics"}) {
        INFO(name);
        CHECK(IsFsr4CapableGpu(amd, true, name));
    }
    for (const char* name :
         {"AMD Radeon RX 5700 XT", "AMD Radeon RX 580", "AMD Radeon RX Vega", "AMD Radeon(TM) 680M",
          "AMD Radeon(TM) Graphics", "AMD Radeon VII", "AMD Radeon RX 590 GME"}) {
        INFO(name);
        CHECK_FALSE(IsFsr4CapableGpu(amd, true, name));
    }
    CHECK_FALSE(IsFsr4CapableGpu(amd, false, "AMD Radeon RX 9070 XT (RADV GFX1201)"));
    CHECK_FALSE(IsFsr4CapableGpu(0x10DE, true, "NVIDIA GeForce RTX 3080"));
    CHECK_FALSE(IsFsr4CapableGpu(0x8086, true, "Intel(R) Arc(TM) A770 Graphics"));
}

TEST_CASE("The fsr4up-vk FSR 4 port runs on RDNA 2 and newer Radeons on RADV with its features",
          "[video_core][fsr4]") {
    using VideoCommon::CheckFsr4Port;
    using VideoCommon::Fsr4PortDevice;
    using VideoCommon::Fsr4PortModel;
    constexpr Fsr4PortDevice rdna3{
        .vendor_id = 0x1002,
        .device_id = 0x744c, // Navi 31
        .radv = true,
        .mixed_dot4_accelerated = true,
        .vulkan_1_3 = true,
        .compute_wave32 = true,
        .shader_basics = true,
        .integer_dot_product = true,
        .compute_derivatives = true,
        .fp8_offered = false,
        .fp8_enabled = false,
    };
    CHECK(CheckFsr4Port(rdna3).model == Fsr4PortModel::Int8);

    Fsr4PortDevice rdna4 = rdna3;
    rdna4.device_id = 0x7550; // Navi 48
    rdna4.fp8_offered = true;
    rdna4.fp8_enabled = true;
    CHECK(CheckFsr4Port(rdna4).model == Fsr4PortModel::Fp8);

    // fsr4up-vk picks FP8 by what the GPU supports: missing FP8 features on the device can't fall
    // back to INT8.
    Fsr4PortDevice rdna4_no_fp8 = rdna4;
    rdna4_no_fp8.fp8_enabled = false;
    CHECK_FALSE(CheckFsr4Port(rdna4_no_fp8).model);
    CHECK_FALSE(CheckFsr4Port(rdna4_no_fp8).reason.empty());

    // The FP8 network needs neither integer dot products nor compute derivatives.
    Fsr4PortDevice rdna4_fp8_only = rdna4;
    rdna4_fp8_only.integer_dot_product = false;
    rdna4_fp8_only.compute_derivatives = false;
    CHECK(CheckFsr4Port(rdna4_fp8_only).model == Fsr4PortModel::Fp8);

    Fsr4PortDevice rdna2 = rdna3;
    rdna2.device_id = 0x73bf; // Navi 21
    rdna2.mixed_dot4_accelerated = false;
    CHECK(CheckFsr4Port(rdna2).model == Fsr4PortModel::Int8);
    rdna2.device_id = 0x1681; // Rembrandt
    CHECK(CheckFsr4Port(rdna2).model == Fsr4PortModel::Int8);
    rdna2.device_id = 0x163f; // Van Gogh
    CHECK(CheckFsr4Port(rdna2).model == Fsr4PortModel::Int8);

    Fsr4PortDevice rdna1 = rdna2;
    rdna1.device_id = 0x731f; // Navi 10
    CHECK_FALSE(CheckFsr4Port(rdna1).model);

    Fsr4PortDevice amdvlk = rdna4;
    amdvlk.radv = false;
    CHECK_FALSE(CheckFsr4Port(amdvlk).model);

    Fsr4PortDevice nvidia = rdna4;
    nvidia.vendor_id = 0x10DE;
    CHECK_FALSE(CheckFsr4Port(nvidia).model);

    for (const Fsr4PortDevice& device : {rdna3, rdna4}) {
        for (bool Fsr4PortDevice::* feature :
             {&Fsr4PortDevice::vulkan_1_3, &Fsr4PortDevice::compute_wave32,
              &Fsr4PortDevice::shader_basics}) {
            Fsr4PortDevice missing = device;
            missing.*feature = false;
            const auto support = CheckFsr4Port(missing);
            CHECK_FALSE(support.model);
            CHECK_FALSE(support.reason.empty());
        }
    }
    for (bool Fsr4PortDevice::* feature :
         {&Fsr4PortDevice::integer_dot_product, &Fsr4PortDevice::compute_derivatives}) {
        Fsr4PortDevice missing = rdna3;
        missing.*feature = false;
        const auto support = CheckFsr4Port(missing);
        CHECK_FALSE(support.model);
        CHECK_FALSE(support.reason.empty());
    }
}
