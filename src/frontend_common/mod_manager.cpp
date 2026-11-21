// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <fmt/format.h>
#include "common/fs/fs.h"
#include "common/fs/fs_types.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "mod_manager.h"

namespace FrontendCommon {

namespace fs = std::filesystem;

namespace {

// Where InstallMod copies a mod before it swaps it in: next to the games' mod folders and not in
// one of them, so the loader never takes it for a mod. The name is as long as a program id, so a
// path that fits here fits in the game's folder too.
constexpr std::string_view STAGING_DIR = ".install-staging";

fs::path Utf8Path(std::string_view text) {
    return fs::path{Common::FS::ToU8String(text)};
}

// A folder beside the staging folder that no install clears, for what is left of a mod an install
// could not finish.
fs::path KeptDir(const fs::path& load_dir) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return load_dir / fmt::format(".install-kept-{}", now.count());
}

} // Anonymous namespace

// TODO: Handle cases where the folder appears to contain multiple mods.
std::vector<std::filesystem::path> GetModFolder(const std::string& root) {
    std::vector<std::filesystem::path> paths;

    auto callback = [&paths](const std::filesystem::directory_entry& entry) -> bool {
        const auto name = entry.path().filename().string();
        static const std::array<std::string, 5> valid_names = {"exefs",
                                                               "romfs",
                                                               "romfs_ext",
                                                               "cheats",
                                                               "romfslite"};

        if (std::ranges::find(valid_names, name) != valid_names.end()) {
            const auto parent = entry.path().parent_path();
            if (std::ranges::find(paths, parent) == paths.end()) {
                paths.emplace_back(parent);
            }
        }

        return true;
    };

    Common::FS::IterateDirEntriesRecursively(root, callback, Common::FS::DirEntryFilter::Directory);

    return paths;
}

fs::path GetModsDir(u64 program_id) {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
           fmt::format("{:016X}", program_id);
}

bool IsModName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string_view::npos) {
        return false;
    }
#ifdef _WIN32
    // A folder joined with "D:" is replaced by drive D's current folder, and Windows drops the
    // periods and spaces a name ends with.
    if (name.find(':') != std::string_view::npos || name.back() == '.' || name.back() == ' ') {
        return false;
    }
#endif
    return true;
}

std::string InstalledModName(const fs::path& path, const ModInstallOptions& options) {
    if (!options.name.empty()) {
        return options.name;
    }
    // A path that ends in a separator has an empty last part.
    const fs::path name = path.has_filename() ? path.filename() : path.parent_path().filename();
    return Common::FS::PathToUTF8String(name);
}

bool IsModInstalled(u64 program_id, std::string_view name) {
    std::error_code ec;
    return IsModName(name) && fs::exists(GetModsDir(program_id) / Utf8Path(name), ec);
}

ModInstallResult InstallMod(const fs::path& path, u64 program_id,
                            const ModInstallOptions& options) {
    const std::string name = InstalledModName(path, options);
    if (!IsModName(name)) {
        LOG_ERROR(Frontend, "Mod install failed: \"{}\" cannot be the name of a mod's folder",
                  name);
        return Failed;
    }

    const fs::path mods_dir = GetModsDir(program_id);
    const fs::path mod_dir = mods_dir / Utf8Path(name);
    std::error_code ec;
    if (fs::equivalent(path, mod_dir, ec)) {
        LOG_INFO(Frontend, "Mod {} is installed from its own folder: nothing to do", name);
        return Success;
    }
    const bool installed = fs::exists(mod_dir, ec);
    if (installed && !options.replace_existing) {
        LOG_INFO(Frontend, "Mod {} is already installed and was left as it is", name);
        return Cancelled;
    }

    const fs::path load_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir);
    const fs::path staging = load_dir / Utf8Path(STAGING_DIR);
    const fs::path staged = staging / "new";
    const fs::path replaced = staging / "replaced";
    if (fs::exists(replaced, ec)) {
        // An install that was cut off (the emulator closed or crashed) moved a mod here and did
        // not put it back: it may be the only copy left, so it is kept, not cleared.
        const fs::path kept = KeptDir(load_dir);
        fs::rename(staging, kept, ec);
        if (ec) {
            LOG_ERROR(Frontend,
                      "Mod install failed: {} holds a mod an earlier install did not put back, "
                      "and it could not be moved aside: {}",
                      Common::FS::PathToUTF8String(staging), ec.message());
            return Failed;
        }
        LOG_WARNING(Frontend, "A mod an earlier install did not put back was kept in {}",
                    Common::FS::PathToUTF8String(kept));
    }
    // Anything else left there would be copied in with this mod.
    fs::remove_all(staging, ec);
    if (ec) {
        LOG_ERROR(Frontend, "Mod install failed: {} could not be cleared: {}",
                  Common::FS::PathToUTF8String(staging), ec.message());
        return Failed;
    }

    // Copy first. Until the copy is complete, a mod installed under this name is not touched.
    try {
        fs::create_directories(staging);
        fs::copy(path, staged, fs::copy_options::recursive);
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Mod install failed with message {}", e.what());
        fs::remove_all(staging, ec);
        return Failed;
    }

    if (installed) {
        // Moved aside as a whole, which fails as a whole while the game has one of its files
        // open, instead of deleting half of it.
        fs::rename(mod_dir, replaced, ec);
        if (ec == std::errc::cross_device_link) {
            // The game's mod folder is on another volume (a linked folder): it cannot step aside.
            fs::remove_all(mod_dir, ec);
        }
        if (ec) {
            LOG_ERROR(Frontend, "Mod install failed: the installed {} could not be replaced: {}",
                      name, ec.message());
            fs::remove_all(staging, ec);
            return Failed;
        }
    }

    fs::create_directories(mods_dir, ec);
    fs::rename(staged, mod_dir, ec);
    if (ec) {
        // The game's mod folder is on another volume, or something holds one of the fresh files
        // (a virus scanner): the complete copy is copied instead of moved.
        fs::remove_all(mod_dir, ec);
        fs::copy(staged, mod_dir, fs::copy_options::recursive, ec);
    }
    if (ec) {
        LOG_ERROR(Frontend, "Mod install failed: {} could not be put in place: {}", name,
                  ec.message());
        fs::remove_all(mod_dir, ec);
        bool nothing_lost = !installed;
        if (fs::exists(replaced, ec)) {
            // The mod that was installed goes back.
            fs::rename(replaced, mod_dir, ec);
            nothing_lost = !ec;
        }
        if (nothing_lost) {
            fs::remove_all(staging, ec);
            return Failed;
        }
        // The installed mod is gone or could not go back: what the staging folder holds is all
        // that is left, so it is kept under a name the next install does not clear.
        const fs::path kept = KeptDir(load_dir);
        fs::rename(staging, kept, ec);
        LOG_ERROR(Frontend, "What is left of mod {} is in {}", name,
                  Common::FS::PathToUTF8String(ec ? staging : kept));
        return Failed;
    }

    fs::remove_all(staging, ec);
    if (!options.copy) {
        fs::remove_all(path, ec);
    }

    LOG_INFO(Frontend, "Installed mod {} from {} to {}", name, Common::FS::PathToUTF8String(path),
             Common::FS::PathToUTF8String(mod_dir));

    return Success;
}

} // namespace FrontendCommon
