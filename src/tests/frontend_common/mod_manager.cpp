// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "frontend_common/mod_manager.h"

namespace {

namespace fs = std::filesystem;

constexpr u64 ProgramId = 0x0100000000010000;

// A load folder of its own, and a folder for the mods a player would import.
class ModTestDirs {
public:
    ModTestDirs() : previous{Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir)} {
        fs::create_directories(root / "load");
        fs::create_directories(root / "downloads");
        Common::FS::SetEdenPath(Common::FS::EdenPath::LoadDir, root / "load");
    }

    ~ModTestDirs() {
        Common::FS::SetEdenPath(Common::FS::EdenPath::LoadDir, previous);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    ModTestDirs(const ModTestDirs&) = delete;
    ModTestDirs& operator=(const ModTestDirs&) = delete;

    // Writes one file of a mod to import and returns the mod's folder.
    fs::path Source(const std::string& mod, const std::string& file,
                    const std::string& text) const {
        Write(root / "downloads" / mod / file, text);
        return root / "downloads" / mod;
    }

    static fs::path Installed(const std::string& mod) {
        return FrontendCommon::GetModsDir(ProgramId) / mod;
    }

    static void Write(const fs::path& path, const std::string& text) {
        fs::create_directories(path.parent_path());
        std::ofstream file{path, std::ios::binary};
        file << text;
    }

    static std::string Read(const fs::path& path) {
        std::ifstream file{path, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    }

    const fs::path root =
        fs::temp_directory_path() /
        ("mod-manager-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

private:
    const fs::path previous;
};

} // Anonymous namespace

TEST_CASE("A mod is copied into the game's mod folder", "[mod_manager]") {
    ModTestDirs dirs;
    const fs::path source = dirs.Source("Cool Mod", "romfs/a.txt", "new");

    CHECK(FrontendCommon::GetModsDir(ProgramId) == dirs.root / "load" / "0100000000010000");
    REQUIRE_FALSE(FrontendCommon::IsModInstalled(ProgramId, "Cool Mod"));
    REQUIRE(FrontendCommon::InstallMod(source, ProgramId) == FrontendCommon::Success);

    CHECK(FrontendCommon::IsModInstalled(ProgramId, "Cool Mod"));
    CHECK(ModTestDirs::Read(ModTestDirs::Installed("Cool Mod") / "romfs" / "a.txt") == "new");
    // Copied, not moved.
    CHECK(ModTestDirs::Read(source / "romfs" / "a.txt") == "new");
    // Nothing is left of the folder the copy was staged in.
    CHECK_FALSE(fs::exists(dirs.root / "load" / ".install-staging"));
}

TEST_CASE("A mod of the same name is replaced only when told to", "[mod_manager]") {
    ModTestDirs dirs;
    const fs::path installed = ModTestDirs::Installed("Cool Mod");
    REQUIRE(FrontendCommon::InstallMod(dirs.Source("Cool Mod", "romfs/a.txt", "old"),
                                       ProgramId) == FrontendCommon::Success);
    // A newer download of the mod, with another file.
    fs::remove_all(dirs.root / "downloads");
    const fs::path newer = dirs.Source("Cool Mod", "romfs/b.txt", "new");

    SECTION("Without a yes the installed mod stays as it is") {
        CHECK(FrontendCommon::InstallMod(newer, ProgramId) == FrontendCommon::Cancelled);
        CHECK(ModTestDirs::Read(installed / "romfs" / "a.txt") == "old");
        CHECK_FALSE(fs::exists(installed / "romfs" / "b.txt"));
    }

    SECTION("With a yes it is replaced as a whole, not merged") {
        CHECK(FrontendCommon::InstallMod(newer, ProgramId, {.replace_existing = true}) ==
              FrontendCommon::Success);
        CHECK_FALSE(fs::exists(installed / "romfs" / "a.txt"));
        CHECK(ModTestDirs::Read(installed / "romfs" / "b.txt") == "new");
        CHECK_FALSE(fs::exists(dirs.root / "load" / ".install-staging"));
    }

    SECTION("A copy that fails leaves the installed mod alone") {
        // The folder to import is gone by the time it is copied.
        const fs::path gone = dirs.root / "downloads" / "gone" / "Cool Mod";
        CHECK(FrontendCommon::InstallMod(gone, ProgramId, {.replace_existing = true}) ==
              FrontendCommon::Failed);
        CHECK(ModTestDirs::Read(installed / "romfs" / "a.txt") == "old");
        CHECK_FALSE(fs::exists(dirs.root / "load" / ".install-staging"));
    }

    SECTION("Installing a mod from its own folder changes nothing") {
        CHECK(FrontendCommon::InstallMod(installed, ProgramId, {.copy = false}) ==
              FrontendCommon::Success);
        CHECK(ModTestDirs::Read(installed / "romfs" / "a.txt") == "old");
    }
}

TEST_CASE("An install clears only what it may", "[mod_manager]") {
    ModTestDirs dirs;
    const fs::path load = dirs.root / "load";
    const fs::path staging = load / ".install-staging";

    SECTION("Files an earlier install left in the staging folder are not installed") {
        ModTestDirs::Write(staging / "new" / "romfs" / "stale.txt", "stale");
        REQUIRE(FrontendCommon::InstallMod(dirs.Source("Cool Mod", "romfs/a.txt", "new"),
                                           ProgramId) == FrontendCommon::Success);
        CHECK(ModTestDirs::Read(ModTestDirs::Installed("Cool Mod") / "romfs" / "a.txt") == "new");
        CHECK_FALSE(fs::exists(ModTestDirs::Installed("Cool Mod") / "romfs" / "stale.txt"));
        CHECK_FALSE(fs::exists(staging));
    }

    SECTION("A mod an interrupted install moved aside is kept") {
        // Cut off between moving the installed mod aside and putting the new one in its place,
        // an install leaves the only copy of the installed mod in the staging folder.
        ModTestDirs::Write(staging / "replaced" / "romfs" / "a.txt", "old");
        REQUIRE(FrontendCommon::InstallMod(dirs.Source("Cool Mod", "romfs/b.txt", "new"),
                                           ProgramId) == FrontendCommon::Success);
        CHECK(ModTestDirs::Read(ModTestDirs::Installed("Cool Mod") / "romfs" / "b.txt") == "new");
        CHECK_FALSE(fs::exists(staging));

        int kept = 0;
        for (const auto& entry : fs::directory_iterator(load)) {
            if (entry.path().filename().string().starts_with(".install-kept-")) {
                ++kept;
                CHECK(ModTestDirs::Read(entry.path() / "replaced" / "romfs" / "a.txt") == "old");
            }
        }
        CHECK(kept == 1);
    }
}

TEST_CASE("A mod is installed under the name it is given", "[mod_manager]") {
    ModTestDirs dirs;
    const fs::path source = dirs.Source("folder", "exefs/x.pchtxt", "patch");

    CHECK(FrontendCommon::InstalledModName(fs::path{"a"} / "b" / "Cool Mod") == "Cool Mod");
    CHECK(FrontendCommon::InstalledModName(fs::path{"a"} / "b" / "Cool Mod" / "") == "Cool Mod");
    CHECK(FrontendCommon::InstalledModName(source, {.name = "Better"}) == "Better");

    SECTION("A given name wins over the folder's") {
        REQUIRE(FrontendCommon::InstallMod(source, ProgramId, {.name = "Better Name"}) ==
                FrontendCommon::Success);
        CHECK(ModTestDirs::Read(ModTestDirs::Installed("Better Name") / "exefs" / "x.pchtxt") ==
              "patch");
        CHECK_FALSE(fs::exists(ModTestDirs::Installed("folder")));
    }

    SECTION("A name that is not the name of one folder is refused") {
        for (const char* name : {"..", ".", "a/b", "a\\b"}) {
            CAPTURE(name);
            CHECK(FrontendCommon::InstallMod(source, ProgramId, {.name = name}) ==
                  FrontendCommon::Failed);
            CHECK_FALSE(FrontendCommon::IsModInstalled(ProgramId, name));
        }
        // Nothing was written for the game, or beside its folder.
        CHECK_FALSE(fs::exists(FrontendCommon::GetModsDir(ProgramId)));
        CHECK_FALSE(fs::exists(dirs.root / "load" / "b"));
    }

    SECTION("A move removes the folder it was imported from") {
        REQUIRE(FrontendCommon::InstallMod(source, ProgramId, {.copy = false}) ==
                FrontendCommon::Success);
        CHECK_FALSE(fs::exists(source));
        CHECK(fs::exists(ModTestDirs::Installed("folder") / "exefs" / "x.pchtxt"));
    }
}

TEST_CASE("A mod's name names one folder in the folder it goes in", "[mod_manager]") {
    for (const char* name : {"Cool Mod", ".hidden", "Mod v1.2"}) {
        CAPTURE(name);
        CHECK(FrontendCommon::IsModName(name));
    }
    for (const char* name : {"", ".", "..", "a/b", "a\\b", "/", "\\"}) {
        CAPTURE(name);
        CHECK_FALSE(FrontendCommon::IsModName(name));
    }
#ifdef _WIN32
    // Joined to a folder on drive C:, "D:" and "D:." are drive D's current folder, and "C:" is the
    // folder itself. Windows drops the periods and spaces a name ends with, so "...", " " and ". ."
    // name the folder itself too, and "Mod." names "Mod".
    for (const char* name : {"D:", "D:.", "C:", "a:b", "...", " ", ". .", "Mod.", "Mod "}) {
        CAPTURE(name);
        CHECK_FALSE(FrontendCommon::IsModName(name));
    }
#else
    // Elsewhere these are names like any other.
    for (const char* name : {"D:", "a:b", "Mod.", "Mod "}) {
        CAPTURE(name);
        CHECK(FrontendCommon::IsModName(name));
    }
#endif
}
