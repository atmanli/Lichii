// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef EGLANTINE_STREAMLINE

#include "common/httplib.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QDesktopServices>
#include <QStringList>
#include <QUrl>
#include <fmt/format.h>
#include <zlib.h>

#include "common/common_types.h"
#include "common/fs/fs_util.h"
#include "common/logging.h"
#include "lichii/configuration/graphics_panel_widgets.h"
#include "lichii/configuration/nvidia_dlss_panel.h"
#include "video_core/vulkan_common/streamline.h"

#include "common/net/ca_store.h"

namespace {

namespace Streamline = Vulkan::Streamline;

constexpr const char* RELEASE_PAGE = "https://github.com/NVIDIA-RTX/Streamline/releases";
constexpr const char* RELEASE_VERSION = "2.14.16";
constexpr const char* ZIP_HOST = "https://github.com";
constexpr const char* ZIP_PATH =
    "/NVIDIA-RTX/Streamline/releases/download/v2.14.16/streamline-sdk-v2.14.16.zip";
constexpr const char* INTERPOSER_ENTRY = "bin/x64/sl.interposer.dll";
constexpr const char* MODEL_ENTRY = "bin/x64/nvngx_dlss.dll";
constexpr const char* STREAMLINE_LICENSE_ENTRY = "license.txt";
constexpr const char* DLSS_LICENSE_ENTRY = "bin/x64/nvngx_dlss.license.txt";
constexpr const char* REFLEX_LICENSE_ENTRY = "bin/x64/reflex.license.txt";
constexpr const char* BIN_DIR = "bin/x64/";

std::string FileName(const std::string& entry) {
    return Common::FS::PathToUTF8String(std::filesystem::path{entry}.filename());
}

QString Tr(const char* text) {
    return QCoreApplication::translate("NvidiaDlssPanel", text);
}

u16 U16(const std::string& data, size_t at) {
    return static_cast<u16>(static_cast<u8>(data[at]) | static_cast<u8>(data[at + 1]) << 8);
}

u32 U32(const std::string& data, size_t at) {
    return static_cast<u32>(U16(data, at)) | static_cast<u32>(U16(data, at + 2)) << 16;
}

u64 U64(const std::string& data, size_t at) {
    return static_cast<u64>(U32(data, at)) | static_cast<u64>(U32(data, at + 4)) << 32;
}

struct ZipEntry {
    u16 method{};
    u32 crc{};
    u64 compressed{};
    u64 size{};
    u64 offset{};
};

/// Reads single files out of a zip on a web server with range requests, so only the files
/// needed are downloaded (about 32 MB of the 276 MB release).
class RemoteZip {
public:
    /// Follows GitHub's redirect to the file's (short-lived) download address.
    bool Resolve() {
        host = ZIP_HOST;
        path = ZIP_PATH;
        for (int redirect = 0; redirect < 5; ++redirect) {
            const auto result = MakeClient()->Get(path, httplib::Headers{{"Range", "bytes=0-0"}});
            if (!result) {
                error = httplib::to_string(result.error());
                return false;
            }
            if (result->status == 206) {
                // "bytes 0-0/<size>"; the server doesn't take "last N bytes" ranges.
                const std::string range = result->get_header_value("Content-Range");
                const size_t slash = range.rfind('/');
                zip_size = slash == std::string::npos
                               ? 0
                               : std::strtoull(range.c_str() + slash + 1, nullptr, 10);
                if (zip_size == 0) {
                    error = "no file size";
                    return false;
                }
                return true;
            }
            const std::string location = result->get_header_value("Location");
            const size_t scheme = location.find("://");
            const size_t slash =
                scheme == std::string::npos ? std::string::npos : location.find('/', scheme + 3);
            if (result->status < 300 || result->status >= 400 || slash == std::string::npos) {
                error = fmt::format("HTTP {}", result->status);
                return false;
            }
            host = location.substr(0, slash);
            path = location.substr(slash);
        }
        error = "too many redirects";
        return false;
    }

    bool Open() {
        if (!Resolve()) {
            return false;
        }
        const u64 tail_start = zip_size > 65558 ? zip_size - 65558 : 0;
        const std::optional<std::string> tail =
            Fetch(fmt::format("bytes={}-{}", tail_start, zip_size - 1));
        if (!tail) {
            return false;
        }
        const size_t end = tail->rfind(std::string{"PK\x05\x06", 4});
        if (end == std::string::npos || end + 22 > tail->size()) {
            error = "no zip directory";
            return false;
        }
        u64 directory_size = U32(*tail, end + 12);
        u64 directory_offset = U32(*tail, end + 16);
        if (directory_offset == 0xFFFFFFFF && end >= 20 &&
            tail->compare(end - 20, 4, std::string{"PK\x06\x07", 4}) == 0) {
            const u64 record = U64(*tail, end - 20 + 8);
            const std::optional<std::string> zip64 =
                Fetch(fmt::format("bytes={}-{}", record, record + 55));
            if (!zip64 || zip64->size() < 56) {
                return false;
            }
            directory_size = U64(*zip64, 40);
            directory_offset = U64(*zip64, 48);
        }
        const std::optional<std::string> directory = Fetch(
            fmt::format("bytes={}-{}", directory_offset, directory_offset + directory_size - 1));
        if (!directory) {
            return false;
        }
        for (size_t at = 0; at + 46 <= directory->size() && U32(*directory, at) == 0x02014b50;) {
            const u16 name_length = U16(*directory, at + 28);
            const u16 extra_length = U16(*directory, at + 30);
            const u16 comment_length = U16(*directory, at + 32);
            ZipEntry entry{
                .method = U16(*directory, at + 10),
                .crc = U32(*directory, at + 16),
                .compressed = U32(*directory, at + 20),
                .size = U32(*directory, at + 24),
                .offset = U32(*directory, at + 42),
            };
            const size_t extra_end = at + 46 + name_length + extra_length;
            if (extra_end + comment_length > directory->size()) {
                error = "bad zip directory";
                return false;
            }
            const std::string name = directory->substr(at + 46, name_length);
            // Zip64 sizes and offset, in this order, for the fields that overflowed.
            for (size_t extra = at + 46 + name_length; extra + 4 <= extra_end;) {
                const u16 id = U16(*directory, extra);
                const u16 length = U16(*directory, extra + 2);
                const size_t record_end = std::min<size_t>(extra + 4 + length, extra_end);
                if (id == 0x0001) {
                    size_t field = extra + 4;
                    for (u64* value : {&entry.size, &entry.compressed, &entry.offset}) {
                        if (*value == 0xFFFFFFFF && field + 8 <= record_end) {
                            *value = U64(*directory, field);
                            field += 8;
                        }
                    }
                }
                extra += 4 + length;
            }
            entries.emplace(name, entry);
            at += 46 + name_length + extra_length + comment_length;
        }
        return true;
    }

    const ZipEntry* Find(const std::string& name) const {
        const auto it = entries.find(name);
        return it == entries.end() ? nullptr : &it->second;
    }

    std::optional<std::string> Read(const ZipEntry& entry,
                                    const std::function<bool(u64)>& progress = {}) {
        const std::optional<std::string> header =
            Fetch(fmt::format("bytes={}-{}", entry.offset, entry.offset + 29));
        if (!header) {
            return std::nullopt;
        }
        if (header->size() < 30 || U32(*header, 0) != 0x04034b50) {
            error = "bad zip entry";
            return std::nullopt;
        }
        const u64 start = entry.offset + 30 + U16(*header, 26) + U16(*header, 28);
        std::string data;
        if (entry.compressed != 0) {
            std::optional<std::string> fetched =
                Fetch(fmt::format("bytes={}-{}", start, start + entry.compressed - 1), progress);
            if (!fetched) {
                return std::nullopt;
            }
            data = std::move(*fetched);
        }
        std::string out;
        if (entry.method == 0) {
            out = std::move(data);
        } else if (entry.method == 8) {
            out.resize(entry.size);
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
                error = "zlib";
                return std::nullopt;
            }
            stream.next_in = reinterpret_cast<Bytef*>(data.data());
            stream.avail_in = static_cast<uInt>(data.size());
            stream.next_out = reinterpret_cast<Bytef*>(out.data());
            stream.avail_out = static_cast<uInt>(out.size());
            const int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || stream.total_out != entry.size) {
                error = "could not unpack";
                return std::nullopt;
            }
        } else {
            error = "unknown zip compression";
            return std::nullopt;
        }
        const uLong crc = crc32(0L, reinterpret_cast<const Bytef*>(out.data()),
                                static_cast<uInt>(out.size()));
        if (crc != entry.crc) {
            error = "damaged download (checksum)";
            return std::nullopt;
        }
        return out;
    }

    bool canceled{};
    std::string error;

private:
    std::unique_ptr<httplib::Client> MakeClient() const {
        auto client = std::make_unique<httplib::Client>(host);
        client->set_connection_timeout(30);
        client->set_read_timeout(30);
        client->set_write_timeout(30);
        Common::Net::ConfigureCaStore(*client);
        return client;
    }

    std::optional<std::string> Fetch(const std::string& range,
                                     const std::function<bool(u64)>& progress = {}) {
        std::string body;
        int status = 0;
        const auto result = MakeClient()->Get(
            path, httplib::Headers{{"Range", range}},
            [&status](const httplib::Response& response) {
                status = response.status;
                return response.status == 206;
            },
            [&](const char* data, size_t length) {
                body.append(data, length);
                if (progress && !progress(body.size())) {
                    canceled = true;
                    return false;
                }
                return true;
            });
        if (!result || status != 206) {
            error = result ? fmt::format("HTTP {}", status) : httplib::to_string(result.error());
            return std::nullopt;
        }
        return body;
    }

    std::string host;
    std::string path;
    u64 zip_size{};
    std::map<std::string, ZipEntry> entries;
};

QString Named(const QString& name, const std::string& version) {
    return version.empty() ? name : QStringLiteral("%1 %2").arg(name, QString::fromStdString(version));
}

QString PathText(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

} // Anonymous namespace

NvidiaDlssPanel::NvidiaDlssPanel(QWidget* parent) : QWidget(parent) {
    using namespace GraphicsPanel;
    RuntimeCard card(this, Tr("NVIDIA DLSS"), Tr("Windows · NVIDIA Streamline"),
                     QStringLiteral("graphics-card"));
    status = card.status;
    auto* options = new QVBoxLayout;
    options->setObjectName(QStringLiteral("dlssRuntimeOptions"));
    options->setSpacing(8);
    card.body->addLayout(options);
    auto* release =
        card.ProjectLink(Tr("Streamline project"), QUrl(QString::fromLatin1(RELEASE_PAGE)));
    release->setToolTip(Tr("NVIDIA's Streamline releases on GitHub. The files are in bin\\x64 "
                           "of the release zip."));
    auto* folder_button = new PillButton(Tr("Open Folder"), QStringLiteral("folder-open"),
                                         PillButton::Kind::Control, 34);
    card.actions->addWidget(folder_button);
    card.actions->addStretch();
    download_button = new PillButton(Tr("Download components"), QStringLiteral("download-simple"),
                                     PillButton::Kind::Accent, 34);
    card.actions->addWidget(download_button);
    connect(download_button, &QAbstractButton::clicked, this, [this] { Download(); });
    connect(folder_button, &QAbstractButton::clicked, this, [] {
        const std::filesystem::path folder = Streamline::FindDlssFiles().folder;
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathText(folder)));
    });
    Refresh();
}

void NvidiaDlssPanel::showEvent(QShowEvent* event) {
    Refresh();
    QWidget::showEvent(event);
}

void NvidiaDlssPanel::Refresh() {
    const Streamline::DlssFiles files = Streamline::FindDlssFiles();
    QStringList streamline_missing;
    if (files.interposer.empty()) {
        streamline_missing << QStringLiteral("sl.interposer.dll");
    }
    for (const std::string& name : files.dlss_missing) {
        streamline_missing << QString::fromStdString(name);
    }
    const bool has_streamline = streamline_missing.isEmpty() && !files.dlss_mixed;
    const QString streamline =
        !streamline_missing.isEmpty()
            ? Tr("NVIDIA Streamline (%1)").arg(streamline_missing.join(QStringLiteral(", ")))
            : Tr("NVIDIA Streamline's files (sl.*) from one release; these are from different "
                 "ones");
    const bool has_model = !files.model.empty() || !files.app_model.empty();
    const QString dlss = !files.model.empty()
                             ? Named(QStringLiteral("DLSS"), files.model_version)
                             : Tr("%1 from the NVIDIA app")
                                   .arg(Named(QStringLiteral("DLSS"), files.app_model_version));

    const bool has_frame_gen =
        !frame_gen_wanted || (files.frame_gen_missing.empty() && !files.streamline_mixed);
    QStringList frame_gen_missing;
    for (const std::string& name : files.frame_gen_missing) {
        frame_gen_missing << QString::fromStdString(name);
    }
    QString frame_gen;
    if (frame_gen_wanted) {
        frame_gen = !frame_gen_missing.isEmpty()
                        ? Tr("DLSS Frame Generation also needs %1.")
                              .arg(frame_gen_missing.join(QStringLiteral(", ")))
                    : files.streamline_mixed
                        ? Tr("DLSS Frame Generation needs NVIDIA's Streamline files (sl.*) from "
                             "one release; these are from different ones.")
                        : Tr("DLSS Frame Generation's files are present.");
    }

    QString status;
    QString button;
    if (has_streamline && has_model) {
        status =
            Tr("Ready. Found %1 and %2. DLSS and the settings below apply only "
               "when a game uses its own DLSS. While DLSS runs, the status bar shows DLSS, "
               "its mode and its preset.")
                .arg(Named(QStringLiteral("NVIDIA Streamline"), files.interposer_version), dlss);
        button = Tr("Download Frame Generation files from NVIDIA");
    } else if (has_model) {
        status = Tr("Files needed: %1. %2 will be used. Until then, games use AMD FSR "
                    "3.1.")
                     .arg(streamline, dlss);
        button = has_frame_gen ? Tr("Download from NVIDIA (1 MB)") : Tr("Download from NVIDIA");
    } else if (has_streamline) {
        status = Tr("One file needed: NVIDIA DLSS (nvngx_dlss.dll). Until then, games use "
                    "AMD FSR 3.1.");
        button = has_frame_gen ? Tr("Download from NVIDIA (31 MB)") : Tr("Download from NVIDIA");
    } else {
        status = Tr("NVIDIA's files needed: %1 and DLSS (nvngx_dlss.dll). Until then, "
                    "games use AMD FSR 3.1.")
                     .arg(streamline);
        button = has_frame_gen ? Tr("Download from NVIDIA (32 MB)") : Tr("Download from NVIDIA");
    }
    if (!frame_gen.isEmpty()) {
        status += QStringLiteral(" ") + frame_gen;
    }
    const bool ready = has_streamline && has_model;
    const auto tone =
        ready && has_frame_gen ? GraphicsPanel::Tone::Ready : GraphicsPanel::Tone::Warning;
    const QString summary =
        !has_frame_gen ? Tr("NVIDIA DLSS Frame Generation needs missing runtime components.")
        : ready        ? Tr("Installed · DLSS runtime available")
                       : Tr("NVIDIA DLSS runtime missing.");
    this->status->SetMessage(
        summary, tone, status + QLatin1Char('\n') + Tr("Folder: %1").arg(PathText(files.folder)));
    download_button->setToolTip(button);
    download_button->setEnabled(!ready || !has_frame_gen);
}

void NvidiaDlssPanel::SetFrameGenWanted(bool wanted) {
    if (frame_gen_wanted == wanted) {
        return;
    }
    frame_gen_wanted = wanted;
    Refresh();
}

void NvidiaDlssPanel::Download() {
    const Streamline::DlssFiles files = Streamline::FindDlssFiles();
    std::vector<std::string> wanted;
    // DLSS's Streamline plugins and frame generation's files each come as one set with the loader,
    // all from this release: Streamline loads a module only with the sl.common.dll and loader of
    // its own release.
    if (files.interposer.empty() || !files.dlss_missing.empty() || files.dlss_mixed) {
        wanted.emplace_back(INTERPOSER_ENTRY);
        for (const char* const name : Streamline::DlssPluginFiles()) {
            wanted.push_back(BIN_DIR + std::string{name});
        }
    }
    if (files.model.empty() && files.app_model.empty()) {
        wanted.emplace_back(MODEL_ENTRY);
    }
    std::vector<std::string> frame_gen_set;
    if (frame_gen_wanted && (!files.frame_gen_missing.empty() || files.streamline_mixed)) {
        const auto add = [&wanted, &frame_gen_set](std::string entry) {
            if (std::ranges::find(wanted, entry) == wanted.end()) {
                frame_gen_set.push_back(std::move(entry));
            }
        };
        add(INTERPOSER_ENTRY);
        for (const char* const name : Streamline::FrameGenFiles()) {
            add(BIN_DIR + std::string{name});
        }
    }
    const auto in_frame_gen_set = [&frame_gen_set](const std::string& entry) {
        return std::ranges::find(frame_gen_set, entry) != frame_gen_set.end();
    };
    wanted.insert(wanted.end(), frame_gen_set.begin(), frame_gen_set.end());
    if (wanted.empty()) {
        return;
    }
    // Where Streamline loads from: the loader's folder, or the user folder when there is none yet.
    const std::filesystem::path folder = files.folder;
    const auto fail = [this](const std::string& error) {
        LOG_ERROR(Frontend, "NVIDIA DLSS download failed: {}", error);
        GraphicsPanel::ShowResult(
            this, Tr("NVIDIA DLSS"),
            Tr("Could not download NVIDIA's files: %1").arg(QString::fromStdString(error)),
            GraphicsPanel::Tone::Error);
        Refresh();
    };

    RemoteZip zip;
    std::string licences;
    {
        GraphicsPanel::RuntimeProgress busy(this, Tr("NVIDIA DLSS"),
                                            Tr("Contacting NVIDIA's download..."));
        busy.show();
        QApplication::processEvents();
        if (!zip.Open()) {
            busy.close();
            fail(zip.error);
            return;
        }
        const auto wants = [&wanted](const std::string& name) {
            return std::ranges::find(wanted, BIN_DIR + name) != wanted.end();
        };
        std::vector<std::pair<QString, const char*>> texts{
            {Tr("NVIDIA Streamline"), STREAMLINE_LICENSE_ENTRY}};
        if (std::ranges::find(wanted, MODEL_ENTRY) != wanted.end() || wants("nvngx_dlssg.dll")) {
            texts.emplace_back(Tr("NVIDIA DLSS"), DLSS_LICENSE_ENTRY);
        }
        if (wants("sl.reflex.dll") || wants("NvLowLatencyVk.dll")) {
            texts.emplace_back(Tr("NVIDIA Reflex"), REFLEX_LICENSE_ENTRY);
        }
        for (const auto& [title, entry_name] : texts) {
            const ZipEntry* const entry = zip.Find(entry_name);
            const std::optional<std::string> text = entry ? zip.Read(*entry) : std::nullopt;
            if ((!text || text->empty()) && entry_name == REFLEX_LICENSE_ENTRY) {
                LOG_WARNING(Frontend, "NVIDIA's release has no {}", entry_name);
                continue;
            }
            if (!text || text->empty()) {
                busy.close();
                fail("could not retrieve NVIDIA's licence text");
                return;
            }
            licences += fmt::format("==== {} ====\n\n{}\n\n", title.toStdString(),
                                    *text);
        }
    }

    // Without every file of the frame generation set in the release, none of it is taken.
    if (const auto missing = std::ranges::find_if(
            frame_gen_set, [&zip](const std::string& name) { return !zip.Find(name); });
        missing != frame_gen_set.end()) {
        LOG_WARNING(Frontend,
                    "NVIDIA's Streamline {} release has no {}; skipping DLSS Frame Generation's "
                    "files",
                    RELEASE_VERSION, *missing);
        std::erase_if(wanted, in_frame_gen_set);
        frame_gen_set.clear();
    }
    if (wanted.empty()) {
        fail("the release has none of the missing files");
        return;
    }
    QStringList names;
    for (const std::string& entry : wanted) {
        names << QString::fromStdString(FileName(entry));
    }
    if (!GraphicsPanel::ConfirmDownload(
            this, Tr("Download NVIDIA DLSS"),
            Tr("Lichii will download %1 from NVIDIA's Streamline %2 release on GitHub into %3.\n\n"
               "These files are NVIDIA's, under NVIDIA's licence terms below.")
                .arg(names.join(QStringLiteral(", ")), QString::fromLatin1(RELEASE_VERSION),
                     PathText(folder)),
            QString::fromStdString(licences), Tr("Cancel"), Tr("Accept and Download"))) {
        return;
    }

    u64 total = 0;
    std::vector<const ZipEntry*> entries;
    for (const std::string& name : wanted) {
        const ZipEntry* const entry = zip.Find(name);
        if (!entry) {
            fail(name + " is not in the release");
            return;
        }
        entries.push_back(entry);
        total += entry->compressed;
    }
    GraphicsPanel::RuntimeProgress progress(this, Tr("NVIDIA DLSS"),
                                            Tr("Downloading NVIDIA's DLSS files..."), Tr("Cancel"));
    progress.show();
    QApplication::processEvents();
    // The download address from the first step only lasts a few minutes.
    if (!zip.Resolve()) {
        progress.close();
        fail(zip.error);
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    u64 done = 0;
    // Every file is checked before any is replaced; frame generation's set is saved whole or not
    // at all, so the loader's folder never mixes releases.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> verified;
    const auto discard = [&verified, &ec] {
        for (const auto& [partial, target] : verified) {
            std::filesystem::remove(partial, ec);
        }
    };
    QStringList skipped;
    for (size_t i = 0; i < wanted.size(); ++i) {
        const std::optional<std::string> contents = zip.Read(*entries[i], [&](u64 received) {
            progress.SetValue(static_cast<int>((done + received) * 1000 / std::max<u64>(total, 1)));
            QApplication::processEvents();
            return !progress.WasCanceled();
        });
        if (!contents) {
            discard();
            progress.close();
            if (!zip.canceled) {
                fail(zip.error);
            }
            return;
        }
        done += entries[i]->compressed;
        const std::filesystem::path target = folder / std::filesystem::path{wanted[i]}.filename();
        std::filesystem::path partial = target;
        partial += L".download";
        {
            std::ofstream file(partial, std::ios::binary | std::ios::trunc);
            file.write(contents->data(), static_cast<std::streamsize>(contents->size()));
            if (!file) {
                discard();
                progress.close();
                fail("could not write " + Common::FS::PathToUTF8String(partial));
                return;
            }
        }
        // Streamline's own modules (sl.*) carry Streamline's signature; NVIDIA's other DLLs an
        // Authenticode one.
        const bool streamline_module = FileName(wanted[i]).starts_with("sl.");
        const bool signed_ok = streamline_module ? Streamline::IsStreamlineModule(partial)
                                                 : Streamline::IsSignedByNvidia(partial);
        if (!signed_ok && in_frame_gen_set(wanted[i])) {
            std::filesystem::remove(partial, ec);
            LOG_WARNING(Frontend, "{} from NVIDIA's release is not signed by NVIDIA; skipping it",
                        FileName(wanted[i]));
            skipped << QString::fromStdString(FileName(wanted[i]));
            continue;
        }
        if (!signed_ok) {
            std::filesystem::remove(partial, ec);
            discard();
            progress.close();
            fail(Common::FS::PathToUTF8String(target.filename()) + " is not signed by NVIDIA");
            return;
        }
        verified.emplace_back(partial, target);
    }
    if (!skipped.isEmpty()) {
        std::erase_if(verified, [&](const auto& file) {
            if (!in_frame_gen_set(BIN_DIR + Common::FS::PathToUTF8String(file.second.filename()))) {
                return false;
            }
            std::filesystem::remove(file.first, ec);
            return true;
        });
    }
    // The old files move aside first, so a failed save puts every one of them back.
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> replaced;
    const auto restore = [&replaced, &ec] {
        for (const auto& [backup, target] : replaced) {
            std::filesystem::remove(target, ec);
            if (!backup.empty()) {
                std::filesystem::rename(backup, target, ec);
            }
        }
    };
    for (const auto& [partial, target] : verified) {
        std::filesystem::path backup;
        if (std::filesystem::exists(target, ec)) {
            backup = target;
            backup += L".old";
            std::filesystem::remove(backup, ec);
            std::filesystem::rename(target, backup, ec);
        }
        bool failed = static_cast<bool>(ec);
        if (!failed) {
            std::filesystem::rename(partial, target, ec);
            failed = static_cast<bool>(ec);
            if (failed && !backup.empty()) {
                std::filesystem::rename(backup, target, ec);
            }
        }
        if (failed) {
            restore();
            discard();
            progress.close();
            fail("could not save " + Common::FS::PathToUTF8String(target));
            return;
        }
        replaced.emplace_back(backup, target);
    }
    for (const auto& [backup, target] : replaced) {
        if (!backup.empty()) {
            std::filesystem::remove(backup, ec);
        }
        LOG_INFO(Frontend, "Downloaded {} from NVIDIA's Streamline {} release",
                 Common::FS::PathToUTF8String(target), RELEASE_VERSION);
    }
    progress.SetValue(1000);
    progress.close();
    Refresh();
    if (!skipped.isEmpty()) {
        GraphicsPanel::ShowResult(this, Tr("NVIDIA DLSS"),
                                  Tr("These files failed NVIDIA's signature check, so none of DLSS "
                                     "Frame Generation's files were saved and it stays off: %1")
                                      .arg(skipped.join(QStringLiteral(", "))),
                                  GraphicsPanel::Tone::Error);
        return;
    }
    GraphicsPanel::ShowResult(this, Tr("NVIDIA DLSS"),
                              Tr("NVIDIA DLSS is ready. It is used the next time a game starts."),
                              GraphicsPanel::Tone::Ready);
}

#endif
