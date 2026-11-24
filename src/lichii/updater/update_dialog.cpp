// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <QTimer>
#include <QEventLoop>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QtMath>
#include <qdesktopservices.h>
#include "common/logging.h"
#include "common/scm_rev.h"
#include "frontend_common/update_installer.h"
#include "qt_common/abstract/frontend.h"
#ifdef _WIN32
#include "qt_common/util/compress.h"
#endif
#include "ui_update_dialog.h"
#include "update_dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

#include "common/httplib.h"

#include "common/net/ca_store.h"

#include <QDesktopServices>

#undef GetSaveFileName

namespace {

// Paper's transfer card: the step as the title, the bar with its percentage under it, and Cancel.
class TransferProgress final : public Spotlight::Dialog {
public:
    TransferProgress(const QString& title, QWidget* parent) : Spotlight::Dialog(parent) {
        setWindowTitle(title);
        setWindowModality(Qt::WindowModal);
        SetIcon(u"download-simple", Spotlight::Color::Accent());
        SetCloseVisible(false);
        SetCardWidth(430);

        bar = new QProgressBar(this);
        bar->setRange(0, 100);
        bar->setTextVisible(false);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background-color: rgba(255, 255, 255, 46); border-radius: 4px; "
            "min-height: 8px; max-height: 8px; } "
            "QProgressBar::chunk { border-radius: 4px; }"));
        percent = new QLabel(this);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(13);
        percent->setFont(mono);
        percent->setAlignment(Qt::AlignRight);
        Spotlight::SetTextColor(percent, Spotlight::Color::TextSecondary());
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Cancel)->setAutoDefault(false);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(0);
        layout->addWidget(bar);
        layout->addSpacing(14);
        layout->addWidget(percent);
        layout->addSpacing(26);
        layout->addWidget(buttons);
        SetValue(0);
    }

    void SetValue(int value) {
        bar->setValue(value);
        percent->setText(QStringLiteral("%1%").arg(value));
    }

    bool WasCanceled() const {
        return canceled;
    }

    void reject() override {
        canceled = true;
        QDialog::reject();
    }

private:
    QProgressBar* bar;
    QLabel* percent;
    bool canceled = false;
};

QString FileName(const QString& path) {
    return QFileInfo(path).fileName();
}

} // namespace

UpdateDialog::UpdateDialog(const Common::Net::Release& release, QWidget* parent)
    : Spotlight::Dialog(parent), ui(new Ui::UpdateDialog) {
    ui->setupUi(this);
    SetIcon(u"cloud-arrow-down", Spotlight::Color::Accent());
    setFixedWidth(702);
    resize(width(), 0);

    QString subtitle =
        tr("%1 is available for download.").arg(QString::fromStdString(release.title));
    ui->url->setText(
        tr("<a href=\"%1\" style=\"color: #8fb5ff; text-decoration: none\">View on GitHub</a>")
            .arg(QString::fromStdString(release.html_url)));
    ui->url->setFont(Spotlight::Font(13, 600));
    ui->urlIcon->setPixmap(
        Spotlight::IconPixmap(u"github-logo", 14, Spotlight::Color::Info(), devicePixelRatioF()));

    for (auto* card : {ui->notesCard, ui->groupBox}) {
        card->setStyleSheet(QStringLiteral("#%1 { background-color: rgba(255, 255, 255, 13); "
                                           "border: 1px solid rgba(255, 255, 255, 20); "
                                           "border-radius: 16px; }")
                                .arg(card->objectName()));
    }
    ui->notesTitle->setFont(Spotlight::Font(12, 600));
    Spotlight::SetTextColor(ui->notesTitle, Spotlight::Color::TextSecondary());
    ui->versionsTitle->setFont(Spotlight::Font(13, 600));
    ui->question->setFont(Spotlight::Font(14, 600));
    ui->question->setWordWrap(true);

    std::string text{release.body};
    if (auto pos = text.find("# Packages"); pos != std::string::npos) {
        text = text.substr(0, pos);
    }

    ui->body->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: transparent; border: 0; padding: 0; "
        "color: rgba(255, 255, 255, 158); }"));
    ui->body->viewport()->setAutoFillBackground(false);
    ui->body->document()->setDocumentMargin(0);
    ui->body->document()->setDefaultFont(Spotlight::Font(13, 400));
    ui->body->setMarkdown(QString::fromStdString(text));
    QTextCharFormat heading;
    heading.setFont(Spotlight::DisplayFont(23));
    heading.setForeground(Spotlight::Color::Text());
    for (auto block = ui->body->document()->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().headingLevel() > 0) {
            QTextCursor cursor(block);
            cursor.select(QTextCursor::BlockUnderCursor);
            cursor.mergeCharFormat(heading);
        }
    }
    connect(ui->body->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged, ui->body, [this](QSizeF size) {
                ui->body->setFixedHeight(qMin(qCeil(size.height()), 280));
            });

    ui->buttonBox->button(QDialogButtonBox::No)->setText(tr("Remind Me Later"));
    ui->buttonBox->button(QDialogButtonBox::Yes)->setDefault(true);
    connect(ui->skip, &QPushButton::clicked, this, [this] { done(SkipVersion); });

    // TODO(crueter): Find a way to set default
    const auto assets = release.GetPlatformAssets();

    if (assets.empty()) {
        ui->groupBox->setHidden(true);
        connect(this, &QDialog::accepted, this, [release]() {
            QDesktopServices::openUrl(QUrl{QString::fromStdString(release.html_url)});
        });
    } else if (assets.size() == 1) {
        ui->groupBox->setHidden(true);
        m_asset = assets[0];

        if (m_asset.name == "Standard" &&
            std::string_view{Common::g_build_release_platform}.ends_with("-pgo")) {
            subtitle +=
                tr(" This release has no PGO build, so the standard build will be downloaded.");
        }

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    } else {
        ui->radioButtons->setAlignment(Qt::AlignLeft);
        u32 i = 0;
        for (const Common::Net::Asset& a : assets) {
            QRadioButton* r = new QRadioButton(tr(a.name.c_str()), this);
            r->setFont(Spotlight::Font(14, 600));
            connect(r, &QRadioButton::toggled, this, [a, this](bool checked) {
                if (checked)
                    m_asset = a;
            });

            if (i == 0)
                r->setChecked(true);
            ++i;

            ui->radioButtons->addWidget(r);
        }

        connect(this, &QDialog::accepted, this, &UpdateDialog::Download);
    }
    SetSubtitle(subtitle);
}

UpdateDialog::~UpdateDialog() {
    delete ui;
}

void UpdateDialog::Restart(QWidget* window, const QString& target) {
    auto environment = QProcessEnvironment::systemEnvironment();
    const auto app_dir = environment.value(QStringLiteral("APPDIR"));
    if (!app_dir.isEmpty()) {
        for (const auto& key : environment.keys()) {
            const auto value = environment.value(key);
            if (!value.contains(app_dir)) {
                continue;
            }
            const auto stripped = UpdateInstaller::StripPathListEntries(value.toStdString(),
                                                                        app_dir.toStdString());
            if (stripped.empty()) {
                environment.remove(key);
            } else {
                environment.insert(key, QString::fromStdString(stripped));
            }
        }
    }
    for (const auto& key : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"}) {
        environment.remove(QString::fromLatin1(key));
    }

    // Wait for this process to exit, so the new one reads the settings this one writes on close.
    QProcess process;
    process.setProgram(QStringLiteral("/bin/sh"));
    process.setArguments(
        {QStringLiteral("-c"),
         QStringLiteral("i=0; while kill -0 \"$1\" 2>/dev/null && [ \"$i\" -lt 1500 ]; do "
                        "sleep 0.2; i=$((i+1)); done; kill -0 \"$1\" 2>/dev/null && exit 0; "
                        "exec \"$2\""),
         QStringLiteral("sh"), QString::number(QCoreApplication::applicationPid()), target});
    process.setProcessEnvironment(environment);
    if (!process.startDetached()) {
        LOG_ERROR(Frontend, "Could not start the restart helper for {}", target.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to restart"),
                                     tr("Please start %1 again yourself.").arg(FileName(target)));
        return;
    }
    if (window) {
        QMetaObject::invokeMethod(window, &QWidget::close, Qt::QueuedConnection);
    }
}

#ifdef _WIN32
bool UpdateDialog::LaunchInstaller(QWidget* window, const QString& staging) {
    // The helper runs from a copy, because the installed one is replaced along with everything else.
    const QString helper =
        QCoreApplication::applicationDirPath() + QStringLiteral("/lichii-updater.exe");
    const QString copy = QDir::temp().filePath(
        QStringLiteral("lichii-updater-%1.exe").arg(QCoreApplication::applicationPid()));
    QFile::remove(copy);
    if (!QFile::copy(helper, copy)) {
        LOG_ERROR(Frontend, "Could not copy the installer helper {}", helper.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to install the update"),
                                     tr("Could not start the update installer."));
        return false;
    }

    const auto install_dir = QCoreApplication::applicationDirPath();
    const auto native = [](const QString& path) { return QDir::toNativeSeparators(path); };
    if (!QProcess::startDetached(
            copy, {QString::number(QCoreApplication::applicationPid()), native(staging),
                   native(install_dir), native(QCoreApplication::applicationFilePath())})) {
        LOG_ERROR(Frontend, "Could not start the installer helper {}", copy.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to install the update"),
                                     tr("Could not start the update installer."));
        return false;
    }
    if (window) {
        QMetaObject::invokeMethod(window, &QWidget::close, Qt::QueuedConnection);
    }
    return true;
}
#endif

void UpdateDialog::Download() {
#ifdef _WIN32
    if (m_asset.filename.ends_with(".zip")) {
        const std::filesystem::path exe{QCoreApplication::applicationFilePath().toStdWString()};
        if (UpdateInstaller::CanReplace(exe) &&
            QFile::exists(QCoreApplication::applicationDirPath() +
                          QStringLiteral("/lichii-updater.exe"))) {
            InstallOnRestart(exe);
            return;
        }
    }
#endif
#ifdef __linux__
    if (Common::Net::CurrentLinuxPackage() == Common::Net::LinuxPackage::AppImage &&
        m_asset.filename.ends_with(".AppImage")) {
        const std::filesystem::path target{qEnvironmentVariable("APPIMAGE").toStdString()};
        if (UpdateInstaller::CanReplace(target)) {
            InstallInPlace(target);
            return;
        }
    }
#endif

    // The application folder is read-only inside an AppImage and is the app bundle on macOS.
    auto default_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (default_dir.isEmpty()) {
        default_dir = QDir::homePath();
    }

    const auto filename = QtCommon::Frontend::GetSaveFileName(
        tr("New Version Location"),
        default_dir % QStringLiteral("/") % QString::fromStdString(m_asset.filename),
        tr("All Files (*.*)"));

    if (filename.isEmpty() || !DownloadTo(filename))
        return;

    // Download is complete. User may choose to open in the file manager.
    auto button =
        QtCommon::Frontend::Success(tr("Download Complete"),
                                     tr("Successfully downloaded %1. Would you like to open it?")
                                         .arg(QString::fromStdString(m_asset.filename)),
                                     QtCommon::Frontend::Yes | QtCommon::Frontend::No);

    if (button == QtCommon::Frontend::Yes) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(filename));
    }
}

void UpdateDialog::InstallInPlace(const std::filesystem::path& target) {
    const UpdateInstaller::Lock lock{UpdateInstaller::LockPath(target)};
    if (!lock.Held()) {
        QtCommon::Frontend::Critical(tr("Failed to install the update"),
                                     tr("Another Lichii window is already updating this "
                                        "installation."));
        return;
    }

    const auto staged = UpdateInstaller::StagedPath(target);
    if (!DownloadTo(QString::fromStdString(staged.string()))) {
        return;
    }

    std::error_code ec;
    if (!UpdateInstaller::Swap(staged, target, ec)) {
        LOG_ERROR(Frontend, "Could not replace {}: {}", target.string(), ec.message());
        std::filesystem::remove(staged, ec);
        QtCommon::Frontend::Critical(
            tr("Failed to install the update"),
            tr("Could not replace %1. Your installation was not changed.")
                .arg(QString::fromStdString(target.filename().string())));
        return;
    }

    LOG_INFO(Frontend, "Installed {} over {}", m_asset.filename, target.string());
    const auto button = QtCommon::Frontend::Success(
        tr("Update Installed"),
        tr("The new version is installed. The previous version was kept as %1. Restart now?")
            .arg(QString::fromStdString(UpdateInstaller::PreviousPath(target).filename().string())),
        QtCommon::Frontend::Yes | QtCommon::Frontend::No);
    if (button == QtCommon::Frontend::Yes) {
        Restart(parentWidget(), QString::fromStdString(target.string()));
    }
}

#ifdef _WIN32
void UpdateDialog::InstallOnRestart(const std::filesystem::path& exe) {
    const auto install_dir = exe.parent_path();
    const auto staging = install_dir / ".update-staging";
    const auto archive = install_dir / ".update-download.zip";
    const auto cleanup = [&] {
        std::error_code ec;
        std::filesystem::remove_all(staging, ec);
        std::filesystem::remove(archive, ec);
    };
    const auto fail = [&](const QString& text) {
        cleanup();
        QtCommon::Frontend::Critical(tr("Failed to install the update"), text);
    };

    cleanup();
    std::error_code ec;
    std::filesystem::create_directories(staging, ec);
    if (ec) {
        fail(tr("Could not create a folder for the update."));
        return;
    }
    if (!DownloadTo(QString::fromStdWString(archive.wstring()))) {
        cleanup();
        return;
    }

    TransferProgress progress(tr("Unpacking..."), parentWidget());
    progress.show();
    const auto unpacked = QtCommon::Compress::extractDir(
        QString::fromStdWString(archive.wstring()), QString::fromStdWString(staging.wstring()),
        [&](std::size_t total, std::size_t done) {
            QGuiApplication::processEvents();
            if (total != 0) {
                progress.SetValue(static_cast<int>((done * 100) / total));
            }
            return !progress.WasCanceled();
        });
    progress.close();
    if (unpacked.isEmpty() || !std::filesystem::exists(staging / exe.filename(), ec)) {
        fail(tr("The downloaded update could not be unpacked."));
        return;
    }
    std::filesystem::remove(archive, ec);

    const auto button = QtCommon::Frontend::Success(
        tr("Update Ready"),
        tr("Lichii will close, install the update and start again. The previous version, "
           "settings and data are kept. Restart now?"),
        QtCommon::Frontend::Yes | QtCommon::Frontend::No);
    if (button != QtCommon::Frontend::Yes ||
        !LaunchInstaller(parentWidget(), QString::fromStdWString(staging.wstring()))) {
        cleanup();
    }
}
#endif

// TODO: migrate to a net.cpp wrapper
bool UpdateDialog::DownloadTo(const QString& filename) {
    QSaveFile file(filename);
    if (!file.open(QIODevice::Truncate | QIODevice::WriteOnly)) {
        LOG_WARNING(Frontend, "Could not open file {}", filename.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not open %1 for writing.").arg(FileName(filename)));
        return false;
    }

    // TODO(crueter): Move to net.cpp
    constexpr std::size_t timeout_seconds = 15;

    std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(m_asset.url);
    client->set_connection_timeout(timeout_seconds);
    client->set_read_timeout(timeout_seconds);
    client->set_write_timeout(timeout_seconds);
    client->set_follow_location(true);

    Common::Net::ConfigureCaStore(*client);

    if (client == nullptr) {
        LOG_ERROR(Frontend, "Invalid URL {}{}", m_asset.url, m_asset.path);
        return false;
    }

    TransferProgress progress(tr("Downloading..."), parentWidget());
    progress.show();

    QGuiApplication::processEvents();

    // Progress dialog.
    auto progress_callback = [&](size_t processed_size, size_t total_size) {
        QGuiApplication::processEvents();
        if (total_size != 0) {
            progress.SetValue(static_cast<int>((processed_size * 100) / total_size));
        }
        return !progress.WasCanceled();
    };

    QCryptographicHash hash(QCryptographicHash::Sha256);
    u64 received = 0;

    // Write file in chunks.
    auto content_receiver = [&file, &hash, &received, filename](const char* t_data,
                                                                size_t data_length) -> bool {
        hash.addData(QByteArrayView(t_data, static_cast<qsizetype>(data_length)));
        received += data_length;
        if (file.write(t_data, data_length) == -1) {
            LOG_WARNING(Frontend, "Could not write {} bytes to file {}", data_length,
                        filename.toStdString());
            QtCommon::Frontend::Critical(tr("Failed to save file"),
                                         tr("Could not write to %1.").arg(FileName(filename)));
            return false;
        }

        return true;
    };

    // Now send off request. GitHub's file server sometimes answers 502-504 or drops the
    // connection for a moment (v5.2's release hour): try up to three times, a few seconds apart,
    // starting the file over each time. The size and SHA-256 checks below still decide.
    constexpr int attempts = 3;
    httplib::Result result;
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        if (attempt > 1) {
            file.seek(0);
            file.resize(0);
            hash.reset();
            received = 0;
            progress.SetValue(0);
            progress.setWindowTitle(tr("Downloading... (try %1 of %2)").arg(attempt).arg(attempts));
            QEventLoop wait;
            QTimer::singleShot(2000 * (attempt - 1), &wait, &QEventLoop::quit);
            wait.exec();
            if (progress.WasCanceled()) {
                break;
            }
        }
        result = client->Get(m_asset.path, content_receiver, progress_callback);
        const bool canceled = !result && result.error() == httplib::Error::Canceled;
        const bool transient = (!result && !canceled) ||
                               (result && (result->status >= 500 || result->status == 429));
        if (!transient || attempt == attempts) {
            break;
        }
        LOG_WARNING(Frontend, "GET to {}{} failed ({}), trying again", m_asset.url, m_asset.path,
                    result ? std::to_string(result->status) : httplib::to_string(result.error()));
    }
    progress.close();

    if (!result) {
        LOG_ERROR(Frontend, "GET to {}{} returned null", m_asset.url, m_asset.path);
        if (result.error() != httplib::Error::Canceled) {
            QString error = QString::fromStdString(httplib::to_string(result.error()));
            error[0] = error[0].toLower();
            QtCommon::Frontend::Critical(tr("Failed to download file"),
                                         tr("Could not download %1.\nError code: %2.")
                                             .arg(QString::fromStdString(m_asset.filename), error));
        }
        return false;
    }

    const auto& response = result.value();
    if (response.status >= 400) {
        LOG_ERROR(Frontend, "GET to {}{} returned error status code: {}", m_asset.url, m_asset.path,
                  response.status);
        QtCommon::Frontend::Critical(tr("Failed to download file"),
                                     tr("Could not download %1.\nError code: %2.")
                                         .arg(QString::fromStdString(m_asset.filename),
                                              QString::number(response.status)));
        return false;
    }
    if (!response.has_header("content-type")) {
        LOG_ERROR(Frontend, "GET to {}{} returned no content", m_asset.url, m_asset.path);
        return false;
    }

    const bool wrong_size = m_asset.size != 0 && received != m_asset.size;
    const bool wrong_hash =
        !m_asset.sha256.empty() &&
        hash.result().toHex() != QByteArray::fromStdString(m_asset.sha256);
    if (wrong_size || wrong_hash) {
        LOG_ERROR(Frontend, "Downloaded {} does not match the published size or SHA-256",
                  m_asset.filename);
        file.cancelWriting();
        QtCommon::Frontend::Critical(
            tr("Download failed verification"),
            tr("The file does not match the checksum GitHub published for it, so it was not "
               "saved."));
        return false;
    }

    if (!file.commit()) {
        LOG_WARNING(Frontend, "Could not commit to file {}", filename.toStdString());
        QtCommon::Frontend::Critical(tr("Failed to save file"),
                                     tr("Could not commit to %1.").arg(FileName(filename)));
        return false;
    }

    return true;
}
