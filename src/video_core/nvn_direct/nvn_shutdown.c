// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// The emulator shutting down in direct mode (dotfiles-25x.18). Upstream's replay lives as long as
// its process (detached threads, the GPU until exit); in Lichii the emulation stops first and the
// device goes away while the process goes on.
// - nvn_direct_stop, before the guest's cores are suspended: a game thread inside a halted NVN
//   call (core/nvn_hook.cpp RunHaltedCall) waits on the host, where the kernel cannot suspend it
//   (a fence, room in the replay worker's queue, a present's fill). Those waits return now:
//   nothing more is replayed or submitted and every fence reads as done.
// - nvn_direct_park, before Lichii's device and the renderer objects the replay calls back into
//   (the ASTC decode) are destroyed: the replay worker finishes what it holds, pipeline builds
//   and speculative shader translations stop, the pipeline cache is saved a last time, and the
//   completion thread runs what was submitted and leaves. None of the replay's threads uses the
//   device or guest memory after that. A thread that does not stop within its bound (stuck in the
//   driver) ends the process instead: the device must not go away under it.

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "bananative/dm/gfx/vk_android/vka.h"
#include "nvn_direct.h"

void nvn_direct_present_wake(void); // nvn_present.c
int vka_rc_park(uint64_t timeout_ns); // nvn_recompile.c

static _Atomic int g_parked;

// The shutdown should reach nvn_direct_park within seconds of the stop: if it does not, a line
// (and with DM_VK_STALL_STACKS=1 every thread's stack) says where it waits.
static void* watchdog_main(void* arg) {
    (void)arg;
    pthread_setname_np(pthread_self(), "vk-shutdown");
    for (int i = 0; i < 50; i++) {
        if (atomic_load(&g_parked)) return NULL;
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
    }
    VRED("shutdown: the device not released 5 s after the stop (Lichii's shutdown waits elsewhere; DM_VK_STALL_STACKS=1 logs every thread's stack)");
    vka_stall_stacks();
    return NULL;
}

void nvn_direct_stop(void) {
    if (vka_stopping()) return;
    vka_stop();
    vka_worker_wake();
    nvn_direct_present_wake();
    VLOG("the emulator stops: nothing more is replayed or submitted, every fence reads as done");
    pthread_t th;
    if (!pthread_create(&th, NULL, watchdog_main, NULL)) pthread_detach(th);
}

void nvn_direct_park(void) {
    nvn_direct_stop();
    atomic_store(&g_parked, 1);
    if (!vka_ok()) return;
    vka_vram_trace_stop();
    // Bounded: a thread stuck in the driver must not keep the emulator from shutting down. The
    // device's own wait for idle follows (RendererVulkan's destructor).
    const uint64_t bound = 3000000000ull;
    const uint64_t t0 = now_ns();
    const int worker = vka_worker_idle(bound);
    const int builds = vka_pso_park(bound);
    const int translations = vka_rc_park(bound);
    const int completion = worker && builds && translations && vka_complete_stop(bound);
    const double ms = (now_ns() - t0) / 1e6;
    if (completion) {
        VLOG("parked for the shutdown in %.0f ms: the replay worker is idle, no pipeline is being built or shader translated, the pipeline cache is saved, the completion thread has left", ms);
        return;
    }
    VRED("parked for the shutdown in %.0f ms: the replay worker %s, pipeline builds %s, speculative translations %s, the completion thread %s; the device is still in use: the process exits", ms,
         worker ? "is idle" : "is still replaying", builds ? "stopped" : "still running", translations ? "stopped" : "still running",
         !(worker && builds && translations) ? "was not stopped" : "is still running");
    vka_stall_stacks();
    // Not abort or exit: the replay's own fatal exit (dm_host_shutdown's abort) has been seen to hang
    // the process, and exit handlers and destructors would destroy the device under those threads.
    _exit(1);
}
