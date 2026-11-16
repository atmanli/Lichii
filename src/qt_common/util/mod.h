// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace QtCommon::Mod {

/// The mod folders found in `root`. When it holds one mod, the player is asked for its name, which
/// `install_name` receives: the mod is installed under it, and its folder is left as it is.
QStringList GetModFolders(const QString& root, const QString& fallbackName, QString& install_name);

const QString ExtractMod(const QString& path);

} // namespace QtCommon::Mod
