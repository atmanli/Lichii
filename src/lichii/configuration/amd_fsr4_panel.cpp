// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/httplib.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QUrl>
#include <fmt/format.h>

#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "lichii/configuration/amd_fsr4_panel.h"
#include "lichii/configuration/graphics_panel_widgets.h"
#include "video_core/vulkan_common/ffx_files.h"

#include "common/net/ca_store.h"

namespace {

namespace FfxRuntime = Vulkan::FfxRuntime;

QString Tr(const char* text) {
    return QCoreApplication::translate("AmdFsr4Panel", text);
}

QString PathText(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

u64 DownloadSize() {
    u64 total = 0;
    for (const FfxRuntime::File& file : FfxRuntime::FILES) {
        total += file.size;
    }
    return total;
}

/// GETs one file of the SDK repository at the pinned commit. Empty with `error` set on failure.
std::optional<std::string> Fetch(std::string_view entry, std::string& error,
                                 const std::function<bool(u64)>& progress = {}) {
    httplib::Client client{std::string{FfxRuntime::DOWNLOAD_HOST}};
    client.set_connection_timeout(30);
    client.set_read_timeout(30);
    Common::Net::ConfigureCaStore(client);
    std::string body;
    int status = 0;
    const auto result = client.Get(
        fmt::format("{}{}", FfxRuntime::DOWNLOAD_PATH, entry),
        [&status](const httplib::Response& response) {
            status = response.status;
            return response.status == 200;
        },
        [&](const char* data, size_t length) {
            body.append(data, length);
            return !progress || progress(body.size());
        });
    if (!result || status != 200) {
        error = result ? fmt::format("HTTP {}", status) : httplib::to_string(result.error());
        return std::nullopt;
    }
    return body;
}

} // Anonymous namespace

AmdFsr4Panel::AmdFsr4Panel(QWidget* parent) : QWidget(parent) {
    using namespace GraphicsPanel;
    RuntimeCard card(this, Tr("AMD FSR 4"), Tr("Supported Vulkan devices on Windows or Linux"),
                     QStringLiteral("graphics-card"));
    status = card.status;
#if defined(__linux__)
    card.ProjectLink(Tr("fsr4-vk project"),
                     QUrl(QStringLiteral("https://github.com/Astyyyyy/fsr4-vk")))
        ->setToolTip(Tr("fsr4-vk on GitHub (GPL-3.0). Its licence and AMD's notice ship in the "
                        "package's share/licenses/fsr4-vk folder."));
#else
    card.ProjectLink(
            Tr("FidelityFX project"),
            QUrl(QString::fromUtf8(FfxRuntime::RELEASE_PAGE.data(),
                                   static_cast<qsizetype>(FfxRuntime::RELEASE_PAGE.size()))))
        ->setToolTip(Tr("AMD's FidelityFX SDK release on GitHub. The files are in "
                        "Kits\\FidelityFX\\signedbin."));
#endif
    auto* folder_button = new PillButton(Tr("Open Folder"), QStringLiteral("folder-open"),
                                         PillButton::Kind::Control, 34);
    card.actions->addWidget(folder_button);
    card.actions->addStretch();
    download_button = new PillButton(Tr("Download"), QStringLiteral("download-simple"),
                                     PillButton::Kind::Accent, 34);
    card.actions->addWidget(download_button);
    connect(download_button, &QAbstractButton::clicked, this, [this] { Download(); });
    connect(folder_button, &QAbstractButton::clicked, this, [] {
        const std::filesystem::path folder = FfxRuntime::Folder();
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathText(folder)));
    });
    Refresh();
}

void AmdFsr4Panel::showEvent(QShowEvent* event) {
    Refresh();
    QWidget::showEvent(event);
}

void AmdFsr4Panel::Refresh() {
    using GraphicsPanel::Tone;
#if defined(__linux__)
    const std::filesystem::path library = FfxRuntime::PortLibrary();
    const bool ready = !library.empty();
    download_button->hide();
    const QString details =
        ready ? Tr("Found fsr4-vk, the community Vulkan port of FSR 4, not AMD's official FSR 4. "
                   "If the GPU or driver doesn't support it, games use FSR 3.1.\n%1")
                    .arg(PathText(library))
              : Tr("Install %1 from the fsr4-vk project into %2. Until then, FSR 3.1 is used.")
                    .arg(QString::fromUtf8(FfxRuntime::PORT_LIBRARY.data(),
                                           static_cast<qsizetype>(FfxRuntime::PORT_LIBRARY.size())),
                         PathText(FfxRuntime::Folder()));
#else
    const bool ready = FfxRuntime::FilesPresent();
    const QString version = QString::fromUtf8(FfxRuntime::SDK_VERSION.data(),
                                              static_cast<qsizetype>(FfxRuntime::SDK_VERSION.size()));
    const QString details =
        Tr("AMD FidelityFX SDK %1. FSR 4 runs only on supported GPUs and drivers; otherwise "
           "games use FSR 3.1.\nFolder: %2")
            .arg(version, PathText(FfxRuntime::Folder()));
    download_button->setToolTip(
        Tr("Download from AMD (%1 MB)").arg((DownloadSize() + (1 << 20) - 1) >> 20));
    download_button->setEnabled(!ready);
#endif
    status->SetMessage(ready ? Tr("Installed · FSR 4 runtime available")
                             : Tr("FSR 4 runtime missing."),
                       ready ? Tone::Ready : Tone::Warning, details);
}

void AmdFsr4Panel::Download() {
    const std::filesystem::path folder = FfxRuntime::Folder();
    const auto fail = [this](const std::string& error) {
        LOG_ERROR(Frontend, "AMD FSR 4 download failed: {}", error);
        GraphicsPanel::ShowResult(this, Tr("AMD FSR 4"),
                                  Tr("Could not download AMD's files: %1. FSR 3.1 is used.")
                                      .arg(QString::fromStdString(error)),
                                  GraphicsPanel::Tone::Error);
        Refresh();
    };

    std::string error;
    std::optional<std::string> licence;
    {
        GraphicsPanel::RuntimeProgress busy(this, Tr("AMD FSR 4"),
                                            Tr("Contacting AMD's download..."));
        busy.show();
        QApplication::processEvents();
        licence = Fetch(FfxRuntime::LICENSE_ENTRY, error);
    }
    if (!licence || licence->empty()) {
        fail(error.empty() ? "could not retrieve AMD's licence text" : error);
        return;
    }

    if (!GraphicsPanel::ConfirmDownload(
            this, Tr("Download AMD FSR 4"),
            Tr("Lichii will download amd_fidelityfx_loader_vk.dll and "
               "amd_fidelityfx_upscaler_vk.dll "
               "from AMD's FidelityFX SDK %1 release on GitHub into %2.\n\nThese files are AMD's, "
               "under "
               "AMD's licence terms below. Lichii checks AMD's signature before using them.")
                .arg(QString::fromUtf8(FfxRuntime::SDK_VERSION.data(),
                                       static_cast<qsizetype>(FfxRuntime::SDK_VERSION.size())),
                     PathText(folder)),
            QString::fromStdString(*licence), Tr("Cancel"), Tr("Accept and Download"))) {
        return;
    }

    GraphicsPanel::RuntimeProgress progress(this, Tr("AMD FSR 4"),
                                            Tr("Downloading AMD's FSR 4 files..."), Tr("Cancel"));
    progress.show();
    QApplication::processEvents();
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    const u64 total = DownloadSize();
    u64 done = 0;
    for (const FfxRuntime::File& file : FfxRuntime::FILES) {
        const std::optional<std::string> contents = Fetch(file.entry, error, [&](u64 received) {
            progress.SetValue(static_cast<int>((done + received) * 1000 / total));
            QApplication::processEvents();
            return !progress.WasCanceled();
        });
        if (progress.WasCanceled()) {
            return;
        }
        if (!contents) {
            progress.close();
            fail(error);
            return;
        }
        done += file.size;
        const QByteArray sha256 =
            QCryptographicHash::hash(QByteArrayView{contents->data(),
                                                    static_cast<qsizetype>(contents->size())},
                                     QCryptographicHash::Sha256)
                .toHex();
        if (contents->size() != file.size ||
            sha256 != QByteArray{file.sha256.data(), static_cast<qsizetype>(file.sha256.size())}) {
            progress.close();
            fail(fmt::format("{} doesn't match the FidelityFX SDK {} file", file.name,
                             FfxRuntime::SDK_VERSION));
            return;
        }
        const std::filesystem::path target = folder / file.name;
        std::filesystem::path partial = target;
        partial += L".download";
        {
            std::ofstream stream(partial, std::ios::binary | std::ios::trunc);
            stream.write(contents->data(), static_cast<std::streamsize>(contents->size()));
            if (!stream) {
                progress.close();
                fail("could not write " + Common::FS::PathToUTF8String(partial));
                return;
            }
        }
        if (!FfxRuntime::IsSignedByAmd(partial)) {
            std::filesystem::remove(partial, ec);
            progress.close();
            fail(fmt::format("{} is not signed by AMD", file.name));
            return;
        }
        std::filesystem::rename(partial, target, ec);
        if (ec) {
            std::filesystem::remove(partial, ec);
            progress.close();
            fail("could not save " + Common::FS::PathToUTF8String(target));
            return;
        }
        LOG_INFO(Frontend, "Downloaded {} from AMD's FidelityFX SDK {}",
                 Common::FS::PathToUTF8String(target), FfxRuntime::SDK_VERSION);
    }
    {
        std::ofstream stream(folder / FfxRuntime::LICENSE_FILE, std::ios::binary | std::ios::trunc);
        stream.write(licence->data(), static_cast<std::streamsize>(licence->size()));
    }
    progress.SetValue(1000);
    progress.close();
    Refresh();
    GraphicsPanel::ShowResult(this, Tr("AMD FSR 4"),
                              Tr("AMD's FSR 4 runtime is ready. It is used the next time a game "
                                 "starts."),
                              GraphicsPanel::Tone::Ready);
}
