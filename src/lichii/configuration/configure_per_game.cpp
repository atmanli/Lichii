// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include <QAbstractButton>
#include <QByteArray>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/settings_enums.h"
#include "common/settings_input.h"
#include "common/string_util.h"
#include "configuration/shared_widget.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nup_container.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/xts_archive.h"
#include "core/loader/loader.h"
#include "frontend_common/config.h"
#include "lichii/configuration/configuration_shared.h"
#include "lichii/configuration/configure_applets.h"
#include "lichii/configuration/configure_audio.h"
#include "lichii/configuration/configure_cpu.h"
#include "lichii/configuration/configure_graphics.h"
#include "lichii/configuration/configure_graphics_advanced.h"
#include "lichii/configuration/configure_graphics_extensions.h"
#include "lichii/configuration/configure_input_per_game.h"
#include "lichii/configuration/configure_network.h"
#include "lichii/configuration/configure_per_game.h"
#include "lichii/configuration/configure_per_game_addons.h"
#include "lichii/configuration/configure_system.h"
#include "lichii/configuration/iidb_artwork_dialog.h"
#include "lichii/configuration/iidb_key_dialog.h"
#include "lichii/game/artwork_image.h"
#include "lichii/mod_store/mod_store_install.h"
#include "lichii/nup_dialogs.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"
#include "lichii/spotlight/widgets.h"
#include "lichii/util/util.h"
#include "qt_common/abstract/frontend.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"
#include "qt_common/util/vk.h"
#include "ui_configure_per_game.h"

namespace {

constexpr auto MaxIconBytes = ArtworkImage::MaxBytes;

QImage ReadIconFile(const QString& path) {
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > MaxIconBytes) {
        return {};
    }
    QFile image_file(path);
    if (!image_file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return ArtworkImage::Decode(image_file.read(MaxIconBytes + 1));
}

struct DetectionBudget {
    std::shared_ptr<std::atomic_bool> cancel;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{20};
    int requests_left = 24;

    bool Stopped() const {
        return cancel->load() || std::chrono::steady_clock::now() >= deadline;
    }
};

std::string NormalizeGameSlug(std::string title) {
    title = Common::ToLower(title);
    boost::replace_all(title, "é", "e");
    boost::replace_all(title, "è", "e");
    boost::replace_all(title, "ê", "e");
    boost::replace_all(title, "ō", "o");
    std::replace(title.begin(), title.end(), ' ', '-');
    std::replace(title.begin(), title.end(), '_', '-');
    std::erase_if(title, [](unsigned char c) { return !std::isalnum(c) && c != '-'; });
    title.erase(title.begin(),
                std::find_if(title.begin(), title.end(), [](char ch) { return ch != '-'; }));
    title.erase(std::find_if(title.rbegin(), title.rend(), [](char ch) { return ch != '-'; }).base(),
                title.end());
    title.erase(std::unique(title.begin(), title.end(),
                            [](char a, char b) { return a == '-' && b == '-'; }),
                title.end());
    return title;
}

std::string ResolveGameNameAlias(const std::string& raw_name, u64 title_id) {
    static const std::unordered_map<u64, std::string> by_title_id{
        {0x0400C3F00006E000ULL, "Mario Kart World"},
        {0x0100000000010000ULL, "Super Mario Odyssey"},
        {0x0100152000022000ULL, "Mario Kart 8 Deluxe"},
        {0x01007EF00011E000ULL, "The Legend of Zelda: Breath of the Wild"},
        {0x0100F2C0115B6000ULL, "The Legend of Zelda: Tears of the Kingdom"},
        {0x01006A800016E000ULL, "Super Smash Bros. Ultimate"},
        {0x01006F8002326000ULL, "Animal Crossing: New Horizons"},
    };
    static const std::unordered_map<std::string, std::string> by_alias{
        {"mkw", "Mario Kart World"},
        {"mariokartworld", "Mario Kart World"},
        {"mario-kart-world", "Mario Kart World"},
        {"dkb", "Donkey Kong Bananza"},
        {"donkeykongbananza", "Donkey Kong Bananza"},
        {"kar", "Kirby Air Riders"},
        {"kirbyairriders", "Kirby Air Riders"},
        {"mp4", "Metroid Prime 4: Beyond"},
        {"metroidprime4", "Metroid Prime 4: Beyond"},
        {"plza", "Pokemon Legends: Z-A"},
        {"pokemonlegendsza", "Pokemon Legends: Z-A"},
        {"mk8", "Mario Kart 8 Deluxe"},
        {"mk8d", "Mario Kart 8 Deluxe"},
        {"smo", "Super Mario Odyssey"},
        {"botw", "The Legend of Zelda: Breath of the Wild"},
        {"totk", "The Legend of Zelda: Tears of the Kingdom"},
        {"ssbu", "Super Smash Bros. Ultimate"},
        {"acnh", "Animal Crossing: New Horizons"},
        {"smbw", "Super Mario Bros. Wonder"},
        {"splatoon3", "Splatoon 3"},
        {"xc3", "Xenoblade Chronicles 3"},
        {"lm3", "Luigi's Mansion 3"},
        {"pikmin4", "Pikmin 4"},
    };
    const std::string slug = NormalizeGameSlug(raw_name);
    if (const auto it = by_alias.find(slug); it != by_alias.end()) {
        return it->second;
    }
    if (slug.empty()) {
        if (const auto it = by_title_id.find(title_id); it != by_title_id.end()) {
            return it->second;
        }
    }
    return raw_name;
}

std::string UrlEncode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

std::optional<std::string> HttpGetUrl(const std::string& full_url, DetectionBudget& budget,
                                      size_t max_bytes = MaxIconBytes) {
    if (budget.Stopped() || budget.requests_left <= 0 || full_url.size() > 4096) {
        return std::nullopt;
    }
    const QUrl url(QString::fromStdString(full_url), QUrl::StrictMode);
    const auto host = url.host();
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasFragment() ||
        (host != QStringLiteral("raw.githubusercontent.com") &&
         host != QStringLiteral("en.wikipedia.org") &&
         host != QStringLiteral("upload.wikimedia.org") &&
         host != QStringLiteral("www.bing.com") && host != QStringLiteral("th.bing.com") &&
         host != QStringLiteral("tse1.mm.bing.com") && host != QStringLiteral("tse2.mm.bing.com") &&
         host != QStringLiteral("tse3.mm.bing.com") &&
         host != QStringLiteral("tse4.mm.bing.com"))) {
        return std::nullopt;
    }
    --budget.requests_left;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        budget.deadline - std::chrono::steady_clock::now());
    if (budget.Stopped() || remaining.count() <= 0) {
        return std::nullopt;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setRawHeader("User-Agent", "Eden icon lookup");
    request.setRawHeader("Accept-Encoding", "identity");

    std::string body;
    bool failed = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QTimer cancellation;
    auto* reply = manager.get(request);
    reply->setReadBufferSize(64 * 1024);

    const auto abort = [&] {
        failed = true;
        reply->abort();
        loop.quit();
    };
    const auto receive = [&] {
        if (failed || budget.Stopped() ||
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            abort();
            return;
        }
        const auto data = reply->read(static_cast<qint64>(max_bytes - body.size()) + 1);
        if (static_cast<size_t>(data.size()) > max_bytes - body.size()) {
            abort();
            return;
        }
        body.append(data.constData(), static_cast<size_t>(data.size()));
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, receive);
    QObject::connect(reply, &QNetworkReply::finished, &loop, [&] { loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, abort);
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
        if (budget.Stopped()) {
            abort();
        }
    });
    timeout.start(static_cast<int>(std::min(std::chrono::milliseconds{5000}, remaining).count()));
    cancellation.start(50);
    if (!reply->isFinished()) {
        loop.exec();
    }
    if (!failed) {
        receive();
    }
    if (failed || budget.Stopped() || reply->error() != QNetworkReply::NoError || body.empty()) {
        return std::nullopt;
    }
    return body;
}

int ScoreCandidateTitle(const std::string& candidate, const std::string& query) {
    const std::string c_slug = NormalizeGameSlug(candidate);
    const std::string q_slug = NormalizeGameSlug(query);
    if (c_slug.empty() || q_slug.empty()) {
        return -1000;
    }
    if (c_slug == q_slug) {
        return 1000;
    }
    int score = 0;
    if (c_slug.starts_with(q_slug + "-") || q_slug.starts_with(c_slug + "-")) {
        score += 750 - static_cast<int>(std::abs(static_cast<long>(c_slug.size()) -
                                                 static_cast<long>(q_slug.size())));
    } else if (c_slug.find(q_slug) != std::string::npos) {
        score += 600;
    }
    const auto split_words = [](const std::string& s) {
        std::set<std::string> words;
        std::size_t start = 0;
        while (start < s.size()) {
            const auto dash = s.find('-', start);
            const auto word = s.substr(start, dash - start);
            if (!word.empty()) {
                words.insert(word);
            }
            if (dash == std::string::npos) {
                break;
            }
            start = dash + 1;
        }
        return words;
    };
    const auto q_words = split_words(q_slug);
    const auto c_words = split_words(c_slug);
    int overlap = 0;
    for (const auto& w : q_words) {
        if (c_words.contains(w)) {
            ++overlap;
        }
    }
    score += overlap * 120 - static_cast<int>(c_words.size()) * 10;
    const std::string lower_cand = Common::ToLower(candidate);
    if (lower_cand.find("(series)") != std::string::npos ||
        lower_cand.find("(character)") != std::string::npos ||
        lower_cand.find("soundtrack") != std::string::npos) {
        score -= 400;
    }
    return score;
}

class RoundedBox final : public QWidget {
public:
    RoundedBox(QColor fill_, QColor stroke_, qreal radius_)
        : fill{fill_}, stroke{stroke_}, radius{radius_} {
        setProperty("spotlightTransparent", true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(stroke.alpha() == 0 ? QPen(Qt::NoPen) : QPen(stroke, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

private:
    QColor fill;
    QColor stroke;
    qreal radius;
};

enum class NoteKind { Info, Warning, Error };

/// A rounded strip with an icon and wrapped text (Paper: note strip, radius 14, 13/18 text).
RoundedBox* NoteStrip(QLabel* text, NoteKind kind) {
    struct Tint {
        QColor fill;
        QColor stroke;
        QColor ink;
        QColor icon;
        QString glyph;
    };
    const Tint tint = kind == NoteKind::Warning
                          ? Tint{QColor(0xff, 0xd3, 0x7a, 0x14), QColor(0xff, 0xd3, 0x7a, 0x33),
                                 QColor(0xff, 0xe7, 0xb3), QColor(0xff, 0xd3, 0x7a),
                                 QStringLiteral("warning-circle")}
                      : kind == NoteKind::Error
                          ? Tint{QColor(0xff, 0x8f, 0xae, 0x1f), QColor(0xff, 0x8f, 0xae, 0x40),
                                 QColor(0xff, 0xc2, 0xd2), Spotlight::Color::AccentLight(),
                                 QStringLiteral("warning-circle")}
                          : Tint{QColor(0xff, 0xff, 0xff, 0x0a), Spotlight::Color::StrokeSubtle(),
                                 QColor(0xff, 0xff, 0xff, 0xb8),
                                 Spotlight::Color::TextSecondary(), QStringLiteral("info")};
    auto* box = new RoundedBox(tint.fill, tint.stroke, 14);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);
    auto* icon = new QLabel;
    icon->setFixedSize(16, 18);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(Spotlight::IconPixmap(tint.glyph, 16, tint.icon, box->devicePixelRatioF()));
    layout->addWidget(icon, 0, Qt::AlignTop);
    text->setFont(Spotlight::Font(13, 400));
    text->setWordWrap(true);
    Spotlight::SetTextColor(text, tint.ink);
    layout->addWidget(text, 1);
    return box;
}

/// A pill with an optional leading icon (Paper: secondary action, 36 px, 13/600 text, 18 side
/// padding; 30 px rows use 12 px text).
class ActionPill final : public QAbstractButton {
public:
    ActionPill(const QString& text, QString icon_, QColor fill_, QColor ink_, QColor stroke_ = {},
               int height_ = 36)
        : icon{std::move(icon_)}, fill{fill_}, ink{ink_}, stroke{stroke_}, pill_height{height_} {
        setText(text);
        setFont(Spotlight::Font(pill_height < 36 ? 12 : 13, 600));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedHeight(pill_height);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override {
        return {fontMetrics().horizontalAdvance(text()) + 36 + (icon.isEmpty() ? 0 : 24),
                pill_height};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool enabled = isEnabled();
        QColor background = enabled ? fill : QColor(0xff, 0xff, 0xff, 0x0f);
        if (enabled && underMouse()) {
            background.setAlpha(std::min(255, background.alpha() + 0x14));
        }
        const QColor foreground = enabled ? ink : QColor(0xff, 0xff, 0xff, 0x59);
        const QRectF bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setPen(Spotlight::HasKeyboardFocus(this) ? QPen(Spotlight::Color::Accent(), 2)
                       : stroke.isValid() && enabled    ? QPen(stroke, 1)
                                                        : QPen(Qt::NoPen));
        painter.setBrush(background);
        painter.drawRoundedRect(bounds, bounds.height() / 2, bounds.height() / 2);
        const int content = fontMetrics().horizontalAdvance(text()) + (icon.isEmpty() ? 0 : 24);
        int x = (width() - content) / 2;
        if (!icon.isEmpty()) {
            const qreal dpr = devicePixelRatioF();
            if (glyph_enabled != enabled || glyph.devicePixelRatio() != dpr) {
                glyph = Spotlight::IconPixmap(icon, 16, foreground, dpr);
                glyph_enabled = enabled;
            }
            painter.drawPixmap(x, (height() - 16) / 2, glyph);
            x += 24;
        }
        painter.setPen(foreground);
        painter.drawText(QRect(x, 0, width() - x, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
    }
    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }

private:
    QString icon;
    QColor fill;
    QColor ink;
    QColor stroke;
    int pill_height;
    QPixmap glyph;
    bool glyph_enabled = false;
};

/// Keeps a pill's enabled state and label in step with the hidden button it clicks.
class MirrorButton final : public QObject {
public:
    MirrorButton(QAbstractButton* source_, QAbstractButton* target_)
        : QObject(target_), source{source_}, target{target_} {
        source->installEventFilter(this);
        Sync();
        connect(target, &QAbstractButton::clicked, source, &QAbstractButton::click);
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::EnabledChange) {
            Sync();
        }
        return false;
    }

private:
    void Sync() {
        target->setEnabled(source->isEnabled());
        if (target->text() != source->text()) {
            target->setText(source->text());
            target->updateGeometry();
        }
        target->setToolTip(source->toolTip());
    }

    QAbstractButton* source;
    QAbstractButton* target;
};

QWidget* SmallBadge(const QString& text, const QColor& fill, const QColor& ink) {
    auto* badge = new RoundedBox(fill, Qt::transparent, 11);
    badge->setFixedHeight(22);
    auto* layout = new QHBoxLayout(badge);
    layout->setContentsMargins(9, 0, 9, 0);
    auto* label = new QLabel(text);
    label->setFont(Spotlight::Font(11, 600));
    Spotlight::SetTextColor(label, ink);
    layout->addWidget(label);
    badge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return badge;
}

} // namespace

ConfigurePerGame::ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name,
                                   std::vector<VkDeviceInfo::Record>& vk_device_records,
                                   Core::System& system_)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigurePerGame>()),
      file_path(std::filesystem::path(Common::FS::ToU8String(file_name))), title_id{title_id_},
      system{system_}, devices{vk_device_records},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      tab_group{std::make_shared<std::vector<ConfigurationShared::Tab*>>()} {
    const auto config_file_name = title_id == 0 ? Common::FS::PathToUTF8String(file_path.filename())
                                                : fmt::format("{:016X}", title_id);
    game_config = std::make_unique<QtConfig>(config_file_name, Config::ConfigType::PerGameConfig);
    addons_tab = std::make_unique<ConfigurePerGameAddons>(system_, this);
    connect(addons_tab.get(), &ConfigurePerGameAddons::BrowseModsRequested, this,
            &ConfigurePerGame::BrowseModsRequested);
    audio_tab = std::make_unique<ConfigureAudio>(system_, tab_group, *builder, this);
    cpu_tab = std::make_unique<ConfigureCpu>(system_, tab_group, *builder, this);
    graphics_advanced_tab =
        std::make_unique<ConfigureGraphicsAdvanced>(system_, tab_group, *builder, this);
    graphics_extensions_tab =
        std::make_unique<ConfigureGraphicsExtensions>(system_, tab_group, *builder, this);
    graphics_tab = std::make_unique<ConfigureGraphics>(
        system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
        [](Settings::AspectRatio, Settings::ResolutionSetup) {}, tab_group, *builder, this);
    input_tab = std::make_unique<ConfigureInputPerGame>(system_, game_config.get(), this);
    system_tab = std::make_unique<ConfigureSystem>(system_, tab_group, *builder, this);
    network_tab = std::make_unique<ConfigureNetwork>(system_, this, true);
    applets_tab = std::make_unique<ConfigureApplets>(system_, tab_group, *builder, this);

    ui->setupUi(this);
    Spotlight::FadeInOnShow(this);

    setFocusPolicy(Qt::ClickFocus);
    setWindowTitle(tr("Game Settings"));

    addons_tab->SetTitleId(title_id);

    connect(ui->button_upload_icon, &QPushButton::clicked, this,
            &ConfigurePerGame::OnUploadNupIcon);
    connect(ui->button_detect_icon, &QPushButton::clicked, this,
            &ConfigurePerGame::OnDetectNupIcon);
    ui->button_remove_icon->setEnabled(false);
    connect(ui->button_remove_icon, &QPushButton::clicked, this,
            &ConfigurePerGame::OnRemoveCustomIcon);
    connect(ui->button_browse_iidb, &QPushButton::clicked, this,
            &ConfigurePerGame::OnBrowseIiDB);
    // Where a picked icon goes, and for NUP and NX files the editor of what the file stores.
    icon_destination = new QLabel(ui->nup_icon_widget);
    icon_destination->setWordWrap(true);
    Spotlight::SetTextColor(icon_destination, Spotlight::Color::TextMuted());
    ui->nup_icon_layout->addWidget(icon_destination, 4, 0, 1, 2);
    auto* edit_in_file = new Spotlight::PillButton(tr("Edit title and icon in the file"),
                                                   QStringLiteral("archive"));
    edit_in_file->setObjectName(QStringLiteral("editInFile"));
    edit_in_file->setToolTip(tr("Saves and closes Game Settings, then opens the editor"));
    edit_in_file->hide();
    ui->nup_icon_layout->addWidget(edit_in_file, 5, 0, 1, 2);
    connect(edit_in_file, &QAbstractButton::clicked, this, [this] {
        archive_edit_requested = true;
        accept();
    });
    connect(&detect_watcher, &QFutureWatcher<DetectIconResult>::finished, this, [this] {
        if (!detect_cancel || detect_cancel->load()) {
            return;
        }
        ui->button_detect_icon->setText(tr("Detect Icon"));
        ui->button_detect_icon->setEnabled(true);
        ui->button_upload_icon->setEnabled(true);
        ui->button_browse_iidb->setEnabled(true);
        ui->display_name->setEnabled(true);
        ui->allow_online_detection->setEnabled(true);
        if (artwork_detecting != nullptr) {
            artwork_detecting->hide();
        }
        auto out = detect_watcher.result();
        if (out.icon.isNull()) {
            UpdateRemoveIconButton();
            ShowArtworkProblem(out.error.empty()
                                   ? tr("Upload an image or enable online lookup.")
                                   : QString::fromStdString(out.error),
                               false);
            return;
        }
        if (artwork_problem != nullptr) {
            artwork_problem->hide();
        }
        if (!out.resolved_title.empty()) {
            ui->display_name->setText(QString::fromStdString(out.resolved_title));
        }
        StageCustomIcon(std::move(out.icon));
    });

    ui->footer->setVisible(system.IsPoweredOn());
    BuildSettings();
    UpdateIconPreview({});
    ConfigurationShared::FitToScreen(this, QSize(1280, 800));
}

void ConfigurePerGame::BuildSettings() {
    using Spotlight::SettingsPage;
    namespace Color = Spotlight::Color;
    namespace Tints = Spotlight::SettingsColor;
    setWindowFlag(Qt::FramelessWindowHint, Spotlight::ClientFrame);
    if constexpr (Spotlight::ClientFrame) {
        Spotlight::RoundWindowCorners(this);
    }
    settings_page = new SettingsPage(this, true);
    ui->verticalLayout_3->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout_3->setSpacing(0);
    ui->verticalLayout_3->addWidget(settings_page);
    settings_page->SetFooter(ui->footer);
    connect(settings_page, &SettingsPage::BackRequested, this, &QDialog::accept);

    const auto archive_suffix = QString::fromStdString(file_path.extension().string()).toLower();
    const bool archive_icon_file =
        archive_suffix == QStringLiteral(".nup") || archive_suffix == QStringLiteral(".nx");
    const int overview = settings_page->AddCategory(tr("Overview"),
                                                    tr("Everything about this game in one place"),
                                                    QStringLiteral("star"), Color::Accent());
    const bool is_switch_2 =
        Core::FindSwitch2Program(Core::GetGameFileFromPath(system.GetFilesystem(),
                                                           Common::FS::PathToUTF8String(file_path)),
                                 title_id, 0)
            .has_value();
    const int graphics = settings_page->AddCategory(
        tr("Graphics"),
        is_switch_2 ? tr("Picture and guest frame-rate settings for this game")
                    : tr("Picture settings for this game"),
        QStringLiteral("monitor"), Tints::Green());
    settings_page->SetAdvancedSubtitle(
        graphics, tr("Advanced: rows say Global until you change them for this game."));
    auto* fps = is_switch_2 ? SettingsPage::Editor(graphics_tab.get(), Settings::values.fps_limit)
                            : nullptr;
    const int frame_rate =
        fps == nullptr
            ? -1
            : settings_page->AddCategory(tr("Frame rate"), tr("Set a guest FPS cap for this game."),
                                         QStringLiteral("lightning"), Tints::Gold());
    const int controls = settings_page->AddCategory(
        tr("Controls"),
        tr("Choose a saved profile for each slot. Button mapping is in global Controls."),
        QStringLiteral("game-controller"), Tints::Blue());
    const int mods = settings_page->AddCategory(
        tr("Mods and cheats"),
        tr("Turn mods and cheats on per game. Changes apply next launch."),
        QStringLiteral("puzzle-piece"), Tints::Lavender());
    const int updates = settings_page->AddCategory(
        tr("Updates and DLC"), tr("Choose which update runs and which DLC is on"),
        QStringLiteral("cloud-arrow-down"), Color::Accent());
    const int saves =
        settings_page->AddCategory(tr("Save data"), tr("Open this game's saves in the file manager."),
                                   QStringLiteral("folder"), Tints::Green());
    const int audio = settings_page->AddCategory(
        tr("Audio"), tr("Only what you change here differs from global."),
        QStringLiteral("speaker-high"), Tints::Blue(), {}, true);
    const int system_category = settings_page->AddCategory(
        tr("System"), tr("Only what you change here differs from global."), QStringLiteral("cpu"),
        Color::Accent(), {}, true);
    artwork_category = settings_page->AddCategory(
        tr("Artwork"),
        archive_icon_file ? tr("How this game looks in your library. Icons go into the game file.")
                          : tr("How this game looks in your library. Stays on this PC."),
        QStringLiteral("image"), Tints::Gold(), {}, true);
    const int info = settings_page->AddCategory(
        tr("Info"), tr("What Lichii knows about this game and its file"), QStringLiteral("info"),
        Tints::Lavender(), tr("Info and file"), true);
    const int cpu = settings_page->AddCategory(
        tr("CPU"),
        tr("CPU choices apply to this game only. Available backends depend on the host and build."),
        QStringLiteral("lightning"), Tints::Gold(), {}, true);
    const int network = settings_page->AddCategory(
        tr("Network"), tr("Only what you change here differs from global."),
        QStringLiteral("globe"), Tints::Green(), {}, true);
    const int applets = settings_page->AddCategory(
        tr("Applets"), tr("Only what you change here differs from global."),
        QStringLiteral("squares-four"), Tints::Lavender(), {}, true);

    auto* banner = new QWidget;
    auto* banner_layout = new QHBoxLayout(banner);
    banner_layout->setContentsMargins(0, 4, 0, 6);
    banner_layout->setSpacing(18);
    game_banner_icon = new QLabel;
    game_banner_icon->setFixedSize(72, 72);
    banner_layout->addWidget(game_banner_icon, 0, Qt::AlignVCenter);
    auto* identity = new QVBoxLayout;
    identity->setSpacing(4);
    game_banner_title = SettingsPage::Text({}, 24, 600);
    game_banner_title->setFont(Spotlight::DisplayFont(24));
    game_banner_title->setWordWrap(true);
    game_banner_summary = SettingsPage::Text({}, 13, 400, true);
    game_banner_summary->setWordWrap(true);
    identity->addWidget(game_banner_title);
    identity->addWidget(game_banner_summary);
    auto* details = new QHBoxLayout;
    details->setContentsMargins(0, 2, 0, 0);
    details->setSpacing(8);
    if (title_id != 0) {
        details->addWidget(is_switch_2
                               ? SmallBadge(tr("Switch 2"), Color::Accent(), Color::OnAccent())
                               : SmallBadge(tr("Switch 1"), Color::Text(), Color::OnLight()));
    }
    game_banner_version = SettingsPage::Text({}, 12, 500, true);
    details->addWidget(game_banner_version, 1);
    identity->addLayout(details);
    banner_layout->addLayout(identity, 1);
    auto* reset =
        new Spotlight::PillButton(tr("Reset all"), QStringLiteral("arrow-counter-clockwise"));
    reset->setToolTip(tr("Remove this game's custom configuration"));
    reset->setEnabled(!system.IsPoweredOn());
    connect(reset, &QAbstractButton::clicked, this, [this] {
        if (QMessageBox::warning(
                this, tr("Reset game settings"),
                tr("Remove this game's custom configuration and close Game Settings?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
            return;
        }
        QtCommon::Game::RemoveCustomConfiguration(title_id,
                                                  Common::FS::PathToUTF8String(file_path));
        done(QDialog::Rejected);
    });
    banner_layout->addWidget(reset, 0, Qt::AlignVCenter);
    settings_page->SetBanner(banner, {overview, graphics});
    connect(ui->display_name, &QLineEdit::textChanged, this, [this] { UpdateSpotlightIdentity(); });

    const auto add_setting = [this](QVBoxLayout* section, const Settings::BasicSetting& setting,
                                    const QString& name, const QString& description,
                                    const QString& icon, const QColor& color) -> QLabel* {
        auto* source = SettingsPage::Editor(this, setting);
        auto* control = source != nullptr ? SettingsPage::BindSetting(source) : nullptr;
        return control != nullptr
                   ? SettingsPage::AddRow(section, name, description, icon, color, control)
                   : nullptr;
    };
    const auto watch = [this](const Settings::BasicSetting& setting, QLabel* label,
                              std::function<QString()> text) {
        auto* source = SettingsPage::Editor(this, setting);
        if (label == nullptr || source == nullptr) {
            return;
        }
        const auto update = [label, text = std::move(text)] {
            label->setText(text());
            label->setVisible(!label->text().isEmpty());
        };
        SettingsPage::WatchOverride(source, label, update);
        if (source->combobox != nullptr) {
            connect(source->combobox, &QComboBox::currentIndexChanged, label, update);
        }
        if (source->spinbox != nullptr) {
            connect(source->spinbox, &QSpinBox::valueChanged, label, update);
        }
        if (source->checkbox != nullptr) {
            connect(source->checkbox, &QCheckBox::toggled, label, update);
        }
        update();
    };
    const auto is_custom = [this](const Settings::BasicSetting& setting) {
        auto* source = SettingsPage::Editor(this, setting);
        return source != nullptr && source->property("custom").toBool();
    };
    const auto scale = [](Settings::ResolutionSetup setup) {
        Settings::ResolutionScalingInfo info{};
        Settings::TranslateResolutionInfo(setup, info);
        return info.up_factor;
    };
    const auto fps_text = [](u16 value) {
        return value == 0 ? tr("Auto") : tr("%1 FPS").arg(value);
    };
    const auto add_fps = [add_setting, watch, is_custom, fps_text](QVBoxLayout* section,
                                                                   bool overview_row) {
        auto* detail = add_setting(
            section, Settings::values.fps_limit,
            overview_row ? tr("Guest FPS cap") : tr("FPS limit"), {}, QStringLiteral("lightning"),
            Tints::Green());
        if (detail == nullptr) {
            return;
        }
        if (!overview_row) {
            detail->setText(tr("0–480 FPS. Explicit Auto follows the game; Global inherits your "
                               "Graphics setting."));
            detail->show();
            return;
        }
        watch(Settings::values.fps_limit, detail, [is_custom, fps_text] {
            const auto& fps_limit = Settings::values.fps_limit;
            const QString global = tr("global is %1").arg(fps_text(fps_limit.GetValue(true)));
            if (!is_custom(fps_limit)) {
                return tr("Follows global · %1").arg(global);
            }
            const u16 value = fps_limit.GetValue();
            return value == 0 ? tr("Explicit Auto follows the game · %1").arg(global)
                              : tr("Capped at %1 · %2").arg(fps_text(value), global);
        });
    };
    const auto add_resolution = [add_setting, watch, is_custom, scale](QVBoxLayout* section,
                                                                       bool overview_row) {
        auto* detail =
            add_setting(section, Settings::values.resolution_setup, tr("Resolution"), {},
                        QStringLiteral("monitor"), Tints::Gold());
        watch(Settings::values.resolution_setup, detail, [is_custom, scale, overview_row] {
            const auto& setup = Settings::values.resolution_setup;
            const QString global = tr("%1×").arg(scale(setup.GetValue(true)));
            if (!overview_row) {
                return tr("Global is %1").arg(global);
            }
            if (!is_custom(setup)) {
                return tr("Follows global · global is %1").arg(global);
            }
            Settings::ResolutionScalingInfo info{};
            Settings::TranslateResolutionInfo(setup.GetValue(), info);
            return tr("%1× · %2p handheld / %3p docked · global is %4")
                .arg(info.up_factor)
                .arg(720 * info.up_factor)
                .arg(1080 * info.up_factor)
                .arg(global);
        });
    };
    const auto add_content = [this, mods, updates](QVBoxLayout* content, bool details) {
        auto* manage_mods = SettingsPage::Manage(tr("Manage"));
        connect(manage_mods, &QAbstractButton::clicked, this,
                [this, mods] { settings_page->SetCategory(mods); });
        auto* mods_detail = SettingsPage::AddRow(
            content, tr("Mods and cheats"),
            details ? tr("Manage installed mods and cheats") : QString{},
            QStringLiteral("puzzle-piece"), Tints::Blue(), manage_mods);
        if (details) {
            mods_summaries.append(mods_detail);
        }
        auto* manage_updates = SettingsPage::Manage(tr("Manage"));
        connect(manage_updates, &QAbstractButton::clicked, this,
                [this, updates] { settings_page->SetCategory(updates); });
        SettingsPage::AddRow(content, tr("Updates and DLC"),
                             details ? tr("Review installed updates and DLC") : QString{},
                             QStringLiteral("cloud-arrow-down"), Color::Accent(), manage_updates);
        UpdateModsSummary();
    };
    settings_page->SetBuilder(overview, [this, overview, frame_rate, add_fps, add_resolution,
                                         add_content] {
        auto* common = settings_page->AddSection(overview, tr("This game"));
        if (frame_rate != -1) {
            add_fps(common, true);
        }
        add_resolution(common, true);
        add_content(settings_page->AddSection(overview), true);
        if (system.IsPoweredOn()) {
            auto* text = new QLabel(tr("Reset all is unavailable while emulation runs."));
            settings_page->AddBlock(overview, NoteStrip(text, NoteKind::Info), text->text());
        }
    });
    settings_page->SetBuilder(graphics, [this, graphics, frame_rate, add_setting, add_fps,
                                         add_resolution, add_content, watch, is_custom] {
        auto* picture = settings_page->AddSection(graphics, tr("Graphics"));
        picture->parentWidget()->setProperty("compactRows", true);
        AddPresetRow(picture);
        add_resolution(picture, false);
        add_setting(picture, Settings::values.scaling_filter, tr("Scaling filter"), {},
                    QStringLiteral("sliders-horizontal"), Tints::Lavender());
        auto* frame_gen =
            add_setting(picture, Settings::values.frame_gen, tr("Enable frame generation"), {},
                        QStringLiteral("film-strip"), Tints::Gold());
        watch(Settings::values.frame_gen, frame_gen, [is_custom] {
            return is_custom(Settings::values.frame_gen) ? tr("Changed for this game")
                                                         : tr("Inherits from Global");
        });
        add_content(picture, false);
        if (frame_rate == -1) {
            return;
        }
        auto* cap =
            settings_page->AddSection(graphics, tr("Guest FPS cap · also shown in Frame rate"));
        cap->parentWidget()->setProperty("compactRows", true);
        add_fps(cap, false);
        BuildNativeModes(graphics);
    });
    if (frame_rate != -1) {
        settings_page->SetBuilder(frame_rate, [this, frame_rate, add_setting] {
            auto* section = settings_page->AddSection(frame_rate, tr("Frame rate"));
            section->parentWidget()->setProperty("compactRows", true);
            add_setting(section, Settings::values.fps_limit, tr("Guest FPS cap"),
                        tr("Auto follows the game; other values cap how often it presents."),
                        QStringLiteral("lightning"), Tints::Gold());
            settings_page->AddBlock(
                frame_rate,
                SettingsPage::Note(tr("The Frame rate page appears for Switch 2 games with an "
                                      "available cap editor. The same setting is on Overview and "
                                      "Graphics.")),
                {});
        });
    }
    for (const int category : {mods, updates}) {
        settings_page->SetBuilder(category, [this, mods, updates] { BuildAddons(mods, updates); });
    }
    settings_page->SetBuilder(saves, [this, saves] {
        auto* save = SettingsPage::Manage(tr("Open folder"));
        save->setEnabled(title_id != 0);
        connect(save, &QAbstractButton::clicked, this, &ConfigurePerGame::OpenSaveDataRequested);
        SettingsPage::AddRow(settings_page->AddSection(saves), tr("Save data folder"),
                             tr("Opens the saves for the applicable profile."),
                             QStringLiteral("folder-open"), Tints::Green(), save);
        settings_page->AddBlock(
            saves, SettingsPage::Note(tr("Unavailable for files without a game title ID.")), {});
    });
    settings_page->SetBuilder(controls, [this, controls] {
        const auto& profiles = input_tab->ProfileComboBoxes();
        for (size_t index = 0; index < profiles.size(); ++index) {
            auto* source = profiles[index];
            auto* row = new QWidget;
            row->setFixedHeight(46);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(18, 0, 16, 0);
            layout->setSpacing(14);
            auto* copy = new QVBoxLayout;
            copy->setSpacing(2);
            const QString title = tr("Player %1 profile").arg(index + 1);
            copy->addWidget(SettingsPage::Text(title, 14, 600));
            auto* changed = SettingsPage::Text(tr("Changed for this game"), 12, 400, true);
            copy->addWidget(changed);
            layout->addLayout(copy, 1);
            layout->addWidget(SettingsPage::BindCombo(source));
            const auto update = [source, changed] {
                changed->setVisible(source->currentIndex() > 0);
            };
            connect(source, &QComboBox::currentIndexChanged, changed, update);
            update();
            settings_page->AddBlock(controls, row, title);
        }
        settings_page->AddBlock(
            controls,
            SettingsPage::Note(
                tr("If a profile no longer exists, Lichii falls back to its usual input "
                   "configuration.")),
            {});
    });
    settings_page->SetBuilder(artwork_category, [this] { BuildArtworkPage(artwork_category); });
    settings_page->SetBuilder(info, [this, info] { BuildInfoPage(info); });
    settings_page->SetBuilder(system_category, [this, system_category] {
        auto* text =
            new QLabel(tr("Select a compatible language and region if a game starts with the "
                          "wrong locale."));
        settings_page->AddBlock(system_category, NoteStrip(text, NoteKind::Warning),
                                text->text(), true);
    });

    addons_tab->hide();
    ui->gameInfo->hide();
    const auto queue_addons = [this] {
        if (std::exchange(addons_queued, true)) {
            return;
        }
        QTimer::singleShot(0, this, [this] {
            addons_queued = false;
            if (store_rows != nullptr) {
                RebuildAddons();
            } else {
                UpdateModsSummary();
            }
        });
    };
    auto* addons_model = addons_tab->Model();
    connect(addons_model, &QAbstractItemModel::rowsInserted, this, queue_addons);
    connect(addons_model, &QAbstractItemModel::rowsRemoved, this, queue_addons);
    connect(addons_model, &QAbstractItemModel::modelReset, this, queue_addons);
    connect(addons_model, &QStandardItemModel::itemChanged, this,
            &ConfigurePerGame::UpdateModsSummary);
    input_tab->hide();
    for (const auto& [category, widget, name] :
         std::initializer_list<std::tuple<int, QWidget*, QString>>{
             {system_category, system_tab.get(), tr("System")},
             {cpu, cpu_tab.get(), tr("CPU")},
             {audio, audio_tab.get(), tr("Audio")},
             {network, network_tab.get(), tr("Network")},
             {applets, applets_tab.get(), tr("Applets")},
             {graphics, graphics_tab.get(), tr("Graphics")},
             {graphics, graphics_advanced_tab.get(), tr("Advanced graphics")},
             {graphics, graphics_extensions_tab.get(), tr("Extra graphics")},
         }) {
        ConfigurationShared::UseToggles(widget);
        settings_page->AddPage(category, widget, name);
    }
    const QString graphics_chip = tr("Graphics");
    const QString advanced_chip = tr("Advanced graphics");
    const QString extra_chip = tr("Extra graphics");
    settings_page->SetAdvancedSections(
        graphics,
        {
            {tr("Graphics · Device"),
             tr("Depends on the renderer and whether a game is running"),
             0,
             {QStringLiteral("renderer_backend"), QStringLiteral("vulkan_device"),
              QStringLiteral("vsync_mode")},
             graphics_chip},
            {tr("Graphics · Picture and display"),
             {},
             0,
             {QStringLiteral("fullscreen_mode"), QStringLiteral("aspect_ratio"),
              QStringLiteral("resolution_setup"), QStringLiteral("scaling_filter"),
              QStringLiteral("fsr_sharpening_slider"), QStringLiteral("anti_aliasing"),
              QStringLiteral("hdr_mode"), QStringLiteral("hdr_swapchain"),
              QStringLiteral("hdr_reference_white"), QStringLiteral("hdr_peak_luminance")},
             graphics_chip},
            {tr("Graphics · Frame generation"),
             {},
             0,
             {QStringLiteral("frame_gen"), QStringLiteral("frame_gen_type"),
              QStringLiteral("frame_gen_multiplier"), QStringLiteral("frame_gen_target_rate"),
              QStringLiteral("frame_gen_flow_scale_auto"), QStringLiteral("frame_gen_flow_scale"),
              QStringLiteral("frame_gen_queue_target"),
              QStringLiteral("frame_gen_flyhigh_protect_hud"), QStringLiteral("fps_limit")},
             graphics_chip},
            {tr("Graphics · Upscaling and effects"),
             tr("On supported builds and devices"),
             0,
             {QStringLiteral("game_dlss_network"), QStringLiteral("nvidia_dlss"),
              QStringLiteral("nvidia_dlss_quality"), QStringLiteral("nvidia_dlss_preset"),
              QStringLiteral("amd_fsr4"), QStringLiteral("post_shader_enabled")},
             graphics_chip},
            {tr("Advanced graphics · Accuracy and memory"),
             tr("Change when this game draws incorrectly"),
             1,
             {QStringLiteral("gpu_accuracy"), QStringLiteral("dma_accuracy"),
              QStringLiteral("gpu_fence_behavior"), QStringLiteral("native_compute_write_mark"),
              QStringLiteral("vram_usage_mode"), QStringLiteral("nvdec_emulation"),
              QStringLiteral("max_anisotropy"), QStringLiteral("accelerate_astc"),
              QStringLiteral("astc_recompression"), QStringLiteral("sync_memory_operations")},
             advanced_chip},
            {tr("Advanced graphics · Pacing and shaders"),
             {},
             1,
             {QStringLiteral("frame_pacing_mode"), QStringLiteral("use_video_framerate"),
              QStringLiteral("renderer_force_max_clock"), QStringLiteral("use_disk_shader_cache"),
              QStringLiteral("use_vulkan_driver_pipeline_cache"),
              QStringLiteral("native_async_draws"), QStringLiteral("native_pipeline_wait_ms"),
              QStringLiteral("skip_unbuilt_mesh_draws"), QStringLiteral("precache_shaders"),
              QStringLiteral("build_all_cached_shaders"),
              QStringLiteral("enable_compute_pipelines"), QStringLiteral("use_reactive_flushing"),
              QStringLiteral("barrier_feedback_loops"), QStringLiteral("enable_buffer_history"),
              QStringLiteral("render_thread")},
             advanced_chip},
            {tr("Extra graphics · Hacks and Vulkan extensions"),
             {},
             1,
             {QStringLiteral("skip_cpu_inner_invalidation"), QStringLiteral("async_presentation"),
              QStringLiteral("fix_bloom_effects"), QStringLiteral("use_asynchronous_shaders"),
              QStringLiteral("gpu_unswizzle_enabled"), QStringLiteral("gpu_unswizzle_texture_size"),
              QStringLiteral("gpu_unswizzle_stream_size"),
              QStringLiteral("gpu_unswizzle_chunk_size"), QStringLiteral("pin_gpu_threads"),
              QStringLiteral("dyna_state"), QStringLiteral("sample_shading"),
              QStringLiteral("vertex_input_dynamic_state"), QStringLiteral("mesh_shaders")},
             extra_chip},
            {tr("Mario Kart World"),
             tr("Advanced native modes"),
             1,
             {QStringLiteral("mkw_60fps_fallback"), QStringLiteral("mkw_dynamic_fps")},
             tr("Title-specific settings")},
            {tr("Hyrule Warriors: Age of Imprisonment"),
             tr("Advanced native modes"),
             1,
             {QStringLiteral("hyrule_dynamic_fps")},
             tr("Title-specific settings")},
            {tr("The Legend of Zelda: Ocarina of Time"),
             {},
             1,
             {QStringLiteral("ocarina_fsr"), QStringLiteral("oot_anti_aliasing")},
             tr("Title-specific settings")},
        },
        {QStringLiteral("mkw_30fps"), QStringLiteral("mkw_60fps"),
         QStringLiteral("hyrule_30fps")});
    settings_page->SetAdvancedSections(
        system_category,
        {
            {tr("Console"),
             tr("What this game sees"),
             0,
             {QStringLiteral("use_docked_mode"), QStringLiteral("language_index"),
              QStringLiteral("region_index"), QStringLiteral("time_zone_index"),
              QStringLiteral("custom_rtc"), QStringLiteral("rng_seed")}},
            {tr("Core and clocks"),
             {},
             0,
             {QStringLiteral("use_multi_core"), QStringLiteral("memory_layout_mode"),
              QStringLiteral("cpu_clock"), QStringLiteral("gpu_clock")}},
            {tr("Emulation speed"),
             {},
             1,
             {QStringLiteral("use_speed_limit"), QStringLiteral(">speed_limit"),
              QStringLiteral("slow_speed_limit"), QStringLiteral("turbo_speed_limit"),
              QStringLiteral("sync_core_speed")}},
        });
    settings_page->SetAdvancedSections(
        cpu, {
                 {tr("CPU"),
                  {},
                  0,
                  {QStringLiteral("cpu_backend|%1|%2")
                       .arg(tr("Backend"), tr("Hypervisor requires multicore.")),
                   QStringLiteral("cpu_accuracy|%1|%2")
                       .arg(tr("Accuracy"), tr("Unsafe exposes the Dynarmic options.")),
                   QStringLiteral("cpu_ticks|%1|%2").arg(tr("Custom CPU ticks"), tr("77–65535"))}},
                 {tr("Unsafe CPU optimization settings"),
                  tr("Dynarmic + Unsafe only"),
                  1,
                  {QStringLiteral("cpuopt_unsafe_host_mmu"),
                   QStringLiteral("cpuopt_unsafe_unfuse_fma|%1").arg(tr("Unfuse FMA")),
                   QStringLiteral("cpuopt_unsafe_reduce_fp_error"),
                   QStringLiteral("cpuopt_unsafe_ignore_standard_fpcr|%1")
                       .arg(tr("Faster ASIMD instructions (32-bit)")),
                   QStringLiteral("cpuopt_unsafe_inaccurate_nan"),
                   QStringLiteral("cpuopt_unsafe_fastmem_check"),
                   QStringLiteral("cpuopt_unsafe_ignore_global_monitor")}},
             });
    settings_page->SetAdvancedSections(
        audio, {{tr("Audio"),
                 tr("The output engine determines the device list"),
                 0,
                 {QStringLiteral("sink_id"), QStringLiteral("audio_output_device_id"),
                  QStringLiteral("sound_index"), QStringLiteral("volume")}}});

    QHash<u32, ConfigurationShared::Widget*> editors;
    for (auto* editor : findChildren<ConfigurationShared::Widget*>()) {
        if (editor->Valid() && editor->restore_button != nullptr) {
            editors.insert(editor->SettingId(), editor);
        }
    }
    const auto update_count = [this, editors] {
        int count = 0;
        for (const auto& entry : Settings::values.linkage.by_category) {
            for (const auto* setting : entry.second) {
                const auto* editor = editors.value(setting->Id());
                if (editor ? editor->property("custom").toBool()
                           : setting->Switchable() && !setting->UsingGlobal()) {
                    ++count;
                }
            }
        }
        game_banner_summary->setText(
            count == 0 ? tr("Uses your global settings.")
            : count == 1
                ? tr("Uses your global settings, except 1 change. Presets never overwrite these.")
                : tr("Uses your global settings, except %n changes. Presets never overwrite these.",
                     nullptr, count));
    };
    for (auto* editor : editors) {
        SettingsPage::WatchOverride(editor, game_banner_summary, update_count);
    }
    update_count();
    settings_page->SetCategory(graphics);
}

void ConfigurePerGame::BuildNativeModes(int category) {
    using Spotlight::SettingsPage;
    struct Mode {
        const Settings::BasicSetting* setting;
        QString name;
    };
    QString game;
    QList<Mode> modes;
    if (title_id == 0x0400C3F00006E000ULL) {
        game = tr("Mario Kart World");
        modes = {{&Settings::values.mkw_30fps, tr("30 FPS mode")},
                 {&Settings::values.mkw_60fps, tr("60 FPS mode")}};
    } else if (title_id == 0x04003F40232DE000ULL) {
        game = tr("Hyrule Warriors: Age of Imprisonment");
        modes = {{&Settings::values.hyrule_30fps, tr("30 FPS mode")}};
    } else {
        return;
    }
    auto* block = new QWidget;
    block->setProperty("spotlightTransparent", true);
    auto* columns = new QHBoxLayout(block);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(16);
    auto* column = new QVBoxLayout;
    column->setSpacing(10);
    auto* heading = new QHBoxLayout;
    heading->setContentsMargins(6, 0, 6, 0);
    heading->setSpacing(10);
    heading->addWidget(SettingsPage::Text(game, 15, 600), 0, Qt::AlignBaseline);
    auto* hint = SettingsPage::Text(tr("Native game modes"), 12, 400);
    Spotlight::SetTextColor(hint, Spotlight::Color::TextMuted());
    heading->addWidget(hint, 1, Qt::AlignBaseline);
    column->addLayout(heading);
    auto* panel =
        new RoundedBox(Spotlight::Color::FillFaint(), Spotlight::Color::StrokeSubtle(), 18);
    panel->setProperty("compactRows", true);
    auto* rows = new QVBoxLayout(panel);
    rows->setContentsMargins(1, 1, 1, 1);
    rows->setSpacing(0);
    column->addWidget(panel);
    columns->addLayout(column, 1);
    columns->addStretch(1);
    QStringList names{game};
    for (const auto& mode : modes) {
        auto* source = SettingsPage::Editor(this, *mode.setting);
        if (auto* control = source != nullptr ? SettingsPage::BindSetting(source) : nullptr) {
            SettingsPage::AddRow(rows, mode.name, {}, {}, {}, control);
            names.append(mode.name);
        }
    }
    if (names.size() == 1) {
        delete block;
        return;
    }
    settings_page->AddBlock(category, block, names.join(QLatin1Char('\n')));
    settings_page->AddBlock(
        category,
        SettingsPage::Note(tr("Native game modes are separate from the guest cap, VSync and "
                              "generated display frames.")),
        {});
}

void ConfigurePerGame::BuildInfoPage(int category) {
    using Spotlight::SettingsPage;
    namespace Color = Spotlight::Color;
    auto* page = new QWidget;
    page->setProperty("spotlightTransparent", true);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    info_image = new QLabel;
    info_image->setFixedSize(132, 132);
    layout->addWidget(info_image, 0, Qt::AlignTop);
    auto* card = new RoundedBox(Color::FillFaint(), Color::StrokeSubtle(), 18);
    auto* rows = new QVBoxLayout(card);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    const QString type = ui->display_format->text();
    const bool archive = is_nup_file || icon_in_file;
    const QString format = is_nup_file ? tr("NUP archive")
                           : archive   ? tr("NX archive")
                           : type.isEmpty() ? QString{}
                                            : tr("%1 file").arg(type);
    const QString name = original_title.empty() ? ui->display_name->text()
                                                : QString::fromStdString(original_title);
    const std::initializer_list<std::pair<QString, QString>> entries{
        {tr("Name"), name},
        {tr("Title ID"), title_id == 0 ? QString{} : ui->display_title_id->text()},
        {tr("Filename"), ui->display_filename->text()},
        {tr("Format"), format},
        {tr("Version"), ui->display_version->text()},
        {tr("Size"), ui->display_size->text()},
        {tr("Developer"), ui->display_developer->text()},
    };
    for (const auto& [label, value] : entries) {
        if (value.isEmpty()) {
            continue;
        }
        if (rows->count() > 0) {
            auto* divider = new QWidget;
            divider->setProperty("spotlightTransparent", true);
            auto* divider_layout = new QHBoxLayout(divider);
            divider_layout->setContentsMargins(18, 0, 18, 0);
            auto* line = new RoundedBox(QColor(0xff, 0xff, 0xff, 0x12), Qt::transparent, 0);
            line->setFixedHeight(1);
            divider_layout->addWidget(line);
            rows->addWidget(divider);
        }
        auto* row = new QWidget;
        row->setProperty("spotlightTransparent", true);
        row->setMinimumHeight(46);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(18, 0, 18, 0);
        row_layout->setSpacing(12);
        auto* key = SettingsPage::Text(label, 13, 500, true);
        key->setFixedWidth(130);
        row_layout->addWidget(key);
        auto* text = SettingsPage::Text(value, 14, 500);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row_layout->addWidget(text, 1);
        if (label == tr("Format") && !type.isEmpty()) {
            row_layout->addWidget(archive ? SmallBadge(type, QColor(0xc9, 0xb3, 0xff, 0x2e),
                                                       QColor(0xc9, 0xb3, 0xff))
                                          : SmallBadge(type, Color::Control(), Color::Text()));
        }
        rows->addWidget(row);
    }
    layout->addWidget(card, 1, Qt::AlignTop);
    settings_page->AddBlock(category, page, tr("Info and file"), true);
    UpdateIconPreview(shown_icon);
}

void ConfigurePerGame::BuildArtworkPage(int category) {
    using Spotlight::SettingsPage;
    namespace Color = Spotlight::Color;
    auto* page = new QWidget;
    page->setProperty("spotlightTransparent", true);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);
    auto* top = new QHBoxLayout;
    top->setSpacing(18);
    artwork_image = new QLabel;
    artwork_image->setFixedSize(180, 180);
    top->addWidget(artwork_image, 0, Qt::AlignTop);
    auto* side = new QVBoxLayout;
    side->setSpacing(18);
    auto* name_block = new QVBoxLayout;
    name_block->setSpacing(8);
    name_block->addWidget(SettingsPage::Text(tr("Name"), 13, 600, true));
    auto* name_row = new QHBoxLayout;
    name_row->setSpacing(8);
    ui->display_name->setFixedHeight(40);
    ui->display_name->setFont(Spotlight::Font(14, 500));
    name_row->addWidget(ui->display_name, 1);
    auto* restore_name = new Spotlight::CircleButton(QStringLiteral("arrow-counter-clockwise"), 38);
    restore_name->setToolTip(tr("Use the name from the game file"));
    restore_name->setAccessibleName(restore_name->toolTip());
    restore_name->setFocusPolicy(Qt::StrongFocus);
    restore_name->setEnabled(title_id != 0);
    connect(restore_name, &QAbstractButton::clicked, ui->display_name, &QLineEdit::clear);
    name_row->addWidget(restore_name);
    name_block->addLayout(name_row);
    auto* name_hint =
        SettingsPage::Text(tr("Clear the custom name to use the name from the game file."), 12, 400);
    Spotlight::SetTextColor(name_hint, Color::TextMuted());
    name_hint->setWordWrap(true);
    name_block->addWidget(name_hint);
    side->addLayout(name_block);

    const QColor fill = Color::Fill();
    const auto mirror = [](QAbstractButton* source, const QString& icon, const QColor& tint_fill,
                           const QColor& ink) {
        auto* pill = new ActionPill(source->text(), icon, tint_fill, ink);
        new MirrorButton(source, pill);
        return pill;
    };
    auto* icon_controls = new QWidget;
    icon_controls->setProperty("spotlightTransparent", true);
    icon_controls->setVisible(title_id != 0);
    auto* icon_layout = new QVBoxLayout(icon_controls);
    icon_layout->setContentsMargins(0, 0, 0, 0);
    icon_layout->setSpacing(18);
    auto* detect_row = new QHBoxLayout;
    detect_row->setSpacing(12);
    detect_row->addWidget(
        mirror(ui->button_detect_icon, QStringLiteral("magnifying-glass"), fill, Color::Text()));
    ui->allow_online_detection->setFont(Spotlight::Font(14, 500));
    detect_row->addWidget(ui->allow_online_detection);
    detect_row->addStretch();
    icon_layout->addLayout(detect_row);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    actions->addWidget(
        mirror(ui->button_upload_icon, QStringLiteral("image"), fill, Color::Text()));
    actions->addWidget(
        mirror(ui->button_browse_iidb, QStringLiteral("magnifying-glass"), fill, Color::Text()));
    actions->addWidget(mirror(ui->button_remove_icon, QStringLiteral("trash"),
                              QColor(0xff, 0x8f, 0xae, 0x1f), Color::AccentLight()));
    actions->addStretch();
    icon_layout->addLayout(actions);
    if (auto* edit_in_file = findChild<QWidget*>(QStringLiteral("editInFile"))) {
        icon_layout->addWidget(edit_in_file, 0, Qt::AlignLeft);
    }
    side->addWidget(icon_controls);
    top->addLayout(side, 1);
    layout->addLayout(top);

    auto* detecting = new QLabel(
        tr("Lichii is looking for an icon. Wait for detection before closing Game Settings."));
    artwork_detecting = NoteStrip(detecting, NoteKind::Info);
    artwork_detecting->setVisible(detect_watcher.isRunning());
    layout->addWidget(artwork_detecting);
    artwork_problem = new QWidget;
    artwork_problem->setProperty("spotlightTransparent", true);
    auto* problem_layout = new QVBoxLayout(artwork_problem);
    problem_layout->setContentsMargins(0, 0, 0, 0);
    problem_layout->setSpacing(0);
    artwork_problem_text = new QLabel(artwork_problem);
    artwork_problem_text->hide();
    artwork_problem->hide();
    layout->addWidget(artwork_problem);
    layout->addWidget(NoteStrip(icon_destination, NoteKind::Info));
    layout->addStretch(1);
    settings_page->AddBlock(category, page, tr("Artwork"), true);
    UpdateIconPreview(shown_icon);
}

void ConfigurePerGame::ShowArtworkProblem(const QString& text, bool error) {
    if (artwork_problem == nullptr) {
        QMessageBox::warning(this, tr("Game Artwork"), text);
        return;
    }
    auto* layout = artwork_problem->layout();
    while (auto* item = layout->takeAt(0)) {
        if (auto* widget = item->widget(); widget != nullptr && widget != artwork_problem_text) {
            widget->deleteLater();
        }
        delete item;
    }
    artwork_problem_text->setText(text);
    layout->addWidget(NoteStrip(artwork_problem_text, error ? NoteKind::Error : NoteKind::Warning));
    artwork_problem_text->show();
    artwork_problem->show();
    settings_page->SetCategory(artwork_category);
}

void ConfigurePerGame::AddPresetRow(QVBoxLayout* section) {
    using Spotlight::SettingsPage;
    const auto& values = Settings::values;
    auto* gfx = graphics_tab.get();
    const std::array editors{
        SettingsPage::Editor(gfx, values.resolution_setup),
        SettingsPage::Editor(gfx, values.scaling_filter),
        SettingsPage::Editor(gfx, values.anti_aliasing),
        SettingsPage::Editor(gfx, values.frame_gen),
    };
    if (std::ranges::any_of(editors, [](const auto* editor) {
            return editor == nullptr || editor->restore_button == nullptr;
        }) ||
        !editors[0]->combobox || !editors[1]->combobox || !editors[2]->combobox ||
        !editors[3]->checkbox) {
        return;
    }
    auto* resolution = editors[0]->combobox;
    auto* filter = editors[1]->combobox;
    auto* aa = editors[2]->combobox;
    auto* frame_generation = editors[3]->checkbox;
    const auto suggested_tier = [this] {
        const int index = Settings::values.vulkan_device.GetValue();
        if (index < 0 || std::cmp_greater_equal(index, devices.size())) {
            return 0;
        }
        return Spotlight::SuggestedPresetTier(QString::fromStdString(devices[index].name),
                                              devices[index].fsr4_capable);
    };

    auto* choices = new QComboBox(this);
    choices->hide();
    choices->addItems({{}, tr("Auto"), tr("Low"), tr("Mid"), tr("High")});
    auto* summary = SettingsPage::AddRow(section, tr("Preset"),
                                         tr("Global clears resolution, scaling filter, "
                                            "anti-aliasing and frame generation overrides."),
                                         QStringLiteral("sliders-horizontal"),
                                         Spotlight::Color::Accent(),
                                         SettingsPage::BindCombo(choices, tr("Custom")));
    const auto sync = [editors, resolution, filter, aa, frame_generation, suggested_tier, choices] {
        const auto& global_values = Settings::values;
        const int global = Spotlight::PerformancePresetIndex({
            global_values.resolution_setup.GetValue(true),
            global_values.scaling_filter.GetValue(true),
            global_values.anti_aliasing.GetValue(true),
            global_values.frame_gen.GetValue(true),
        });
        const QStringList tiers{tr("Low"), tr("Mid"), tr("High")};
        choices->setItemText(0, tr("Global (%1)").arg(global == -1 ? tr("Custom") : tiers[global]));
        if (std::ranges::none_of(editors, [](const auto* editor) {
                return editor->property("custom").toBool();
            })) {
            choices->setCurrentIndex(0);
            return;
        }
        const int tier = Spotlight::PerformancePresetIndex({
            static_cast<Settings::ResolutionSetup>(resolution->currentData().toUInt()),
            static_cast<Settings::ScalingFilter>(filter->currentData().toUInt()),
            static_cast<Settings::AntiAliasing>(aa->currentData().toUInt()),
            frame_generation->isChecked(),
        });
        if (tier == -1) {
            choices->setCurrentIndex(-1);
            return;
        }
        choices->setCurrentIndex(tier == suggested_tier() ? 1 : tier + 2);
    };
    connect(choices, &QComboBox::activated, this,
            [editors, resolution, filter, aa, frame_generation, suggested_tier](int selection) {
                if (selection == 0) {
                    for (auto* editor : editors) {
                        if (editor->property("custom").toBool()) {
                            editor->restore_button->click();
                        }
                    }
                    return;
                }
                const auto& preset =
                    Spotlight::PerformancePresets[selection == 1 ? suggested_tier()
                                                                 : selection - 2];
                SettingsPage::SetComboIndex(
                    resolution, resolution->findData(static_cast<u32>(preset.resolution)));
                SettingsPage::SetComboIndex(filter,
                                            filter->findData(static_cast<u32>(preset.filter)));
                SettingsPage::SetComboIndex(aa,
                                            aa->findData(static_cast<u32>(preset.anti_aliasing)));
                if (frame_generation->isChecked() == preset.frame_generation) {
                    frame_generation->click();
                }
                frame_generation->click();
            });
    for (auto* editor : editors) {
        SettingsPage::WatchOverride(editor, summary, sync);
    }
    for (auto* combo : {resolution, filter, aa}) {
        connect(combo, &QComboBox::currentIndexChanged, summary, sync);
    }
    connect(frame_generation, &QCheckBox::toggled, summary, sync);
    sync();
}

void ConfigurePerGame::BuildAddons(int mods, int updates) {
    using Spotlight::SettingsPage;
    namespace Color = Spotlight::Color;
    if (store_rows != nullptr) {
        return;
    }
    update_summary = new QLabel;
    auto* version_pill = new RoundedBox(Color::Control(), Qt::transparent, 11);
    version_pill->setFixedHeight(22);
    auto* pill_layout = new QHBoxLayout(version_pill);
    pill_layout->setContentsMargins(9, 0, 9, 0);
    update_summary->setFont(Spotlight::Font(11, 600));
    Spotlight::SetTextColor(update_summary, Color::Text());
    pill_layout->addWidget(update_summary);
    const QString name = original_title.empty() ? ui->display_name->text()
                                                : QString::fromStdString(original_title);
    SettingsPage::AddRow(settings_page->AddSection(updates, tr("Game version")), name,
                         tr("Currently detected version"), QStringLiteral("info"),
                         Spotlight::SettingsColor::Lavender(), version_pill);
    content_rows = settings_page->AddSection(updates, tr("Installed"));
    auto* updates_note = new QLabel(tr("If there are no updates or DLC installed, use the game's "
                                       "Library menu to install content."));
    settings_page->AddBlock(updates, NoteStrip(updates_note, NoteKind::Info), updates_note->text());

    auto* actions = new QWidget;
    actions->setProperty("spotlightTransparent", true);
    auto* actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(10);
    auto* add_folder =
        new ActionPill(tr("Add folder"), QStringLiteral("folder-open"), Color::Fill(), Color::Text());
    connect(add_folder, &QAbstractButton::clicked, addons_tab.get(),
            &ConfigurePerGameAddons::InstallModFolder);
    actions_layout->addWidget(add_folder);
    auto* add_zip =
        new ActionPill(tr("Add ZIP"), QStringLiteral("archive"), Color::Fill(), Color::Text());
    connect(add_zip, &QAbstractButton::clicked, addons_tab.get(),
            &ConfigurePerGameAddons::InstallModZip);
    actions_layout->addWidget(add_zip);
    actions_layout->addStretch();
    auto* browse = new ActionPill(tr("Browse the Mod Store"), QStringLiteral("storefront"),
                                  Color::Control(), Color::Text(), Color::Stroke());
    connect(browse, &QAbstractButton::clicked, this, &ConfigurePerGame::BrowseModsRequested);
    actions_layout->addWidget(browse);
    settings_page->AddBlock(mods, actions, tr("Browse the Mod Store"));

    const auto add_group = [this, mods](QLabel*& header) {
        auto* group = new QWidget;
        group->setProperty("spotlightTransparent", true);
        auto* layout = new QVBoxLayout(group);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        header = SettingsPage::Text({}, 15, 600);
        header->setContentsMargins(6, 0, 6, 0);
        layout->addWidget(header);
        auto* card = new RoundedBox(Color::FillFaint(), Color::StrokeSubtle(), 18);
        auto* rows = new QVBoxLayout(card);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(0);
        layout->addWidget(card);
        settings_page->AddBlock(mods, group, {});
        return rows;
    };
    store_rows = add_group(store_header);
    own_rows = add_group(own_header);
    settings_page->AddBlock(mods,
                            SettingsPage::Note(tr("Removing a mod asks for confirmation. Installed updates "
                                        "and DLC are managed separately.")),
                            {});
    UpdateSpotlightIdentity();
    RebuildAddons();
}

void ConfigurePerGame::RebuildAddons() {
    using Spotlight::SettingsPage;
    namespace Color = Spotlight::Color;
    namespace Tints = Spotlight::SettingsColor;
    for (auto* rows : {store_rows, own_rows, content_rows}) {
        while (auto* item = rows->takeAt(0)) {
            item->widget()->hide();
            item->widget()->deleteLater();
            delete item;
        }
    }

    QHash<QString, ModStore::InstalledMod> store;
    const QString load_dir = QDir(QString::fromStdString(Common::FS::GetEdenPathString(
                                      Common::FS::EdenPath::LoadDir)))
                                 .absolutePath();
    for (auto& mod : ModStore::ReadInstalledMods(
             load_dir, QString::fromStdString(fmt::format("{:016X}", title_id)))) {
        store.insert(mod.id, std::move(mod));
    }

    const auto add_row = [](QVBoxLayout* rows, QWidget* leading, const QString& title,
                            const QString& detail, QWidget* trailing) {
        if (rows->count() > 0) {
            auto* divider = new QWidget;
            divider->setProperty("spotlightTransparent", true);
            auto* divider_layout = new QHBoxLayout(divider);
            divider_layout->setContentsMargins(18, 0, 18, 0);
            auto* line = new RoundedBox(QColor(0xff, 0xff, 0xff, 0x12), Qt::transparent, 0);
            line->setFixedHeight(1);
            divider_layout->addWidget(line);
            rows->addWidget(divider);
        }
        auto* row = new QWidget;
        row->setProperty("spotlightTransparent", true);
        row->setProperty("searchText", QString(title + QLatin1Char('\n') + detail));
        row->setMinimumHeight(56);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(14, 8, 16, 8);
        layout->setSpacing(12);
        if (leading != nullptr) {
            layout->addWidget(leading, 0, Qt::AlignVCenter);
        }
        auto* copy = new QVBoxLayout;
        copy->setSpacing(2);
        copy->addWidget(SettingsPage::Text(title, 14, 600));
        if (!detail.isEmpty()) {
            copy->addWidget(SettingsPage::Text(detail, 12, 400, true));
        }
        layout->addLayout(copy, 1);
        layout->addWidget(trailing, 0, Qt::AlignVCenter);
        rows->addWidget(row);
    };
    const auto add_empty = [](QVBoxLayout* rows, const QString& title, const QString& text) {
        auto* empty = new QWidget;
        empty->setProperty("spotlightTransparent", true);
        auto* layout = new QVBoxLayout(empty);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);
        auto* heading = new QLabel(title);
        heading->setFont(Spotlight::DisplayFont(18));
        Spotlight::SetTextColor(heading, Color::Text());
        layout->addWidget(heading);
        layout->addWidget(NoteStrip(new QLabel(text), NoteKind::Info));
        rows->addWidget(empty);
    };

    auto* model = addons_tab->Model();
    int store_count = 0;
    int own_count = 0;
    for (int row = 0; row < model->rowCount(); ++row) {
        QStandardItem* const item = model->item(row, 0);
        const QString version = model->item(row, 1) ? model->item(row, 1)->text() : QString{};
        const QString location =
            item->data(ConfigurePerGameAddons::PATCH_LOCATION).toString();
        const auto type =
            static_cast<FileSys::PatchType>(item->data(ConfigurePerGameAddons::PATCH_TYPE).toInt());

        Spotlight::ToggleSwitch* toggle = nullptr;
        if (item->isCheckable()) {
            toggle = new Spotlight::ToggleSwitch;
            toggle->setChecked(item->checkState() == Qt::Checked);
            toggle->setAccessibleName(item->text());
            connect(toggle, &QAbstractButton::toggled, this, [item](bool on) {
                item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            });
            connect(model, &QStandardItemModel::itemChanged, toggle,
                    [item, toggle](QStandardItem* changed) {
                        if (changed == item) {
                            const QSignalBlocker blocker(toggle);
                            toggle->setChecked(item->checkState() == Qt::Checked);
                        }
                    });
        }

        if (type != FileSys::PatchType::Mod) {
            const bool embedded = !item->isCheckable() && type == FileSys::PatchType::Update;
            const QString title = embedded ? tr("Embedded Update (NUP)") : item->text();
            const QString detail =
                embedded ? tr("An update inside the game file cannot be disabled.")
                : type == FileSys::PatchType::DLC
                    ? tr("Installed")
                    : (version.isEmpty() ? item->toolTip() : tr("v%1").arg(version));
            QWidget* control = toggle;
            if (embedded) {
                control = SmallBadge(tr("Embedded"), Color::Control(), Color::Text());
            }
            SettingsPage::AddRow(content_rows, title, detail,
                                 embedded ? QStringLiteral("archive")
                                 : type == FileSys::PatchType::DLC
                                     ? QStringLiteral("puzzle-piece")
                                     : QStringLiteral("arrow-circle-up"),
                                 Tints::Lavender(), control, item->toolTip());
            continue;
        }

        auto* trailing = new QWidget;
        trailing->setProperty("spotlightTransparent", true);
        auto* trailing_layout = new QHBoxLayout(trailing);
        trailing_layout->setContentsMargins(0, 0, 0, 0);
        trailing_layout->setSpacing(10);
        auto* leading = new QWidget;
        leading->setProperty("spotlightTransparent", true);
        auto* leading_layout = new QHBoxLayout(leading);
        leading_layout->setContentsMargins(0, 0, 0, 0);
        leading_layout->setSpacing(12);
        if (toggle != nullptr) {
            leading_layout->addWidget(toggle);
        }
        const auto found = store.constFind(item->text());
        const bool from_store = found != store.cend();
        if (from_store) {
            if (found->verified == false) {
                trailing_layout->addWidget(SmallBadge(tr("Unverified"),
                                                      QColor(0xff, 0xd3, 0x7a, 0x2e),
                                                      QColor(0xff, 0xd3, 0x7a)));
            }
            if (found->update_available == true) {
                auto* update =
                    new ActionPill(tr("Update"), {}, Color::Control(), Color::Text(), {}, 30);
                connect(update, &QAbstractButton::clicked, this,
                        &ConfigurePerGame::BrowseModsRequested);
                trailing_layout->addWidget(update);
            }
            auto* tile = new RoundedBox(Color::Fill(), Qt::transparent, 10);
            tile->setFixedSize(32, 32);
            auto* tile_layout = new QHBoxLayout(tile);
            tile_layout->setContentsMargins(0, 0, 0, 0);
            auto* glyph = new QLabel;
            glyph->setPixmap(Spotlight::IconPixmap(QStringLiteral("puzzle-piece"), 16,
                                                   Color::Text(), tile->devicePixelRatioF()));
            tile_layout->addWidget(glyph, 0, Qt::AlignCenter);
            leading_layout->addWidget(tile);
        } else if (version.contains(QStringLiteral("Cheats"), Qt::CaseInsensitive)) {
            auto* glyph = new QLabel;
            glyph->setPixmap(Spotlight::IconPixmap(QStringLiteral("key"), 16,
                                                   Color::TextSecondary(),
                                                   leading->devicePixelRatioF()));
            leading_layout->addWidget(glyph);
        }
        if (!location.isEmpty()) {
            auto* more = new Spotlight::CircleButton(QStringLiteral("dots-three"), 32);
            more->setAccessibleName(tr("More for %1").arg(item->text()));
            more->setFocusPolicy(Qt::StrongFocus);
            auto* menu = new Spotlight::Menu(more);
            menu->addAction(tr("Local mod actions"))->setEnabled(false);
            menu->addAction(Spotlight::Icon(u"folder-open", Color::MenuInk(), 16), tr("Open folder"),
                            this, [location] {
                                QDesktopServices::openUrl(QUrl::fromLocalFile(location));
                            });
            auto* remove = menu->addAction(Spotlight::Icon(u"trash", Color::AccentLight(), 16),
                                           tr("Remove"), this, [this, item] {
                                               addons_tab->AddonDeleteRequested({item->index()});
                                           });
            remove->setProperty("spotlightDestructive", true);
            connect(more, &QAbstractButton::clicked, menu,
                    [menu, more] {
                        menu->popup(more->mapToGlobal(
                            QPoint(more->width() - menu->sizeHint().width(), more->height() + 6)));
                    });
            trailing_layout->addWidget(more);
        }

        if (from_store) {
            ++store_count;
            QStringList facts{tr("Mod Store")};
            if (!found->version.isEmpty()) {
                facts << tr("v%1").arg(found->version);
            }
            if (!found->author.isEmpty()) {
                facts << tr("by %1").arg(found->author);
            }
            add_row(store_rows, leading, found->name, facts.join(QStringLiteral(" · ")), trailing);
            continue;
        }
        ++own_count;
        const QString path =
            location.isEmpty() ? QString{} : QDir(load_dir).relativeFilePath(location);
        add_row(own_rows, leading, item->text(),
                version.isEmpty() || path.isEmpty() ? version + path
                                                    : tr("%1 · %2").arg(version, path),
                trailing);
    }

    store_header->setText(tr("From the Mod Store · %1").arg(store_count));
    auto* store_group = store_header->parentWidget();
    store_group->setProperty("sourceVisible", store_count > 0);
    store_group->setVisible(store_count > 0);
    own_header->setText(tr("Your own files · %1").arg(own_count));
    if (own_count == 0) {
        add_empty(own_rows, tr("No mods yet"), tr("Add a folder or ZIP, or browse the Mod Store."));
    }
    if (content_rows->isEmpty()) {
        add_empty(content_rows, tr("No updates or DLC"),
                  tr("Install them using the game's Library menu."));
    }
    UpdateModsSummary();
}

void ConfigurePerGame::UpdateModsSummary() {
    auto* model = addons_tab->Model();
    int mod_count = 0;
    int enabled_count = 0;
    for (int row = 0; row < model->rowCount(); ++row) {
        const QStandardItem* item = model->item(row, 0);
        if (item->data(ConfigurePerGameAddons::PATCH_TYPE).toInt() ==
            static_cast<int>(FileSys::PatchType::Mod)) {
            ++mod_count;
            enabled_count += item->checkState() == Qt::Checked;
        }
    }
    for (auto* summary : mods_summaries) {
        summary->setText(mod_count == 0 ? tr("No mods installed")
                                        : tr("%1 of %2 on").arg(enabled_count).arg(mod_count));
    }
}

void ConfigurePerGame::UpdateSpotlightIdentity() {
    const QString name = ui->display_name->text().simplified();
    const auto fallback = QString::fromStdString(Common::FS::PathToUTF8String(file_path.stem()));
    const QString title = name.isEmpty() ? ui->display_name->placeholderText().isEmpty()
                                               ? fallback
                                               : ui->display_name->placeholderText()
                                         : name;
    game_banner_title->setText(title);
    settings_page->SetGame(title, shown_icon);
    const QString version = ui->display_version->text();
    const QString id = title_id == 0 ? QString{} : ui->display_title_id->text();
    const QString shown_version = version.isEmpty() ? QString{} : tr("v%1").arg(version);
    game_banner_version->setText(id.isEmpty() || shown_version.isEmpty()
                                     ? shown_version + id
                                     : tr("%1 · %2").arg(shown_version, id));
    if (update_summary != nullptr) {
        update_summary->setText(version.isEmpty() ? tr("Unknown") : tr("v%1").arg(version));
    }
}

ConfigurePerGame::~ConfigurePerGame() {
    if (detect_cancel) {
        detect_cancel->store(true);
    }
}

void ConfigurePerGame::done(int result) {
    if (result == QDialog::Accepted && (detect_watcher.isRunning() || !SaveCustomMetadata(true))) {
        return;
    }
    if (detect_cancel) {
        detect_cancel->store(true);
    }
    QDialog::done(result);
}

void ConfigurePerGame::reject() {
    accept();
}

void ConfigurePerGame::ApplyConfiguration() {
    for (const auto tab : *tab_group) {
        tab->ApplyConfiguration();
    }
    addons_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();

    if (Settings::IsDockedMode() && Settings::values.players.GetValue()[0].controller_type ==
                                        Settings::ControllerType::Handheld) {
        Settings::values.use_docked_mode.SetValue(Settings::ConsoleMode::Handheld);
        Settings::values.use_docked_mode.SetGlobal(true);
    }

    system.ApplySettings();
    Settings::LogSettings();

    game_config->SaveAllValues();
}
void ConfigurePerGame::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QDialog::changeEvent(event);
}

void ConfigurePerGame::RetranslateUI() {
    ui->retranslateUi(this);
    setWindowTitle(tr("Game Settings"));
}

void ConfigurePerGame::LoadFromFile(FileSys::VirtualFile file_) {
    file = std::move(file_);
    LoadConfiguration();
}

void ConfigurePerGame::ReloadAddonsAfterModStore(const QSet<QString>& store_toggled) {
    addons_tab->ReloadAfterModStore(store_toggled);
}

void ConfigurePerGame::LoadConfiguration() {
    if (file == nullptr) {
        return;
    }

    addons_tab->LoadFromFile(file);

    ui->display_title_id->setText(
        QStringLiteral("%1").arg(title_id, 16, 16, QLatin1Char{'0'}).toUpper());

    const FileSys::PatchManager pm{title_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto control = pm.GetControlMetadata();
    const auto loader = Loader::GetLoader(system, file, title_id);

    is_nup_file = loader != nullptr && loader->GetFileType() == Loader::FileType::NUP;
#ifdef LICHII_NX_TOOLS
    const bool is_nx_file = loader != nullptr && loader->GetFileType() == Loader::FileType::NX;
#else
    const bool is_nx_file = false;
#endif
    icon_in_file = is_nup_file || is_nx_file;
    icon_destination->setText(
        is_nup_file  ? tr("Icons you pick are written into the NUP file when you close Game "
                          "Settings, so they show on every PC.")
        : is_nx_file ? tr("Icons you pick are written into the NX archive when you close Game "
                          "Settings, so they show on every PC.")
                     : tr("Icons you pick are saved for this PC only: %1 files are never "
                          "changed.")
                           .arg(loader != nullptr ? QString::fromStdString(Loader::GetFileTypeString(
                                                        loader->GetFileType()))
                                                  : tr("game")));
    if (auto* edit_in_file = findChild<QWidget*>(QStringLiteral("editInFile"))) {
        edit_in_file->setVisible(icon_in_file && title_id != 0);
    }
    // Custom artwork is stored per program ID, outside the game file, so it works for every
    // format and while a game runs.
    ui->nup_icon_widget->setVisible(title_id != 0);
    ui->nup_icon_widget->setEnabled(true);
    // The name is stored per program ID too: a rename never touches the game file.
    ui->display_name->setReadOnly(title_id == 0);
    ui->display_name->setToolTip(
        title_id != 0 ? tr("Name shown in the library. Clear it to use the game's own name.")
                      : QString{});

    if (!is_nup_file && control.first != nullptr) {
        ui->display_version->setText(QString::fromStdString(control.first->GetVersionString()));
        ui->display_name->setText(QString::fromStdString(control.first->GetApplicationName()));
        ui->display_developer->setText(QString::fromStdString(control.first->GetDeveloperName()));
    } else if (loader != nullptr) {
        std::string title;
        if (loader->ReadTitle(title) == Loader::ResultStatus::Success) {
            ui->display_name->setText(QString::fromStdString(title));
        } else if (is_nup_file) {
            ui->display_name->setText(
                QFileInfo(QString::fromStdString(file->GetName())).completeBaseName());
        }

        std::string developer;
        if (loader->ReadDeveloper(developer) == Loader::ResultStatus::Success) {
            ui->display_developer->setText(QString::fromStdString(developer));
        }
        std::string version;
        if (loader->ReadDisplayVersion(version) == Loader::ResultStatus::Success &&
            !version.empty()) {
            ui->display_version->setText(QString::fromStdString(version));
        } else {
            ui->display_version->setText(QStringLiteral("1.0.0"));
        }
    }
    original_title = ui->display_name->text().simplified().toStdString();
    ui->display_name->setPlaceholderText(QString::fromStdString(original_title));
    if (const auto custom = QtCommon::Game::ReadCustomTitle(title_id); !custom.empty()) {
        ui->display_name->setText(QString::fromStdString(custom));
    }
    initial_title = ui->display_name->text().simplified().toStdString();
    pending_custom_icon = {};
    remove_custom_icon = false;

    file_icon = QPixmap{};
    if (!is_nup_file && control.second != nullptr) {
        const auto bytes = control.second->ReadAllBytes();
        file_icon.loadFromData(bytes.data(), static_cast<u32>(bytes.size()));
    } else if (loader != nullptr) {
        std::vector<u8> bytes;
        if (loader->ReadIcon(bytes) == Loader::ResultStatus::Success) {
            file_icon.loadFromData(bytes.data(), static_cast<u32>(bytes.size()));
        }
    }
    QPixmap custom_icon;
    if (const auto custom = QtCommon::Game::ReadCustomArtwork(title_id); !custom.empty()) {
        custom_icon.loadFromData(custom.data(), static_cast<u32>(custom.size()));
    }
    has_custom_icon = !custom_icon.isNull();
    UpdateIconPreview(has_custom_icon ? custom_icon : file_icon);
    UpdateRemoveIconButton();

    ui->display_filename->setText(QString::fromStdString(file->GetName()));

    if (loader != nullptr) {
        ui->display_format->setText(
            QString::fromStdString(Loader::GetFileTypeString(loader->GetFileType())));
    }

    const auto valueText = QtCommon::ReadableByteSize(file->GetSize());
    ui->display_size->setText(valueText);
}

void ConfigurePerGame::UpdateIconPreview(const QPixmap& map) {
    shown_icon = map;
    const qreal dpr = devicePixelRatioF();
    game_banner_icon->setPixmap(Spotlight::SettingsPage::WholeIcon(map, 72, dpr));
    if (artwork_image != nullptr) {
        artwork_image->setPixmap(Spotlight::SettingsPage::WholeIcon(map, 180, dpr));
    }
    if (info_image != nullptr) {
        info_image->setPixmap(Spotlight::SettingsPage::WholeIcon(map, 132, dpr));
    }
    UpdateSpotlightIdentity();
}

void ConfigurePerGame::StageCustomIcon(QImage icon) {
    if (icon.isNull()) {
        return;
    }
    pending_custom_icon = std::move(icon);
    remove_custom_icon = false;
    UpdateIconPreview(QPixmap::fromImage(pending_custom_icon));
    UpdateRemoveIconButton();
}

void ConfigurePerGame::OnBrowseIiDB() {
    if (!file || title_id == 0 || detect_watcher.isRunning()) {
        return;
    }
    auto title = ui->display_name->text().simplified();
    if (title.isEmpty()) {
        title = QString::fromStdString(original_title);
    }
    IiDB::Client client{IiDB::SessionRateLimits()};
    IiDB::ArtworkDialog dialog(this, title, IiDB::SessionKey(), client);
    if (dialog.exec() == QDialog::Accepted) {
        StageCustomIcon(dialog.TakeIcon());
    }
}

void ConfigurePerGame::OnUploadNupIcon() {
    if (title_id == 0 || detect_watcher.isRunning()) {
        return;
    }
    const QString default_dir =
        file_path.has_parent_path()
            ? QString::fromStdString(Common::FS::PathToUTF8String(file_path.parent_path()))
            : QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString selected = QtCommon::Frontend::GetOpenFileName(
        tr("Select Game Icon"), default_dir,
        tr("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
    if (selected.isEmpty()) {
        return;
    }
    QImage img = ReadIconFile(selected);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Invalid Image"),
                             tr("Select a PNG, JPEG, BMP or WebP image up to 8 MiB, "
                                "8192 pixels per side and 16 megapixels."));
        return;
    }
    StageCustomIcon(ArtworkImage::SquareIcon(img));
}

void ConfigurePerGame::OnRemoveCustomIcon() {
    if (title_id == 0 || detect_watcher.isRunning()) {
        return;
    }
    // Drops a picked image that is not saved yet, and the saved artwork when Game Settings closes.
    pending_custom_icon = {};
    remove_custom_icon = has_custom_icon;
    UpdateIconPreview(file_icon);
    UpdateRemoveIconButton();
}

void ConfigurePerGame::UpdateRemoveIconButton() {
    ui->button_remove_icon->setEnabled(!pending_custom_icon.isNull() ||
                                       (has_custom_icon && !remove_custom_icon));
}

void ConfigurePerGame::OnDetectNupIcon() {
    if (title_id == 0 || detect_watcher.isRunning()) {
        return;
    }
    QString query_qstr = ui->display_name->text().trimmed();
    if (query_qstr.isEmpty()) {
        query_qstr = QFileInfo(QString::fromStdString(
            Common::FS::PathToUTF8String(file_path))).completeBaseName();
    }
    const std::string raw_query = query_qstr.toStdString();
    if (raw_query.empty() && title_id == 0) {
        QMessageBox::information(this, tr("Detect Icon"),
                                 tr("Please enter a game name first so the icon can be detected."));
        return;
    }
    if (raw_query.size() > 1024) {
        QMessageBox::information(this, tr("Detect Icon"),
                                 tr("The game name is too long to search."));
        return;
    }

    ui->button_detect_icon->setText(tr("Detecting icon…"));
    ui->button_detect_icon->setEnabled(false);
    ui->button_upload_icon->setEnabled(false);
    ui->button_browse_iidb->setEnabled(false);
    ui->button_remove_icon->setEnabled(false);
    ui->display_name->setEnabled(false);
    ui->allow_online_detection->setEnabled(false);

    if (artwork_detecting != nullptr) {
        artwork_detecting->show();
    }
    if (artwork_problem != nullptr) {
        artwork_problem->hide();
    }
    detect_cancel = std::make_shared<std::atomic_bool>(false);
    const bool allow_online = ui->allow_online_detection->isChecked();
    detect_watcher.setFuture(QtConcurrent::run(
        [raw_query, tid = title_id, nup_path = file_path, allow_online, cancel = detect_cancel] {
            DetectIconResult out;
            out.error = "Icon detection timed out.";
            DetectionBudget budget{cancel};
            try {
                const std::string search_name = ResolveGameNameAlias(raw_query, tid);
                if (search_name != raw_query) {
                    out.resolved_title = search_name;
                }
                const std::string slug = NormalizeGameSlug(search_name);
                const auto try_decode_bytes = [&](const std::string& bytes) {
                    if (budget.Stopped() || bytes.size() > MaxIconBytes) {
                        return false;
                    }
                    out.icon = ArtworkImage::SquareIcon(
                        ArtworkImage::Decode(QByteArray(bytes.data(), static_cast<int>(bytes.size()))));
                    return !out.icon.isNull();
                };

                // 1. Check local image files next to the NUP archive
                if (!nup_path.empty()) {
                    const auto dir = nup_path.parent_path();
                    const auto stem = Common::FS::PathToUTF8String(nup_path.stem());
                    for (const auto& base : {slug, stem, std::string{"icon"}}) {
                        if (base.empty()) {
                            continue;
                        }
                        for (const char* ext : {".png", ".jpg", ".jpeg", ".webp"}) {
                            if (budget.Stopped()) {
                                return out;
                            }
                            const auto candidate = dir / Common::FS::ToU8String(base + ext);
                            out.icon = ArtworkImage::SquareIcon(ReadIconFile(
                                QString::fromStdString(Common::FS::PathToUTF8String(candidate))));
                            if (!out.icon.isNull()) {
                                return out;
                            }
                        }
                    }
                }
                if (!allow_online) {
                    out.error = "No matching local icon was found. Select Allow online lookup to "
                                "search online, or upload an image.";
                    return out;
                }

                // 2. Check Eden official boxart repository
                if (!slug.empty()) {
                    std::vector<std::string> candidate_slugs{slug};
                    if (!slug.starts_with("the-legend-of-")) {
                        candidate_slugs.push_back("the-legend-of-" + slug);
                    }
                    if (!slug.starts_with("super-")) {
                        candidate_slugs.push_back("super-" + slug);
                    }
                    for (const auto& s : candidate_slugs) {
                        if (budget.Stopped()) {
                            break;
                        }
                        const auto url = fmt::format(
                            "https://raw.githubusercontent.com/eden-emulator/boxart/"
                            "refs/heads/master/img/{}.png",
                            s);
                        if (const auto body = HttpGetUrl(url, budget);
                            body && try_decode_bytes(*body)) {
                            return out;
                        }
                    }
                }

                // 3. Query Wikipedia pageimages API (handles Switch & Switch 2 games)
                if (!search_name.empty()) {
                    for (const auto& suffix : {" video game", " Nintendo", ""}) {
                        if (budget.Stopped()) {
                            break;
                        }
                        const std::string api_url = fmt::format(
                            "https://en.wikipedia.org/w/api.php?action=query&format=json"
                            "&generator=search"
                            "&gsrsearch={}&gsrlimit=8&prop=pageimages&piprop=thumbnail|original"
                            "&pithumbsize=512&pilicense=any",
                            UrlEncode(search_name + suffix));
                        const auto json_body = HttpGetUrl(api_url, budget, 256 * 1024);
                        if (!json_body) {
                            continue;
                        }
                        const auto parsed = nlohmann::json::parse(
                            *json_body,
                            [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
                                return depth < 32;
                            },
                            false);
                        if (parsed.is_discarded() || !parsed.is_object() ||
                            !parsed.contains("query") || !parsed["query"].is_object() ||
                            !parsed["query"].contains("pages") ||
                            !parsed["query"]["pages"].is_object()) {
                            continue;
                        }
                        struct WikiCandidate {
                            int score{};
                            int index{};
                            std::string img_url;
                        };
                        std::vector<WikiCandidate> candidates;
                        int pages_left = 8;
                        for (const auto& [_, page] : parsed["query"]["pages"].items()) {
                            if (budget.Stopped() || pages_left-- <= 0) {
                                break;
                            }
                            if (!page.is_object() || !page.contains("title") ||
                                !page["title"].is_string()) {
                                continue;
                            }
                            std::string img_url;
                            for (const auto* image : {"thumbnail", "original"}) {
                                if (page.contains(image) && page[image].is_object() &&
                                    page[image].contains("source") &&
                                    page[image]["source"].is_string()) {
                                    img_url = page[image]["source"].get<std::string>();
                                    break;
                                }
                            }
                            const auto page_title = page["title"].get<std::string>();
                            if (img_url.empty() || img_url.size() > 4096 ||
                                page_title.size() > 1024) {
                                continue;
                            }
                            const int page_index =
                                page.contains("index") && page["index"].is_number_integer()
                                    ? page["index"].get<int>()
                                    : 99;
                            candidates.push_back({ScoreCandidateTitle(page_title, search_name),
                                                  page_index, std::move(img_url)});
                        }
                        std::ranges::sort(candidates, [](const auto& a, const auto& b) {
                            if (a.score != b.score) {
                                return a.score > b.score;
                            }
                            return a.index < b.index;
                        });
                        int images_left = 3;
                        for (const auto& cand : candidates) {
                            if (budget.Stopped() || images_left-- <= 0) {
                                break;
                            }
                            if (const auto body = HttpGetUrl(cand.img_url, budget);
                                body && try_decode_bytes(*body)) {
                                return out;
                            }
                        }
                    }
                }

                // 4. Fallback to Bing square game icon thumbnail search
                if (!budget.Stopped() && !search_name.empty()) {
                    const std::string bing_url = fmt::format(
                        "https://www.bing.com/images/search?q={}&qft=+filterui:aspect-square",
                        UrlEncode(search_name + " nintendo switch game icon cover"));
                    if (const auto html = HttpGetUrl(bing_url, budget, 256 * 1024)) {
                        const QRegularExpression thumb_re(QStringLiteral(
                            R"(https://(?:tse[1-4]\.mm|th)\.bing\.com/th/id/OIP\.[A-Za-z0-9_\-]+)"));
                        auto matches = thumb_re.globalMatch(QString::fromStdString(*html));
                        int images_left = 4;
                        while (!budget.Stopped() && images_left-- > 0 && matches.hasNext()) {
                            const auto thumb_url =
                                matches.next().captured().toStdString() + "?w=256&h=256&c=7";
                            if (const auto body = HttpGetUrl(thumb_url, budget);
                                body && try_decode_bytes(*body)) {
                                return out;
                            }
                        }
                    }
                }

                out.error = budget.Stopped()
                                ? "Icon detection timed out."
                                : fmt::format("Could not find a matching icon for \"{}\".",
                                              search_name);
            } catch (const std::exception&) {
                out.error = "Icon detection failed. Try uploading an image instead.";
            }
            return out;
        }));
}

bool ConfigurePerGame::SaveCustomMetadata(bool show_error) {
    if (remove_custom_icon) {
        if (!QtCommon::Game::RemoveCustomArtwork(title_id)) {
            if (show_error) {
                ShowArtworkProblem(tr("The artwork could not be removed. Game Settings remains "
                                      "open so you can try again."),
                                   true);
            }
            return false;
        }
        remove_custom_icon = false;
        has_custom_icon = false;
        UpdateRemoveIconButton();
        UISettings::values.is_game_list_reload_pending.exchange(true);
        emit CustomArtworkChanged(title_id);
    }
    if (!pending_custom_icon.isNull() && icon_in_file) {
        // Written into the NUP or NX file once this dialog has released it. The local artwork,
        // which would hide it, is removed when the write succeeds.
        archive_icon = EncodeArchiveIcon(pending_custom_icon, pending_custom_icon.rect());
        pending_custom_icon = {};
    } else if (!pending_custom_icon.isNull()) {
        if (!QtCommon::Game::SaveCustomArtwork(title_id, pending_custom_icon)) {
            if (show_error) {
                ShowArtworkProblem(tr("The artwork could not be saved. Game Settings remains "
                                      "open so you can try again."),
                                   true);
            }
            return false;
        }
        pending_custom_icon = {};
        has_custom_icon = true;
        UpdateRemoveIconButton();
        UISettings::values.is_game_list_reload_pending.exchange(true);
        emit CustomArtworkChanged(title_id);
    }
    const std::string new_title = ui->display_name->text().simplified().toStdString();
    if (title_id == 0 || new_title == initial_title) {
        return true;
    }
    // Stored next to the custom artwork; the game file is never rewritten for a rename. The game's
    // own name, or an empty field, removes the custom name.
    const std::string custom_title = new_title == original_title ? std::string{} : new_title;
    if (!QtCommon::Game::SaveCustomTitle(title_id, custom_title)) {
        if (show_error) {
            ShowArtworkProblem(tr("The game name could not be saved. Game Settings remains open "
                                  "so you can try again."),
                               true);
        }
        return false;
    }
    initial_title = new_title;
    title_changed = true;
    const std::string shown = custom_title.empty() ? original_title : custom_title;
    if (shown.empty()) {
        // No name to show in place: let the library read it again.
        UISettings::values.is_game_list_reload_pending.exchange(true);
    } else {
        renamed_title = QString::fromStdString(shown);
    }
    return true;
}
