// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFuture>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <QSlider>
#include <QtConcurrentRun>

#include <nxformat.h>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/decrypted_nca.h"
#include "core/file_sys/nup_container.h"
#include "core/file_sys/nup_writer.h"
#include "core/file_sys/program_metadata.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/loader/loader.h"
#include "core/loader/nx.h"
#include "frontend_common/nx_archive.h"
#include "qt_common/qt_common.h"
#include "lichii/nup_dialogs.h"
#include "lichii/nx_tools.h"

namespace {

enum class Kind { None, Package, Nup, Dump };

std::filesystem::path ToPath(const std::string& path) {
    return std::filesystem::path(Common::FS::ToU8String(path));
}

std::string ToUtf8(const std::filesystem::path& path) {
    return Common::FS::PathToUTF8String(path);
}

std::string ToUtf8(const QString& text) {
    return text.toStdString();
}

QString ToQString(const std::filesystem::path& path) {
    return QString::fromStdString(ToUtf8(path));
}

bool HasExtension(const std::string& path, const char* extension) {
    return QString::fromStdString(path).endsWith(QString::fromLatin1(extension),
                                                 Qt::CaseInsensitive);
}

Kind KindOf(const std::string& path) {
    if (FileSys::Nup::DumpRootFromMain(ToPath(path))) {
        return Kind::Dump;
    }
    if (HasExtension(path, ".nup")) {
        return Kind::Nup;
    }
    if (HasExtension(path, ".nsp") || HasExtension(path, ".xci")) {
        return Kind::Package;
    }
    return Kind::None;
}

bool NpdmIsSwitch2(const FileSys::VirtualFile& npdm) {
    FileSys::ProgramMetadata metadata;
    return npdm != nullptr && metadata.Load(npdm) == Loader::ResultStatus::Success &&
           metadata.IsSwitch2();
}

// The platform of a NUP, as the emulator decides it at boot: the NPDM of the newest update
// with an ExeFS, else of the base.
bool NupIsSwitch2(const FileSys::VirtualFile& file) {
    if (!FileSys::NupContainer::IsNupFile(file)) {
        return false;
    }
    FileSys::NupContainer nup{file};
    if (nup.GetStatus() != Loader::ResultStatus::Success) {
        return false;
    }
    const auto base = nup.FindBasePackage();
    const auto update = nup.FindBestUpdatePackage();
    std::optional<std::size_t> program;
    if (update && nup.GetExeFSDir(*update) != nullptr) {
        program = update;
    } else if (base && nup.GetExeFSDir(*base) != nullptr) {
        program = base;
    }
    const auto exefs = program ? nup.GetExeFSDir(*program) : nullptr;
    return NpdmIsSwitch2(exefs != nullptr ? exefs->GetFile("main.npdm") : nullptr);
}

// The platform of a loose dump: the NPDM of the base, or of the first update.
bool DumpIsSwitch2(const FileSys::VirtualFilesystem& vfs, const std::filesystem::path& root) {
    std::vector<std::filesystem::path> candidates{root / "exefs" / "main.npdm",
                                                  root / "base" / "exefs" / "main.npdm"};
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root / "updates", ec), end; !ec && it != end;
         it.increment(ec)) {
        candidates.push_back(it->path() / "exefs" / "main.npdm");
    }
    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return NpdmIsSwitch2(vfs->OpenFile(ToUtf8(candidate), FileSys::OpenMode::Read));
        }
    }
    return false;
}

const char* PlatformArgument(bool switch2) {
    return switch2 ? "switch2" : "switch1";
}

// Reads one decrypted package for nxformat. Calls for one source are serialized.
int ReadSource(void* user, uint64_t offset, void* buffer, size_t size) {
    const auto& file = *static_cast<FileSys::VirtualFile*>(user);
    if (offset > file->GetSize() || size > file->GetSize() - offset) {
        return 1;
    }
    return file->Read(static_cast<u8*>(buffer), size, static_cast<std::size_t>(offset)) == size
               ? 0
               : 1;
}

// The update version written in an update folder name: "196608", "v196608" or "[v196608]".
std::optional<u32> VersionFromName(const QString& name) {
    bool ok = false;
    const auto plain = name.toUInt(&ok);
    if (ok) {
        return plain;
    }
    const QRegularExpression pattern(QStringLiteral("(?:^|[^0-9A-Za-z])v([0-9]{1,10})(?:$|[^0-9])"),
                                     QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(name);
    if (match.hasMatch()) {
        const auto value = match.captured(1).toULongLong(&ok);
        if (ok && value <= 0xFFFFFFFFull) {
            return static_cast<u32>(value);
        }
    }
    return std::nullopt;
}

// The title ID written in a package name, as in "Game [0100ABCD12340000][v0].nsp".
std::optional<u64> NameTitleId(const std::filesystem::path& path) {
    static const QRegularExpression pattern(QStringLiteral("\\[([0-9A-Fa-f]{16})\\]"));
    const auto match = pattern.match(ToQString(path.filename()));
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    bool ok = false;
    const u64 id = match.captured(1).toULongLong(&ok, 16);
    return ok ? std::optional<u64>{id} : std::nullopt;
}

// The title IDs a package holds, read from its content metadata.
std::vector<u64> PackageTitleIds(const FileSys::VirtualFile& file, const std::string& path) {
    std::vector<u64> ids;
    if (file == nullptr) {
        return ids;
    }
    std::shared_ptr<FileSys::NSP> nsp;
    if (HasExtension(path, ".xci")) {
        nsp = FileSys::XCI(file).GetSecurePartitionNSP();
    } else {
        nsp = std::make_shared<FileSys::NSP>(file);
    }
    if (nsp != nullptr) {
        for (const auto& [id, contents] : nsp->GetNCAs()) {
            ids.push_back(id);
        }
    }
    return ids;
}

// The title IDs of a package: from its name when written there, else from its contents.
std::vector<u64> TitleIdsOf(const FileSys::VirtualFilesystem& vfs, const std::filesystem::path& path) {
    if (const auto id = NameTitleId(path)) {
        return {*id};
    }
    const auto utf8 = ToUtf8(path);
    return PackageTitleIds(vfs->OpenFile(utf8, FileSys::OpenMode::Read), utf8);
}

// Updates and DLC of the same game found next to a Switch 1 package, sorted by name.
std::vector<std::filesystem::path> RelatedPackages(const FileSys::VirtualFilesystem& vfs,
                                                   const std::filesystem::path& package) {
    std::vector<std::filesystem::path> related;
    const auto own = TitleIdsOf(vfs, package);
    if (own.empty()) {
        return related;
    }
    std::vector<u64> bases;
    for (const u64 id : own) {
        bases.push_back(FileSys::GetBaseTitleID(id));
    }
    for (const auto& path : FrontendCommon::NxArchive::RegularFiles(package.parent_path())) {
        const auto utf8 = ToUtf8(path);
        if (path == package || !(HasExtension(utf8, ".nsp") || HasExtension(utf8, ".xci"))) {
            continue;
        }
        // A name with another game's ID is skipped without opening the file.
        const auto named = NameTitleId(path);
        if (named && std::find(bases.begin(), bases.end(), FileSys::GetBaseTitleID(*named)) ==
                         bases.end()) {
            continue;
        }
        const auto ids = TitleIdsOf(vfs, path);
        const bool same_game = std::any_of(ids.begin(), ids.end(), [&](u64 id) {
            return std::find(bases.begin(), bases.end(), FileSys::GetBaseTitleID(id)) !=
                   bases.end();
        });
        // Another copy of the same base is not an update or a DLC.
        const bool other_base = std::all_of(ids.begin(), ids.end(), [&](u64 id) {
            return std::find(own.begin(), own.end(), id) != own.end();
        });
        if (same_game && !other_base) {
            related.push_back(path);
        }
    }
    std::sort(related.begin(), related.end());
    return related;
}

struct Snapshot {
    QString identity;
    QString kind;
    QString title_id;
    QString version;
    QString display_version;
    QString name;
};

} // namespace

struct NxTools::Job {
    ~Job() {
        for (auto* source : sources) {
            nxformat_source_free(source);
        }
        nxformat_cancel_free(cancel);
    }

    QString title;
    // The game the progress card shows.
    QString display_title;
    QPixmap display_icon;
    QString done_message;
    // Converted games or dumps the user may send to the Recycle Bin at the end.
    std::vector<std::filesystem::path> originals;
    std::vector<std::string> args;
    // Decrypted views read by nxformat; they live until the run has returned.
    std::vector<std::unique_ptr<FileSys::VirtualFile>> files;
    std::vector<nxformat_source*> sources;
    nxformat_cancel* cancel = nxformat_cancel_new();
    QFuture<void> future;

    std::atomic<u64> total{0};
    std::atomic<u64> written{0};
    QElapsedTimer clock;
    // When nxformat started counting bytes (ms on `clock`), and the smoothed time left.
    qint64 counting_since = -1;
    double seconds_left = -1;
    std::mutex lines_mutex;
    QStringList added;
    QStringList skipped;
    // The new icon given to set-metadata, removed when the job ends.
    std::unique_ptr<QTemporaryFile> icon_file;

    // nxformat 1.0.1 fails pack and add when the rebuild recipe cannot be kept (1.0.0 only
    // warned). Such a failure can be run again without the recipe.
    bool CanRetryWithoutRecipe(const QString& error) const {
        if (args.empty() || (args[0] != "pack" && args[0] != "add") ||
            std::ranges::find(args, std::string{"--no-recipe"}) != args.end()) {
            return false;
        }
        return error.contains(QStringLiteral("recipe"), Qt::CaseInsensitive) ||
               error.contains(QStringLiteral("rebuild"), Qt::CaseInsensitive);
    }

    // Runs the same command again, without the recipe. The decrypted sources stay open.
    void PrepareRetryWithoutRecipe() {
        args.emplace_back("--no-recipe");
        total = 0;
        written = 0;
        counting_since = -1;
        seconds_left = -1;
        // `added` is kept: what the failed run committed stays in the archive.
        std::scoped_lock lock{lines_mutex};
        skipped.clear();
    }

    // Closes the packages nxformat read, so the original can be moved.
    void ReleaseSources() {
        for (auto* source : sources) {
            nxformat_source_free(source);
        }
        sources.clear();
        files.clear();
    }

    // Adds an NSP or XCI to the command, decrypted on the fly. Returns why it cannot be,
    // empty on success.
    QString AddPackage(const FileSys::VirtualFilesystem& filesystem,
                       const std::filesystem::path& path) {
        const auto file = filesystem->OpenFile(ToUtf8(path), FileSys::OpenMode::Read);
        std::string error = "file not found";
        // An XCI goes in as a whole decrypted gamecard image, so nxformat keeps the firmware of
        // its update partition. Other packages go in as a decrypted NSP.
        std::string gamecard_error;
        auto decrypted =
            file != nullptr ? FileSys::OpenDecryptedGamecard(file, gamecard_error) : nullptr;
        const bool gamecard = decrypted != nullptr;
        if (!gamecard && file != nullptr) {
            if (gamecard_error != "not a gamecard image") {
                LOG_WARNING(Frontend,
                            "{}: converted without its gamecard update partition (firmware): {}",
                            ToUtf8(path.filename()), gamecard_error);
            }
            decrypted = FileSys::OpenDecryptedPackage(file, error);
        }
        if (decrypted == nullptr) {
            return QString::fromStdString(error);
        }
        auto& held =
            files.emplace_back(std::make_unique<FileSys::VirtualFile>(std::move(decrypted)));
        const auto name = ToUtf8(path.stem()) + (gamecard ? ".xci" : ".nsp");
        auto* source =
            nxformat_source_new(name.c_str(), (*held)->GetSize(), &ReadSource, held.get());
        if (source == nullptr) {
            return QStringLiteral("out of memory");
        }
        sources.push_back(source);
        args.emplace_back(nxformat_source_path(source));
        return {};
    }
};

namespace {

QString CannotDecrypt(const std::filesystem::path& path, const QString& error) {
    return NxTools::tr("%1 cannot be decrypted: %2\n\nCheck that your keys are installed.")
        .arg(ToQString(path.filename()), error);
}

// Lists an archive with "contents", one tab-split line at a time. Returns the error, empty on
// success.
QString ListContents(const std::string& archive,
                     const std::function<void(const QStringList&)>& on_line) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const char* args[] = {"contents", archive.c_str()};
    char error[512]{};
    const auto code = nxformat_run(
        args, 2,
        [](void* user, int is_error, const char* line) {
            if (!is_error) {
                (*static_cast<const std::function<void(const QStringList&)>*>(user))(
                    QString::fromUtf8(line).split(QLatin1Char('\t')));
            }
        },
        const_cast<std::function<void(const QStringList&)>*>(&on_line), nullptr, error,
        sizeof(error));
    QApplication::restoreOverrideCursor();
    if (code == NXFORMAT_OK) {
        return {};
    }
    return error[0] != '\0' ? QString::fromUtf8(error) : QStringLiteral("unknown error");
}

} // namespace

NxTools::NxTools(FileSys::VirtualFilesystem vfs_, QWidget* parent)
    : QObject(parent), vfs{std::move(vfs_)}, parent_widget{parent} {}

NxTools::~NxTools() {
    if (running) {
        nxformat_cancel_set(running->cancel);
        running->future.waitForFinished();
    }
}

bool NxTools::Loaded() const {
#ifdef LICHII_NXFORMAT_DYNAMIC
    std::string error;
    if (NxFormatDynamic::Load(error)) {
        return true;
    }
    ArchiveNotice(parent_widget, tr("NX Archive"),
                         tr("The NX tools need nxformat.dll next to the Lichii executable.\n\n%1")
                             .arg(QString::fromStdString(error)));
    return false;
#else
    return true;
#endif
}

bool NxTools::CanConvert(const std::string& game_path) {
    return KindOf(game_path) != Kind::None;
}

bool NxTools::IsNxArchive(const std::string& game_path) {
    return HasExtension(game_path, ".nx");
}

void NxTools::ConvertToNx(const std::string& game_path) {
    if (running || !Loaded()) {
        return;
    }
    const QString title = tr("Convert to NX");
    const auto kind = KindOf(game_path);
    auto job = std::make_shared<Job>();
    job->title = tr("Converting to NX");

    std::filesystem::path source = ToPath(game_path);
    std::filesystem::path suggested;
    if (kind == Kind::Dump) {
        source = *FileSys::Nup::DumpRootFromMain(source);
        auto name = source.filename();
        name += ".nx";
        suggested = source.parent_path() / name;
    } else {
        suggested = source;
        suggested.replace_extension(".nx");
    }
    std::vector<std::filesystem::path> related;
    std::vector<ArchivePackage> related_packages;
    if (kind == Kind::Package) {
        related = RelatedPackages(vfs, source);
        for (const auto& related_path : related) {
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(related_path, ec);
            related_packages.push_back(
                {ToQString(related_path.filename()), {}, ec ? 0 : bytes, true});
        }
    }
    NxConvertDialog dialog(ToQString(source), std::move(related_packages), suggested,
                           kind != Kind::Nup, parent_widget);
    const int result = dialog.exec();
    if (result == ArchiveConvertDialog::OtherFormat) {
        emit ConvertToNupRequested(game_path);
        return;
    }
    if (result != QDialog::Accepted) {
        return;
    }
    const auto output =
        FrontendCommon::NxArchive::WithNxExtension(ToUtf8(ToQString(dialog.Archive())));
    const QString chosen = QString::fromStdString(output);
    if (std::error_code ec; std::filesystem::exists(ToPath(output), ec)) {
        ArchiveNotice(parent_widget, title,
                             tr("The archive already exists:\n%1").arg(chosen));
        return;
    }

    switch (kind) {
    case Kind::Package: {
        // Switch 1 NSP or XCI: decrypted on the fly, as a DNSP view of the package.
        job->args = {"pack"};
        if (const auto error = job->AddPackage(vfs, source); !error.isEmpty()) {
            ArchiveNotice(parent_widget, title, CannotDecrypt(source, error));
            return;
        }
        job->originals.push_back(source);

        // Updates and DLC of the same game next to it go into the same archive.
        const auto included = dialog.IncludedPackages();
        for (std::size_t i = 0; i < related.size(); ++i) {
            if (!included[i]) {
                continue;
            }
            if (const auto error = job->AddPackage(vfs, related[i]); !error.isEmpty()) {
                ArchiveNotice(parent_widget, title, CannotDecrypt(related[i], error),
                              ArchiveTone::Error);
                return;
            }
            job->originals.push_back(related[i]);
        }
        job->args.push_back(output);
        job->args.push_back("--frontend");
        break;
    }
    case Kind::Nup: {
        const auto file = vfs->OpenFile(game_path, FileSys::OpenMode::Read);
        job->args = {
            "pack-nup",   game_path,   output, "--platform", PlatformArgument(NupIsSwitch2(file)),
            "--frontend", "--no-trail"};
        break;
    }
    case Kind::Dump:
        job->args = {"pack-tree",
                     ToUtf8(source),
                     output,
                     "--platform",
                     PlatformArgument(DumpIsSwitch2(vfs, source)),
                     "--frontend",
                     "--no-trail"};
        break;
    case Kind::None:
        return;
    }
    job->done_message = tr("The NX archive was created:\n%1").arg(chosen);
    if (job->originals.empty()) {
        job->originals.push_back(source);
    }
    Start(std::move(job));
}

void NxTools::AddToNx(const std::string& archive_path) {
    ManageNx(archive_path);
}

void NxTools::RemoveFromNx(const std::string& archive_path) {
    ManageNx(archive_path);
}

void NxTools::ManageNx(const std::string& archive_path) {
    if (running || !Loaded()) {
        return;
    }
    const QString title = tr("Updates and DLC");
    // Read what the card shows and let go of the file: the job locks it for editing.
    u32 platform = 0;
    std::string game_title;
    std::vector<u8> icon_bytes;
    if (const auto archive = vfs->OpenFile(archive_path, FileSys::OpenMode::Read);
        archive != nullptr) {
        platform = Loader::AppLoader_NX::ReadPlatform(archive, 0);
        if (QtCommon::system != nullptr) {
            Loader::AppLoader_NX loader(*QtCommon::system, archive);
            loader.ReadTitle(game_title);
            loader.ReadIcon(icon_bytes);
        }
    }
    if (platform == 0) {
        ArchiveNotice(parent_widget, title, tr("This NX archive cannot be read."),
                      ArchiveTone::Error);
        return;
    }
    const bool switch2 = platform == 2;
    std::vector<Snapshot> snapshots;
    if (const auto error = ListContents(archive_path, [&](const QStringList& fields) {
            if (fields.size() >= 8 && fields[0] == QStringLiteral("@SNAPSHOT")) {
                snapshots.push_back(
                    {fields[1], fields[2], fields[4], fields[5], fields[6], fields[7]});
            }
        });
        !error.isEmpty()) {
        ArchiveNotice(parent_widget, title, tr("This NX archive cannot be read: %1").arg(error),
                      ArchiveTone::Error);
        return;
    }
    QPixmap icon;
    icon.loadFromData(icon_bytes.data(), static_cast<uint>(icon_bytes.size()));
    std::vector<ArchiveContentsDialog::Entry> entries;
    for (const auto& s : snapshots) {
        const QString version = s.display_version.isEmpty() ? s.version : s.display_version;
        if (s.kind == QStringLiteral("base")) {
            const QString file_name =
                QFileInfo(QString::fromStdString(archive_path)).completeBaseName();
            const QString name =
                game_title.empty() ? file_name : QString::fromStdString(game_title);
            entries.push_back(
                {name, tr("Base game · v%1").arg(version), ArchiveContentsDialog::Kind::Base});
        } else if (s.kind == QStringLiteral("dlc")) {
            entries.push_back({s.name.isEmpty() ? tr("DLC %1").arg(s.title_id.toUpper()) : s.name,
                               tr("DLC · %1").arg(s.title_id.toUpper()),
                               ArchiveContentsDialog::Kind::Dlc});
        } else {
            entries.push_back({tr("Update v%1").arg(version), tr("Version %1").arg(s.version),
                               ArchiveContentsDialog::Kind::Update});
        }
    }
    std::error_code ec;
    const auto archive_bytes = std::filesystem::file_size(ToPath(archive_path), ec);
    // Native Switch 1 packages only; a Switch 2 game takes extracted update folders.
    ArchiveContentsDialog contents(ToQString(ToPath(archive_path).filename()),
                                   ec ? 0 : archive_bytes, icon, entries, !switch2, parent_widget);
    if (contents.exec() != QDialog::Accepted) {
        return;
    }

    auto job = std::make_shared<Job>();
    switch (contents.Chosen()) {
    case ArchiveContentsDialog::Choice::None:
        return;
    case ArchiveContentsDialog::Choice::Remove: {
        const auto& entry = entries[contents.Removed()];
        ArchiveMessageDialog confirm(
            tr("Remove %1?").arg(entry.title),
            tr("It is removed from the archive in place. The base game and the other content "
               "stay."),
            parent_widget, ArchiveTone::Warning);
        confirm.AddButton(tr("Cancel"), {}, false);
        confirm.AddButton(tr("Remove"), QStringLiteral("trash"), true);
        if (confirm.exec() != QDialog::Accepted) {
            return;
        }
        job->title = tr("Removing from NX");
        job->args = {"remove", archive_path, ToUtf8(snapshots[contents.Removed()].identity),
                     "--frontend"};
        job->done_message = tr("%1 was removed from the NX archive.").arg(entry.title);
        break;
    }
    case ArchiveContentsDialog::Choice::AddFolder: {
        job->title = tr("Adding to NX");
        const QString chosen = QFileDialog::getExistingDirectory(
            parent_widget, tr("Select the Update Folder"),
            ToQString(ToPath(archive_path).parent_path()));
        if (chosen.isEmpty()) {
            return;
        }
        const QString folder =
            ToQString(FrontendCommon::NxArchive::UpdateFolder(ToPath(ToUtf8(chosen))));
        job->args = {
            "add-tree",   archive_path, ToUtf8(folder), "--platform", PlatformArgument(switch2),
            "--frontend", "--no-trail"};
        // A folder that holds exefs/ itself is one update; its version comes from the name.
        if (QFileInfo(folder + QStringLiteral("/exefs")).isDir()) {
            auto version = VersionFromName(QFileInfo(folder).fileName());
            if (!version) {
                bool ok = false;
                const int value = QInputDialog::getInt(
                    parent_widget, title,
                    tr("Version number of this update (for example 196608 for v196608):"), 65536,
                    1, std::numeric_limits<int>::max(), 1, &ok);
                if (!ok) {
                    return;
                }
                version = static_cast<u32>(value);
            }
            job->args.push_back("--update-version");
            job->args.push_back(std::to_string(*version));
        }
        job->done_message = tr("The NX archive was updated.");
        break;
    }
    case ArchiveContentsDialog::Choice::AddFiles: {
        job->title = tr("Adding to NX");
        const QStringList files = QFileDialog::getOpenFileNames(
            parent_widget, tr("Select Updates or DLC"),
            ToQString(ToPath(archive_path).parent_path()), tr("Switch packages (*.nsp *.xci)"));
        if (files.isEmpty()) {
            return;
        }
        job->args = {"add", archive_path};
        for (const auto& name : files) {
            const auto path = ToPath(ToUtf8(name));
            if (const auto error = job->AddPackage(vfs, path); !error.isEmpty()) {
                ArchiveNotice(parent_widget, title, CannotDecrypt(path, error), ArchiveTone::Error);
                return;
            }
        }
        job->args.push_back("--frontend");
        job->done_message = tr("The NX archive was updated.");
        break;
    }
    }
    // The game list's content provider keeps the archive open with a shared lock, which the
    // job's edit lock waits on. Drop it (the refresh after the job fills it again).
    if (QtCommon::provider != nullptr && !QtCommon::system->IsPoweredOn()) {
        QtCommon::provider->ClearAllEntries();
    }
    Start(std::move(job));
}

bool NxTools::ReadNxMetadata(const std::string& archive_path, NxMetadata& out) {
    const QString title = tr("Edit title and icon");
    auto error = ListContents(archive_path, [&](const QStringList& fields) {
        if (out.application_id.isEmpty() && fields.size() >= 2 &&
            fields[0] == QStringLiteral("@APP")) {
            out.application_id = fields[1];
        }
    });
    if (error.isEmpty() && out.application_id.isEmpty()) {
        error = QStringLiteral("no application");
    }
    if (!error.isEmpty()) {
        ArchiveNotice(parent_widget, title,
                             tr("This NX archive cannot be read: %1").arg(error));
        return false;
    }
    std::string current_title;
    if (const auto file = vfs->OpenFile(archive_path, FileSys::OpenMode::Read);
        file != nullptr && QtCommon::system != nullptr) {
        Loader::AppLoader_NX loader(*QtCommon::system, file);
        loader.ReadTitle(current_title);
        loader.ReadIcon(out.current.icon);
        loader.ReadPublisher(out.current.publisher);
        loader.ReadOriginalIcon(out.application_id.toULongLong(nullptr, 16), out.original_icon);
    }
    // Start from the name the game list shows: the title, or the file name without one.
    out.current.title_name =
        current_title.empty()
            ? QFileInfo(QString::fromStdString(archive_path)).completeBaseName().toStdString()
            : current_title;
    return true;
}

void NxTools::EditNxMetadata(const std::string& archive_path,
                             const std::function<bool()>& prepare_write) {
    if (running || !Loaded()) {
        return;
    }
    NxMetadata metadata;
    if (!ReadNxMetadata(archive_path, metadata)) {
        return;
    }
    ArchiveEditDialog dialog(ArchiveEditDialog::Format::Nx, ToPath(archive_path), metadata.current,
                             metadata.original_icon, parent_widget);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    WriteNxMetadata(archive_path, metadata, dialog.Edit(), dialog.CurrentIcon(), prepare_write);
}

void NxTools::SetNxIcon(const std::string& archive_path, const QByteArray& icon,
                        const std::function<bool()>& prepare_write) {
    if (running || !Loaded()) {
        return;
    }
    NxMetadata metadata;
    if (!ReadNxMetadata(archive_path, metadata)) {
        return;
    }
    FileSys::Nup::MetadataEdit edit;
    edit.icon = std::vector<u8>(icon.begin(), icon.end());
    QPixmap shown;
    shown.loadFromData(icon);
    WriteNxMetadata(archive_path, metadata, edit, shown, prepare_write);
}

void NxTools::WriteNxMetadata(const std::string& archive_path, const NxMetadata& metadata,
                              const FileSys::Nup::MetadataEdit& edit, const QPixmap& icon,
                              const std::function<bool()>& prepare_write) {
    const QString title = tr("Edit title and icon");
    auto job = std::make_shared<Job>();
    job->title = tr("Saving into the NX archive");
    job->display_title = QString::fromStdString(edit.title_name.value_or(metadata.current.title_name));
    job->display_icon = icon;
    job->args = {"set-metadata", archive_path, ToUtf8(metadata.application_id)};
    if (edit.title_name &&
        FrontendCommon::NxArchive::IsTitleEdit(*edit.title_name, metadata.current.title_name)) {
        job->args.push_back("title=" + *edit.title_name);
    }
    if (edit.publisher && !edit.publisher->empty()) {
        job->args.push_back("publisher=" + *edit.publisher);
    }
    if (edit.icon && !edit.icon->empty()) {
        job->icon_file = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/lichii-icon-XXXXXX"));
        if (!job->icon_file->open() ||
            job->icon_file->write(reinterpret_cast<const char*>(edit.icon->data()),
                                  static_cast<qint64>(edit.icon->size())) !=
                static_cast<qint64>(edit.icon->size()) ||
            !job->icon_file->flush()) {
            ArchiveNotice(parent_widget, title, tr("The icon cannot be saved."));
            return;
        }
        job->icon_file->close();
        job->args.push_back("icon=" + ToUtf8(job->icon_file->fileName()));
    }
    if (job->args.size() == 3 || running || !prepare_write()) {
        return;
    }
    job->done_message = tr("The new details were written into the NX archive, so they show on every PC that "
                           "opens it.");
    Start(std::move(job));
}

void NxTools::Start(std::shared_ptr<Job> job) {
    running = job;
    emit RunningChanged(true);

    progress = new NupProgressDialog(job->title, job->display_title, job->display_icon,
                                     parent_widget);
    connect(progress, &NupProgressDialog::CancelRequested, this, [this, job] {
        nxformat_cancel_set(job->cancel);
        if (progress) {
            progress->SetCancelling();
        }
    });
    job->clock.start();
    auto* timer = new QTimer(progress);
    connect(timer, &QTimer::timeout, this, [this, job] {
        if (!progress) {
            return;
        }
        // nxformat counts the bytes it reads, not the checks it makes before and after:
        // those steps are shown as preparing and finishing, with the time elapsed.
        const auto total = job->total.load();
        const auto done = std::min(job->written.load(), total);
        const qint64 now = job->clock.elapsed();
        QString step;
        if (total == 0) {
            step = tr("Preparing");
        } else if (done >= total) {
            step = tr("Finishing");
        } else {
            if (job->counting_since < 0) {
                job->counting_since = now;
            }
            // Time left: measured from the first counted byte, smoothed so it does not jump.
            const double fraction = static_cast<double>(done) / static_cast<double>(total);
            const double counting = (now - job->counting_since) / 1000.0;
            if (fraction >= 0.01 && counting >= 5.0) {
                const double estimate = counting * (1.0 - fraction) / fraction;
                job->seconds_left = job->seconds_left < 0
                                        ? estimate
                                        : job->seconds_left * 0.9 + estimate * 0.1;
                step = tr("%1 left").arg(TimeLeft(job->seconds_left));
            }
        }
        const qint64 seconds = now / 1000;
        const QString elapsed =
            seconds >= 3600
                ? QStringLiteral("%1:%2:%3")
                      .arg(seconds / 3600)
                      .arg(seconds / 60 % 60, 2, 10, QLatin1Char('0'))
                      .arg(seconds % 60, 2, 10, QLatin1Char('0'))
                : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10,
                                                                 QLatin1Char('0'));
        QString text = tr("%1 · %2 elapsed").arg(job->title, elapsed);
        if (!step.isEmpty()) {
            text += QStringLiteral(" · ") + step;
        }
        progress->SetStep(text);
        progress->SetProgress({.bytes_done = std::min<u64>(done, total ? total - 1 : 0),
                               .bytes_total = total});
    });
    timer->start(500);
    progress->show();

    job->future = QtConcurrent::run([this, job] {
        std::vector<const char*> args;
        args.reserve(job->args.size());
        for (const auto& arg : job->args) {
            args.push_back(arg.c_str());
        }
        char error[1024]{};
        const auto code = nxformat_run(
            args.data(), args.size(),
            [](void* user, int is_error, const char* line) {
                auto& j = *static_cast<Job*>(user);
                if (is_error) {
                    return;
                }
                const auto fields = QString::fromUtf8(line).split(QLatin1Char('\t'));
                if (fields[0] == QStringLiteral("@TOTAL") && fields.size() >= 2) {
                    j.total = j.total.load() + fields[1].toULongLong();
                } else if (fields[0] == QStringLiteral("@PROGRESS") && fields.size() >= 2) {
                    j.written = fields[1].toULongLong();
                } else if (fields[0] == QStringLiteral("@ADDED") && fields.size() >= 4) {
                    std::scoped_lock lock{j.lines_mutex};
                    j.added.push_back(fields[1] + QLatin1Char(' ') + fields[2].toUpper() +
                                      QStringLiteral(" v") + fields[3]);
                } else if (fields[0] == QStringLiteral("@SKIPPED") && fields.size() >= 4) {
                    std::scoped_lock lock{j.lines_mutex};
                    auto entry = fields[1] + QLatin1Char(' ') + fields[2].toUpper() +
                                 QStringLiteral(" v") + fields[3];
                    // A retry finds what the failed run added: it is listed as added.
                    if (!j.added.contains(entry)) {
                        j.skipped.push_back(std::move(entry));
                    }
                }
            },
            job.get(), job->cancel, error, sizeof(error));
        QMetaObject::invokeMethod(
            this, [this, job, code, message = QString::fromUtf8(error)] {
                Finished(job, code, message);
            },
            Qt::QueuedConnection);
    });
}

void NxTools::ReplyOriginal(Job& job, const QString& text) {
    const auto& originals = job.originals;
    QStringList names;
    for (const auto& original : originals) {
        names.push_back(ToQString(original));
    }
    QString label;
    if (originals.size() > 1) {
        label = tr("Move the %1 original packages to the Recycle Bin").arg(originals.size());
    } else if (std::error_code ec; std::filesystem::is_directory(originals.front(), ec)) {
        label = tr("Move the original folder to the Recycle Bin:\n%1").arg(names.front());
    } else {
        label = tr("Move the original game to the Recycle Bin:\n%1").arg(names.front());
    }
    ArchiveMessageDialog box(job.title, text, parent_widget, ArchiveTone::Success);
    auto* remove = box.AddCheckBox(label, false);
    box.AddButton(tr("Done"), {}, true);
    box.exec();
    if (remove->isChecked()) {
        // nxformat has returned; close what it read before moving the originals.
        job.ReleaseSources();
        // The game list's content provider keeps every package it scanned open, for updates and
        // DLC. Drop it (the refresh below fills it again), unless a game is running.
        if (QtCommon::provider != nullptr && !QtCommon::system->IsPoweredOn()) {
            QtCommon::provider->ClearAllEntries();
        }
        int failed = 0;
        for (const auto& name : names) {
            if (!QFile::moveToTrash(name)) {
                ++failed;
            }
        }
        if (failed > 0) {
            ArchiveNotice(
                parent_widget, job.title,
                originals.size() > 1
                    ? tr("%1 of the %2 original packages could not be moved to the Recycle Bin. "
                         "They may be in use.")
                          .arg(failed)
                          .arg(originals.size())
                    : tr("The original could not be moved to the Recycle Bin:\n%1\n\n"
                         "It may be in use. You can delete it yourself.")
                          .arg(names.front()));
        }
    }
    emit ArchivesChanged();
}

QString NxTools::TimeLeft(double seconds) {
    const auto minutes = static_cast<qint64>(seconds / 60.0 + 0.5);
    if (seconds < 60.0) {
        return tr("less than a minute");
    }
    if (minutes < 60) {
        return tr("about %n minute(s)", nullptr, static_cast<int>(minutes));
    }
    return tr("about %1 h %2 min").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

void NxTools::Finished(std::shared_ptr<Job> job, int code, QString error) {
    if (progress) {
        // Closing the card asks for a cancel: disconnect first, or a failed or finished job shows
        // "Cancelling" behind its result.
        QObject::disconnect(progress, nullptr, this, nullptr);
        progress->Finish();
        progress->deleteLater();
        progress.clear();
    }
    running.reset();
    emit RunningChanged(false);
    const bool metadata_write = job->args.front() == "set-metadata";
    if (metadata_write) {
        emit MetadataWriteFinished(code == NXFORMAT_OK);
    }

    // add keeps the content it committed before a failure or a cancel, also across a retry:
    // report it and refresh.
    const bool changed = !job->added.isEmpty();
    const QString kept = changed ? QStringLiteral("\n\n") +
                                       tr("Already added to the archive: %1")
                                           .arg(job->added.join(QStringLiteral(", ")))
                                 : QString{};
    switch (code) {
    case NXFORMAT_OK: {
        QString text = job->done_message;
        if (!job->originals.empty()) {
            ReplyOriginal(*job, text);
            break;
        }
        if (!job->added.isEmpty()) {
            text += QStringLiteral("\n\n") + tr("Added: %1").arg(job->added.join(QStringLiteral(", ")));
        }
        if (!job->skipped.isEmpty()) {
            text += QStringLiteral("\n\n") +
                    tr("Already in the archive: %1").arg(job->skipped.join(QStringLiteral(", ")));
        }
        ArchiveNotice(parent_widget, metadata_write ? tr("Saved into the NX archive") : job->title,
                      text, ArchiveTone::Success);
        if (!metadata_write) {
            emit ArchivesChanged();
        }
        break;
    }
    case NXFORMAT_CANCELLED:
        if (changed) {
            ArchiveNotice(parent_widget, job->title, tr("Cancelled.") + kept,
                          ArchiveTone::Warning);
            emit ArchivesChanged();
        } else {
            ArchiveNotice(parent_widget, job->title, tr("Cancelled. Nothing was changed on disk."),
                          ArchiveTone::Warning);
        }
        break;
    default: {
        if (job->CanRetryWithoutRecipe(error)) {
            ArchiveMessageDialog retry(
                job->title,
                tr("The operation failed because the rebuild recipe could not be kept:\n%1\n\n"
                   "Try again without the recipe? The game still runs from the NX archive, but "
                   "the original package cannot be rebuilt exactly from it.")
                        .arg(error) +
                    kept,
                parent_widget, ArchiveTone::Error);
            retry.AddButton(tr("Cancel"), {}, false);
            retry.AddButton(tr("Try again"), QStringLiteral("arrow-counter-clockwise"), true);
            if (retry.exec() == QDialog::Accepted) {
                // The retry refreshes the archives when it ends.
                job->PrepareRetryWithoutRecipe();
                Start(std::move(job));
                break;
            }
        } else {
            ArchiveNotice(parent_widget, job->title,
                          tr("The operation failed:\n%1").arg(error) + kept, ArchiveTone::Error);
        }
        if (changed) {
            emit ArchivesChanged();
        }
        break;
    }
    }
}
