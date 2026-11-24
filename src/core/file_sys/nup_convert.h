// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "core/file_sys/nup_writer.h"
#include "core/file_sys/vfs/vfs_types.h"

namespace FileSys {
class ContentProvider;
class ManualContentProvider;
class NCA;
} // namespace FileSys

// Converts a game the emulator reads through its loaders (NSP, XCI, NCA or an installed title) to
// a NUP: its packages are written to a temporary loose dump, which PackDump packs.
namespace FileSys::Nup {

enum class SourceKind { Base, Update, AddOn };

// One package as the emulator reads it: decrypted views, nothing on disk.
struct SourcePackage {
    SourceKind kind{};
    u64 title_id{};
    u32 version{};
    VirtualDir exefs;
    // The whole tree. An update's is the base with the update applied; only the files that
    // differ from the base are written.
    VirtualDir romfs;
    VirtualFile nacp;
    std::vector<u8> icon;
    // Bytes of the ExeFS and RomFS trees.
    u64 size{};
};

struct GameSource {
    // The base first, then the newest update, then the add-ons.
    std::vector<SourcePackage> packages;
    // What the NUP is written with unless the caller overrides it.
    Metadata metadata;
    // The archives the views read from.
    std::vector<std::shared_ptr<NCA>> ncas;
};

// The base program of an NSP, XCI or NCA file (or the installed one when the file holds none),
// the newest update and the add-ons of the same game, from the file and the content provider.
// game_folders, the provider of the games found in the game folders, gives the versions of the
// updates found there.
std::optional<GameSource> ReadGameSource(const ContentProvider& provider,
                                         const ManualContentProvider* game_folders,
                                         const VirtualFile& file, u64 program_id,
                                         std::string* error_out = nullptr);

// Writes the packages as a loose dump PackDump reads: base/{exefs,romfs}, updates/<version>/
// with only the RomFS files that differ from the base, and dlc/<title id>/0/romfs. Control data
// and the icon go next to each program's ExeFS. out_dir must not exist; it is removed again when
// the dump is not complete.
ExtractResult WriteDump(const GameSource& source, const std::filesystem::path& out_dir,
                        const std::function<void(const PackProgress&)>& progress = {},
                        const std::atomic_bool* cancel = nullptr,
                        std::string* error_out = nullptr);

enum class ConvertStep { Extract, Pack };

// Bytes that must be free on the archive's volume: packing deletes each temporary file once it
// is in the archive, so the dump and the archive only overlap by about one chunk.
u64 ConvertSpaceNeeded(const GameSource& source);

// Writes the dump next to the archive, packs it and removes it. Nothing is left behind when the
// conversion is cancelled or fails, and an existing archive is never overwritten.
PackResult ConvertToNup(const GameSource& source, const std::filesystem::path& archive,
                        PackOptions options,
                        const std::function<void(ConvertStep, const PackProgress&)>& progress = {},
                        const std::atomic_bool* cancel = nullptr, std::string* error_out = nullptr);

} // namespace FileSys::Nup
