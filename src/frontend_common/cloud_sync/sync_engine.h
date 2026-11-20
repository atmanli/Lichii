// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

#include "common/common_types.h"

namespace Service::Account {
class ProfileManager;
}

// Syncs game saves with Google Drive (layout in sync_layout.h). Each save of a title (every
// profile's account save and the device save) is compared with the version both sides last had
// (sync_state.h): the side that changed wins, a side that is missing gets the other's, and when
// both changed the caller picks. Nothing is deleted because the other side lacks it, and only the
// saves of profiles in the ProfileManager are synced, so orphaned save folders stay local.
// When Drive has two files for one save (two machines made it at once), every machine syncs the
// first created one; one with other contents is kept and reported in SyncResult.errors. Drive's
// save is read again right before it is replaced, so another machine's upload since the decision
// becomes a conflict rather than being overwritten.
//
// The calls block on Drive: run them on a worker thread, and don't edit the profiles while
// SyncRunning. A frontend calls BeginGuest before a title boots and EndGuest after it stops: the
// guest's title isn't synced meanwhile. A stop request aborts the Drive request in flight
// (but not a connection still being made, which times out after 30 seconds) and returns with
// cancelled set; it never interrupts a save being restored. An unreachable Drive fails the call
// right away.
namespace FrontendCommon::CloudSync {

struct Title {
    u64 id{};
    // The game list's name, for the game's folder on Drive.
    std::string name;
};

enum class ConflictChoice {
    // Upload the local save; Drive keeps the replaced one as a revision.
    KeepLocal,
    // Restore Drive's save; the replaced local one is backed up (BackupFolder in save_archive.h).
    KeepRemote,
    // Leave both, and ask again next time.
    Skip,
};

// A save that changed both here and on Drive since they were last synced, or that both sides
// have but that was never synced from here.
struct Conflict {
    u64 title_id{};
    std::string title_name;
    // The profile's nickname, empty for the device save.
    std::string nickname;
    std::chrono::system_clock::time_point local_time;
    std::chrono::system_clock::time_point remote_time;
};

struct SyncCallbacks {
    // Required. Called on the syncing thread for every conflict. It should give up with Skip once
    // stop is requested (WaitForAnswer does).
    std::function<ConflictChoice(const Conflict&, std::stop_token stop)> resolve_conflict;
    // Optional: steps done out of total, the saves of the title for PullTitle and PushTitle and
    // the titles for SyncAll.
    std::function<void(std::size_t done, std::size_t total)> progress;
};

// For a resolve_conflict that asks on another thread: post hands that thread a function to answer
// with, and this waits for the answer, or returns Skip once stop is requested. The answer may come
// late, or more than once; only the first counts.
ConflictChoice WaitForAnswer(std::stop_token stop,
                             const std::function<void(std::function<void(ConflictChoice)>)>& post);

// A guest is about to boot title_id: stops the syncs that touch it and waits until they have let
// go of it (a connection being made or a save being restored can take a while). Until EndGuest,
// syncs other than PullTitle skip the title.
void BeginGuest(u64 title_id);
void EndGuest();

// A sync runs, and may read or change the profiles: frontends leave them alone meanwhile.
bool SyncRunning();

struct SyncResult {
    std::size_t pushed{};
    std::size_t pulled{};
    // Conflicts resolve_conflict skipped.
    std::size_t skipped{};
    bool cancelled{};
    // What went wrong, for the user. Empty when everything synced.
    std::vector<std::string> errors;
};

// Before the title boots, after BeginGuest: brings in the saves that changed on Drive, and
// resolves conflicts. Local changes wait for PushTitle.
SyncResult PullTitle(Service::Account::ProfileManager& profiles, const Title& title,
                     const SyncCallbacks& callbacks, std::stop_token stop);

// After the title exits: uploads the saves that changed here, and resolves conflicts. Changes on
// Drive wait for the next PullTitle.
SyncResult PushTitle(Service::Account::ProfileManager& profiles, const Title& title,
                     const SyncCallbacks& callbacks, std::stop_token stop);

// "Sync now": both directions for every title. Stops at the first title Drive can't be reached
// for.
SyncResult SyncAll(Service::Account::ProfileManager& profiles, std::span<const Title> titles,
                   const SyncCallbacks& callbacks, std::stop_token stop);

} // namespace FrontendCommon::CloudSync
