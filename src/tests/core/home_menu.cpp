// SPDX-FileCopyrightText: Copyright 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "common/common_funcs.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/file_sys/fssystem/fssystem_nca_header.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/process_creation.h"
#include "core/hle/service/ns/application_manager_interface.h"
#include "core/hle/service/ns/game_list_applications.h"
#include "tests/core/system_test_context.h"

using Service::AM::AppletProgramId;
using Service::AM::IsAppletProgramId;

namespace {

template <typename T>
void Put(std::vector<u8>& out, size_t offset, const T& value) {
    if (out.size() < offset + sizeof(T)) {
        out.resize(offset + sizeof(T));
    }
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

// An NPDM with its ACID at 0x100 and its ACI0 after it, in the Switch 1 or the Switch 2 layout.
std::vector<u8> MakeNpdm(bool switch2) {
    const u32 shift = switch2 ? 0x100 : 0;
    std::vector<u8> bytes(0x480 + shift);
    std::memcpy(bytes.data(), "META", 4);
    Put(bytes, 0x70, u32{0x400 + shift});
    Put(bytes, 0x74, u32{0x40});
    Put(bytes, 0x78, u32{0x100});
    Put(bytes, 0x7C, u32{0x240 + shift});
    std::memcpy(bytes.data() + 0x300 + shift, "ACID", 4);
    Put(bytes, 0x320 + shift, u32{0x240 + shift});
    Put(bytes, 0x324 + shift, u32{0x2C});
    std::memcpy(bytes.data() + 0x400 + shift, "ACI0", 4);
    Put(bytes, 0x420 + shift, u32{0x40});
    Put(bytes, 0x424 + shift, u32{0x1C});
    Put(bytes, 0x430 + shift, u32{0x60});
    Put(bytes, 0x434 + shift, u32{8});
    return bytes;
}

// A decrypted program NCA whose ExeFS holds main and the NPDM. An update also gets a patch RomFS
// section, which makes it unreadable without a base.
FileSys::VirtualFile MakeProgramNca(u64 title_id, bool switch2, bool update) {
    using namespace FileSys;
    const std::vector<std::pair<std::string, std::vector<u8>>> files{
        {"main", std::vector<u8>(0x10)}, {"main.npdm", MakeNpdm(switch2)}};
    std::vector<u8> exefs;
    Put(exefs, 0, Common::MakeMagic('P', 'F', 'S', '0'));
    Put(exefs, 4, u32{2});
    Put(exefs, 8, u32{16});
    u64 offset = 0;
    u32 name_offset = 0;
    for (size_t i = 0; i < files.size(); ++i) {
        Put(exefs, 0x10 + i * 0x18, offset);
        Put(exefs, 0x10 + i * 0x18 + 8, u64{files[i].second.size()});
        Put(exefs, 0x10 + i * 0x18 + 0x10, name_offset);
        offset += files[i].second.size();
        name_offset += static_cast<u32>(files[i].first.size() + 1);
    }
    exefs.resize(0x40);
    const std::string strtab{"main\0main.npdm\0", 16};
    exefs.insert(exefs.end(), strtab.begin(), strtab.end());
    for (const auto& file : files) {
        exefs.insert(exefs.end(), file.second.begin(), file.second.end());
    }

    NcaHeader header{};
    header.magic = NcaHeader::MagicDecrypted;
    header.content_type = NcaHeader::ContentType::Program;
    header.program_id = title_id;
    header.sdk_addon_version = 0x000C0000;
    header.fs_info[0].start_sector = 6;
    header.fs_info[0].end_sector = 6 + static_cast<u32>((exefs.size() + 0x1FF) / 0x200);
    NcaFsHeader fs{};
    fs.version = 2;
    fs.fs_type = NcaFsHeader::FsType::PartitionFs;
    fs.hash_type = NcaFsHeader::HashType::None;
    fs.encryption_type = NcaFsHeader::EncryptionType::None;
    std::vector<u8> bytes(0xC00);
    Put(bytes, NcaHeader::Size, fs);
    u32 end_sector = header.fs_info[0].end_sector;
    if (update) {
        header.fs_info[1].start_sector = end_sector;
        header.fs_info[1].end_sector = ++end_sector;
        fs.fs_type = NcaFsHeader::FsType::RomFs;
        fs.encryption_type = NcaFsHeader::EncryptionType::AesCtrEx;
        Put(bytes, NcaHeader::Size + sizeof(fs), fs);
    }
    Put(bytes, 0, header);
    bytes.insert(bytes.end(), exefs.begin(), exefs.end());
    bytes.resize(size_t{end_sector} * 0x200);
    return std::make_shared<FileSys::VectorVfsFile>(std::move(bytes), "program.nca");
}

} // namespace

TEST_CASE("Applet program ids cover the HOME Menu, overlayDisp and the LLE applets only",
          "[core][am][home_menu]") {
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::QLaunch)));
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::OverlayDisplay)));
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::Starter)));
    CHECK(IsAppletProgramId(static_cast<u64>(AppletProgramId::MaxProgramId)));
    CHECK_FALSE(IsAppletProgramId(0x0100000000000FFFULL));
    CHECK_FALSE(IsAppletProgramId(0x0100000000002000ULL));
    CHECK_FALSE(IsAppletProgramId(0x0100152000022000ULL));
    CHECK_FALSE(IsAppletProgramId(0x0400C3F00006E000ULL));
}

TEST_CASE("A HOME Menu launch in a new session reaches the frontend that can boot it",
          "[core][am][home_menu]") {
    ScopedSystemTestContext environment;
    Core::System system;
    constexpr u64 Switch2Title = 0x0400C3F00006E000ULL;

    CHECK_FALSE(system.LaunchInNewSession(Switch2Title));

    std::optional<u64> launched;
    system.RegisterLaunchInNewSessionCallback([&](u64 program_id) { launched = program_id; });
    CHECK(system.LaunchInNewSession(Switch2Title));
    CHECK(launched == Switch2Title);
}

TEST_CASE("An installed Switch 2 update is detected without its base RomFS",
          "[core][am][home_menu]") {
    constexpr u64 Title = 0x0400C3F00006E000ULL;
    const bool switch2_update = GENERATE(true, false);
    ScopedSystemTestContext environment;
    Core::System system;
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    FileSys::ManualContentProvider installed;
    installed.AddEntry(FileSys::TitleType::Application, FileSys::ContentRecordType::Program, Title,
                       MakeProgramNca(Title, !switch2_update, false));
    installed.AddEntry(FileSys::TitleType::Update, FileSys::ContentRecordType::Program,
                       FileSys::GetUpdateTitleID(Title),
                       MakeProgramNca(FileSys::GetUpdateTitleID(Title), switch2_update, true));
    system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual, &installed);

    CHECK(Service::AM::IsSwitch2Application(system, Title) == switch2_update);
}

TEST_CASE("The HOME Menu lists game list applications next to installed ones",
          "[core][ns][home_menu]") {
    ScopedSystemTestContext environment;
    Core::System system;
    REQUIRE(system.Initialize());
    std::optional<Kernel::KThread> thread;
    thread.emplace(system.Kernel());
    REQUIRE(Kernel::KThread::InitializeDummyThread(system, &*thread, nullptr).IsSuccess());
    system.Kernel().SetCurrentEmuThread(&*thread);
    system.Kernel().Initialize();

    constexpr u64 Installed = 0x0100152000022000ULL;
    constexpr u64 InstalledSubProgram = 0x0100152000022001ULL;
    constexpr u64 Switch2Game = 0x0400C3F00006E000ULL;
    constexpr u64 Switch2Edition = 0x04004D300C5AE001ULL;

    FileSys::ManualContentProvider provider;
    const auto file = std::make_shared<FileSys::VectorVfsFile>();
    provider.AddEntry(FileSys::TitleType::Application, FileSys::ContentRecordType::Program,
                      Installed, file);
    provider.AddEntry(FileSys::TitleType::Application, FileSys::ContentRecordType::Program,
                      InstalledSubProgram, file);
    provider.ReplaceApplicationPaths({{Installed, "installed.nsp"},
                                      {Switch2Game, "first.nup"},
                                      {Switch2Edition, "edition.nx"}});
    system.SetContentProvider(std::make_unique<FileSys::ContentProviderUnion>());
    system.RegisterContentProvider(FileSys::ContentProviderUnionSlot::FrontendManual, &provider);

    CHECK(Service::NS::GetGameListApplicationPath(system, Switch2Game) == "first.nup");
    CHECK_FALSE(Service::NS::GetGameListApplicationPath(system, 0x0400000000000000ULL));

    std::vector<u64> listed;
    {
        Service::NS::IApplicationManagerInterface ns{system};
        std::array<Service::NS::ApplicationRecord, 8> records{};
        s32 count{};
        REQUIRE(ns.ListApplicationRecord(std::span<Service::NS::ApplicationRecord>{records}, &count, 0).IsSuccess());
        for (s32 i = 0; i < count; ++i) {
            listed.push_back(records[i].application_id);
        }
    }
    std::ranges::sort(listed);
    CHECK(listed == std::vector<u64>{Installed, Switch2Edition, Switch2Game});

    FileSys::GameListApplicationPaths next_scan{{Switch2Edition, "moved.nx"}};
    provider.ClearAllEntries();
    CHECK(Service::NS::GetGameListApplicationPath(system, Switch2Game) == "first.nup");
    CHECK(Service::NS::GetGameListApplicationPath(system, Switch2Edition) == "edition.nx");
    provider.ReplaceApplicationPaths(std::move(next_scan));
    CHECK_FALSE(Service::NS::GetGameListApplicationPath(system, Switch2Game));
    CHECK(Service::NS::GetGameListApplicationPath(system, Switch2Edition) == "moved.nx");

    system.CoreTiming().ClearPendingEvents();
    system.ShutdownMainProcess();
    system.Kernel().SetCurrentEmuThread(nullptr);
    system.Kernel().UnregisterKernelObject(&*thread);
    thread.reset();
}
