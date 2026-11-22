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
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "common/fs/fs_util.h"
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
#include "qt_common/abstract/frontend.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"
#include "qt_common/util/vk.h"
#include "ui_configure_per_game.h"
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
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "lichii/configuration/iidb_key_dialog.h"
#include "lichii/game/artwork_image.h"
#include "lichii/util/util.h"

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

} // namespace

ConfigurePerGame::ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name,
                                   std::vector<VkDeviceInfo::Record>& vk_device_records,
                                   Core::System& system_)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigurePerGame>()),
      file_path(std::filesystem::path(Common::FS::ToU8String(file_name))), title_id{title_id_},
      system{system_},
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

    setFocusPolicy(Qt::ClickFocus);
    setWindowTitle(tr("Game Settings"));

    addons_tab->SetTitleId(title_id);

    scene = new QGraphicsScene(this);
    ui->icon_view->setScene(scene);
    ui->button_detect_icon->setIcon(
        QIcon::fromTheme(QStringLiteral("edit-find"), QIcon::fromTheme(QStringLiteral("search"))));
    ui->button_upload_icon->setIcon(QIcon::fromTheme(
        QStringLiteral("document-open"), QIcon::fromTheme(QStringLiteral("insert-image"))));

    connect(ui->button_upload_icon, &QPushButton::clicked, this,
            &ConfigurePerGame::OnUploadNupIcon);
    connect(ui->button_detect_icon, &QPushButton::clicked, this,
            &ConfigurePerGame::OnDetectNupIcon);
    ui->button_remove_icon->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear")));
    ui->button_remove_icon->setEnabled(false);
    connect(ui->button_remove_icon, &QPushButton::clicked, this,
            &ConfigurePerGame::OnRemoveCustomIcon);
    connect(ui->button_browse_iidb, &QPushButton::clicked, this,
            &ConfigurePerGame::OnBrowseIiDB);
    connect(&detect_watcher, &QFutureWatcher<DetectIconResult>::finished, this, [this] {
        if (!detect_cancel || detect_cancel->load()) {
            return;
        }
        ui->button_detect_icon->setEnabled(true);
        ui->button_detect_icon->setText(tr("Detect Icon"));
        ui->button_upload_icon->setEnabled(true);
        ui->button_browse_iidb->setEnabled(true);
        ui->display_name->setEnabled(true);
        ui->allow_online_detection->setEnabled(true);
        ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
        if (auto* apply = ui->buttonBox->button(QDialogButtonBox::Apply)) {
            apply->setEnabled(true);
        }
        auto out = detect_watcher.result();
        if (out.icon.isNull()) {
            UpdateRemoveIconButton();
            QMessageBox::information(this, tr("Detect Icon"), QString::fromStdString(out.error));
            return;
        }
        if (!out.resolved_title.empty()) {
            ui->display_name->setText(QString::fromStdString(out.resolved_title));
        }
        StageCustomIcon(std::move(out.icon));
    });

    if (system.IsPoweredOn()) {
        QPushButton* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigurePerGame::HandleApplyButtonClicked);
    }

    ui->label_8->setVisible(system.IsPoweredOn());
    BuildSettings();
    UpdateIconPreview({});
    ConfigurationShared::FitToScreen(this, QSize(1280, 800));
}

void ConfigurePerGame::BuildSettings() {
    using Spotlight::SettingsPage;
    namespace Color = Spotlight::Color;
    namespace Tints = Spotlight::SettingsColor;
    settings_page = new SettingsPage(this, true);
    ui->verticalLayout_3->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout_3->setSpacing(0);
    ui->verticalLayout_3->addWidget(settings_page);
    settings_page->SetFooter(ui->footer);
    connect(settings_page, &SettingsPage::BackRequested, this, &QDialog::reject);

    const int overview = settings_page->AddCategory(
        tr("Overview"), tr("Only what you change here differs from global"),
        QStringLiteral("house"), Color::Accent());
    const bool is_switch_2 =
        Core::FindSwitch2Program(Core::GetGameFileFromPath(system.GetFilesystem(),
                                                           Common::FS::PathToUTF8String(file_path)),
                                 title_id, 0)
            .has_value();
    const int graphics = settings_page->AddCategory(
        tr("Graphics"), tr("Only what you change here differs from global"),
        QStringLiteral("monitor"), Tints::Green(),
        is_switch_2 ? tr("Graphics and frame rate") : tr("Graphics"));
    auto* fps = is_switch_2 ? SettingsPage::Editor(graphics_tab.get(), Settings::values.fps_limit)
                            : nullptr;
    const int frame_rate =
        fps == nullptr
            ? -1
            : settings_page->AddCategory(tr("Frame rate"), tr("Set a guest FPS cap for this game"),
                                         QStringLiteral("timer"), Tints::Gold());
    const int controls =
        settings_page->AddCategory(tr("Controls"), tr("Choose input profiles for this game"),
                                   QStringLiteral("game-controller"), Tints::Blue());
    const int mods = settings_page->AddCategory(tr("Mods and cheats"),
                                                tr("Manage the installed add-ons for this game"),
                                                QStringLiteral("puzzle-piece"), Tints::Lavender());
    const int updates = settings_page->AddCategory(
        tr("Updates and DLC"), tr("Find the installed version and manage downloadable content"),
        QStringLiteral("download-simple"), Color::Accent());
    const int saves =
        settings_page->AddCategory(tr("Save data"), tr("Access this game's save data"),
                                   QStringLiteral("archive"), Tints::Green());

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
    details->setSpacing(8);
    if (title_id != 0) {
        details->addWidget(SettingsPage::Badge(is_switch_2 ? tr("Switch 2") : tr("Switch 1"),
                                               is_switch_2 ? Color::Accent() : Color::Text(),
                                               is_switch_2 ? Color::OnAccent() : Color::OnLight()));
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
        if (QMessageBox::question(
                this, tr("Reset game settings"),
                tr("Remove this game's custom configuration and close Game Settings?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QtCommon::Game::RemoveCustomConfiguration(title_id,
                                                  Common::FS::PathToUTF8String(file_path));
        reject();
    });
    banner_layout->addWidget(reset, 0, Qt::AlignVCenter);
    settings_page->SetBanner(banner);
    connect(ui->display_name, &QLineEdit::textChanged, this, [this] { UpdateSpotlightIdentity(); });

    const auto add_setting = [this](QVBoxLayout* section, const Settings::BasicSetting& setting,
                                    const QString& name, const QString& description,
                                    const QString& icon, const QColor& color) {
        if (auto* source = SettingsPage::Editor(graphics_tab.get(), setting)) {
            if (auto* control = SettingsPage::BindSetting(source)) {
                SettingsPage::AddRow(section, name, description, icon, color, control);
            }
        }
    };
    auto* common = settings_page->AddSection(overview, tr("This game"));
    auto* picture = settings_page->AddSection(graphics, tr("This game"));
    if (frame_rate != -1) {
        for (auto* section :
             {common, picture, settings_page->AddSection(frame_rate, tr("Frame rate"))}) {
            add_setting(section, Settings::values.fps_limit, tr("Guest FPS cap"),
                        tr("Auto follows the game; other values cap how often it presents"),
                        QStringLiteral("timer"), Tints::Green());
        }
    }
    const auto& values = Settings::values;
    const int global_preset = Spotlight::PerformancePresetIndex({
        values.resolution_setup.GetValue(true),
        values.scaling_filter.GetValue(true),
        values.anti_aliasing.GetValue(true),
        values.frame_gen.GetValue(true),
    });
    const auto preset_name = Spotlight::PerformancePresetName(global_preset);
    SettingsPage::AddRow(picture, tr("Global preset"), preset_name, QStringLiteral("lightning"),
                         Color::Accent(), nullptr);
    for (auto* section : {common, picture}) {
        add_setting(section, Settings::values.resolution_setup, tr("Resolution"),
                    tr("Internal rendering scale for this game"), QStringLiteral("monitor"),
                    Tints::Blue());
    }
    add_setting(picture, Settings::values.scaling_filter, tr("Scaling filter"),
                tr("How the image fits the window"), QStringLiteral("sliders-horizontal"),
                Tints::Lavender());
    add_setting(picture, Settings::values.frame_gen, tr("Frame generation"),
                tr("Adds displayed frames between game frames"), QStringLiteral("film-strip"),
                Tints::Gold());
    const auto add_updates_row = [this, mods](QVBoxLayout* section, const QString& detail) {
        auto* button = SettingsPage::Manage();
        connect(button, &QAbstractButton::clicked, this,
                [this, mods] { settings_page->SetCategory(mods); });
        return SettingsPage::AddRow(section, tr("Updates and DLC"), detail,
                                    QStringLiteral("download-simple"), Tints::Lavender(), button);
    };
    for (const int category : {overview, graphics}) {
        auto* content = settings_page->AddSection(category);
        auto* manage_mods = SettingsPage::Manage(tr("Manage"));
        connect(manage_mods, &QAbstractButton::clicked, this,
                [this, mods] { settings_page->SetCategory(mods); });
        SettingsPage::AddRow(content, tr("Mods and cheats"), tr("Manage installed mods and cheats"),
                             QStringLiteral("puzzle-piece"), Tints::Blue(), manage_mods);
        add_updates_row(content, tr("Review installed updates and DLC"));
    }
    update_summary =
        add_updates_row(settings_page->AddSection(updates), tr("Review installed content"));

    auto* save = SettingsPage::Manage(tr("Open folder"));
    save->setEnabled(title_id != 0);
    connect(save, &QAbstractButton::clicked, this, &ConfigurePerGame::OpenSaveDataRequested);
    SettingsPage::AddRow(settings_page->AddSection(saves), tr("Save data folder"),
                         tr("Open this game's saves in the file manager"),
                         QStringLiteral("folder-open"), Tints::Green(), save);

    settings_page->AddPage(mods, addons_tab.get(), tr("Add-ons"), false);
    settings_page->AddPage(controls, input_tab.get(), tr("Input profiles"), false);
    settings_page->AddPage(overview, ui->gameInfo, tr("Game information and artwork"));
    for (const auto& [category, widget, name] :
         std::initializer_list<std::tuple<int, QWidget*, QString>>{
             {overview, system_tab.get(), tr("System")},
             {overview, cpu_tab.get(), tr("CPU")},
             {overview, audio_tab.get(), tr("Audio")},
             {overview, network_tab.get(), tr("Network")},
             {overview, applets_tab.get(), tr("Applets")},
             {graphics, graphics_tab.get(), tr("Graphics")},
             {graphics, graphics_advanced_tab.get(), tr("Advanced graphics")},
             {graphics, graphics_extensions_tab.get(), tr("Extra graphics")},
         }) {
        ConfigurationShared::UseToggles(widget);
        settings_page->AddPage(category, widget, name);
    }

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
    const QString shown_version = version.isEmpty() ? QString{} : tr("Version %1").arg(version);
    game_banner_version->setText(id.isEmpty() || shown_version.isEmpty()
                                     ? shown_version + id
                                     : tr("%1 · %2").arg(shown_version, id));
    update_summary->setText(version.isEmpty() ? tr("Review installed content")
                                              : tr("Game version %1").arg(version));
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

void ConfigurePerGame::HandleApplyButtonClicked() {
    if (detect_watcher.isRunning() || !SaveCustomMetadata(true)) {
        return;
    }
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
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
    if (is_nup_file && !ui->buttonBox->button(QDialogButtonBox::Apply)) {
        auto* apply_button = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply_button, &QAbstractButton::clicked, this,
                &ConfigurePerGame::HandleApplyButtonClicked);
    }
    // Custom artwork is stored per program ID, outside the game file, so it works for every
    // format and while a game runs.
    ui->nup_icon_widget->setVisible(title_id != 0);
    ui->nup_icon_widget->setEnabled(true);
    // The name is stored per program ID too: a rename never touches the game file.
    ui->display_name->setReadOnly(title_id == 0);
    ui->display_name->setClearButtonEnabled(title_id != 0);
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
    game_banner_icon->setPixmap(Spotlight::SettingsPage::WholeIcon(map, 72, devicePixelRatioF()));
    UpdateSpotlightIdentity();
    scene->clear();
    if (map.isNull()) {
        return;
    }
    scene->addPixmap(
        Spotlight::SettingsPage::WholeIcon(map, ui->icon_view->width(), devicePixelRatioF()));
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
    // Drops a picked image that is not saved yet, and the saved artwork on Apply or OK.
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

    ui->button_detect_icon->setEnabled(false);
    ui->button_detect_icon->setText(tr("Detecting..."));
    ui->button_upload_icon->setEnabled(false);
    ui->button_browse_iidb->setEnabled(false);
    ui->button_remove_icon->setEnabled(false);
    ui->display_name->setEnabled(false);
    ui->allow_online_detection->setEnabled(false);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    if (auto* apply = ui->buttonBox->button(QDialogButtonBox::Apply)) {
        apply->setEnabled(false);
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
                QMessageBox::warning(this, tr("Game Artwork"),
                                     tr("The artwork could not be removed."));
            }
            return false;
        }
        remove_custom_icon = false;
        has_custom_icon = false;
        UpdateRemoveIconButton();
        UISettings::values.is_game_list_reload_pending.exchange(true);
        emit CustomArtworkChanged(title_id);
    }
    if (!pending_custom_icon.isNull()) {
        // Saved as the game's custom artwork: rewriting the whole archive to embed it took a full
        // copy of the game on the UI thread (25 GB for some NUPs) and failed when the game file
        // was in use.
        if (!QtCommon::Game::SaveCustomArtwork(title_id, pending_custom_icon)) {
            if (show_error) {
                QMessageBox::warning(this, tr("Game Artwork"),
                                     tr("The artwork could not be saved."));
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
            QMessageBox::warning(this, tr("Game Name"), tr("The game name could not be saved."));
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
