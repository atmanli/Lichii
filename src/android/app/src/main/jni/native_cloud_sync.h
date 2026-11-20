// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The end of the guest that NativeCloudSync.beginGuest began (frontend_common/cloud_sync), for the
// emulation run in native.cpp.
namespace AndroidCloudSync {

// The guest loaded: its saves are pushed once it stops.
void GuestLoaded();

// The guest stopped, other than for a program change: ends BeginGuest, and has the app push the
// saves if it loaded.
void GuestStopped();

} // namespace AndroidCloudSync
