// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringView>

namespace ModStore {

enum class Source { Demo, Submission, GameBanana, GitHub };

struct InstallDetails {
    QString author;
    Source source{};
    QDateTime file_date;
    bool verified{};
    QByteArray thumbnail_jpeg;
};

struct InstalledMod {
    QString id;
    QString version;
    QString name;
    QByteArray sha256;
    QString author;
    std::optional<Source> source;
    QDateTime file_date;
    QString thumbnail_path;
    std::optional<bool> verified;
    std::optional<bool> update_available;
    QDateTime update_checked;
};

enum class GitHubPatchKind { Pchtxt, Ips, Cheat };

struct GitHubPatch {
    GitHubPatchKind kind;
    QString build_id;
    QString member;
    QString tag;
};

[[nodiscard]] QString SourceName(Source source);
[[nodiscard]] bool MatchesGitHubIpsFilename(const QString& filename, QStringView build_id);
[[nodiscard]] bool IsCheatTag(const QString& tag);

[[nodiscard]] QString InstallMod(const QString& root, const QString& title_id,
                                 const QString& mod_id, const QString& name, const QString& version,
                                 const QString& archive_path, const QByteArray& sha256,
                                 bool external, const InstallDetails& details);
[[nodiscard]] QString InstallGitHubPatch(const QString& root, const QString& title_id,
                                         const QString& mod_id, const QString& name,
                                         const QString& version, const QString& archive_path,
                                         const QByteArray& sha256, const GitHubPatch& patch,
                                         const InstallDetails& details);
[[nodiscard]] QString UninstallMod(const QString& root, const QString& title_id,
                                   const QString& mod_id);
[[nodiscard]] QList<InstalledMod> ReadInstalledMods(const QString& root, const QString& title_id);
bool RecordUpdateCheck(const QString& root, const QString& title_id, const QString& mod_id,
                       const QString& installed_version, bool update_available);

} // namespace ModStore
