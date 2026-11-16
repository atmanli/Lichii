// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "common/common_types.h"

namespace InputCommon::SteamHaptics {

inline constexpr u16 UsbProductId = 0x1302;
inline constexpr u16 BluetoothProductId = 0x1303;
inline constexpr u16 PuckProductId = 0x1304;
inline constexpr u16 InternalReceiverProductId = 0x1305;
inline constexpr u8 UsbPcmFormat = 8;
inline constexpr u8 WirelessPcmFormat = 9;
inline constexpr u32 UsbSampleRate = 8000;
inline constexpr u32 WirelessSampleRate = 4000;
inline constexpr std::size_t SamplesPerGrip = 31;
using Report = std::array<u8, 2 + 2 * SamplesPerGrip>;
static_assert(Report{}.size() == 64);

constexpr bool IsSupportedProduct(u16 product_id) {
    return product_id == UsbProductId || product_id == BluetoothProductId ||
           product_id == PuckProductId || product_id == InternalReceiverProductId;
}

constexpr u8 FormatForProduct(u16 product_id) {
    return product_id == UsbProductId ? UsbPcmFormat : WirelessPcmFormat;
}

constexpr u32 SampleRateForProduct(u16 product_id) {
    return product_id == UsbProductId ? UsbSampleRate : WirelessSampleRate;
}

constexpr bool CompleteReportWrite(int written, std::size_t size) {
    return written >= static_cast<int>(size);
}

struct Target {
    f32 low_amplitude{};
    f32 low_frequency{};
    f32 high_amplitude{};
    f32 high_frequency{};
};

inline f32 Amplitude(f32 value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

inline Target Sanitize(Target target) {
    target.low_amplitude = Amplitude(target.low_amplitude);
    target.high_amplitude = Amplitude(target.high_amplitude);
    const auto frequency = [](f32 value) {
        return std::isfinite(value) && value > 0.0f ? std::min(value, 1253.0f) : 0.0f;
    };
    target.low_frequency = frequency(target.low_frequency);
    target.high_frequency = frequency(target.high_frequency);
    if (target.low_frequency == 0.0f) {
        target.low_amplitude = 0.0f;
    }
    if (target.high_frequency == 0.0f) {
        target.high_amplitude = 0.0f;
    }
    return target;
}

inline bool Active(const Target& target) {
    return target.low_amplitude > 0.0f || target.high_amplitude > 0.0f;
}

// G.711 mu-law, with the standard 132 bias and 32635 linear PCM clip point.
inline u8 EncodeMuLaw(f32 sample) {
    const int pcm = static_cast<int>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
    const int magnitude = std::min(std::abs(pcm), 32635) + 132;
    int exponent = 7;
    for (int mask = 0x4000; exponent > 0 && (magnitude & mask) == 0; mask >>= 1) {
        --exponent;
    }
    const int mantissa = (magnitude >> (exponent + 3)) & 0xf;
    return static_cast<u8>(~((pcm < 0 ? 0x80 : 0) | (exponent << 4) | mantissa));
}

class GripPcm {
public:
    explicit GripPcm(u32 sample_rate)
        : rate{sample_rate}, smoothing{1.0f - std::exp(-1.0f / (static_cast<f32>(rate) * 0.001f))} {
    }

    Report Build(const std::array<Target, 2>& targets) {
        Report report{};
        report[0] = 0x88;
        report[1] = static_cast<u8>(SamplesPerGrip);
        for (std::size_t side = 0; side < 2; ++side) {
            const Target target = Sanitize(targets[side]);
            // A perceptual amplitude curve, shared by both bands; no pitch-dependent EQ or
            // onset envelope. Normalize the sum to retain headroom for simultaneous tones.
            f32 low = std::sqrt(target.low_amplitude);
            f32 high = std::sqrt(target.high_amplitude);
            const f32 scale = 0.8f / std::max(1.0f, low + high);
            low *= scale;
            high *= scale;
            auto& voice = voices[side];
            for (std::size_t sample = 0; sample < SamplesPerGrip; ++sample) {
                voice.low += (low - voice.low) * smoothing;
                voice.high += (high - voice.high) * smoothing;
                const f64 value =
                    voice.low * std::sin(voice.low_phase) + voice.high * std::sin(voice.high_phase);
                report[2 + side * SamplesPerGrip + sample] = EncodeMuLaw(static_cast<f32>(value));
                Advance(voice.low_phase, target.low_frequency);
                Advance(voice.high_phase, target.high_frequency);
            }
        }
        return report;
    }

private:
    void Advance(f64& phase, f32 frequency) const {
        constexpr f64 turn = 2.0 * std::numbers::pi;
        phase += turn * static_cast<f64>(frequency) / static_cast<f64>(rate);
        if (phase >= turn) {
            phase -= turn;
        }
    }

    struct Voice {
        f64 low_phase{};
        f64 high_phase{};
        f32 low{};
        f32 high{};
    };
    u32 rate;
    f32 smoothing;
    std::array<Voice, 2> voices{};
};

} // namespace InputCommon::SteamHaptics
