// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/mod_store/mod_store_install.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <utility>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtEndian>
#include <quazip.h>
#include <quazipfile.h>
#include <zlib.h>

namespace ModStore {
namespace {

using namespace Qt::StringLiterals;

constexpr qint64 MaxArchiveBytes = 64 * 1024 * 1024;
constexpr quint64 MaxExpandedBytes = 256 * 1024 * 1024;
constexpr int MaxEntries = 4096;
constexpr qint64 MaxManifestBytes = 16 * 1024;
constexpr qint64 ChunkSize = 64 * 1024;
const QString ManifestName = u".lichii-mod-store.json"_s;
const QString StagingTemplate = u".lichii-mod-store-XXXXXX"_s;

std::mutex install_mutex;

bool IsTitleId(const QString& title_id) {
    static const QRegularExpression pattern(u"\\A[0-9a-fA-F]{16}\\z"_s);
    return pattern.match(title_id).hasMatch();
}

bool IsReservedName(const QString& component) {
    static const QRegularExpression pattern(
        u"\\A(?:CON|PRN|AUX|NUL|CLOCK\\$|CONIN\\$|CONOUT\\$|(?:COM|LPT)[0-9¹²³])\\z"_s);
    return pattern.match(component.section(u'.', 0, 0).trimmed().toUpper()).hasMatch();
}

bool IsModId(const QString& mod_id) {
    static const QRegularExpression pattern(u"\\A[a-z0-9][a-z0-9-]{0,62}\\z"_s);
    return pattern.match(mod_id).hasMatch() && !IsReservedName(mod_id);
}

bool IsDisplayMetadata(const QString& name, const QString& version) {
    return !name.trimmed().isEmpty() && name.size() <= 512 && !version.trimmed().isEmpty() &&
           version.size() <= 256;
}

bool IsLink(const QFileInfo& info) {
    return info.isSymLink() || info.isJunction();
}

bool IsDirectory(const QString& path) {
    const QFileInfo info(path);
    return info.isDir() && !IsLink(info);
}

bool IsRegularFile(const QString& path) {
    const QFileInfo info(path);
    return info.isFile() && !IsLink(info);
}

bool IsAbsent(const QString& path) {
    const QFileInfo info(path);
    return !info.exists() && !IsLink(info);
}

bool HasNameConflict(const QDir& parent, const QString& name) {
    const auto entries =
        parent.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    return std::ranges::any_of(entries, [&](const QString& entry) {
        return entry != name && entry.compare(name, Qt::CaseInsensitive) == 0;
    });
}

std::optional<InstalledMod> ReadOwnedMod(const QString& path, const QString& title_id,
                                         const QString& mod_id) {
    const QString manifest_path = QDir(path).filePath(ManifestName);
    QFile file(manifest_path);
    if (!IsDirectory(path) || !IsRegularFile(manifest_path) || !file.open(QIODevice::ReadOnly) ||
        file.size() > MaxManifestBytes) {
        return std::nullopt;
    }
    const QByteArray bytes = file.read(MaxManifestBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > MaxManifestBytes) {
        return std::nullopt;
    }
    const QJsonObject object = QJsonDocument::fromJson(bytes).object();
    InstalledMod mod{
        .id = object[u"id"_s].toString(),
        .version = object[u"version"_s].toString(),
        .name = object[u"name"_s].toString(),
        .sha256 = {},
    };
    if (object[u"schema"_s] != QJsonValue(1) || object[u"title_id"_s].toString() != title_id ||
        !IsModId(mod.id) || (!mod_id.isEmpty() && mod.id != mod_id) ||
        !IsDisplayMetadata(mod.name, mod.version)) {
        return std::nullopt;
    }
    if (const QJsonValue digest = object.value(u"sha256"_s); !digest.isUndefined()) {
        static const QRegularExpression pattern(u"\\A[0-9a-f]{64}\\z"_s);
        const QString hex = digest.toString();
        if (!pattern.match(hex).hasMatch()) {
            return std::nullopt;
        }
        mod.sha256 = QByteArray::fromHex(hex.toLatin1());
    }
    return mod;
}

bool HasSafeContents(const QString& path) {
    QDirIterator entries(path,
                         QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        entries.next();
        const QFileInfo info = entries.fileInfo();
        if (IsLink(info) || (!info.isDir() && !info.isFile())) {
            return false;
        }
    }
    return true;
}

void RestoreInterruptedUpdates(const QDir& mods_dir, const QDir& title_dir, const QString& title) {
    const QString pattern = StagingTemplate.left(StagingTemplate.size() - 6) + u"??????"_s;
    const auto staged = mods_dir.entryList({pattern}, QDir::Dirs | QDir::Hidden | QDir::NoSymLinks);
    for (const auto& entry : staged) {
        const QDir transaction(mods_dir.filePath(entry));
        if (!IsDirectory(transaction.path())) {
            continue;
        }
        const QString backup = transaction.filePath(u"previous"_s);
        const QString pending = transaction.filePath(u"new"_s);
        const auto mod = ReadOwnedMod(backup, title, {});
        if (!mod || !ReadOwnedMod(pending, title, mod->id) || HasNameConflict(title_dir, mod->id) ||
            !HasSafeContents(backup)) {
            continue;
        }
        const auto destination = title_dir.filePath(mod->id);
        if (IsAbsent(destination) && QDir().rename(backup, destination)) {
            if (HasSafeContents(pending)) {
                QDir(pending).removeRecursively();
            }
            QDir().rmdir(transaction.path());
        }
    }
}

struct ZipEntry {
    QStringList parts;
    QString relative_path;
    quint64 compressed_size;
    quint64 expanded_size;
    quint32 crc;
    quint16 method;
    bool directory;
};

int ReadDirectoryCount(QTemporaryFile& file) {
    constexpr int footer_size = 22;
    constexpr int header_size = 46;
    constexpr qint64 max_directory_bytes = 4 * 1024 * 1024;
    const qint64 end = file.size() - footer_size;
    if (end < 0 || !file.seek(0) || file.read(4) != QByteArrayLiteral("PK\x03\x04") ||
        !file.seek(end)) {
        return 0;
    }
    const QByteArray footer = file.read(footer_size);
    if (footer.size() != footer_size || !footer.startsWith("PK\x05\x06") ||
        qFromLittleEndian<quint32>(footer.constData() + 4) != 0 ||
        qFromLittleEndian<quint16>(footer.constData() + 20) != 0) {
        return 0;
    }
    const int count = qFromLittleEndian<quint16>(footer.constData() + 10);
    const qint64 size = qFromLittleEndian<quint32>(footer.constData() + 12);
    const qint64 offset = qFromLittleEndian<quint32>(footer.constData() + 16);
    if (count < 1 || count > MaxEntries || size > max_directory_bytes || offset + size != end ||
        qFromLittleEndian<quint16>(footer.constData() + 8) != count) {
        return 0;
    }
    qint64 pos = offset;
    QSet<quint32> local_offsets;
    for (int i = 0; i < count; ++i) {
        if (end - pos < header_size || !file.seek(pos)) {
            return 0;
        }
        const QByteArray header = file.read(header_size);
        if (header.size() != header_size || !header.startsWith("PK\x01\x02")) {
            return 0;
        }
        const quint32 local = qFromLittleEndian<quint32>(header.constData() + 42);
        const quint32 compressed = qFromLittleEndian<quint32>(header.constData() + 20);
        if (offset - local - 30 < compressed || local_offsets.contains(local)) {
            return 0;
        }
        local_offsets.insert(local);
        pos += header_size + qFromLittleEndian<quint16>(header.constData() + 28) +
               qFromLittleEndian<quint16>(header.constData() + 30) +
               qFromLittleEndian<quint16>(header.constData() + 32);
    }
    return pos == end && file.seek(0) ? count : 0;
}

bool IsPortableComponent(const QString& component, bool external) {
    static const QRegularExpression server_name(u"\\A[A-Za-z0-9][A-Za-z0-9._ -]{0,119}\\z"_s);
    static const QRegularExpression forbidden(u"[\\p{Cc}\\p{Cf}\\x{fffd}<>:\"|?*~\\\\]"_s);
    return !component.isEmpty() && component.toUtf8().size() <= 255 && !component.endsWith(u'.') &&
           !component.endsWith(u' ') && !IsReservedName(component) &&
           (external ? !component.contains(forbidden) : server_name.match(component).hasMatch());
}

bool HasSafeExtraFields(QByteArrayView extra) {
    while (!extra.empty()) {
        if (extra.size() < 4) {
            return false;
        }
        const quint16 tag = qFromLittleEndian<quint16>(extra.data());
        const quint16 size = qFromLittleEndian<quint16>(extra.data() + 2);
        if (size > extra.size() - 4 || tag == 0x0001 || tag == 0x000d || tag == 0x756e) {
            return false;
        }
        extra = extra.sliced(size + 4);
    }
    return true;
}

QList<ZipEntry> ReadEntries(QuaZip& zip, int count, quint64 archive_bytes, bool external) {
    struct PathNode {
        QString spelling;
        bool directory;
        bool explicit_entry;
    };
    QHash<QString, PathNode> paths;
    QList<ZipEntry> entries;
    entries.reserve(count);
    quint64 expanded = 0;
    quint64 compressed = 0;
    for (bool more = zip.goToFirstFile(); more; more = zip.goToNextFile()) {
        QuaZipFileInfo64 info;
        if (entries.size() >= count || !zip.getCurrentFileInfo(&info) ||
            info.diskNumberStart != 0 || (info.flags & ~0x080e) != 0 ||
            (info.method != 0 && info.method != Z_DEFLATED) ||
            (info.method == 0 && info.compressedSize != info.uncompressedSize) ||
            info.compressedSize > archive_bytes - compressed ||
            info.uncompressedSize > MaxExpandedBytes - expanded ||
            !HasSafeExtraFields(info.extra)) {
            return {};
        }

        const bool directory = info.name.endsWith(u'/');
        const quint32 mode_type = info.externalAttr >> 16 & 0170000;
        const QString path = directory ? info.name.chopped(1) : info.name;
        QStringList parts = path.split(u'/');
        if ((mode_type != 0 && mode_type != (directory ? 0040000U : 0100000U)) ||
            (info.externalAttr & 0x0408) != 0 ||
            (!directory && (info.externalAttr & 0x0010) != 0) ||
            (directory && info.uncompressedSize != 0) ||
            path.toUtf8().size() > (external ? 512 : 220) || parts.size() > (external ? 24 : 16)) {
            return {};
        }

        QString prefix;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            const QString& component = parts[i];
            if (!IsPortableComponent(component, external)) {
                return {};
            }
            prefix = prefix.isEmpty() ? component : prefix + u'/' + component;
            const QString key = prefix.normalized(QString::NormalizationForm_C).toCaseFolded();
            const bool last = i + 1 == parts.size();
            const bool node_is_directory = directory || !last;
            auto existing = paths.find(key);
            if (existing == paths.end()) {
                paths.insert(key, {prefix, node_is_directory, last});
            } else if (existing->spelling != prefix || existing->directory != node_is_directory ||
                       (last && existing->explicit_entry)) {
                return {};
            } else if (last) {
                existing->explicit_entry = true;
            }
        }

        expanded += info.uncompressedSize;
        compressed += info.compressedSize;
        entries.append({std::move(parts),
                        {},
                        info.compressedSize,
                        info.uncompressedSize,
                        info.crc,
                        info.method,
                        directory});
    }
    if (zip.getZipError() != 0 || entries.size() != count) {
        return {};
    }
    return entries;
}

bool IsPayloadRoot(const QString& name) {
    return name == u"exefs" || name == u"romfs" || name == u"cheats" || name == u"romfslite" ||
           name == u"romfs_ext";
}

bool IsDocumentation(const QString& name) {
    static const QRegularExpression pattern(
        u"\\A(?:readme|license|licence)(?:\\.(?:txt|md|rst|markdown))?\\z"_s,
        QRegularExpression::CaseInsensitiveOption);
    return pattern.match(name).hasMatch();
}

std::optional<QStringList> FindPackageRoot(const QStringList& path, const QString& title_id,
                                           bool external) {
    qsizetype depth = 0;
    if (external) {
        const QString first = path.front().toLower();
        if (!IsPayloadRoot(first) && first != u"atmosphere") {
            if (first == u"contents" || IsTitleId(first)) {
                return std::nullopt;
            }
            ++depth;
        }
        if (path.size() > depth && path[depth].compare(u"atmosphere"_s, Qt::CaseInsensitive) == 0) {
            if (path.size() < depth + 5 ||
                path[depth + 1].compare(u"contents"_s, Qt::CaseInsensitive) != 0 ||
                path[depth + 2].toUpper() != title_id) {
                return std::nullopt;
            }
            depth += 3;
        }
    }
    if (path.size() > depth + 1 && IsPayloadRoot(external ? path[depth].toLower() : path[depth])) {
        return path.mid(0, depth);
    }
    return std::nullopt;
}

bool PlanExtraction(QList<ZipEntry>& entries, const QString& title_id, bool external) {
    std::optional<QStringList> package_root;
    for (const ZipEntry& entry : entries) {
        if (!entry.directory) {
            if (auto root = FindPackageRoot(entry.parts, title_id, external)) {
                if (package_root && *package_root != *root) {
                    return false;
                }
                package_root = std::move(root);
            }
        }
    }
    if (!package_root) {
        return false;
    }

    for (ZipEntry& entry : entries) {
        const qsizetype depth = package_root->size();
        const qsizetype length = entry.parts.size();
        if (entry.directory && length <= depth && package_root->mid(0, length) == entry.parts) {
            continue;
        }
        if (!entry.directory && IsDocumentation(entry.parts.back()) && length <= depth &&
            package_root->mid(0, length - 1) == entry.parts.mid(0, length - 1)) {
            continue;
        }
        if (length <= depth || entry.parts.mid(0, depth) != *package_root) {
            return false;
        }
        QStringList relative = entry.parts.mid(depth);
        if (external) {
            relative.front() = relative.front().toLower();
        }
        if (IsPayloadRoot(relative.front()) && (entry.directory || relative.size() > 1)) {
            entry.relative_path = relative.join(u'/');
        } else if (!entry.directory && relative.size() == 1 && IsDocumentation(relative.front())) {
            entry.relative_path = relative.front();
        } else {
            return false;
        }
    }
    return true;
}

bool ExtractEntry(QuaZip& zip, const ZipEntry& entry, QFile* output) {
    QuaZipFile input(&zip);
    int method = 0;
    if (!input.open(QIODevice::ReadOnly, &method, nullptr, true) || method != entry.method ||
        !HasSafeExtraFields(input.getLocalExtraField()) || input.getZipError() != 0) {
        return false;
    }

    quint64 remaining = entry.compressed_size;
    quint64 written = 0;
    uLong crc = crc32(0, Z_NULL, 0);
    const auto write = [&](const Bytef* bytes, uInt size) {
        if (size > entry.expanded_size - written ||
            (output && output->write(reinterpret_cast<const char*>(bytes), size) != size)) {
            return false;
        }
        written += size;
        crc = crc32(crc, bytes, size);
        return true;
    };

    std::array<Bytef, ChunkSize> in;
    const auto read = [&] {
        const qint64 bytes =
            input.read(reinterpret_cast<char*>(in.data()), std::min<quint64>(remaining, in.size()));
        if (bytes <= 0) {
            return uInt{0};
        }
        remaining -= bytes;
        return static_cast<uInt>(bytes);
    };

    if (method == 0) {
        while (remaining != 0) {
            const uInt bytes = read();
            if (bytes == 0 || !write(in.data(), bytes)) {
                return false;
            }
        }
    } else {
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return false;
        }
        const auto cleanup = qScopeGuard([&] { inflateEnd(&stream); });
        std::array<Bytef, ChunkSize> out;
        for (;;) {
            if (stream.avail_in == 0 && remaining != 0) {
                stream.next_in = in.data();
                stream.avail_in = read();
                if (stream.avail_in == 0) {
                    return false;
                }
            }
            const uInt capacity = std::min<quint64>(out.size(), entry.expanded_size - written + 1);
            stream.next_out = out.data();
            stream.avail_out = capacity;
            const int status = inflate(&stream, Z_NO_FLUSH);
            if ((status != Z_OK && status != Z_STREAM_END) ||
                !write(out.data(), capacity - stream.avail_out)) {
                return false;
            }
            if (status == Z_STREAM_END) {
                if (remaining != 0 || stream.avail_in != 0) {
                    return false;
                }
                break;
            }
        }
    }

    input.close();
    return input.getZipError() == 0 && written == entry.expanded_size && crc == entry.crc;
}

bool ExtractArchive(QuaZip& zip, const QList<ZipEntry>& entries, const QDir& stage) {
    if (!zip.goToFirstFile()) {
        return false;
    }
    QSet<QString> directories;
    for (const ZipEntry& entry : entries) {
        if (!zip.hasCurrentFile()) {
            return false;
        }
        QFile output;
        if (!entry.relative_path.isEmpty()) {
            for (qsizetype end = 0; end < entry.relative_path.size();) {
                end = entry.relative_path.indexOf(u'/', end);
                if (end < 0 && !entry.directory) {
                    output.setFileName(stage.filePath(entry.relative_path));
                    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
                        return false;
                    }
                    break;
                }
                if (end < 0) {
                    end = entry.relative_path.size();
                }
                const QString dir = entry.relative_path.left(end++);
                if (!directories.contains(dir)) {
                    if (!stage.mkdir(dir)) {
                        return false;
                    }
                    directories.insert(dir);
                }
            }
        }
        if (!ExtractEntry(zip, entry, output.isOpen() ? &output : nullptr)) {
            return false;
        }
        if (output.isOpen()) {
            output.close();
            if (output.error() != QFileDevice::NoError) {
                return false;
            }
        }
        zip.goToNextFile();
    }
    return !zip.hasCurrentFile() && zip.getZipError() == 0;
}

} // namespace

QString InstallMod(const QString& root, const QString& title_id, const QString& mod_id,
                   const QString& name, const QString& version, const QString& archive_path,
                   const QByteArray& sha256, bool external) {
    if (!IsTitleId(title_id) || !IsModId(mod_id) || !IsDisplayMetadata(name, version) ||
        root.isEmpty() || sha256.size() != 32) {
        return u"The mod installation information is invalid."_s;
    }
    const QString title = title_id.toUpper();
    QDir mods_dir(QDir(root).absolutePath());
    const QDir title_dir(mods_dir.filePath(title));
    const QString destination = title_dir.filePath(mod_id);
    const QString root_error = u"The mods directory is unavailable or is a link."_s;
    const QString conflict_error =
        u"An existing mod folder is not owned by the Lichii Mod Store."_s;
    QFile source(archive_path);
    QTemporaryFile snapshot;
    if (!IsRegularFile(archive_path) || !source.open(QIODevice::ReadOnly) || !snapshot.open()) {
        return u"Could not open the downloaded ZIP."_s;
    }
    if (source.size() > MaxArchiveBytes) {
        return u"The downloaded ZIP exceeds the 64 MiB limit."_s;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (qint64 total = 0;;) {
        const QByteArray block = source.read(ChunkSize);
        if (source.error() != QFileDevice::NoError) {
            return u"Could not read the downloaded ZIP."_s;
        }
        if (block.isEmpty()) {
            break;
        }
        total += block.size();
        if (total > MaxArchiveBytes) {
            return u"The downloaded ZIP exceeds the 64 MiB limit."_s;
        }
        hash.addData(block);
        if (snapshot.write(block) != block.size()) {
            return u"Could not stage the downloaded ZIP."_s;
        }
    }
    const QByteArray snapshot_sha256 = hash.result();
    if (snapshot_sha256 != sha256) {
        return u"The downloaded ZIP does not match its expected SHA-256."_s;
    }

    const QString unsafe_archive_error = u"The ZIP is malformed or contains unsafe entries."_s;
    const int count = ReadDirectoryCount(snapshot);
    QuaZip zip(&snapshot);
    zip.setAutoClose(false);
    zip.setFileNameCodec("UTF-8");
    if (count == 0 || !zip.open(QuaZip::mdUnzip) || zip.getEntriesCount() != count) {
        return unsafe_archive_error;
    }
    QList<ZipEntry> entries = ReadEntries(zip, count, snapshot.size(), external);
    if (entries.isEmpty()) {
        return unsafe_archive_error;
    }
    if (!PlanExtraction(entries, title, external)) {
        return u"The ZIP does not contain a single supported mod layout for this title."_s;
    }

    if (!IsDirectory(mods_dir.path()) &&
        (!IsAbsent(mods_dir.path()) || !QDir().mkpath(mods_dir.path()) ||
         !IsDirectory(mods_dir.path()))) {
        return root_error;
    }
    QTemporaryDir transaction(mods_dir.filePath(StagingTemplate));
    if (!transaction.isValid()) {
        return u"Could not create a staging folder in the mods directory."_s;
    }
    const QDir stage(transaction.filePath(u"new"_s));
    if (!QDir().mkdir(stage.path()) || !ExtractArchive(zip, entries, stage)) {
        return u"Could not extract the mod ZIP safely."_s;
    }

    QFile manifest(stage.filePath(ManifestName));
    const QJsonObject object{
        {u"schema"_s, 1},
        {u"title_id"_s, title},
        {u"id"_s, mod_id},
        {u"name"_s, name},
        {u"version"_s, version},
        {u"sha256"_s, QString::fromLatin1(snapshot_sha256.toHex())},
    };
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (!manifest.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        manifest.write(bytes) != bytes.size()) {
        return u"Could not write the mod ownership manifest."_s;
    }
    manifest.close();
    if (manifest.error() != QFileDevice::NoError) {
        return u"Could not write the mod ownership manifest."_s;
    }

    const std::scoped_lock lock(install_mutex);
    if (!IsDirectory(mods_dir.path()) || HasNameConflict(mods_dir, title) ||
        (!IsDirectory(title_dir.path()) && (!IsAbsent(title_dir.path()) || !mods_dir.mkdir(title) ||
                                            !IsDirectory(title_dir.path())))) {
        return root_error;
    }
    RestoreInterruptedUpdates(mods_dir, title_dir, title);
    if (HasNameConflict(title_dir, mod_id)) {
        return conflict_error;
    }
    const bool updating = !IsAbsent(destination);
    const QString backup = transaction.filePath(u"previous"_s);
    if (updating) {
        if (!ReadOwnedMod(destination, title, mod_id) || !HasSafeContents(destination)) {
            return conflict_error;
        }
        if (!mods_dir.rename(destination, backup)) {
            return u"Could not move the previous mod out of the way."_s;
        }
    }
    if (!mods_dir.rename(stage.path(), destination)) {
        if (updating && !mods_dir.rename(backup, destination)) {
            transaction.setAutoRemove(false);
            return u"Could not install or restore the mod. The previous files are in %1."_s.arg(
                backup);
        }
        return u"Could not move the staged mod into the mods directory."_s;
    }
    return {};
}

QString UninstallMod(const QString& root, const QString& title_id, const QString& mod_id) {
    if (root.isEmpty() || !IsTitleId(title_id) || !IsModId(mod_id)) {
        return u"The mod installation information is invalid."_s;
    }
    std::unique_lock lock(install_mutex);
    QDir mods_dir(QDir(root).absolutePath());
    const QString title = title_id.toUpper();
    const QDir title_dir(mods_dir.filePath(title));
    if (IsAbsent(mods_dir.path())) {
        return {};
    }
    if (!IsDirectory(mods_dir.path()) || HasNameConflict(mods_dir, title)) {
        return u"The mods directory is unavailable or is a link."_s;
    }
    if (IsAbsent(title_dir.path())) {
        return {};
    }
    if (!IsDirectory(title_dir.path())) {
        return u"The mods directory is unavailable or is a link."_s;
    }
    RestoreInterruptedUpdates(mods_dir, title_dir, title);
    const QString destination = title_dir.filePath(mod_id);
    if (HasNameConflict(title_dir, mod_id) ||
        (!IsAbsent(destination) &&
         (!ReadOwnedMod(destination, title, mod_id) || !HasSafeContents(destination)))) {
        return u"An existing mod folder is not owned by the Lichii Mod Store."_s;
    }
    if (IsAbsent(destination)) {
        return {};
    }

    QTemporaryDir transaction(mods_dir.filePath(StagingTemplate));
    if (!transaction.isValid() ||
        !mods_dir.rename(destination, transaction.filePath(u"removed"_s))) {
        return u"Could not remove the mod from the mods directory."_s;
    }
    lock.unlock();
    return {};
}

QList<InstalledMod> ReadInstalledMods(const QString& root, const QString& title_id) {
    if (root.isEmpty() || !IsTitleId(title_id)) {
        return {};
    }
    const std::scoped_lock lock(install_mutex);
    const QDir mods_dir(QDir(root).absolutePath());
    const QString title = title_id.toUpper();
    const QDir title_dir(mods_dir.filePath(title));
    if (!IsDirectory(mods_dir.path()) || HasNameConflict(mods_dir, title) ||
        !IsDirectory(title_dir.path())) {
        return {};
    }
    RestoreInterruptedUpdates(mods_dir, title_dir, title);
    QList<InstalledMod> installed;
    for (const QString& id : title_dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (IsModId(id)) {
            if (auto mod = ReadOwnedMod(title_dir.filePath(id), title, id)) {
                installed.append(std::move(*mod));
            }
        }
    }
    return installed;
}

} // namespace ModStore
