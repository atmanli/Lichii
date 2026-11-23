// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "common/swap.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nup_container.h"
#include "core/file_sys/nup_convert.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/file_sys/vfs/vfs_vector.h"

namespace {

namespace fs = std::filesystem;
using FileSys::NupContainer;
using FileSys::VirtualDir;
using FileSys::VirtualFile;

constexpr u64 ProgramId = 0x0100ABCD00000000ULL;
constexpr u64 AddOnId = 0x0100ABCD00001001ULL;

struct TempDir {
    fs::path path = fs::temp_directory_path() /
                    ("nup-convert-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDir() {
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::vector<u8> Bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

VirtualFile File(const std::string& name, std::string_view content) {
    return std::make_shared<FileSys::VectorVfsFile>(Bytes(content), name);
}

VirtualDir Dir(const std::string& name, std::vector<VirtualFile> files,
               std::vector<VirtualDir> dirs = {}) {
    return std::make_shared<FileSys::VectorVfsDirectory>(std::move(files), std::move(dirs), name);
}

std::string Npdm(u64 program_id) {
    std::string npdm(0x100, '\0');
    npdm.replace(0x80, 4, "ACI0");
    for (int i = 0; i < 8; ++i) {
        npdm[0x90 + i] = static_cast<char>(program_id >> (8 * i));
    }
    return npdm;
}

VirtualFile Nacp(const std::string& name, const std::string& developer,
                 const std::string& version) {
    std::vector<u8> nacp(sizeof(FileSys::RawNACP));
    std::copy(name.begin(), name.end(), nacp.begin());
    std::copy(developer.begin(), developer.end(), nacp.begin() + 0x200);
    std::copy(version.begin(), version.end(),
              nacp.begin() + offsetof(FileSys::RawNACP, version_string));
    return std::make_shared<FileSys::VectorVfsFile>(std::move(nacp), "control.nacp");
}

FileSys::Nup::GameSource MakeSource() {
    FileSys::Nup::GameSource source;
    const auto exefs = [](std::string_view main) {
        return Dir("exefs", {File("main", main), File("main.npdm", Npdm(ProgramId))});
    };
    FileSys::Nup::SourcePackage base{.kind = FileSys::Nup::SourceKind::Base,
                                     .title_id = ProgramId};
    base.exefs = exefs("base main");
    base.romfs = Dir("romfs", {File("a.bin", "base a"), File("same.bin", "unchanged")},
                     {Dir("data", {File("x.txt", "x")}), Dir("empty", {})});
    base.nacp = Nacp("Fixture Game", "Lichii Fixtures", "1.0.0");
    base.icon = Bytes("\xFF\xD8\xFF\xE0 icon");
    // The update's tree is the base with the update applied.
    FileSys::Nup::SourcePackage update{.kind = FileSys::Nup::SourceKind::Update,
                                       .title_id = FileSys::GetUpdateTitleID(ProgramId),
                                       .version = 65536};
    update.exefs = exefs("update main");
    update.romfs = Dir("romfs",
                       {File("a.bin", "update a"), File("same.bin", "unchanged"),
                        File("new.bin", "new")},
                       {Dir("data", {File("x.txt", "x")})});
    update.nacp = Nacp("Fixture Game", "Lichii Fixtures", "1.1.0");
    FileSys::Nup::SourcePackage addon{.kind = FileSys::Nup::SourceKind::AddOn,
                                      .title_id = AddOnId,
                                      .version = 1};
    addon.romfs = Dir("romfs", {File("dlc.bin", "dlc")});
    for (auto* package : {&base, &update, &addon}) {
        source.packages.push_back(std::move(*package));
    }
    return source;
}

std::vector<std::string> Names(const VirtualDir& dir) {
    std::vector<std::string> names;
    for (const auto& file : dir->GetFiles()) {
        names.push_back(file->GetName());
    }
    for (const auto& sub : dir->GetSubdirectories()) {
        for (const auto& name : Names(sub)) {
            names.push_back(sub->GetName() + "/" + name);
        }
    }
    std::ranges::sort(names);
    return names;
}

} // namespace

TEST_CASE("Convert to NUP keeps the base, the changed update files and the add-ons",
          "[nup][nup-convert]") {
    TempDir t;
    const auto archive = t.path / "Game.nup";
    std::string error;
    std::map<FileSys::Nup::ConvertStep, u64> last_done;
    const auto result = FileSys::Nup::ConvertToNup(
        MakeSource(), archive, {},
        [&](FileSys::Nup::ConvertStep step, const FileSys::Nup::PackProgress& progress) {
            REQUIRE(progress.bytes_done <= progress.bytes_total);
            last_done[step] = progress.bytes_done;
        },
        nullptr, &error);
    INFO(error);
    REQUIRE(result == FileSys::Nup::PackResult::Success);
    REQUIRE(last_done.contains(FileSys::Nup::ConvertStep::Extract));
    REQUIRE(last_done.contains(FileSys::Nup::ConvertStep::Pack));
    REQUIRE_FALSE(fs::exists(t.path / "Game.nup.convert"));
    REQUIRE_FALSE(fs::exists(t.path / "Game.nup.part"));

    FileSys::RealVfsFilesystem real;
    NupContainer nup{real.OpenFile(archive.string(), FileSys::OpenMode::Read)};
    REQUIRE(nup.GetStatus() == Loader::ResultStatus::Success);
    REQUIRE(nup.VerifyIntegrity({}) == Loader::ResultStatus::Success);
    const auto& packages = nup.GetPackages();
    REQUIRE(packages.size() == 3);
    REQUIRE(packages[0].kind == NupContainer::PackageKind::Base);
    REQUIRE(packages[0].title_id == ProgramId);
    REQUIRE(packages[0].title_name == "Fixture Game");
    REQUIRE(packages[0].publisher == "Lichii Fixtures");
    REQUIRE(packages[0].icon == Bytes("\xFF\xD8\xFF\xE0 icon"));
    REQUIRE(packages[1].kind == NupContainer::PackageKind::Update);
    REQUIRE(packages[1].version == 65536);
    REQUIRE(packages[1].display_version == "1.1.0");
    REQUIRE(packages[2].kind == NupContainer::PackageKind::Addon);
    REQUIRE(packages[2].title_id == AddOnId);

    REQUIRE(Names(nup.GetRomFSDir(0)) ==
            std::vector<std::string>{"a.bin", "data/x.txt", "same.bin"});
    // Only what the update changes is stored again; the loader lays it over the base.
    REQUIRE(Names(nup.GetRomFSDir(1)) == std::vector<std::string>{"a.bin", "new.bin"});
    REQUIRE(nup.GetRomFSDir(1)->GetFile("a.bin")->ReadAllBytes() == Bytes("update a"));
    REQUIRE(nup.GetExeFSDir(1)->GetFile("main")->ReadAllBytes() == Bytes("update main"));
    REQUIRE(Names(nup.GetRomFSDir(2)) == std::vector<std::string>{"dlc.bin"});
}

TEST_CASE("A cancelled conversion to NUP leaves nothing behind", "[nup][nup-convert]") {
    for (const auto stop_at : {FileSys::Nup::ConvertStep::Extract,
                               FileSys::Nup::ConvertStep::Pack}) {
        TempDir t;
        const auto archive = t.path / "Game.nup";
        std::atomic_bool cancel{false};
        const auto result = FileSys::Nup::ConvertToNup(
            MakeSource(), archive, {},
            [&](FileSys::Nup::ConvertStep step, const FileSys::Nup::PackProgress&) {
                if (step == stop_at) {
                    cancel = true;
                }
            },
            &cancel);
        REQUIRE(result == FileSys::Nup::PackResult::Cancelled);
        REQUIRE(fs::is_empty(t.path));
    }
}

TEST_CASE("Convert to NUP never replaces an existing archive", "[nup][nup-convert]") {
    TempDir t;
    const auto archive = t.path / "Game.nup";
    { std::ofstream(archive) << "keep"; }
    std::string error;
    REQUIRE(FileSys::Nup::ConvertToNup(MakeSource(), archive, {}, {}, nullptr, &error) ==
            FileSys::Nup::PackResult::IoError);
    REQUIRE(fs::file_size(archive) == 4);
    REQUIRE_FALSE(fs::exists(t.path / "Game.nup.convert"));
}

TEST_CASE("An extracted NSP is read without its RomFS image in the ExeFS", "[nup][nup-convert]") {
    // PFS0 with main, main.npdm and a RomFS image, as homebrew NSPs ship.
    const auto romfs = FileSys::CreateRomFS(Dir("romfs", {File("data.bin", "romfs data")}));
    REQUIRE(romfs != nullptr);
    const std::vector<std::pair<std::string, std::vector<u8>>> entries{
        {"main", Bytes("main")}, {"main.npdm", Bytes(Npdm(ProgramId))},
        {"game.romfs", romfs->ReadAllBytes()}};
    std::string strings;
    std::vector<u32> name_offsets;
    for (const auto& [name, data] : entries) {
        name_offsets.push_back(static_cast<u32>(strings.size()));
        strings += name;
        strings += '\0';
    }
    std::vector<u8> pfs(0x10 + entries.size() * 0x18);
    const auto put = [&pfs](size_t at, u64 value, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            pfs[at + i] = static_cast<u8>(value >> (8 * i));
        }
    };
    std::copy_n("PFS0", 4, pfs.begin());
    put(4, entries.size(), 4);
    put(8, strings.size(), 4);
    u64 offset = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        put(0x10 + i * 0x18, offset, 8);
        put(0x10 + i * 0x18 + 8, entries[i].second.size(), 8);
        put(0x10 + i * 0x18 + 0x10, name_offsets[i], 4);
        offset += entries[i].second.size();
    }
    pfs.insert(pfs.end(), strings.begin(), strings.end());
    for (const auto& [name, data] : entries) {
        pfs.insert(pfs.end(), data.begin(), data.end());
    }

    FileSys::ManualContentProvider provider;
    std::string error;
    const auto source = FileSys::Nup::ReadGameSource(
        provider, &provider, std::make_shared<FileSys::VectorVfsFile>(std::move(pfs), "game.nsp"), ProgramId,
        &error);
    INFO(error);
    REQUIRE(source.has_value());
    REQUIRE(source->packages.size() == 1);
    REQUIRE(Names(source->packages[0].exefs) == std::vector<std::string>{"main", "main.npdm"});
    REQUIRE(Names(source->packages[0].romfs) == std::vector<std::string>{"data.bin"});
}
