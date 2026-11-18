#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Builds fsr4up-vk (github.com/auz466/fsr4up-vk, MIT), the community native Linux Vulkan
# implementation of FSR 4.1.2 upscaling the x86-64 Linux packages ship. It is built from AMD's
# signed FSR 4.1.2 Vulkan upscaler DLL of FidelityFX SDK 2.4.0, which the SDK's license.md lists
# under MIT; the build reads AMD's shaders and initializer data out of it (it never runs it). The
# DLL, the SDK licence, the fsr4up-vk commit and the Vulkan headers it compiles against are pinned.
#
# Usage: tools/fsr4up-vk/build.sh <work dir> <output dir>
# The work dir holds the checkouts, the DLL and the build, and may be cached between runs.
# The output dir receives amd_fidelityfx_upscaler_vk.so and licenses/.
# Needs git, curl, cmake, ninja, python3 and a C++20 compiler.

set -euo pipefail

FSR4UP_REPO=https://github.com/auz466/fsr4up-vk.git
FSR4UP_COMMIT=bef010431d775de1d07e1145d387798ecf84c47b
# FidelityFX SDK v2.4.0.
SDK_URL=https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/7539ae8b6568ad29ed78a753404351daf3b5cca7
DLL_ENTRY=Kits/FidelityFX/signedbin/amd_fidelityfx_upscaler_vk.dll
DLL_SHA256=4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7
LICENSE_ENTRY=Kits/FidelityFX/docs/license.md
LICENSE_SHA256=32357259b965d6a8001e9e508ba60eda6034027c5923a7d226ffbaff813c2bfb
# The Vulkan-Headers release the emulator builds with (cpmfile.json); fsr4up-vk needs
# VK_EXT_shader_float8, which older system headers lack.
VULKAN_HEADERS_REPO=https://github.com/KhronosGroup/Vulkan-Headers.git
VULKAN_HEADERS_COMMIT=74d8a6cb930c68ef617b202c3ff3c59d919e086b # v1.4.345

if [ $# -ne 2 ]; then
    echo "usage: $0 <work dir> <output dir>" >&2
    exit 2
fi
mkdir -p "$1" "$2"
work=$(cd "$1" && pwd)
out=$(cd "$2" && pwd)

fetch() {
    local entry=$1 sha256=$2 file=$3
    if [ ! -f "$file" ] || ! echo "$sha256  $file" | sha256sum --check --status; then
        curl -fsSL --retry 3 -o "$file.part" "$SDK_URL/$entry"
        echo "$sha256  $file.part" | sha256sum --check --quiet
        mv "$file.part" "$file"
    fi
}
fetch "$DLL_ENTRY" "$DLL_SHA256" "$work/amd_fidelityfx_upscaler_vk.dll"
fetch "$LICENSE_ENTRY" "$LICENSE_SHA256" "$work/amd-fidelityfx-sdk-license.md"

checkout() {
    local repo=$1 commit=$2 dir=$3
    if [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" != "$commit" ]; then
        rm -rf "$dir"
        git init -q "$dir"
        git -C "$dir" fetch -q --depth 1 "$repo" "$commit"
        git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
    fi
}
src="$work/fsr4up-vk"
headers="$work/Vulkan-Headers"
checkout "$FSR4UP_REPO" "$FSR4UP_COMMIT" "$src"
checkout "$VULKAN_HEADERS_REPO" "$VULKAN_HEADERS_COMMIT" "$headers"

# Only the library: the test harnesses need AMD's DLL under Wine.
build="$work/build"
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-isystem $headers/include" \
    -DVULKAN_HEADERS_DIR="$headers/include" \
    -DFSR4UP_AMD_DLL="$work/amd_fidelityfx_upscaler_vk.dll" \
    -DFSR4UP_BUILD_HARNESS=OFF -DFSR4UP_BUILD_HARNESS_EXE=OFF
cmake --build "$build" --target fsr4up --parallel "${FSR4UP_BUILD_JOBS:-$(nproc)}"

library="$build/amd_fidelityfx_upscaler_vk.so"
for symbol in ffxCreateContext ffxDestroyContext ffxConfigure ffxQuery ffxDispatch; do
    if ! nm -D --defined-only "$library" | grep -qw "$symbol"; then
        echo "$library lacks $symbol" >&2
        exit 1
    fi
done
install -m 644 "$library" "$out/"
rm -rf "$out/licenses"
mkdir -p "$out/licenses/fsr4up-vk"
cp "$src/LICENSE" "$src/THIRD_PARTY_NOTICES.md" "$out/licenses/fsr4up-vk/"
cp "$work/amd-fidelityfx-sdk-license.md" "$out/licenses/fsr4up-vk/AMD-FidelityFX-SDK-license.md"
