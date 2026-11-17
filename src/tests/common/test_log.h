// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "tests/common/restore_eden_path.h"

namespace Tests {

namespace Detail {
inline bool IsPrivateDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(directory, error))) {
        return false; // missing, a regular file or a link
    }
#ifndef _WIN32
    struct stat info{};
    if (lstat(directory.c_str(), &info) != 0 || info.st_uid != geteuid() ||
        (info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return false;
    }
#endif
    return true;
}
} // namespace Detail

/// Starts the log for tests that read it through the in-memory ring (SetRingCapacity/ReadRing).
/// The first Log::Initialize of a process opens eden_log.txt in the Eden log folder and keeps it
/// open, and opening it moves an earlier eden_log.txt over eden_log.txt.old.txt: a test run with
/// a normal HOME rotated the user's own log away. The file goes to one folder under the
/// temporary folder instead, the same every run (an open file can't be removed on Windows, and a
/// new folder per run was left behind each time); nothing reads it, so two tests processes may
/// share it.
inline void InitializeTestLog() {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto previous = Common::FS::GetEdenPath(Common::FS::EdenPath::LogDir);
#ifdef _WIN32
        // The temporary folder is the user's own.
        const auto directory = std::filesystem::temp_directory_path() / "lichii-tests-log";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
#else
        // /tmp may be shared: one folder per user, made private from the start (create_directory
        // would follow the umask, which may leave it group-writable).
        const auto directory = std::filesystem::temp_directory_path() /
                               ("lichii-tests-log-" + std::to_string(geteuid()));
        mkdir(directory.c_str(), 0700);
#endif
        // SetEdenPath keeps the old folder when the new one isn't a directory, and the log would
        // then start in the user's folder. The temporary folder may be shared with other users
        // (/tmp), so the folder must also be a real one this user owns and nobody else can write
        // to: not a link to the user's own log folder, nor one where another user could plant one.
        if (!Detail::IsPrivateDirectory(directory)) {
            throw std::runtime_error("Cannot use the tests' log folder");
        }
        Common::FS::SetEdenPath(Common::FS::EdenPath::LogDir, directory);
        Common::Log::Initialize();
        RestoreEdenPath(Common::FS::EdenPath::LogDir, previous);
    });
}

} // namespace Tests
