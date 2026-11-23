// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/file_sys/nup_convert.h"

#include <algorithm>
#include <map>
#include <span>
#include <stdexcept>

#include <fmt/format.h>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "core/loader/loader.h"

namespace FileSys::Nup {
namespace {

namespace fs = std::filesystem;

constexpr size_t CopyBufferSize = 4 * 1024 * 1024;
constexpr u64 ProgressStep = 32ULL * 1024 * 1024;
// One pack chunk (256 MiB) the dump and the archive hold at the same time, and some margin.
constexpr u64 PackOverlap = 320ULL * 1024 * 1024;
constexpr const char* WorkMarker = ".lichii-convert";

class DumpError : public std::runtime_error {
public:
    DumpError(ExtractResult result_, const std::string& what)
        : std::runtime_error(what), result{result_} {}
    ExtractResult result;
};

std::string ToUtf8(const fs::path& path) {
    return Common::FS::PathToUTF8String(path);
}

fs::path FromUtf8(const std::string& value) {
    return fs::path{std::u8string{value.begin(), value.end()}};
}

fs::path WithSuffix(fs::path path, std::string_view suffix) {
    path += FromUtf8(std::string{suffix});
    return path;
}

struct TreeStats {
    u64 bytes{};
    u64 files{};
};

void AddTree(const VirtualDir& dir, TreeStats& stats) {
    if (dir == nullptr) {
        return;
    }
    for (const auto& file : dir->GetFiles()) {
        stats.bytes += file->GetSize();
        ++stats.files;
    }
    for (const auto& sub : dir->GetSubdirectories()) {
        AddTree(sub, stats);
    }
}

TreeStats PackageStats(const SourcePackage& package) {
    TreeStats stats;
    AddTree(package.exefs, stats);
    AddTree(package.romfs, stats);
    if (package.nacp != nullptr) {
        stats.bytes += package.nacp->GetSize();
        ++stats.files;
    }
    if (!package.icon.empty()) {
        stats.bytes += package.icon.size();
        ++stats.files;
    }
    return stats;
}

struct Control {
    VirtualFile nacp;
    std::vector<u8> icon;
};

Control ReadControl(const NCA* nca) {
    Control control;
    if (nca == nullptr || nca->GetStatus() != Loader::ResultStatus::Success) {
        return control;
    }
    const auto dir = ExtractRomFS(nca->GetRomFS());
    if (dir == nullptr) {
        return control;
    }
    control.nacp = dir->GetFile("control.nacp");
    for (const auto* language : LANGUAGE_NAMES) {
        if (const auto icon = dir->GetFile(fmt::format("icon_{}.dat", language))) {
            control.icon = icon->ReadAllBytes();
            break;
        }
    }
    return control;
}

void CheckedName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string::npos) {
        throw DumpError(ExtractResult::InvalidSource, "unsafe file name in the game: " + name);
    }
}

class DumpWriter {
public:
    DumpWriter(const std::function<void(const PackProgress&)>& progress_,
               const std::atomic_bool* cancel_, TreeStats total)
        : progress_cb{progress_}, cancel{cancel_}, buffer(CopyBufferSize),
          base_buffer(CopyBufferSize) {
        progress.bytes_total = total.bytes;
        progress.files_total = total.files;
        Report();
    }

    // Writes a tree. With a base, files equal to the base's at the same path are left out.
    void WriteTree(const VirtualDir& dir, const fs::path& dest, const VirtualDir& base = nullptr) {
        std::error_code ec;
        if (base == nullptr) {
            fs::create_directories(dest, ec);
        }
        for (const auto& file : dir->GetFiles()) {
            CheckedName(file->GetName());
            const auto base_file = base != nullptr ? base->GetFile(file->GetName()) : nullptr;
            if (base_file != nullptr && SameAs(file, base_file)) {
                ++progress.files_done;
                continue;
            }
            WriteFile(file, dest / FromUtf8(file->GetName()));
        }
        for (const auto& sub : dir->GetSubdirectories()) {
            CheckedName(sub->GetName());
            WriteTree(sub, dest / FromUtf8(sub->GetName()),
                      base != nullptr ? base->GetSubdirectory(sub->GetName()) : nullptr);
        }
    }

    void WriteFile(const VirtualFile& file, const fs::path& dest) {
        std::error_code ec;
        fs::create_directories(dest.parent_path(), ec);
        Common::FS::IOFile out{dest, Common::FS::FileAccessMode::Write};
        if (!out.IsOpen()) {
            throw DumpError(ExtractResult::IoError, "cannot create " + ToUtf8(dest));
        }
        const u64 size = file->GetSize();
        for (u64 offset = 0; offset < size;) {
            CheckCancel();
            const size_t chunk = static_cast<size_t>(std::min<u64>(buffer.size(), size - offset));
            if (file->Read(buffer.data(), chunk, offset) != chunk) {
                throw DumpError(ExtractResult::IoError, "cannot read " + file->GetName());
            }
            if (out.WriteSpan(std::span<const u8>{buffer.data(), chunk}) != chunk) {
                throw DumpError(ExtractResult::IoError,
                                "cannot write " + ToUtf8(dest) + ", the disk may be full");
            }
            offset += chunk;
            Advance(chunk);
        }
        if (!out.Flush()) {
            throw DumpError(ExtractResult::IoError,
                            "cannot write " + ToUtf8(dest) + ", the disk may be full");
        }
        ++progress.files_done;
    }

    void WriteBytes(std::span<const u8> bytes, const fs::path& dest) {
        CheckCancel();
        Common::FS::IOFile out{dest, Common::FS::FileAccessMode::Write};
        if (!out.IsOpen() || out.WriteSpan(bytes) != bytes.size() || !out.Flush()) {
            throw DumpError(ExtractResult::IoError, "cannot write " + ToUtf8(dest));
        }
        Advance(bytes.size());
        ++progress.files_done;
    }

    void Finish() {
        progress.bytes_done = progress.bytes_total;
        progress.files_done = progress.files_total;
        Report();
    }

private:
    bool SameAs(const VirtualFile& file, const VirtualFile& base) {
        const u64 size = file->GetSize();
        if (size != base->GetSize()) {
            return false;
        }
        for (u64 offset = 0; offset < size;) {
            CheckCancel();
            const size_t chunk = static_cast<size_t>(std::min<u64>(buffer.size(), size - offset));
            if (file->Read(buffer.data(), chunk, offset) != chunk ||
                base->Read(base_buffer.data(), chunk, offset) != chunk) {
                throw DumpError(ExtractResult::IoError, "cannot read " + file->GetName());
            }
            if (!std::equal(buffer.begin(), buffer.begin() + chunk, base_buffer.begin())) {
                // What was compared is read again when the file is written.
                progress.bytes_done -= std::min(progress.bytes_done, offset);
                return false;
            }
            offset += chunk;
            Advance(chunk);
        }
        return true;
    }

    void CheckCancel() const {
        if (cancel != nullptr && cancel->load()) {
            throw DumpError(ExtractResult::Cancelled, "cancelled");
        }
    }

    void Advance(u64 bytes) {
        progress.bytes_done = std::min(progress.bytes_done + bytes, progress.bytes_total);
        if (progress.bytes_done - last_report >= ProgressStep) {
            Report();
        }
    }

    void Report() {
        last_report = progress.bytes_done;
        if (progress_cb) {
            progress_cb(progress);
        }
    }

    const std::function<void(const PackProgress&)>& progress_cb;
    const std::atomic_bool* cancel;
    PackProgress progress{};
    u64 last_report{};
    std::vector<u8> buffer;
    std::vector<u8> base_buffer;
};

} // namespace

std::optional<GameSource> ReadGameSource(const ContentProvider& provider,
                                         const ManualContentProvider* game_folders,
                                         const VirtualFile& file, u64 program_id,
                                         std::string* error_out) {
    const auto fail = [&](std::string message) -> std::optional<GameSource> {
        if (error_out) {
            *error_out = std::move(message);
        }
        return std::nullopt;
    };
    if (file == nullptr) {
        return fail("the game file cannot be opened");
    }

    GameSource source;
    std::shared_ptr<NSP> nsp;
    std::shared_ptr<NCA> base_nca;
    VirtualDir base_exefs;
    VirtualFile base_romfs;
    switch (Loader::IdentifyFile(file)) {
    case Loader::FileType::XCI:
        nsp = XCI{file, program_id}.GetSecurePartitionNSP();
        break;
    case Loader::FileType::NSP:
        nsp = std::make_shared<NSP>(file, program_id);
        break;
    case Loader::FileType::NCA:
        base_nca = std::make_shared<NCA>(file);
        break;
    default:
        break;
    }
    const bool extracted = nsp != nullptr && nsp->IsExtractedType();
    if (extracted) {
        // An extracted NSP holds the ExeFS files and the RomFS image side by side.
        base_romfs = nsp->GetRomFS();
        std::vector<VirtualFile> exefs_files;
        for (const auto& entry : nsp->GetExeFS()->GetFiles()) {
            if (entry != base_romfs) {
                exefs_files.push_back(entry);
            }
        }
        base_exefs = std::make_shared<VectorVfsDirectory>(std::move(exefs_files),
                                                          std::vector<VirtualDir>{}, "exefs");
    } else if (nsp != nullptr) {
        base_nca = nsp->GetNCA(program_id, ContentRecordType::Program);
    }
    if (base_nca == nullptr && base_exefs == nullptr) {
        base_nca = provider.GetEntry(program_id, ContentRecordType::Program);
    }
    if (base_nca != nullptr) {
        if (base_nca->GetStatus() != Loader::ResultStatus::Success) {
            return fail(fmt::format("the program cannot be read: {}. Check that your keys are "
                                    "installed.",
                                    Loader::GetResultStatusString(base_nca->GetStatus())));
        }
        base_exefs = base_nca->GetExeFS();
        base_romfs = base_nca->GetRomFS();
        source.ncas.push_back(base_nca);
    }
    if (base_exefs == nullptr || base_exefs->GetFile("main") == nullptr) {
        return fail("the game holds no program to convert");
    }

    SourcePackage base{.kind = SourceKind::Base, .title_id = program_id};
    base.exefs = base_exefs;
    base.romfs = base_romfs != nullptr ? ExtractRomFS(base_romfs) : nullptr;
    Control base_control;
    if (nsp != nullptr && !extracted) {
        base_control = ReadControl(nsp->GetNCA(program_id, ContentRecordType::Control).get());
    }
    if (base_control.nacp == nullptr) {
        base_control = ReadControl(provider.GetEntry(program_id, ContentRecordType::Control).get());
    }
    base.nacp = base_control.nacp;
    base.icon = std::move(base_control.icon);
    base.size = PackageStats(base).bytes;
    source.packages.push_back(std::move(base));

    // The newest update the provider knows of, else one packed with the game.
    const u64 update_id = GetUpdateTitleID(program_id);
    VirtualFile update_raw = provider.GetEntryRaw(update_id, ContentRecordType::Program);
    u32 update_version = provider.GetEntryVersion(update_id).value_or(0);
    // Updates found in the game folders carry their versions only in that provider, which
    // reports none through GetEntryVersion and hands out any one of them through GetEntryRaw.
    if (update_version == 0 && game_folders != nullptr) {
        for (const auto& entry : game_folders->ListUpdateVersions(update_id)) {
            if (auto raw = game_folders->GetEntryForVersion(update_id, ContentRecordType::Program,
                                                            entry.version)) {
                update_raw = std::move(raw);
                update_version = entry.version;
                break;
            }
        }
    }
    std::shared_ptr<NCA> packed_control;
    if (update_raw == nullptr && nsp != nullptr && !extracted) {
        update_raw = nsp->GetPackedUpdateFile(program_id);
        packed_control = nsp->GetNCA(update_id, ContentRecordType::Control, TitleType::Update);
        update_version = 0;
    }
    if (update_raw != nullptr && base_nca != nullptr) {
        auto update = std::make_shared<NCA>(update_raw, base_nca.get());
        if (update->GetStatus() == Loader::ResultStatus::Success && update->GetExeFS() != nullptr) {
            SourcePackage package{.kind = SourceKind::Update,
                                  .title_id = update_id,
                                  .version = update_version};
            package.exefs = update->GetExeFS();
            package.romfs = update->GetRomFS() != nullptr ? ExtractRomFS(update->GetRomFS()) : nullptr;
            auto control = packed_control != nullptr
                               ? ReadControl(packed_control.get())
                               : ReadControl(provider.GetEntry(update_id, ContentRecordType::Control).get());
            package.nacp = control.nacp;
            package.icon = std::move(control.icon);
            package.size = PackageStats(package).bytes;
            source.packages.push_back(std::move(package));
            source.ncas.push_back(std::move(update));
        }
    }

    // Add-ons from the provider and from the game's own package.
    std::vector<u64> addon_ids;
    for (const auto& entry : provider.ListEntriesFilter(TitleType::AOC, ContentRecordType::Data)) {
        addon_ids.push_back(entry.title_id);
    }
    std::map<u64, std::shared_ptr<NCA>> own_addons;
    if (nsp != nullptr && !extracted) {
        for (const auto& [id, contents] : nsp->GetNCAs()) {
            for (const auto& [key, nca] : contents) {
                if (key.first == TitleType::AOC && key.second == ContentRecordType::Data) {
                    own_addons[id] = nca;
                    addon_ids.push_back(id);
                }
            }
        }
    }
    std::ranges::sort(addon_ids);
    addon_ids.erase(std::unique(addon_ids.begin(), addon_ids.end()), addon_ids.end());
    for (const u64 id : GetAddOnContent(program_id, addon_ids)) {
        const auto own = own_addons.find(id);
        std::shared_ptr<NCA> nca = own != own_addons.end()
                                       ? own->second
                                       : std::shared_ptr<NCA>(provider.GetEntry(id, ContentRecordType::Data));
        if (nca == nullptr || nca->GetStatus() != Loader::ResultStatus::Success ||
            nca->GetRomFS() == nullptr) {
            continue;
        }
        SourcePackage package{.kind = SourceKind::AddOn,
                              .title_id = id,
                              .version = provider.GetEntryVersion(id).value_or(0)};
        package.romfs = ExtractRomFS(nca->GetRomFS());
        if (package.romfs == nullptr) {
            continue;
        }
        package.size = PackageStats(package).bytes;
        source.packages.push_back(std::move(package));
        source.ncas.push_back(std::move(nca));
    }

    // Names and the icon come from the base, the version from the newest update.
    auto& metadata = source.metadata;
    for (const auto& package : source.packages) {
        if (package.kind == SourceKind::AddOn) {
            continue;
        }
        if (metadata.icon.empty()) {
            metadata.icon = package.icon;
        }
        if (package.nacp == nullptr) {
            continue;
        }
        const NACP nacp{package.nacp};
        if (metadata.title_name.empty()) {
            metadata.title_name = nacp.GetApplicationName();
        }
        if (metadata.publisher.empty()) {
            metadata.publisher = nacp.GetDeveloperName();
        }
        if (const auto version = nacp.GetVersionString(); !version.empty()) {
            metadata.display_version = version;
            metadata.version_source = package.kind == SourceKind::Update
                                          ? fmt::format("update v{}", package.version)
                                          : std::string{"base"};
        }
    }
    return source;
}

ExtractResult WriteDump(const GameSource& source, const fs::path& out_dir,
                        const std::function<void(const PackProgress&)>& progress,
                        const std::atomic_bool* cancel, std::string* error_out) {
    std::error_code ec;
    if (fs::exists(out_dir, ec)) {
        if (error_out) {
            *error_out = "the output folder already exists: " + ToUtf8(out_dir);
        }
        return ExtractResult::InvalidSource;
    }
    TreeStats total;
    for (const auto& package : source.packages) {
        const auto stats = PackageStats(package);
        total.bytes += stats.bytes;
        total.files += stats.files;
    }
    const auto base = std::ranges::find(source.packages, SourceKind::Base, &SourcePackage::kind);
    const VirtualDir base_romfs = base != source.packages.end() ? base->romfs : nullptr;
    try {
        if (!fs::create_directories(out_dir, ec) || ec) {
            throw DumpError(ExtractResult::IoError, "cannot create " + ToUtf8(out_dir));
        }
        DumpWriter writer{progress, cancel, total};
        for (const auto& package : source.packages) {
            fs::path dir;
            switch (package.kind) {
            case SourceKind::Base:
                dir = out_dir / "base";
                break;
            case SourceKind::Update:
                dir = out_dir / "updates" / std::to_string(package.version);
                break;
            case SourceKind::AddOn:
                dir = out_dir / "dlc" / fmt::format("{:016X}", package.title_id) / "0";
                break;
            }
            fs::create_directories(dir, ec);
            if (package.exefs != nullptr) {
                writer.WriteTree(package.exefs, dir / "exefs");
            }
            if (package.romfs != nullptr) {
                writer.WriteTree(package.romfs, dir / "romfs",
                                 package.kind == SourceKind::Update ? base_romfs : nullptr);
            }
            if (package.nacp != nullptr) {
                writer.WriteFile(package.nacp, dir / "control.nacp");
            }
            if (!package.icon.empty()) {
                writer.WriteBytes(package.icon, dir / "icon_AmericanEnglish.dat");
            }
        }
        writer.Finish();
    } catch (const DumpError& e) {
        fs::remove_all(out_dir, ec);
        if (error_out && e.result != ExtractResult::Cancelled) {
            *error_out = e.what();
        }
        return e.result;
    } catch (const std::exception& e) {
        fs::remove_all(out_dir, ec);
        if (error_out) {
            *error_out = e.what();
        }
        return ExtractResult::IoError;
    }
    return ExtractResult::Success;
}

u64 ConvertSpaceNeeded(const GameSource& source) {
    u64 bytes = PackOverlap;
    for (const auto& package : source.packages) {
        bytes += package.size;
    }
    return bytes;
}

PackResult ConvertToNup(const GameSource& source, const fs::path& archive, PackOptions options,
                        const std::function<void(ConvertStep, const PackProgress&)>& progress,
                        const std::atomic_bool* cancel, std::string* error_out) {
    const auto fail = [&](PackResult result, std::string message) {
        if (error_out) {
            *error_out = std::move(message);
        }
        return result;
    };
    std::error_code ec;
    if (fs::exists(archive, ec)) {
        return fail(PackResult::IoError, "the archive already exists: " + ToUtf8(archive));
    }
    const auto parent = archive.parent_path();
    const u64 needed = ConvertSpaceNeeded(source);
    const u64 available = Common::FS::GetFreeSpaceSize(parent);
    if (available < needed) {
        return fail(PackResult::IoError,
                    fmt::format("Not enough free space on {}: about {} MiB is needed and {} MiB "
                                "is free.",
                                ToUtf8(parent), needed >> 20, available >> 20));
    }

    // A work folder is only reused when this function made it.
    const auto work = WithSuffix(archive, ".convert");
    if (fs::exists(work, ec)) {
        if (!fs::exists(work / WorkMarker, ec)) {
            return fail(PackResult::IoError, "a folder is in the way: " + ToUtf8(work));
        }
        fs::remove_all(work, ec);
    }
    if (!fs::create_directories(work, ec) || ec) {
        return fail(PackResult::IoError, "cannot create " + ToUtf8(work));
    }
    if (Common::FS::IOFile marker{work / WorkMarker, Common::FS::FileAccessMode::Write};
        !marker.IsOpen()) {
        fs::remove_all(work, ec);
        return fail(PackResult::IoError, "cannot write in " + ToUtf8(parent));
    }
    const auto remove_work = [&] { fs::remove_all(work, ec); };

    const auto dump = work / archive.stem();
    std::string error;
    const auto extracted = WriteDump(
        source, dump,
        [&](const PackProgress& p) {
            if (progress) {
                progress(ConvertStep::Extract, p);
            }
        },
        cancel, &error);
    if (extracted != ExtractResult::Success) {
        remove_work();
        switch (extracted) {
        case ExtractResult::Cancelled:
            return PackResult::Cancelled;
        case ExtractResult::InvalidSource:
            return fail(PackResult::InvalidSource, error);
        default:
            return fail(PackResult::IoError, error);
        }
    }

    options.delete_sources = true;
    const auto result = PackDump(
        dump, archive, options,
        [&](const PackProgress& p) {
            if (progress) {
                progress(ConvertStep::Pack, p);
            }
        },
        cancel, &error);
    if (result != PackResult::Success) {
        fs::remove(WithSuffix(archive, ".part"), ec);
        fs::remove(WithSuffix(archive, ".journal"), ec);
    }
    remove_work();
    if (result != PackResult::Success && result != PackResult::Cancelled) {
        return fail(result, error);
    }
    return result;
}

} // namespace FileSys::Nup
