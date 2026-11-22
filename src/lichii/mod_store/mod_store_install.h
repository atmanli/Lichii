// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace ModStore {

struct InstalledMod {
    QString id;
    QString version;
    QString name;
    QByteArray sha256;
};

[[nodiscard]] QString InstallMod(const QString& root, const QString& title_id,
                                 const QString& mod_id, const QString& name, const QString& version,
                                 const QString& archive_path, const QByteArray& sha256,
                                 bool external);
[[nodiscard]] QString UninstallMod(const QString& root, const QString& title_id,
                                   const QString& mod_id);
[[nodiscard]] QList<InstalledMod> ReadInstalledMods(const QString& root, const QString& title_id);

} // namespace ModStore
