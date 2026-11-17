// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "common/common_types.h"

namespace VideoCommon {

/// Whether a GPU is one AMD's FSR 4 runtime supports: Radeon RX 6000, 7000 and 9000 (and the
/// matching Radeon PRO W cards), and the RDNA 3/3.5 integrated GPUs of Ryzen and Ryzen AI
/// processors, on AMD's own Windows driver. Told apart by the Vulkan device name, which AMD's driver
/// reports as for example "AMD Radeon RX 9070 XT" or "AMD Radeon(TM) 890M Graphics". AMD's runtime
/// still decides at run time, so a wrong guess only shows or hides the option.
constexpr bool IsFsr4CapableGpu(u32 vendor_id, bool amd_driver, std::string_view name) {
    constexpr u32 amd_vendor = 0x1002;
    if (vendor_id != amd_vendor || !amd_driver) {
        return false;
    }
    const auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto digits_at = [&](size_t at) {
        size_t count = 0;
        while (at + count < name.size() && is_digit(name[at + count])) {
            ++count;
        }
        return count;
    };
    // "RX 6800 XT", "RX 7600S", "PRO W7900": a four-digit model whose first digit is the series.
    for (const std::string_view prefix : {std::string_view{"RX "}, std::string_view{"PRO W"}}) {
        for (size_t at = name.find(prefix); at != std::string_view::npos;
             at = name.find(prefix, at + 1)) {
            const size_t model = at + prefix.size();
            if (digits_at(model) == 4) {
                const char series = name[model];
                if (series == '6' || series == '7' || series == '9') {
                    return true;
                }
            }
        }
    }
    // Integrated: "780M", "890M", "8060S". RDNA 3 starts at the 700 series; RDNA 2's 680M doesn't
    // run FSR 4.
    for (size_t at = 0; at < name.size(); ++at) {
        if (!is_digit(name[at]) || (at > 0 && is_digit(name[at - 1]))) {
            continue;
        }
        const size_t count = digits_at(at);
        const size_t end = at + count;
        if ((count == 3 || count == 4) && end < name.size() &&
            (name[end] == 'M' || name[end] == 'S') && (name[at] == '7' || name[at] == '8') &&
            (end + 1 == name.size() || name[end + 1] == ' ')) {
            return true;
        }
    }
    return false;
}

/// FSR 4 through fsr4up-vk (github.com/auz466/fsr4up-vk), the community native Linux Vulkan
/// implementation of FSR 4.1.2 that the Linux packages ship, on Mesa's RADV driver. It runs AMD's
/// own 4.1.2 networks, taken from AMD's Vulkan DLL: FP8 where the GPU has FP8 cooperative matrices
/// (RDNA 4), else one of AMD's INT8 networks (RDNA 3, RDNA 2, and FSR 4 Lite on Van Gogh and Strix
/// Point). fsr4up-vk picks the network from what the GPU supports, not from what the device
/// enabled, so the device must have enabled everything that network uses.
enum class Fsr4PortModel { Int8, Fp8 };

/// What fsr4up-vk needs, as enabled on the logical device (or supported, before one exists).
struct Fsr4PortDevice {
    u32 vendor_id;
    u32 device_id;
    bool radv;
    /// Accelerated mixed-signedness 4x8-bit dot products: RDNA 3 and newer.
    bool mixed_dot4_accelerated;
    /// Vulkan 1.3.
    bool vulkan_1_3;
    /// Compute pipelines can require 32-lane subgroups (subgroupSizeControl).
    bool compute_wave32;
    /// shaderFloat16, shaderInt8, shaderStorageImageWriteWithoutFormat and
    /// shaderStorageImageExtendedFormats: what every network uses.
    bool shader_basics;
    /// shaderIntegerDotProduct: the INT8 networks.
    bool integer_dot_product;
    /// Linear compute shader derivatives: the INT8 networks' prepass.
    bool compute_derivatives;
    /// The GPU supports VK_KHR_cooperative_matrix and VK_EXT_shader_float8 with FP8 cooperative
    /// matrices, so fsr4up-vk runs its FP8 network on it.
    bool fp8_offered;
    /// What the FP8 network uses, enabled: cooperativeMatrix, shaderFloat8,
    /// shaderFloat8CooperativeMatrix, shaderInt16, storageBuffer16BitAccess, vulkanMemoryModel and
    /// variablePointers(StorageBuffer).
    bool fp8_enabled;
};

struct Fsr4PortSupport {
    std::optional<Fsr4PortModel> model;
    /// Why that model, or why none.
    std::string_view reason;
};

/// RDNA 2 GPUs and APUs by PCI device ID: Navi 21 to 24, Van Gogh, Rembrandt, Raphael and
/// Mendocino.
constexpr bool IsAmdRdna2DeviceId(u32 device_id) {
    return (device_id >= 0x73a0 && device_id <= 0x73ff) ||
           (device_id >= 0x7420 && device_id <= 0x743f) || device_id == 0x1506 ||
           device_id == 0x163f || device_id == 0x164e || device_id == 0x1681;
}

/// Whether fsr4up-vk runs on a GPU: an RDNA 2 or newer Radeon on RADV. CheckFsr4Port also checks
/// the device's features.
constexpr bool IsFsr4PortGpu(u32 vendor_id, u32 device_id, bool radv, bool mixed_dot4_accelerated) {
    constexpr u32 amd_vendor = 0x1002;
    return vendor_id == amd_vendor && radv &&
           (mixed_dot4_accelerated || IsAmdRdna2DeviceId(device_id));
}

/// The network fsr4up-vk will run on the device, or none when the device lacks something it uses.
constexpr Fsr4PortSupport CheckFsr4Port(const Fsr4PortDevice& device) {
    if (!IsFsr4PortGpu(device.vendor_id, device.device_id, device.radv,
                       device.mixed_dot4_accelerated)) {
        return {std::nullopt, "needs an RDNA 2 or newer Radeon on Mesa's RADV driver"};
    }
    if (!device.vulkan_1_3) {
        return {std::nullopt, "needs Vulkan 1.3"};
    }
    if (!device.compute_wave32) {
        return {std::nullopt, "compute shaders can't require 32-lane subgroups"};
    }
    if (!device.shader_basics) {
        return {std::nullopt,
                "no float16/int8 shader arithmetic or storage image writes without a format"};
    }
    if (device.fp8_offered) {
        // fsr4up-vk takes FP8 whenever the GPU supports it, whatever this device enabled.
        if (!device.fp8_enabled) {
            return {std::nullopt, "the GPU runs the FP8 network, but this device lacks features "
                                  "it uses (cooperative matrices, float8, 16-bit storage, the "
                                  "Vulkan memory model or variable pointers)"};
        }
        return {Fsr4PortModel::Fp8, "FP8 cooperative matrices (RDNA 4)"};
    }
    if (!device.integer_dot_product) {
        return {std::nullopt, "no integer dot products for the INT8 network"};
    }
    if (!device.compute_derivatives) {
        return {std::nullopt, "no linear compute shader derivatives for the INT8 network"};
    }
    return {Fsr4PortModel::Int8, "no FP8 cooperative matrices, so an INT8 network"};
}

} // namespace VideoCommon
