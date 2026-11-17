// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"

namespace FrontendCommon {

enum ModInstallResult {
    Cancelled,
    Failed,
    Success,
};

struct ModInstallOptions {
    /// Leave the source where it is. False removes it once the mod is installed.
    bool copy = true;
    /// Replace a mod already installed under the same name. False leaves that mod as it is, and
    /// InstallMod returns Cancelled: ask the player first, then call again with true.
    bool replace_existing = false;
    /// The name to install under. Empty takes the source folder's name.
    std::string name;
};

std::vector<std::filesystem::path> GetModFolder(const std::string& root);

/// The folder that holds a game's mods, one folder per mod.
[[nodiscard]] std::filesystem::path GetModsDir(u64 program_id);

/// Whether `name` can be the name of a mod's folder: joined to the folder it goes in, it names a
/// folder inside it. On Windows a colon (a drive) and a last character of "." or " " (which
/// Windows drops, so "..." names the folder itself) are refused as well.
[[nodiscard]] bool IsModName(std::string_view name);

/// The name InstallMod installs `path` under with `options`.
[[nodiscard]] std::string InstalledModName(const std::filesystem::path& path,
                                           const ModInstallOptions& options = {});

/// Whether a mod of that name is installed for the game.
[[nodiscard]] bool IsModInstalled(u64 program_id, std::string_view name);

/// Installs the mod folder `path` for the game. The copy is made next to the mods first and
/// swapped in when it is complete, so a copy that fails leaves an installed mod of the same name
/// untouched.
ModInstallResult InstallMod(const std::filesystem::path& path, u64 program_id,
                            const ModInstallOptions& options = {});

} // namespace FrontendCommon
