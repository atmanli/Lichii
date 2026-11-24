// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/httplib.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QUrl>
#include <fmt/format.h>

#include "common/common_types.h"
#include "common/fs/fs_util.h"
#include "common/logging.h"
#include "common/net/ca_store.h"
#include "lichii/configuration/flyhigh_panel.h"
#include "lichii/configuration/graphics_panel_widgets.h"
#include "video_core/frame_gen/flyhigh_model.h"

namespace {

namespace FrameGen = VideoCore::FrameGen;
namespace Model = VideoCore::FrameGen::FlyHighModel;

constexpr std::string_view WEIGHTS_LICENSE = R"license(# FlyHighFG model weights: license

The model weights in this directory, `flyhighfg.bin` (f16 blob) and its `flyhighfg.json` manifest (`graph.json` is
the engine's op graph), are dual licensed.

## Noncommercial use

**Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**:
<https://creativecommons.org/licenses/by-nc/4.0/>

Free to use, share and adapt for noncommercial purposes with attribution. This covers personal use, research,
education, open-source projects that are not sold and products that are not monetised.

## Commercial use

A product that ships these weights, or a service that runs them, and that is sold, monetised or used in a commercial
offering needs a commercial license. It is granted by agreement with the maintainers: open an issue in the repository
or write to the maintainers through the GitHub organisation. The engine code needs no such license.

## The code

The engine **code** is licensed separately under the Apache License 2.0 (see `../LICENSE`), commercially too; that
license does not cover these weights. A model you trained or converted yourself is yours to license as you like.
)license";

QString Tr(const char* text) {
    return QCoreApplication::translate("FlyHighPanel", text);
}

QString PathText(const std::filesystem::path& path) {
    return QString::fromStdString(Common::FS::PathToUTF8String(path));
}

QString Text(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

u64 DownloadSize() {
    u64 total = 0;
    for (const Model::File& file : Model::FILES) {
        total += file.size;
    }
    return total;
}

std::optional<std::string> Fetch(const Model::File& file, std::string& error,
                                 const std::function<bool(u64)>& progress) {
    httplib::Client client{std::string{Model::DOWNLOAD_HOST}};
    client.set_connection_timeout(30);
    client.set_read_timeout(30);
    client.set_max_timeout(60'000);
    client.set_payload_max_length(static_cast<size_t>(file.size));
    client.set_decompress(false);
    Common::Net::ConfigureCaStore(client);
    std::string body;
    body.reserve(static_cast<size_t>(file.size));
    const std::string mismatch = fmt::format("{} doesn't match the pinned model", file.name);
    const auto result = client.Get(
        fmt::format("{}{}", Model::DOWNLOAD_PATH, file.name), {{"Accept-Encoding", "identity"}},
        [&](const httplib::Response& response) {
            if (response.status != 200) {
                error = fmt::format("{}: HTTP {}", file.name, response.status);
                return false;
            }
            if (response.has_header("Content-Length") &&
                response.get_header_value_u64("Content-Length") != file.size) {
                error = mismatch;
                return false;
            }
            return true;
        },
        [&](const char* data, size_t length) {
            if (length > file.size - body.size()) {
                error = mismatch;
                return false;
            }
            body.append(data, length);
            return progress(body.size());
        });
    if (!result) {
        if (error.empty()) {
            error = fmt::format("{}: {}", file.name, httplib::to_string(result.error()));
        }
        return std::nullopt;
    }
    if (result->status != 200) {
        error = fmt::format("{}: HTTP {}", file.name, result->status);
        return std::nullopt;
    }
    if (body.size() != file.size ||
        QCryptographicHash::hash(QByteArrayView{body}, QCryptographicHash::Sha256).toHex() !=
            QByteArrayView{file.sha256}) {
        error = mismatch;
        return std::nullopt;
    }
    return body;
}

bool Save(const std::filesystem::path& path, std::string_view contents, std::string& error) {
    QSaveFile file(PathText(path));
    const auto size = static_cast<qint64>(contents.size());
    if (!file.open(QIODevice::WriteOnly) || file.write(contents.data(), size) != size ||
        !file.commit()) {
        error = fmt::format("could not save {}: {}", Common::FS::PathToUTF8String(path),
                            file.errorString().toStdString());
        return false;
    }
    return true;
}

} // Anonymous namespace

FlyHighPanel::FlyHighPanel(QWidget* parent) : QWidget(parent) {
    using namespace GraphicsPanel;
    RuntimeCard card(this, Tr("FlyHigh"), Tr("Supported builds · FlyHigh selected"),
                     QStringLiteral("lightning"));
    status = card.status;
    card.ProjectLink(Tr("FlyHigh project"), QUrl(Text(Model::RELEASE_PAGE)))
        ->setToolTip(Tr("The FlyHigh engine on GitHub. The engine is Apache-2.0. The weights "
                        "are CC BY-NC 4.0 for noncommercial use and require a separate licence "
                        "for commercial use."));
    auto* folder_button = new PillButton(Tr("Open Folder"), QStringLiteral("folder-open"),
                                         PillButton::Kind::Control, 34);
    card.actions->addWidget(folder_button);
    card.actions->addStretch();
    download_button = new PillButton(Tr("Download Model"), QStringLiteral("download-simple"),
                                     PillButton::Kind::Accent, 34);
    card.actions->addWidget(download_button);
    connect(download_button, &QAbstractButton::clicked, this, [this] { Download(); });
    connect(folder_button, &QAbstractButton::clicked, this, [] {
        const std::filesystem::path folder = FrameGen::FlyHighModelDir();
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathText(folder)));
    });
}

void FlyHighPanel::showEvent(QShowEvent* event) {
    Refresh();
    QWidget::showEvent(event);
}

void FlyHighPanel::Refresh() {
    using GraphicsPanel::Tone;
    const bool library = FrameGen::FlyHighLibraryPresent();
    const bool model = FrameGen::FlyHighModelInstalled();
    const QString folder = Tr("Folder: %1").arg(PathText(FrameGen::FlyHighModelDir()));
    if (!library) {
        status->SetMessage(Tr("FlyHigh runtime unavailable. Model download is unavailable."),
                           Tone::Warning,
                           Tr("FlyHigh's library must be beside Lichii. The release archives "
                              "include it.\n%1")
                               .arg(folder));
    } else if (!model) {
        status->SetMessage(Tr("Runtime available. Download a model before using FlyHigh."),
                           Tone::Warning,
                           Tr("The network weights are about %1 MB.\n%2")
                               .arg((DownloadSize() + (1 << 19)) >> 20)
                               .arg(folder));
    } else {
        status->SetMessage(Tr("FlyHigh runtime and model ready."), Tone::Ready,
                           Tr("FlyHigh runs on a separate Vulkan device on the same GPU. "
                              "The first start at a new resolution times its kernels.\n%1")
                               .arg(folder));
    }
    download_button->setEnabled(library && !model);
}

void FlyHighPanel::Download() {
    DownloadFlyHighModel(this);
    Refresh();
}

void OfferFlyHighModel(QWidget* parent) {
    if (FrameGen::FlyHighModelInstalled() || !FrameGen::FlyHighLibraryPresent()) {
        return;
    }
    DownloadFlyHighModel(parent);
}

bool DownloadFlyHighModel(QWidget* parent) {
    static bool downloading = false;
    if (downloading) {
        return false;
    }
    const QScopedValueRollback download_guard{downloading, true};
    const std::filesystem::path folder = FrameGen::FlyHighModelDir();
    const auto fail = [parent](const std::string& error) {
        LOG_ERROR(Frontend, "FlyHigh model download failed: {}", error);
        GraphicsPanel::ShowResult(parent, Tr("FlyHigh"),
                                  Tr("Could not download the FlyHigh model: %1. Optical Flow "
                                     "Interpolation runs until it is there.")
                                      .arg(QString::fromStdString(error)),
                                  GraphicsPanel::Tone::Error);
        return false;
    };

    if (!GraphicsPanel::ConfirmDownload(
            parent, Tr("Download the FlyHigh Model"),
            Tr("If you accept, Lichii will download the FlyHigh model (graph.json, flyhighfg.json "
               "and flyhighfg.bin) from GitHub into %1.\n\n"
               "The weights are available for noncommercial use with attribution under CC BY-NC "
               "4.0. Commercial use needs a separate licence from the FlyHigh maintainers. "
               "The Apache-2.0 engine licence does not cover the weights.")
                .arg(PathText(folder)),
            Text(WEIGHTS_LICENSE), Tr("Cancel"), Tr("Accept and Download"))) {
        return false;
    }

    GraphicsPanel::RuntimeProgress progress(parent, Tr("FlyHigh"),
                                            Tr("Downloading the FlyHigh model..."), Tr("Cancel"));
    progress.show();
    QApplication::processEvents();
    std::string error;
    const u64 total = DownloadSize();
    u64 done = 0;
    std::array<std::string, Model::FILES.size()> downloads;
    for (size_t i = 0; i < downloads.size(); ++i) {
        if (progress.WasCanceled()) {
            return false;
        }
        const Model::File& file = Model::FILES[i];
        auto contents = Fetch(file, error, [&](u64 received) {
            progress.SetValue(static_cast<int>((done + received) * 999 / total));
            QApplication::processEvents();
            return !progress.WasCanceled();
        });
        if (progress.WasCanceled()) {
            return false;
        }
        if (!contents) {
            progress.close();
            return fail(error);
        }
        done += file.size;
        downloads[i] = std::move(*contents);
    }

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    if (ec) {
        progress.close();
        return fail(fmt::format("could not create {}: {}", Common::FS::PathToUTF8String(folder),
                                ec.message()));
    }
    if (!Save(folder / Model::LICENSE_FILE, WEIGHTS_LICENSE, error)) {
        progress.close();
        return fail(error);
    }
    for (size_t i = 0; i < downloads.size(); ++i) {
        if (!Save(folder / Model::FILES[i].name, downloads[i], error)) {
            progress.close();
            return fail(error);
        }
    }
    LOG_INFO(Frontend, "Downloaded the FlyHigh model into {}", Common::FS::PathToUTF8String(folder));
    progress.SetValue(1000);
    progress.close();
    return true;
}
