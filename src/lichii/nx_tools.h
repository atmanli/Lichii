// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QObject>
#include <QPointer>

#include "core/file_sys/nup_writer.h"
#include "core/file_sys/vfs/vfs_types.h"

class NupProgressDialog;
class QPixmap;
class QWidget;

#ifdef LICHII_NXFORMAT_DYNAMIC
namespace NxFormatDynamic {
/// Loads nxformat.dll once; false, with the reason in error, when it is missing or unusable.
bool Load(std::string& error);
} // namespace NxFormatDynamic
#endif

/// NX archive tools of the game list, run in the process with nxformat:
/// conversion to NX of an NSP, XCI, NUP or loose dump folder, and in-place editing of an
/// NX archive (add an update folder, an NSP or an XCI; remove an update or DLC).
/// NSP and XCI are decrypted by the emulator while nxformat reads them: nxformat never sees
/// keys or encrypted bytes, and nothing decrypted is written to disk.
class NxTools : public QObject {
    Q_OBJECT

public:
    NxTools(FileSys::VirtualFilesystem vfs, QWidget* parent);
    ~NxTools() override;

    /// True when ConvertToNx accepts this game list entry.
    static bool CanConvert(const std::string& game_path);
    /// True when the path is an NX archive.
    static bool IsNxArchive(const std::string& game_path);

    void ConvertToNx(const std::string& game_path);
    void AddToNx(const std::string& archive_path);
    void RemoveFromNx(const std::string& archive_path);
    /// Sets the title, publisher and icon the game list shows, in place.
    void EditNxMetadata(const std::string& archive_path, const std::function<bool()>& prepare_write);
    /// Writes a new icon (JPEG or PNG bytes) into the archive, in place.
    void SetNxIcon(const std::string& archive_path, const QByteArray& icon,
                   const std::function<bool()>& prepare_write);

signals:
    /// A conversion or an edit started (true) or ended (false).
    void RunningChanged(bool running);
    /// An archive was created or changed: the game list must be refreshed.
    void ArchivesChanged();
    // The NUP card of Convert to a single file was picked.
    void ConvertToNupRequested(const std::string& game_path);
    /// A metadata write ended; written is false when it failed or was cancelled.
    void MetadataWriteFinished(bool written);

private:
    struct Job;
    struct NxMetadata {
        QString application_id;
        FileSys::Nup::Metadata current;
        std::vector<u8> original_icon;
    };
    /// Whether nxformat can be called; says why not when it cannot.
    bool Loaded() const;
    bool ReadNxMetadata(const std::string& archive_path, NxMetadata& out);
    void WriteNxMetadata(const std::string& archive_path, const NxMetadata& metadata,
                         const FileSys::Nup::MetadataEdit& edit, const QPixmap& icon,
                         const std::function<bool()>& prepare_write);
    // Add to NX and Remove from NX: the archive's updates and DLC, to remove or add to.
    void ManageNx(const std::string& archive_path);
    void Start(std::shared_ptr<Job> job);
    void Finished(std::shared_ptr<Job> job, int code, QString error);
    void ReplyOriginal(Job& job, const QString& text);
    static QString TimeLeft(double seconds);

    FileSys::VirtualFilesystem vfs;
    QWidget* parent_widget;
    std::shared_ptr<Job> running;
    QPointer<NupProgressDialog> progress;
};
