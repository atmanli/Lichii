// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 citron Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Static Qt on macOS doesn't use Vulkan
// Other platforms do, and thus have conflicting VulkanMemoryAllocator symbols
#include "core/launch_params.h"
#if defined(QT_STATICPLUGIN) && !defined(__APPLE__)
#undef VMA_IMPLEMENTATION
#endif

#include <QSysInfo>
#include <boost/algorithm/string/split.hpp>
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "common/shutdown_progress.h"
#include "frontend_common/settings_generator.h"
#include "render/performance_overlay.h"
#ifdef HAS_RESHADE
#include "configuration/configure_post_processing.h"
#include "video_core/post_processing/fx_preset.h"
#endif
#include "updater/update_dialog.h"

#include "common/fs/ryujinx_compat.h"
#include "main_window.h"
#include "network/network.h"
#include "qt_common/discord/discord.h"
#include "ui_main.h"

// Other Lichii stuff //
#include "debugger/console.h"
#include "debugger/controller.h"

#include "about_dialog.h"
#include "data_dialog.h"
#include "deps_dialog.h"
#include "install_dialog.h"

#include "bootmanager.h"
#include "lichii/bug_report_dialog.h"
#include "lichii/game/game_list.h"
#include "lichii/mod_store/mod_store_dialog.h"
#include "lichii/nup_dialogs.h"
#include "lichii/recommended_settings_dialog.h"
#include "lichii/shader_warm_dialog.h"
#include "lichii/spotlight/confirm.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/fullscreen_hint.h"
#include "lichii/spotlight/ingame_bar.h"
#include "lichii/spotlight/ingame_menu.h"
#include "lichii/spotlight/launcher.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/shader_chip.h"
#include "lichii/spotlight/startup_prompt.h"
#include "lichii/spotlight/stopping_overlay.h"
#include "lichii/spotlight/style.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"
#include "lichii/spotlight/widgets.h"
#include "lichii/whats_new_dialog.h"
#include "loading_screen.h"
#include "qt_common/util/vk.h"
#include "ryujinx_dialog.h"
#include "set_play_time_dialog.h"
#include "util/util.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#ifdef LICHII_NX_TOOLS
#include "lichii/nx_tools.h"
#endif

#include "applets/qt_amiibo_settings.h"
#include "applets/qt_controller.h"
#include "applets/qt_error.h"
#include "applets/qt_profile_select.h"
#include "applets/qt_software_keyboard.h"
#include "applets/qt_web_browser.h"

#include "configuration/configuration_shared.h"
#include "configuration/configure_dialog.h"
#ifdef HAS_FLYHIGH
#include "configuration/flyhigh_dialog.h"
#include "configuration/flyhigh_menu_row.h"
#include "configuration/flyhigh_panel.h"
#endif
#include "configuration/configure_input.h"
#include "configuration/configure_network.h"
#include "configuration/configure_per_game.h"
#include "configuration/configure_tas.h"

#include "util/clickable_label.h"
#include "util/controller_navigation.h"
#include "util/overlay_dialog.h"

#include "multiplayer/state.h"

// Qt Stuff //
#define QT_NO_OPENGL

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

#include <optional>
#include <QAbstractButton>
#include <QActionGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMimeData>
#include <QPalette>
#include <QProgressDialog>
#include <QScreen>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrentRun>

#ifdef __APPLE__
#include "util/macos_sleep.h"
#endif

// Qt Common //
#include "qt_common/config/shared_translation.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

#include "qt_common/abstract/frontend.h"

#include "qt_common/qt_common.h"
#include "qt_common/qt_string_lookup.h"

#include "qt_common/util/content.h"
#include "qt_common/util/fs.h"
#include "qt_common/util/game.h"
#include "qt_common/util/meta.h"
#include "qt_common/util/mod.h"
#include "qt_common/util/path.h"

#include "qt_common/render/emu_thread.h"

// These are wrappers to avoid the calls to CreateDirectory and CreateFile because of the Windows
// defines.
static FileSys::VirtualDir VfsFilesystemCreateDirectoryWrapper(const std::string& path,
                                                               FileSys::OpenMode mode) {
    return QtCommon::vfs->CreateDirectory(path, mode);
}

static FileSys::VirtualFile VfsDirectoryCreateFileWrapper(const FileSys::VirtualDir& dir,
                                                          const std::string& path) {
    return dir->CreateFile(path);
}

// Frontend //
#include "frontend_common/play_time_manager.h"
#include "frontend_common/recommended_settings.h"

#ifdef ENABLE_UPDATE_CHECKER
#include "frontend_common/update_checker.h"
#include "frontend_common/update_installer.h"
#endif

// Common //
#include "common/fs/fs.h"
#include "common/logging.h"
#include "common/memory_detect.h"
#include "common/nextendo.h"
#include "common/npln_patch_catalog.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/string_util.h"

#ifdef ARCHITECTURE_x86_64
#include "common/cpu_features.h"
#endif

// Core //
#include "core/frontend/applets/general.h"
#include "core/frontend/applets/mii_edit.h"
#include "core/frontend/applets/software_keyboard.h"

#include "core/hle/kernel/k_process.h"
#include "core/hle/service/acc/nextendo_id_token.h"
#include "core/hle/service/acc/profile_manager.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/frontend/applet_web_browser_types.h"
#include "core/hle/service/ns/game_list_applications.h"
#include "core/hle/service/bcat/backend/nextendo.h"

#include "core/file_sys/card_image.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/nup_convert.h"
#include "core/file_sys/nup_writer.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs.h"
#include "core/file_sys/savedata_factory.h"
#include "core/loader/nx.h"

#include "core/tools/renderdoc.h"

#include "core/perf_stats.h"

#include "core/crypto/key_manager.h"

// Input //
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/virtual_amiibo.h"

// Video Core //
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/shader_notify.h"
#ifdef EGLANTINE_STREAMLINE
#include "video_core/vulkan_common/streamline.h"
#include "video_core/vulkan_common/streamline_frame_gen.h"
#endif

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <boost/container/flat_set.hpp>

// Platform stuff //
#include <boost/container/flat_set.hpp>

#ifdef __APPLE__
#include <unistd.h> // for chdir
#endif

#ifdef __unix__

#include <csignal>
#include <QSocketNotifier>
#include <sys/socket.h>
#include "qt_common/gui_settings.h"

#endif

#include "qt_common/gamemode.h"

#ifdef _WIN32
#include "common/windows/timer_resolution.h"
#include "core/core_timing.h"

#include <QPlatformSurfaceEvent>
#include <QSettings>
#include <dwmapi.h>
#include <windows.h>
#ifdef _MSC_VER
#pragma comment(lib, "Dwmapi.lib")
#endif

static inline void ApplyWindowsTitleBarDarkMode(HWND hwnd, bool enabled) {
    if (!hwnd)
        return;
    BOOL val = enabled ? TRUE : FALSE;
    // 33 = DWMWA_WINDOW_CORNER_PREFERENCE, 2 = DWMWCP_ROUND (Windows 11)
    const int corners = 2;
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    // 35 = DWMWA_CAPTION_COLOR: the Lichii window colour, so the title bar matches the dialog
    if (enabled && UISettings::values.theme == "lichii") {
        const COLORREF caption = RGB(0x0b, 0x0c, 0x10);
        DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    }
    // 20 = Win11/21H2+
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, 20, &val, sizeof(val))))
        return;
    // 19 = pre-21H2
    DwmSetWindowAttribute(hwnd, 19, &val, sizeof(val));
}

static inline void ApplyDarkToTopLevel(QWidget* w, bool on) {
    if (!w || !w->isWindow())
        return;
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (w->testAttribute(Qt::WA_TranslucentBackground)) {
        // The window paints its own rounded card inside a transparent shadow margin, so a DWM
        // corner and border would outline the whole window around the card.
        // 33 = DWMWA_WINDOW_CORNER_PREFERENCE, 1 = DWMWCP_DONOTROUND
        const int corners = 1;
        DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
        // 34 = DWMWA_BORDER_COLOR, 0xFFFFFFFE = DWMWA_COLOR_NONE
        const COLORREF border = 0xFFFFFFFE;
        DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
        return;
    }
    ApplyWindowsTitleBarDarkMode(hwnd, on);
}

namespace {
struct TitlebarFilter final : QObject {
    bool dark;
    explicit TitlebarFilter(bool is_dark) : QObject(qApp), dark(is_dark) {}

    void setDark(bool is_dark) {
        dark = is_dark;
    }

    void onFocusChanged(QWidget*, QWidget* now) {
        if (now)
            ApplyDarkToTopLevel(now->window(), dark);
    }

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            switch (ev->type()) {
            case QEvent::WinIdChange:
            case QEvent::Show:
            case QEvent::ShowToParent:
            case QEvent::WindowStateChange:
            case QEvent::ZOrderChange:
                ApplyDarkToTopLevel(w, dark);
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(obj, ev);
    }
};

static TitlebarFilter* g_filter = nullptr;
static QMetaObject::Connection g_focusConn;

} // namespace

static void ApplyGlobalDarkTitlebar(bool is_dark) {
    if (!g_filter) {
        g_filter = new TitlebarFilter(is_dark);
        qApp->installEventFilter(g_filter);
        g_focusConn = QObject::connect(qApp, &QApplication::focusChanged, g_filter,
                                       &TitlebarFilter::onFocusChanged);
    } else {
        g_filter->setDark(is_dark);
    }
    for (QWidget* w : QApplication::topLevelWidgets())
        ApplyDarkToTopLevel(w, is_dark);
}

static void RemoveTitlebarFilter() {
    if (!g_filter)
        return;
    qApp->removeEventFilter(g_filter);
    QObject::disconnect(g_focusConn);
    g_filter->deleteLater();
    g_filter = nullptr;
}

#endif

#ifdef LICHII_CRASH_DUMPS
#include "lichii/breakpad.h"
#endif

using namespace Common::Literals;

#include "qt_common/discord/discord.h"

#ifdef USE_DISCORD_PRESENCE
#include "qt_common/discord/discord_impl.h"
#endif

#ifdef QT_STATICPLUGIN
#include <QtPlugin>

#if defined(_WIN32)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#elif defined(__APPLE__)
Q_IMPORT_PLUGIN(QCocoaIntegrationPlugin)
#endif

#endif

#ifdef _WIN32
#include <windows.h>
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

constexpr int default_mouse_hide_timeout = 2500;
constexpr int default_input_update_timeout = 1;
constexpr int EmuThreadStopTimeoutMs = 10'000;
// A teardown that keeps making progress (Common::ShutdownProgress) is waited for this long at
// most before Lichii gives up on it and exits.
constexpr qint64 MaxEmuThreadShutdownMs = 180'000;

constexpr size_t CopyBufferSize = 1_MiB;

/**
 * "Callouts" are one-time instructional messages shown to the user. In the config settings, there
 * is a bitfield "callout_flags" options, used to track if a message has already been shown to the
 * user. This is 32-bits - if we have more than 32 callouts, we should retire and recycle old ones.
 */
enum class CalloutFlag : uint32_t {
    DRDDeprecation = 0x2,
};

const int MainWindow::max_recent_files_item;

namespace {

constexpr std::array<std::pair<u32, const char*>, 7> default_game_icon_sizes{
    std::make_pair(0, QT_TRANSLATE_NOOP("MainWindow", "None")),
    std::make_pair(32, QT_TRANSLATE_NOOP("MainWindow", "Small (32x32)")),
    std::make_pair(44, QT_TRANSLATE_NOOP("MainWindow", "Medium (44x44)")),
    std::make_pair(64, QT_TRANSLATE_NOOP("MainWindow", "Standard (64x64)")),
    std::make_pair(128, QT_TRANSLATE_NOOP("MainWindow", "Large (128x128)")),
    std::make_pair(160, QT_TRANSLATE_NOOP("MainWindow", "Cover (160x160)")),
    std::make_pair(256, QT_TRANSLATE_NOOP("MainWindow", "Full Size (256x256)")),
};

QString ControllerHotkey(std::string_view action) {
    for (const auto& shortcut : UISettings::values.shortcuts) {
        if (shortcut.group == "Main Window" && shortcut.name == action) {
            return QString::fromStdString(shortcut.shortcut.controller_keyseq)
                .replace(QStringLiteral("Screenshot"), MainWindow::tr("Capture"));
        }
    }
    return {};
}

QString GetTranslatedGameIconSize(size_t index) {
    return QCoreApplication::translate("MainWindow", default_game_icon_sizes[index].second);
}

// After citron-nextendo's NextendoController::ApplyProfileName and WriteProfileAvatar. False when
// the picture couldn't be saved.
bool WriteNextendoProfile(Service::Account::ProfileManager& manager, const Common::UUID& user,
                          const Common::Nextendo::ProfilePull& pull) {
    Service::Account::ProfileBase profile{};
    if (!manager.GetProfileBase(user, profile)) {
        return true;
    }
    const auto username = reinterpret_cast<const char*>(profile.username.data());
    const std::string nickname =
        Common::Nextendo::ProfileNickname(pull.nickname, profile.username.size());
    if (!nickname.empty() && nickname != Common::StringFromFixedZeroTerminatedBuffer(
                                             username, profile.username.size())) {
        profile.username.fill(0);
        std::ranges::copy(nickname, profile.username.begin());
        manager.SetProfileBase(user, profile);
        manager.WriteUserSaveFile();
        LOG_INFO(Frontend, "Took the Nextendo nickname for the current user");
    }
    if (!pull.avatar) {
        return true;
    }
    QImage image;
    if (!image.loadFromData(reinterpret_cast<const uchar*>(pull.avatar->data()),
                            static_cast<int>(pull.avatar->size()))) {
        LOG_WARNING(Frontend, "nextendo.network sent a profile picture Lichii can't read");
        return true;
    }
    constexpr int size = 256;
    image = image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    image = image.copy((image.width() - size) / 2, (image.height() - size) / 2, size, size);
    const auto path = QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
        fmt::format("system/save/8000000000000010/su/avators/{}.jpg", user.FormattedString())));
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath()) || !image.save(path, "JPEG", 100)) {
        LOG_WARNING(Frontend, "Could not save the Nextendo profile picture");
        return false;
    }
    LOG_INFO(Frontend, "Took the Nextendo profile picture for the current user");
    return true;
}

} // namespace

#ifndef _WIN32
// TODO(crueter): carboxyl does this, is it needed in qml?
inline static bool isDarkMode() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    const QPalette defaultPalette;
    const auto text = defaultPalette.color(QPalette::WindowText);
    const auto window = defaultPalette.color(QPalette::Window);
    return text.lightness() > window.lightness();
#endif // QT_VERSION
}
#endif // _WIN32

MainWindow::MainWindow(bool has_broken_vulkan)
    : ui{std::make_unique<Ui::MainWindow>()},
      input_subsystem{std::make_shared<InputCommon::InputSubsystem>()}, user_data_migrator{this} {
    QtCommon::Init(this);

    Common::FS::CreateEdenPaths();
    this->config = std::make_unique<QtConfig>();
    const bool first_start = UISettings::values.first_start.GetValue();

    Common::Log::Filter filter;
    filter.ParseFilterString(Settings::values.log_filter.GetValue());
    Common::Log::SetGlobalFilter(filter);

#ifdef __unix__
    SetupSigInterrupts();
#endif

    SetGamemodeEnabled(UISettings::values.enable_gamemode.GetValue());

    UISettings::RestoreWindowState(config);

    LoadTranslation();
    FrontendCommon::GenerateSettings();

    setAcceptDrops(true);
    ui->setupUi(this);
    statusBar()->hide();

    startup_icon_theme = QIcon::themeName();
    // fallback can only be set once, colorful theme icons are okay on both light/dark
    QIcon::setFallbackThemeName(QStringLiteral("colorful"));
    QIcon::setFallbackSearchPaths(QStringList(QStringLiteral(":/icons")));

    default_theme_paths = QIcon::themeSearchPaths();

    UpdateUITheme();

    SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
    discord_rpc->Update();

    play_time_manager = std::make_unique<PlayTime::PlayTimeManager>();

    InitializeWidgets();
    InitializeDebugWidgets();
    InitializeRecentFileMenuActions();
    InitializeHotkeys();

    SetDefaultUIGeometry();
    RestoreUIState();

    ConnectMenuEvents();
    ConnectWidgetEvents();

    QtCommon::SetupHID();
    controller_dialog->refreshConfiguration();

    UpdateWindowTitle();

    show();

#ifdef ENABLE_UPDATE_CHECKER
    connect(&update_watcher, &QFutureWatcher<UpdateChecker::CheckResult>::finished, this,
            &MainWindow::OnEmulatorUpdateAvailable);
    if (UISettings::values.check_for_updates) {
        CheckForUpdates(false);
    }
#endif

    connect(&nextendo_session_watcher,
            &QFutureWatcher<Common::Nextendo::SignOutReason>::finished, this, [this] {
                QString why;
                switch (nextendo_session_watcher.result()) {
                case Common::Nextendo::SignOutReason::None:
                    return;
                case Common::Nextendo::SignOutReason::SessionExpired:
                    why = tr("Your Nextendo session expired, so Lichii signed you out.");
                    break;
                case Common::Nextendo::SignOutReason::SessionRevoked:
                    why = tr("Your session was ended on nextendo.network, so Lichii signed you "
                             "out.");
                    break;
                case Common::Nextendo::SignOutReason::MissingScope:
                    why = tr("Your Nextendo sign-in didn't allow online play, so Lichii signed "
                             "you out.");
                    break;
                }
                QMessageBox::warning(this, tr("Signed out of Nextendo"),
                                     why + QLatin1Char(' ') +
                                         tr("Sign in again in Configure > Network."));
            });
    connect(&nextendo_status_watcher,
            &QFutureWatcher<std::optional<Common::Nextendo::OnlineStatus>>::finished, this, [this] {
                // A reply for the user before a profile switch isn't this user's, and the check
                // a game booted on the new profile meanwhile waited for this one.
                if (nextendo_status_profile !=
                    Service::Account::Nextendo::CurrentProfile(*QtCommon::system)) {
                    CheckNextendoOnlineStatus();
                    return;
                }
                const auto status = nextendo_status_watcher.result();
                QString message;
                if (!Common::Nextendo::LoadAccount(nextendo_status_profile)) {
                    message = tr("Nextendo online play is on, but you aren't signed in. Sign in "
                                 "in Configure > Network.");
                } else if (status && !status->allow) {
                    LOG_WARNING(Frontend, "nextendo.network refuses online play: {}",
                                status->reason);
                    message = ConfigureNetwork::OnlineRefusal(status->reason);
                }
                if (!message.isEmpty()) {
                    statusBar()->showMessage(message, 30000);
                }
            });
    connect(&nextendo_catalog_watcher,
            &QFutureWatcher<std::optional<Common::Nextendo::GameCatalog>>::finished, this, [this] {
                const auto catalog = nextendo_catalog_watcher.result();
                const auto check = std::exchange(nextendo_version_check, std::nullopt);
                if (!catalog || !check) {
                    return;
                }
                const auto verdict =
                    Common::Nextendo::CheckGameVersion(*catalog, check->first, check->second);
                if (!verdict || verdict->kind == Common::Nextendo::VersionVerdict::Kind::Accepted) {
                    return;
                }
                statusBar()->showMessage(
                    verdict->kind == Common::Nextendo::VersionVerdict::Kind::Disabled
                        ? tr("Nextendo doesn't offer online play for this game right now.")
                        : tr("This version of the game can't play online on Nextendo. Update it "
                             "to %1.")
                              .arg(QString::fromStdString(verdict->recommended)),
                    30000);
            });
    connect(&nextendo_session_timer, &QTimer::timeout, this, &MainWindow::KeepNextendoSessionAlive);
    connect(&nextendo_social_timer, &QTimer::timeout, this, &MainWindow::UpdateNextendoSocial);
    connect(&nextendo_profile_watcher,
            &QFutureWatcher<std::optional<Common::Nextendo::ProfilePull>>::finished, this, [this] {
                const auto pull = nextendo_profile_watcher.result();
                // Asked for while this pull ran, for another user or a new sign-in.
                const bool pending = nextendo_profile_force;
                if (pull && Common::Nextendo::OnlinePlayActive() &&
                    Settings::values.nextendo_sync_profile.GetValue()) {
                    // A game that booted meanwhile reads the profile: write it once the game stops.
                    if (QtCommon::system->IsPoweredOn() ||
                        !WriteNextendoProfile(QtCommon::system->GetProfileManager(),
                                              nextendo_profile_user, *pull)) {
                        nextendo_profile_force = true;
                    }
                }
                if (pending) {
                    SyncNextendoProfile(false);
                }
            });

    // Setup content providers.
    QtCommon::SetupContentProviders();

    // Gen keys if necessary
    OnCheckFirmwareDecryption(false);

#ifdef __unix__
    OnCheckGraphicsBackend();
#endif

    // Check for orphaned profiles and reset profile data if necessary
    QtCommon::Content::FixProfiles();

    if (Settings::values.use_dev_keys.GetValue()) {
        Core::Crypto::KeyManager::Instance().ReloadKeys();
    }

    std::optional<Core::LaunchParams> launch;
    if (QStringList args = QApplication::arguments(); args.size() >= 2) {
        QList<QByteArray> qba{};
        for (auto const& e : args) {
            qba.push_back(e.toUtf8());
        }
        std::vector<char*> args_cstr{};
        for (auto& e : qba) {
            args_cstr.push_back(e.data());
        }
        launch =
            Core::ParseLaunchParams(*QtCommon::system, args_cstr.size(), args_cstr.data(), nullptr);
    }
    const bool launch_home = launch && launch->filepath.empty() && !launch->launch_setup &&
                             launch->launch_qlaunch;
    const bool launching_content =
        launch && (!launch->filepath.empty() || launch->launch_setup || launch->launch_qlaunch ||
                   launch->launch_hlaunch);
    const auto build = WhatsNewDialog::ParseBuildTag(Common::g_build_release_tag);
    const auto whats_new_action = WhatsNewDialog::OnStartup(
        build, UISettings::values.last_seen_release.GetValue(), first_start, launching_content,
        LICHII_USE_PENDING_RELEASE_NOTES);
    if (whats_new_action == WhatsNewDialog::StartupAction::Remember) {
        config->SaveLastSeenRelease(build);
    }
    if (!launch_home) {
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }

    // Set up game list mode checkboxes.
    SetGameListMode(UISettings::values.game_list_mode.GetValue());

    // make sure menubar has the arrow cursor instead of inheriting from this
    ui->menubar->setCursor(QCursor());
    statusBar()->setCursor(QCursor());

    mouse_hide_timer.setInterval(default_mouse_hide_timeout);
    connect(&mouse_hide_timer, &QTimer::timeout, this, &MainWindow::HideMouseCursor);
    connect(ui->menubar, &QMenuBar::hovered, this, &MainWindow::ShowMouseCursor);

    update_input_timer.setInterval(default_input_update_timeout);
    connect(&update_input_timer, &QTimer::timeout, this, &MainWindow::UpdateInputDrivers);
    update_input_timer.start();

    if (has_broken_vulkan) {
        UISettings::values.has_broken_vulkan = true;

        Spotlight::StartupPrompt::Notify(this, Spotlight::StartupPrompt::Tone::Warning,
                                         tr("Broken Vulkan Installation Detected"),
                                         tr("Vulkan initialization failed during boot."));
#ifdef HAS_OPENGL
        Settings::values.renderer_backend = Settings::RendererBackend::OpenGL_GLSL;
#else
        Settings::values.renderer_backend = Settings::RendererBackend::Null;
#endif

        UpdateAPIText();
        renderer_status_action->setDisabled(true);
    } else {
        VkDeviceInfo::PopulateRecords(vk_device_records, this->window()->windowHandle());
    }

    // On macOS, SDL video would pump AppKit events alongside Qt and disrupt native menus.
#if !defined(_WIN32) && !defined(__APPLE__)
    SDL_InitSubSystem(SDL_INIT_VIDEO);

    // Set a screensaver inhibition reason string. Currently passed to DBus by SDL and visible to
    // the user through their desktop environment.
    //: TRANSLATORS: This string is shown to the user to explain why Lichii needs to prevent the
    //: computer from sleeping
    QByteArray wakelock_reason = tr("Running a game").toUtf8();
    SDL_SetHint(SDL_HINT_SCREENSAVER_INHIBIT_ACTIVITY_NAME, wakelock_reason.data());

    // SDL disables the screen saver by default, and setting the hint
    // SDL_HINT_VIDEO_ALLOW_SCREENSAVER doesn't seem to work, so we just enable the screen saver
    // for now.
    SDL_EnableScreenSaver();
#endif

    SetupPrepareForSleep();

    // Some moron added a race condition to the status bar
    // so now we have to make this completely unnecessary call
    // to prevent the UI from blowing up.
    UpdateUITheme();

    // Once FixProfiles has read the configured NAND and -u may have picked another user.
    Common::Nextendo::MigrateAccountFile(
        Service::Account::Nextendo::CurrentProfile(*QtCommon::system));
    // A default user made for an unreadable profiles.dat would make every account an orphan.
    auto& profile_manager = QtCommon::system->GetProfileManager();
    auto orphans =
        profile_manager.UserSaveFileRead()
            ? Common::Nextendo::OrphanAccounts(profile_manager.FindExistingProfileUUIDs())
            : std::vector<Common::UUID>{};
    std::erase_if(orphans, [kept = Settings::values.nextendo_kept_accounts.GetValue()](
                               const Common::UUID& profile) {
        return kept.find(profile.FormattedString()) != std::string::npos;
    });
    if (!orphans.empty()) {
        const auto answer = QMessageBox::question(
            this, tr("Nextendo Sign-ins"),
            tr("Lichii found Nextendo sign-ins for %n profile(s) this NAND doesn't have. Profiles "
               "deleted in an older build left theirs behind.\n\nRemove and sign out those "
               "sign-ins? Keep them if the profiles are in another NAND folder.",
               "", static_cast<int>(orphans.size())));
        if (answer == QMessageBox::Yes) {
            for (const auto& profile : orphans) {
                const auto account = Common::Nextendo::LoadAccount(profile);
                Common::Nextendo::ClearAccount(profile);
                if (account && !account->refresh_token.empty()) {
                    void(QtConcurrent::run(
                        [client_id = Settings::values.nextendo_client_id.GetValue(),
                         token = account->refresh_token] {
                            Common::Nextendo::RevokeToken(client_id, token);
                        }));
                }
            }
        } else {
            std::string kept = Settings::values.nextendo_kept_accounts.GetValue();
            for (const auto& profile : orphans) {
                kept += fmt::format("{}{}", kept.empty() ? "" : ",", profile.FormattedString());
            }
            Settings::values.nextendo_kept_accounts = kept;
        }
    }
    nextendo_session_timer.start(std::chrono::minutes{2});
    KeepNextendoSessionAlive();

    if (launch_home) {
        startup_qlaunch_connection = connect(
            game_list, &GameList::PopulatingCompleted, this,
            [this] {
                LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::QLaunch), std::nullopt);
            },
            Qt::SingleShotConnection);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }

    if (launch) {
        const auto& lp = *launch;

        // Override fullscreen setting if gamepath or argument is provided
        if (!lp.filepath.empty() || lp.fullscreen) {
            ui->action_Fullscreen->setChecked(lp.fullscreen);
        }

        if (!lp.filepath.empty()) {
            BootGame(QString::fromStdString(lp.filepath), ApplicationAppletParameters());
        } else if (lp.launch_setup) {
            LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Starter), std::nullopt);
        } else if (!launch_home && lp.launch_hlaunch) {
            std::filesystem::path const sd_dir =
                Common::FS::GetEdenPathString(Common::FS::EdenPath::SDMCDir);
            auto const hbl_path = (sd_dir / "atmosphere" / "hbl.nsp").string();
            BootGame(
                QString::fromStdString(hbl_path),
                LibraryAppletParameters(0x010000000000100Dull, Service::AM::AppletId::QLaunch));
        }
    }

    if (whats_new_action == WhatsNewDialog::StartupAction::Show) {
        startup_whats_new_pending = true;
        QTimer::singleShot(0, this, &MainWindow::MaybeShowStartupWhatsNew);
    }
}

MainWindow::~MainWindow() {
    if (nup_cancel) {
        nup_cancel->store(true);
    }
    nup_watcher.waitForFinished();
#ifdef __APPLE__
    AllowOSSleep();
#endif

    // will get automatically deleted otherwise
    if (render_window->parent() == nullptr) {
        delete render_window;
    }

#ifdef __unix__
    ::close(sig_interrupt_fds[0]);
    ::close(sig_interrupt_fds[1]);
#endif
}

void MainWindow::AmiiboSettingsShowDialog(const Core::Frontend::CabinetParameters& parameters,
                                          std::shared_ptr<Service::NFC::NfcDevice> nfp_device) {
    cabinet_applet =
        new QtAmiiboSettingsDialog(this, parameters, input_subsystem.get(), nfp_device);
    SCOPE_EXIT {
        cabinet_applet->deleteLater();
        cabinet_applet = nullptr;
    };

    cabinet_applet->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowStaysOnTopHint |
                                   Qt::WindowTitleHint | Qt::WindowSystemMenuHint);
    cabinet_applet->setWindowModality(Qt::WindowModal);

    if (cabinet_applet->exec() == QDialog::Rejected) {
        emit AmiiboSettingsFinished(false, {});
        return;
    }

    emit AmiiboSettingsFinished(true, cabinet_applet->GetName());
}

void MainWindow::AmiiboSettingsRequestExit() {
    if (cabinet_applet) {
        cabinet_applet->reject();
    }
}

void MainWindow::ControllerSelectorReconfigureControllers(
    const Core::Frontend::ControllerParameters& parameters) {
    controller_applet =
        new QtControllerSelectorDialog(this, parameters, input_subsystem.get(), *QtCommon::system);
    SCOPE_EXIT {
        controller_applet->deleteLater();
        controller_applet = nullptr;
    };

    controller_applet->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                                      Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                                      Qt::WindowSystemMenuHint);
    controller_applet->setWindowModality(Qt::WindowModal);
    bool is_success = controller_applet->exec() != QDialog::Rejected;

    // Don't forget to apply settings.
    QtCommon::system->HIDCore().DisableAllControllerConfiguration();
    QtCommon::system->ApplySettings();
    config->SaveAllValues();

    UpdateStatusButtons();

    emit ControllerSelectorReconfigureFinished(is_success);
}

void MainWindow::ControllerSelectorRequestExit() {
    if (controller_applet) {
        controller_applet->reject();
    }
}

void MainWindow::ProfileSelectorSelectProfile(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    profile_select_applet = new QtProfileSelectionDialog(*QtCommon::system, this, parameters);
    SCOPE_EXIT {
        profile_select_applet->deleteLater();
        profile_select_applet = nullptr;
    };

    profile_select_applet->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                                          Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                                          Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    profile_select_applet->setWindowModality(Qt::WindowModal);
    if (profile_select_applet->exec() == QDialog::Rejected) {
        emit ProfileSelectorFinishedSelection(std::nullopt);
        return;
    }

    const auto uuid = QtCommon::system->GetProfileManager().GetUser(
        static_cast<std::size_t>(profile_select_applet->GetIndex()));
    if (!uuid.has_value()) {
        emit ProfileSelectorFinishedSelection(std::nullopt);
        return;
    }

    emit ProfileSelectorFinishedSelection(uuid);
}

void MainWindow::ProfileSelectorRequestExit() {
    if (profile_select_applet) {
        profile_select_applet->reject();
    }
}

void MainWindow::SoftwareKeyboardInitialize(
    bool is_inline, Core::Frontend::KeyboardInitializeParameters initialize_parameters) {
    if (software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is already initialized!");
        return;
    }

    software_keyboard = new QtSoftwareKeyboardDialog(render_window, *QtCommon::system, is_inline,
                                                     std::move(initialize_parameters));

    if (is_inline) {
        connect(
            software_keyboard, &QtSoftwareKeyboardDialog::SubmitInlineText, this,
            [this](Service::AM::Frontend::SwkbdReplyType reply_type, std::u16string submitted_text,
                   s32 cursor_position) {
                emit SoftwareKeyboardSubmitInlineText(reply_type, submitted_text, cursor_position);
            },
            Qt::QueuedConnection);
    } else {
        connect(
            software_keyboard, &QtSoftwareKeyboardDialog::SubmitNormalText, this,
            [this](Service::AM::Frontend::SwkbdResult result, std::u16string submitted_text,
                   bool confirmed) {
                emit SoftwareKeyboardSubmitNormalText(result, submitted_text, confirmed);
            },
            Qt::QueuedConnection);
    }
}

void MainWindow::SoftwareKeyboardShowNormal() {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    const auto& layout = render_window->GetFramebufferLayout();

    const auto x = layout.screen.left;
    const auto y = layout.screen.top;
    const auto w = layout.screen.GetWidth();
    const auto h = layout.screen.GetHeight();
    const auto scale_ratio = devicePixelRatioF();

    software_keyboard->ShowNormalKeyboard(render_window->mapToGlobal(QPoint(x, y) / scale_ratio),
                                          QSize(w, h) / scale_ratio);
}

void MainWindow::SoftwareKeyboardShowTextCheck(
    Service::AM::Frontend::SwkbdTextCheckResult text_check_result,
    std::u16string text_check_message) {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    software_keyboard->ShowTextCheckDialog(text_check_result, text_check_message);
}

void MainWindow::SoftwareKeyboardShowInline(
    Core::Frontend::InlineAppearParameters appear_parameters) {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    const auto& layout = render_window->GetFramebufferLayout();

    const auto x =
        static_cast<int>(layout.screen.left + (0.5f * layout.screen.GetWidth() *
                                               ((2.0f * appear_parameters.key_top_translate_x) +
                                                (1.0f - appear_parameters.key_top_scale_x))));
    const auto y =
        static_cast<int>(layout.screen.top + (layout.screen.GetHeight() *
                                              ((2.0f * appear_parameters.key_top_translate_y) +
                                               (1.0f - appear_parameters.key_top_scale_y))));
    const auto w = static_cast<int>(layout.screen.GetWidth() * appear_parameters.key_top_scale_x);
    const auto h = static_cast<int>(layout.screen.GetHeight() * appear_parameters.key_top_scale_y);
    const auto scale_ratio = devicePixelRatioF();

    software_keyboard->ShowInlineKeyboard(std::move(appear_parameters),
                                          render_window->mapToGlobal(QPoint(x, y) / scale_ratio),
                                          QSize(w, h) / scale_ratio);
}

void MainWindow::SoftwareKeyboardHideInline() {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    software_keyboard->HideInlineKeyboard();
}

void MainWindow::SoftwareKeyboardInlineTextChanged(
    Core::Frontend::InlineTextParameters text_parameters) {
    if (!software_keyboard) {
        LOG_ERROR(Frontend, "The software keyboard is not initialized!");
        return;
    }

    software_keyboard->InlineTextChanged(std::move(text_parameters));
}

void MainWindow::SoftwareKeyboardExit() {
    if (!software_keyboard) {
        return;
    }

    software_keyboard->ExitKeyboard();

    software_keyboard = nullptr;
}

void MainWindow::WebBrowserOpenWebPage(const std::string& main_url,
                                       const std::string& additional_args, bool is_local) {
#ifdef LICHII_USE_QT_WEB_ENGINE

    // Raw input breaks with the web applet, Disable web applets if enabled
    if (Settings::values.disable_web_applet || Settings::values.enable_raw_input) {
        emit WebBrowserClosed(Service::AM::Frontend::WebExitReason::WindowClosed,
                              "http://localhost/");
        return;
    }

    web_applet = new QtNXWebEngineView(this, *QtCommon::system, input_subsystem.get());

    ui->action_Pause->setEnabled(false);
    ui->action_Restart->setEnabled(false);
    ui->action_Stop->setEnabled(false);

    {
        QProgressDialog loading_progress(this);
        loading_progress.setLabelText(tr("Loading Web Applet..."));
        loading_progress.setRange(0, 3);
        loading_progress.setValue(0);

        if (is_local && !Common::FS::Exists(main_url)) {
            loading_progress.show();

            auto future = QtConcurrent::run([this] { emit WebBrowserExtractOfflineRomFS(); });

            while (!future.isFinished()) {
                QCoreApplication::processEvents();

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        loading_progress.setValue(1);

        if (is_local) {
            web_applet->LoadLocalWebPage(main_url, additional_args);
        } else {
            web_applet->LoadExternalWebPage(main_url, additional_args);
        }

        if (render_window->IsLoadingComplete()) {
            render_window->hide();
        }

        const auto& layout = render_window->GetFramebufferLayout();
        const auto scale_ratio = devicePixelRatioF();
        web_applet->resize(layout.screen.GetWidth() / scale_ratio,
                           layout.screen.GetHeight() / scale_ratio);
        const QPoint origin = ui->horizontalLayout->parentWidget()->mapTo(this, QPoint{});
        web_applet->move(origin + QPoint(layout.screen.left / scale_ratio,
                                        layout.screen.top / scale_ratio));
        web_applet->setZoomFactor(static_cast<qreal>(layout.screen.GetWidth() / scale_ratio) /
                                  static_cast<qreal>(Layout::ScreenUndocked::Width));

        web_applet->setFocus();
        web_applet->show();

        loading_progress.setValue(2);

        QCoreApplication::processEvents();

        loading_progress.setValue(3);
    }

    bool exit_check = false;

    // TODO (Morph): Remove this
    QAction* exit_action = new QAction(tr("Disable Web Applet"), this);
    connect(exit_action, &QAction::triggered, this, [this] {
        const auto result = QMessageBox::warning(
            this, tr("Disable Web Applet"),
            tr("Disabling the web applet can lead to undefined behavior and should only be used "
               "with Super Mario 3D All-Stars. Are you sure you want to disable the web "
               "applet?\n(This can be re-enabled in the Debug settings.)"),
            QMessageBox::Yes | QMessageBox::No);
        if (result == QMessageBox::Yes) {
            Settings::values.disable_web_applet = true;
            web_applet->SetFinished(true);
        }
    });
    ui->menubar->addAction(exit_action);

    while (!web_applet->IsFinished()) {
        QCoreApplication::processEvents();

        if (!exit_check) {
            web_applet->page()->runJavaScript(
                QStringLiteral("end_applet;"), [&](const QVariant& variant) {
                    exit_check = false;
                    if (variant.toBool()) {
                        web_applet->SetFinished(true);
                        web_applet->SetExitReason(
                            Service::AM::Frontend::WebExitReason::EndButtonPressed);
                    }
                });

            exit_check = true;
        }

        if (web_applet->GetCurrentURL().contains(QStringLiteral("localhost"))) {
            if (!web_applet->IsFinished()) {
                web_applet->SetFinished(true);
                web_applet->SetExitReason(Service::AM::Frontend::WebExitReason::CallbackURL);
            }

            web_applet->SetLastURL(web_applet->GetCurrentURL().toStdString());
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto exit_reason = web_applet->GetExitReason();
    const auto last_url = web_applet->GetLastURL();

    web_applet->hide();

    render_window->setFocus();

    if (render_window->IsLoadingComplete()) {
        render_window->show();
    }

    ui->action_Pause->setEnabled(true);
    ui->action_Restart->setEnabled(true);
    ui->action_Stop->setEnabled(true);

    ui->menubar->removeAction(exit_action);

    QCoreApplication::processEvents();

    emit WebBrowserClosed(exit_reason, last_url);

#else

    // Utilize the same fallback as the default web browser applet.
    emit WebBrowserClosed(Service::AM::Frontend::WebExitReason::WindowClosed, "http://localhost/");

#endif
}

void MainWindow::WebBrowserRequestExit() {
#ifdef LICHII_USE_QT_WEB_ENGINE
    if (web_applet) {
        web_applet->SetExitReason(Service::AM::Frontend::WebExitReason::ExitRequested);
        web_applet->SetFinished(true);
    }
#endif
}

namespace {

QString ControllerKeys(std::string_view action) {
    for (const auto& shortcut : UISettings::values.shortcuts) {
        if (shortcut.group == "Main Window" && shortcut.name == action) {
            return QString::fromStdString(shortcut.shortcut.controller_keyseq)
                .replace(QStringLiteral("Screenshot"), MainWindow::tr("Capture"))
                .replace(QLatin1Char('+'), QStringLiteral(" + "));
        }
    }
    return {};
}

} // namespace

void MainWindow::InitializeWidgets() {
    render_window = new GRenderWindow(this, input_subsystem);
    render_window->hide();

    auto* content = new QWidget(ui->centralwidget);
    delete ui->horizontalLayout;
    ui->horizontalLayout = new QHBoxLayout(content);
    ui->horizontalLayout->setContentsMargins(0, 0, 0, 0);
    ui->horizontalLayout->setSpacing(0);
    auto* frame = new QVBoxLayout(ui->centralwidget);
    frame->setContentsMargins(0, 0, 0, 0);
    frame->setSpacing(0);
    running_title_bar = new Spotlight::RunningTitleBar(ui->centralwidget);
    frame->addWidget(running_title_bar);
    frame->addWidget(content, 1);

    game_list = new GameList(QtCommon::vfs, QtCommon::provider.get(), *play_time_manager,
                             *QtCommon::system, this);
    game_list_placeholder = new GameListPlaceholder(this);
    ui->menu_View->removeAction(ui->action_Show_Filter_Bar);
    ui->action_Show_Filter_Bar->setCheckable(false);
    launcher = new Spotlight::Launcher(this, game_list, game_list_placeholder, running_title_bar,
                                       *play_time_manager,
                                       {.load_file = ui->action_Load_File,
                                        .load_folder = ui->action_Load_Folder,
                                        .install = ui->action_Install_File_NAND,
                                        .settings = ui->action_Configure,
                                        .search = ui->action_Show_Filter_Bar,
                                        .grid = ui->action_Grid_View,
                                        .list = ui->action_Tree_View,
                                        .tools = ui->menu_Tools,
                                        .multiplayer = ui->menu_Multiplayer,
                                        .help = ui->menu_Help},
                                       content);
    ui->horizontalLayout->addWidget(launcher);
    connect(launcher, &Spotlight::Launcher::RescanRequested, this, &MainWindow::OnGameListRefresh);
    connect(game_list, &GameList::OpenGameModsRequested, this,
            [this](u64 title_id, const std::string& file_name) {
                QSet<QString> store_toggled;
                if (OpenModStore(this, title_id, file_name, store_toggled) ||
                    !store_toggled.isEmpty()) {
                    OnGameListRefresh();
                }
            });

    loading_screen = new LoadingScreen(this);
    loading_screen->hide();
    ui->horizontalLayout->addWidget(loading_screen);
    running_title_bar->FloatOver(loading_screen);
    connect(loading_screen, &LoadingScreen::Hidden, this, [&] {
        loading_screen->Clear();
        if (emulation_running) {
            render_window->show();
            render_window->setFocus();
            OnToggleStatusBar();
        }
    });

    multiplayer_state = new MultiplayerState(this, game_list->GetModel(), ui->action_Leave_Room,
                                             ui->action_Show_Room, *QtCommon::system);
    multiplayer_state->setVisible(false);

    ingame_bar = new Spotlight::InGameBar(this);
    setStatusBar(ingame_bar);
    ingame_bar->hide();
    // Sizes the bars before the window's layout runs, so a resize moves them and the picture once.
    installEventFilter(this);
    loading_screen->SetCancelAction(ui->action_Stop);
    ingame_menu_action =
        ui->menu_Emulation->addAction(tr("In-game Menu"), this, &MainWindow::ToggleInGameMenu);
    ingame_menu_action->setEnabled(false);

    quick_controls = new Spotlight::Menu(this);
    quick_controls->setTitle(tr("Quick controls"));
    ui->menu_View->addMenu(quick_controls);
    quick_controls->setToolTipsVisible(true);
    const auto show_hotkeys = [this](QMenu* menu,
                                     std::vector<std::pair<QAction*, const char*>> hotkeys) {
        connect(menu, &QMenu::aboutToShow, this, [this, hotkeys] {
            for (const auto& [action, name] : hotkeys) {
                Spotlight::SetHotkeyHint(action,
                                         hotkey_registry.GetKeySequence("Main Window", name),
                                         ControllerHotkey(name));
            }
        });
    };
    const auto make_submenu = [](QMenu* parent, const QString& name, QStringView icon = {}) {
        auto* menu = parent->addMenu(name);
        menu->setToolTipsVisible(true);
        if (!icon.isEmpty()) {
            menu->menuAction()->setProperty("spotlightIcon",
                                            Spotlight::Icon(icon, Spotlight::Color::MenuInk(), 16));
        }
        return menu;
    };
    const auto in_order = [](const auto& texts, const auto& order) {
        std::vector<std::pair<typename std::decay_t<decltype(texts)>::key_type, QString>> choices;
        for (const auto value : order) {
            if (const auto it = texts.find(value); it != texts.end()) {
                choices.emplace_back(value, it->second);
            }
        }
        return choices;
    };
    const auto add_choices = [this](QMenu* menu, const auto& choices, auto& setting,
                                    const auto& apply, const auto& available) {
        auto* group = new QActionGroup(menu);
        for (const auto& [value, text] : choices) {
            if (!available(value)) {
                continue;
            }
            auto* action = menu->addAction(ConfigurationShared::TranslateMapText(translator, text));
            action->setCheckable(true);
            group->addAction(action);
            connect(action, &QAction::triggered, this, [apply, value] { apply(value); });
            connect(menu, &QMenu::aboutToShow, action,
                    [action, &setting, value] { action->setChecked(setting.GetValue() == value); });
        }
    };
    const auto all = [](auto) { return true; };
    auto* renderer = make_submenu(quick_controls, tr("Renderer"), u"monitor");
    renderer_status_action = renderer->menuAction();
    renderer->addAction(tr("Cycle renderer"), this, &MainWindow::OnToggleGraphicsAPI);
    renderer->addSeparator();
    using Settings::RendererBackend;
    add_choices(
        renderer,
        in_order(ConfigurationShared::renderer_backend_texts_map,
                 std::array{RendererBackend::Vulkan, RendererBackend::OpenGL_GLSL,
                            RendererBackend::OpenGL_SPIRV, RendererBackend::OpenGL_GLASM}),
        Settings::values.renderer_backend,
        [this](auto value) {
            Settings::values.renderer_backend.SetValue(value);
            UpdateAPIText();
        },
        [](auto value) {
#ifndef HAS_OPENGL
            return value == RendererBackend::Vulkan;
#else
            return value != RendererBackend::Null;
#endif
        });

    auto* console = make_submenu(quick_controls, tr("Console mode"), u"game-controller");
    dock_status_action = console->menuAction();
    add_choices(
        console, ConfigurationShared::use_docked_mode_texts_map, Settings::values.use_docked_mode,
        [this](auto value) {
            if (value != Settings::values.use_docked_mode.GetValue()) {
                OnToggleDockedMode();
            }
        },
        all);

    auto* accuracy = make_submenu(quick_controls, tr("GPU accuracy"), u"gauge");
    gpu_accuracy_action = accuracy->menuAction();
    add_choices(
        accuracy, ConfigurationShared::gpu_accuracy_texts_map, Settings::values.gpu_accuracy,
        [this](auto value) {
            Settings::values.gpu_accuracy.SetValue(value);
            UpdateGPUAccuracyButton();
        },
        all);

    auto* filter = make_submenu(quick_controls, tr("Scaling filter"));
    filter_status_action = filter->menuAction();
    using Settings::ScalingFilter;
    add_choices(
        filter,
        in_order(ConfigurationShared::scaling_filter_texts_map,
                 std::array{ScalingFilter::NearestNeighbor, ScalingFilter::Bilinear,
                            ScalingFilter::Bicubic, ScalingFilter::ZeroTangent,
                            ScalingFilter::BSpline, ScalingFilter::Mitchell, ScalingFilter::Spline1,
                            ScalingFilter::Gaussian, ScalingFilter::Lanczos,
                            ScalingFilter::ScaleForce, ScalingFilter::Fsr, ScalingFilter::Area,
                            ScalingFilter::Mmpx, ScalingFilter::Sgsr, ScalingFilter::SgsrEdge,
                            ScalingFilter::Fsr3}),
        Settings::values.scaling_filter,
        [this](auto value) {
            Settings::values.scaling_filter.SetValue(value);
            UpdateFilterText();
        },
        all);

    auto* antialiasing = make_submenu(quick_controls, tr("Anti-aliasing"));
    aa_status_action = antialiasing->menuAction();
    antialiasing->addAction(tr("Cycle anti-aliasing"), this, [this] {
        auto value = Settings::AntiAliasing(u32(Settings::values.anti_aliasing.GetValue()) + 1);
        if (u32(value) > u32(Settings::EnumMetadata<Settings::AntiAliasing>::GetLast())) {
            value = Settings::EnumMetadata<Settings::AntiAliasing>::GetFirst();
        }
        Settings::values.anti_aliasing.SetValue(value);
        UpdateAAText();
    });
    antialiasing->addSeparator();
    add_choices(
        antialiasing, ConfigurationShared::anti_aliasing_texts_map, Settings::values.anti_aliasing,
        [this](auto value) {
            Settings::values.anti_aliasing.SetValue(value);
            UpdateAAText();
        },
        all);

#ifdef HAS_FLYHIGH
    auto* frame_gen = new FlyHighTypeMenu;
    frame_gen->setParent(quick_controls, frame_gen->windowFlags());
#else
    auto* frame_gen = new QMenu(quick_controls);
#endif
    frame_gen->setTitle(tr("Frame generation"));
    frame_gen->setToolTipsVisible(true);
    quick_controls->addMenu(frame_gen);
    frame_gen_status_action = frame_gen->addAction(tr("Enable frame generation"), this, [this] {
        Settings::values.frame_gen.SetValue(!Settings::values.frame_gen.GetValue());
#ifdef HAS_FLYHIGH
        if (Settings::values.frame_gen.GetValue() &&
            Settings::values.frame_gen_type.GetValue() == Settings::FrameGenType::FlyHigh) {
            OfferFlyHighModel(this);
        }
#endif
        UpdateFrameGenButton();
    });
    frame_gen_status_action->setCheckable(true);
    frame_gen->addSeparator();
    for (const auto& [type, text] : ConfigurationShared::frame_gen_type_texts_map) {
#ifndef EGLANTINE_STREAMLINE
        if (type == Settings::FrameGenType::NvidiaDlssG) {
            continue;
        }
#endif
#ifndef HAS_FLYHIGH
        if (type == Settings::FrameGenType::FlyHigh) {
            continue;
        }
#endif
        const QString label = ConfigurationShared::TranslateMapText(translator, text);
        auto* action = frame_gen->addAction(label, this, [this, type] {
            Settings::values.frame_gen_type.SetValue(type);
            Settings::values.frame_gen.SetValue(true);
#ifdef HAS_FLYHIGH
            if (type == Settings::FrameGenType::FlyHigh) {
                OfferFlyHighModel(this);
            }
#endif
            UpdateFrameGenButton();
        });
        action->setCheckable(true);
        connect(frame_gen, &QMenu::aboutToShow, action, [this, action, type] {
            action->setChecked(Settings::values.frame_gen.GetValue() &&
                               Settings::values.frame_gen_type.GetValue() == type);
            action->setEnabled(!emulation_running);
        });
#ifdef HAS_FLYHIGH
        if (type == Settings::FrameGenType::FlyHigh) {
            frame_gen->SetGear(action, [this] {
                FlyHighDialog dialog(this);
                if (dialog.exec() == QDialog::Accepted) {
                    dialog.Apply();
                    config->SaveAllValues();
                    UpdateFrameGenButton();
                }
            });
        }
#endif
    }

    QMenu* effects = nullptr;
#ifdef HAS_RESHADE
    effects = make_submenu(quick_controls, tr("Post-processing"));
    post_shader_status_action = effects->addAction(tr("Enable effects"), this, [this] {
        Settings::values.post_shader_enabled.SetValue(
            !Settings::values.post_shader_enabled.GetValue());
        UpdatePostShaderText();
    });
    post_shader_status_action->setCheckable(true);
    for (const auto& preset : VideoCore::GetFxPresetCatalog()) {
        effects->addAction(QString::fromStdString(preset.name), this, [this, name = preset.name] {
            VideoCore::ApplyFxPreset(name);
            Settings::values.post_shader_enabled.SetValue(true);
            UpdatePostShaderText();
        });
    }
    effects->addSeparator();
    effects->addAction(tr("Configure Effects…"), this, &MainWindow::OnPostProcessingShaders);
    UpdatePostShaderText();
#endif

    auto* audio = make_submenu(quick_controls, tr("Audio"), u"speaker-high");
    auto* slider_action = new QWidgetAction(audio);
    auto* volume_block = new Spotlight::VolumeBlock(audio);
    volume_slider = volume_block->Track();
    slider_action->setDefaultWidget(volume_block);
    audio->addAction(slider_action);
    const auto set_volume = [this](int percentage) {
        Settings::values.audio_muted = false;
        Settings::values.volume.SetValue(static_cast<u8>(percentage));
        UpdateVolumeUI();
    };
    connect(volume_slider, &QAbstractSlider::valueChanged, this, set_volume);
    mute_action = audio->addAction(tr("Mute"), this, &MainWindow::OnMute);
    mute_action->setCheckable(true);
    auto* decrease_volume =
        audio->addAction(tr("Volume down"), this, &MainWindow::OnDecreaseVolume);
    auto* increase_volume = audio->addAction(tr("Volume up"), this, &MainWindow::OnIncreaseVolume);
    auto* reset_volume = audio->addAction(tr("Reset volume"), this, [this] {
        Settings::values.volume.SetValue(100);
        UpdateVolumeUI();
    });
    show_hotkeys(audio, {{mute_action, "Audio Mute/Unmute"},
                         {decrease_volume, "Audio Volume Down"},
                         {increase_volume, "Audio Volume Up"}});

    auto* speed = make_submenu(quick_controls, tr("Emulation speed"), u"timer");
    speed_limit_action = speed->addAction(tr("Toggle speed limit"), this, [this] {
        RememberSpeedLimitBeforeHotkey();
        Settings::ToggleStandardMode();
        SetFPSSuffix();
    });
    speed_limit_action->setEnabled(false);
    turbo_action = speed->addAction(tr("Turbo"), this, [this] {
        RememberSpeedLimitBeforeHotkey();
        Settings::ToggleTurboMode();
        SetFPSSuffix();
    });
    turbo_action->setCheckable(true);
    turbo_action->setEnabled(false);
    slow_action = speed->addAction(tr("Slow"), this, [this] {
        RememberSpeedLimitBeforeHotkey();
        Settings::ToggleSlowMode();
        SetFPSSuffix();
    });
    slow_action->setCheckable(true);
    slow_action->setEnabled(false);
    show_hotkeys(speed, {{speed_limit_action, "Toggle Framerate Limit"},
                         {turbo_action, "Toggle Turbo Speed"},
                         {slow_action, "Toggle Slow Speed"}});

    quick_controls->addSeparator();
    refresh_action =
        quick_controls->addAction(tr("Rescan library"), this, &MainWindow::OnGameListRefresh);
    quick_controls->addAction(ui->action_Show_Performance_Overlay);
    auto* session = new Spotlight::Menu(quick_controls);
    session->setTitle(tr("Session status"));
    session->setToolTipsVisible(true);
    quick_controls->addMenu(session);
    firmware_action = session->addAction(tr("Firmware not installed"));
    firmware_action->setEnabled(false);
    auto* multiplayer = session->addAction(tr("Multiplayer"), multiplayer_state,
                                           &MultiplayerState::OnOpenNetworkRoom);
    tas_action = session->addAction(QString{});
    tas_action->setEnabled(false);
    auto* message = session->addAction(tr("No recent messages"));
    message->setEnabled(false);
    auto* performance = session->addAction(QString{});
    performance->setEnabled(false);
    connect(ingame_bar, &QStatusBar::messageChanged, this, [message](const QString& text) {
        if (!text.isEmpty()) {
            message->setText(text);
        }
    });
    connect(session, &QMenu::aboutToShow, this, [this, multiplayer, performance] {
        const QString status = multiplayer_state->GetStatusText()->text();
        multiplayer->setText(status.isEmpty() ? tr("Multiplayer") : status);
        multiplayer->setIcon(QIcon(multiplayer_state->GetStatusIcon()->pixmap()));
        tas_action->setVisible(emulation_running && Settings::values.tas_enable.GetValue());
        performance->setText(session_status_text);
        performance->setVisible(emulation_running && !session_status_text.isEmpty());
    });

    const Spotlight::InGameControls controls{.renderer = renderer,
                                             .filter = filter,
                                             .frame_gen = frame_gen,
                                             .accuracy = accuracy,
                                             .anti_aliasing = antialiasing,
                                             .effects = effects,
                                             .settings = ui->action_Configure_Current_Game,
                                             .decrease_volume = decrease_volume,
                                             .increase_volume = increase_volume,
                                             .mute = mute_action,
                                             .reset_volume = reset_volume,
                                             .audio = audio,
                                             .speed = speed,
                                             .session = session,
                                             .rescan = refresh_action,
                                             .performance_overlay =
                                                 ui->action_Show_Performance_Overlay};
    ingame_menu = new Spotlight::InGameMenu({.pause = ui->action_Pause,
                                             .restart = ui->action_Restart,
                                             .stop = ui->action_Stop,
                                             .capture = ui->action_Capture_Screenshot,
                                             .fullscreen = ui->action_Fullscreen,
                                             .frame_gen = frame_gen_status_action,
                                             .turbo = turbo_action,
                                             .effects = post_shader_status_action,
                                             .controls = controls},
                                            this);
    const auto set_docked = [this](bool docked) {
        if (Settings::IsDockedMode() != docked) {
            OnToggleDockedMode();
        }
    };
    connect(ingame_menu, &Spotlight::InGameMenu::DockedChanged, this, set_docked);
    connect(ingame_menu, &Spotlight::InGameMenu::VolumeChanged, this, set_volume);

    auto* emulation_menu = new Spotlight::Menu(this);
    emulation_menu->addActions({ui->action_Pause, ui->action_Restart, ui->action_Stop});
    emulation_menu->addSeparator();
    emulation_menu->addActions(
        {ui->action_Fullscreen, ui->action_Capture_Screenshot, ui->action_Load_Amiibo});
    emulation_menu->AddSharedMenu(speed,
                                  Spotlight::Icon(u"timer", Spotlight::Color::MenuInk(), 16));
    emulation_menu->addSeparator();
    emulation_menu->addActions({ui->action_Configure_Current_Game, ui->action_Configure});
    show_hotkeys(emulation_menu, {{ui->action_Pause, "Continue/Pause Emulation"},
                                  {ui->action_Restart, "Restart Emulation"},
                                  {ui->action_Stop, "Stop Emulation"},
                                  {ui->action_Fullscreen, "Fullscreen"},
                                  {ui->action_Capture_Screenshot, "Capture Screenshot"},
                                  {ui->action_Load_Amiibo, "Load/Remove Amiibo"}});
    running_title_bar->AddMenu(tr("Emulation"), emulation_menu);
    auto* graphics_menu = new Spotlight::Menu(this);
    graphics_menu->setToolTipsVisible(true);
    for (QMenu* menu : {renderer, console, accuracy, filter, antialiasing,
                        static_cast<QMenu*>(frame_gen), effects}) {
        if (menu) {
            graphics_menu->addMenu(menu);
        }
    }
    connect(graphics_menu, &QMenu::aboutToShow, this, &MainWindow::UpdateStatusButtons);
    running_title_bar->AddMenu(tr("Graphics"), graphics_menu);
    running_title_bar->AddMenu(tr("Audio"), audio);
    auto* view_menu = new Spotlight::Menu(this);
    view_menu->addActions(
        {ui->action_Show_Performance_Overlay, ingame_menu_action, ui->action_Show_Status_Bar});
    view_menu->addSeparator();
    view_menu->addMenu(session);
    view_menu->addAction(refresh_action);
    show_hotkeys(view_menu, {{ui->action_Show_Performance_Overlay, "Toggle Performance Overlay"},
                             {ingame_menu_action, "In-game Menu"},
                             {ui->action_Show_Status_Bar, "Toggle Status Bar"}});
    running_title_bar->AddMenu(tr("View"), view_menu);
    show_hotkeys(ui->menu_Emulation, {{ui->action_Pause, "Continue/Pause Emulation"},
                                      {ui->action_Stop, "Stop Emulation"},
                                      {ui->action_Restart, "Restart Emulation"}});
    show_hotkeys(ui->menu_View,
                 {{ui->action_Fullscreen, "Fullscreen"},
                  {ui->action_Show_Status_Bar, "Toggle Status Bar"},
                  {ui->action_Show_Performance_Overlay, "Toggle Performance Overlay"}});
    connect(ingame_bar, &Spotlight::InGameBar::ExpandRequested, ui->action_Show_Status_Bar,
            &QAction::trigger);
    auto* shaders = ingame_bar->Shaders();
    connect(shaders, &Spotlight::ShaderChip::AboutToOpen, this, [shaders] {
        const u64 program = QtCommon::system->GetApplicationProcessProgramID();
        shaders->SetPrecompile(program != 0, program != 0 && ShaderWarmRequested(program));
    });
    connect(shaders, &Spotlight::ShaderChip::PrecompileChanged, this, [this](bool checked) {
        const u64 program = QtCommon::system->GetApplicationProcessProgramID();
        if (program != 0) {
            OnGameListPrecompileShaders(program, current_game_path.toStdString(), checked);
        }
    });
    connect(shaders, &Spotlight::ShaderChip::ShaderSettingsRequested,
            ui->action_Configure_Current_Game, &QAction::trigger);

    fullscreen_hint =
        new Spotlight::FullscreenHint(Spotlight::FullscreenHint::Kind::Controls, this);
    fullscreen_shader_toast =
        new Spotlight::FullscreenHint(Spotlight::FullscreenHint::Kind::Shaders, this);
    UpdateStatusButtons();
}

void MainWindow::InitializeDebugWidgets() {
    QMenu* debug_menu = ui->menu_View_Debugging;

    controller_dialog = new ControllerDialog(QtCommon::system->HIDCore(), input_subsystem, this);
    controller_dialog->hide();
    debug_menu->addAction(controller_dialog->toggleViewAction());
}

void MainWindow::InitializeRecentFileMenuActions() {
    for (int i = 0; i < max_recent_files_item; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &MainWindow::OnMenuRecentFile);

        ui->menu_recent_files->addAction(actions_recent_files[i]);
    }
    ui->menu_recent_files->addSeparator();
    QAction* action_clear_recent_files = new QAction(this);
    action_clear_recent_files->setText(tr("&Clear Recent Files"));
    connect(action_clear_recent_files, &QAction::triggered, this, [this] {
        UISettings::values.recent_files.clear();
        UpdateRecentFiles();
    });
    ui->menu_recent_files->addAction(action_clear_recent_files);

    UpdateRecentFiles();
}

namespace {

bool ConfirmCard(QWidget* parent, const QString& title, const QString& text, const QString& action,
                 const QString& icon) {
    Spotlight::ConfirmDialog dialog(parent, title, text, action, icon);
    auto* navigation = new ControllerNavigation(QtCommon::system->HIDCore(), &dialog);
    QObject::connect(navigation, &ControllerNavigation::TriggerKeyboardEvent, &dialog,
                     [&dialog](Qt::Key key) {
                         QCoreApplication::postEvent(
                             &dialog, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
                     });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    navigation->UnloadController();
    return accepted;
}

} // namespace

void MainWindow::LinkActionShortcut(QAction* action, const QString& action_name,
                                    const bool tas_allowed) {
    static const auto main_window = std::string("Main Window");
    action->setShortcut(hotkey_registry.GetKeySequence(main_window, action_name.toStdString()));
    action->setShortcutContext(
        hotkey_registry.GetShortcutContext(main_window, action_name.toStdString()));
    action->setAutoRepeat(false);
    Spotlight::SetHotkeyHint(action, action->shortcut(),
                             ControllerHotkey(action_name.toStdString()));

    this->addAction(action);

    auto* controller =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    const auto* controller_hotkey =
        hotkey_registry.GetControllerHotkey(main_window, action_name.toStdString(), controller);
    connect(
        controller_hotkey, &ControllerShortcut::Activated, this,
        [action, tas_allowed, this] {
            auto [tas_status, current_tas_frame, total_tas_frames] =
                input_subsystem->GetTas()->GetStatus();
            if (tas_allowed || tas_status == InputCommon::TasInput::TasState::Stopped) {
                action->trigger();
            }
        },
        Qt::QueuedConnection);
}

void MainWindow::InitializeHotkeys() {
    hotkey_registry.LoadHotkeys();

    LinkActionShortcut(ui->action_Load_File, QStringLiteral("Load File"));
    LinkActionShortcut(ui->action_Load_Amiibo, QStringLiteral("Load/Remove Amiibo"));
    LinkActionShortcut(ui->action_Exit, QStringLiteral("Exit Eglantine"));
    LinkActionShortcut(ui->action_Restart, QStringLiteral("Restart Emulation"));
    LinkActionShortcut(ui->action_Pause, QStringLiteral("Continue/Pause Emulation"));
    LinkActionShortcut(ui->action_Stop, QStringLiteral("Stop Emulation"));
    LinkActionShortcut(ui->action_Show_Filter_Bar, QStringLiteral("Toggle Filter Bar"));
    LinkActionShortcut(ui->action_Show_Status_Bar, QStringLiteral("Toggle Status Bar"));
    LinkActionShortcut(ui->action_Show_Performance_Overlay,
                       QStringLiteral("Toggle Performance Overlay"));
    LinkActionShortcut(ui->action_Fullscreen, QStringLiteral("Fullscreen"));
    LinkActionShortcut(ui->action_Capture_Screenshot, QStringLiteral("Capture Screenshot"));
    LinkActionShortcut(ui->action_Report_Bug, QStringLiteral("Report a Bug"));
    LinkActionShortcut(ui->action_TAS_Start, QStringLiteral("TAS Start/Stop"), true);
    LinkActionShortcut(ui->action_TAS_Record, QStringLiteral("TAS Record"), true);
    LinkActionShortcut(ui->action_TAS_Reset, QStringLiteral("TAS Reset"), true);
    LinkActionShortcut(ui->action_View_Lobby, QStringLiteral("Browse Public Game Lobby"));
    LinkActionShortcut(ui->action_Start_Room, QStringLiteral("Create Room"));
    LinkActionShortcut(ui->action_Connect_To_Room, QStringLiteral("Direct Connect to Room"));
    LinkActionShortcut(ui->action_Show_Room, QStringLiteral("Show Current Room"));
    LinkActionShortcut(ui->action_Leave_Room, QStringLiteral("Leave Room"));
    LinkActionShortcut(ui->action_Configure, QStringLiteral("Configure"));
    LinkActionShortcut(ui->action_Configure_Current_Game, QStringLiteral("Configure Current Game"));
    LinkActionShortcut(ingame_menu_action, QStringLiteral("In-game Menu"), true);

    static const QString main_window = QStringLiteral("Main Window");
    const auto connect_shortcut = [&]<typename Fn>(const QString& action_name, const Fn& function) {
        const auto* hotkey =
            hotkey_registry.GetHotkey(main_window.toStdString(), action_name.toStdString(), this);
        auto* controller =
            QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
        const auto* controller_hotkey = hotkey_registry.GetControllerHotkey(
            main_window.toStdString(), action_name.toStdString(), controller);
        connect(hotkey, &QShortcut::activated, this, function);
        connect(controller_hotkey, &ControllerShortcut::Activated, this, function,
                Qt::QueuedConnection);
    };
    connect_shortcut(QStringLiteral("Exit Fullscreen"), [&] {
        if (emulation_running && ui->action_Fullscreen->isChecked()) {
            ui->action_Fullscreen->setChecked(false);
            ToggleFullscreen();
        }
    });
    connect_shortcut(QStringLiteral("Change Adapting Filter"), &MainWindow::OnToggleAdaptingFilter);
    connect_shortcut(QStringLiteral("Change Docked Mode"), &MainWindow::OnToggleDockedMode);
    connect_shortcut(QStringLiteral("HOME Button"), [this] {
        if (emulation_running) {
            QtCommon::system->GetAppletManager().PressHomeButton();
        }
    });
    connect_shortcut(QStringLiteral("Change GPU Mode"), &MainWindow::OnToggleGpuAccuracy);
    connect_shortcut(QStringLiteral("Audio Mute/Unmute"), &MainWindow::OnMute);
    connect_shortcut(QStringLiteral("Audio Volume Down"), &MainWindow::OnDecreaseVolume);
    connect_shortcut(QStringLiteral("Audio Volume Up"), &MainWindow::OnIncreaseVolume);

    connect_shortcut(QStringLiteral("Toggle Framerate Limit"),
                     [this] { speed_limit_action->trigger(); });
    connect_shortcut(QStringLiteral("Toggle Turbo Speed"), [this] { turbo_action->trigger(); });
    connect_shortcut(QStringLiteral("Toggle Slow Speed"), [this] { slow_action->trigger(); });

    connect_shortcut(QStringLiteral("Toggle Renderdoc Capture"), [] {
        if (Settings::values.enable_renderdoc_hotkey) {
            QtCommon::system->GetRenderdocAPI().ToggleCapture();
        }
    });
    connect_shortcut(QStringLiteral("Toggle Mouse Panning"), [&] {
        Settings::values.mouse_panning = !Settings::values.mouse_panning;
        if (Settings::values.mouse_panning) {
            render_window->installEventFilter(render_window);
            render_window->setAttribute(Qt::WA_Hover, true);
        }
    });
}

void MainWindow::SetDefaultUIGeometry() {
    // geometry: 53% of the window contents are in the upper screen half, 47% in the lower half
    const QRect screenRect = QGuiApplication::primaryScreen()->geometry();

    const int w = screenRect.width() * 2 / 3;
    const int h = screenRect.height() * 2 / 3;
    const int x = (screenRect.x() + screenRect.width()) / 2 - w / 2;
    const int y = (screenRect.y() + screenRect.height()) / 2 - h * 53 / 100;

    setGeometry(x, y, w, h);
}

void MainWindow::RestoreUIState() {
    setWindowFlag(Qt::FramelessWindowHint, Spotlight::ClientFrame);
    restoreGeometry(UISettings::values.geometry);
    // Work-around because the games list isn't supposed to be full screen
    if (isFullScreen()) {
        showNormal();
    }
    restoreState(UISettings::values.state);
    render_window->setWindowFlags(render_window->windowFlags() & ~Qt::FramelessWindowHint);
    render_window->restoreGeometry(UISettings::values.renderwindow_geometry);

    game_list->LoadInterfaceLayout();

    ui->action_Single_Window_Mode->setChecked(UISettings::values.single_window_mode.GetValue());
    ToggleWindowMode();

    ui->action_Fullscreen->setChecked(UISettings::values.fullscreen.GetValue());

    ui->action_Enable_Overlay_Applet->setChecked(Settings::values.enable_overlay.GetValue());

    ui->action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar.GetValue());
    OnToggleStatusBar();

    ui->action_Show_Performance_Overlay->setChecked(
        UISettings::values.show_perf_overlay.GetValue());
    if (perf_overlay)
        perf_overlay->setVisible(ui->action_Show_Performance_Overlay->isChecked());
    Debugger::ToggleConsole();
}

void MainWindow::OnAppFocusStateChanged(Qt::ApplicationState state) {
    if (state != Qt::ApplicationHidden && state != Qt::ApplicationInactive &&
        state != Qt::ApplicationActive) {
        LOG_DEBUG(Frontend, "ApplicationState unusual flag: {} ", state);
    }
    if (!emulation_running) {
        return;
    }
    if (UISettings::values.pause_when_in_background) {
        if (QtCommon::emu_thread->IsRunning() &&
            (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
            auto_paused = true;
            OnPauseGame();
        } else if (!QtCommon::emu_thread->IsRunning() && auto_paused &&
                   (state & Qt::ApplicationActive)) {
            auto_paused = false;
            OnStartGame();
        }
    }
    if (UISettings::values.mute_when_in_background) {
        if (!Settings::values.audio_muted &&
            (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
            Settings::values.audio_muted = true;
            auto_muted = true;
        } else if (auto_muted && (state & Qt::ApplicationActive)) {
            Settings::values.audio_muted = false;
            auto_muted = false;
        }
        UpdateVolumeUI();
    }
}

void MainWindow::ConnectWidgetEvents() {
    connect(game_list, &GameList::ReportCompatibilityRequested, this, [this](u64 program_id) {
        if (!report_session || report_session->program_id != program_id || emulation_running ||
            boot_in_progress || close_pending) {
            return;
        }
        CompatibilityReporting::Dialog dialog(*report_session, this);
        dialog.exec();
    });
    connect(game_list, &GameList::BootGame, this, &MainWindow::BootGameFromList);
    connect(game_list, &GameList::GameChosen, this, &MainWindow::OnGameListLoadFile);
    connect(game_list, &GameList::OpenDirectory, this, &MainWindow::OnGameListOpenDirectory);
    connect(game_list, &GameList::OpenFolderRequested, this, &MainWindow::OnGameListOpenFolder);
    connect(game_list, &GameList::OpenTransferableShaderCacheRequested, this,
            [this](u64 program_id) { QtCommon::Path::OpenShaderCache(program_id, this); });
    connect(game_list, &GameList::RemoveInstalledEntryRequested, this,
            &MainWindow::OnGameListRemoveInstalledEntry);
    connect(game_list, &GameList::RemoveFileRequested, this, &MainWindow::OnGameListRemoveFile);
    connect(game_list, &GameList::RemovePlayTimeRequested, this,
            &MainWindow::OnGameListRemovePlayTimeData);
    connect(game_list, &GameList::SetPlayTimeRequested, this, &MainWindow::OnGameListSetPlayTime);
    connect(game_list, &GameList::DumpRomFSRequested, this, &MainWindow::OnGameListDumpRomFS);
    connect(game_list, &GameList::VerifyIntegrityRequested, this,
            &MainWindow::OnGameListVerifyIntegrity);
    connect(game_list, &GameList::ConvertToNupRequested, this,
            &MainWindow::OnGameListConvertToNup);
    connect(game_list, &GameList::PrepareShaderWarmRequested, this,
            &MainWindow::OnGameListPrepareShaderWarm);
    connect(game_list, &GameList::DeleteShaderWarmRequested, this,
            &MainWindow::OnGameListDeleteShaderWarm);
    connect(game_list, &GameList::PrecompileShadersRequested, this,
            &MainWindow::OnGameListPrecompileShaders);
    connect(game_list, &GameList::ExtractNupRequested, this, &MainWindow::OnGameListExtractNup);
    connect(game_list, &GameList::EditNupMetadataRequested, this,
            &MainWindow::OnGameListEditNupMetadata);
#ifdef LICHII_NX_TOOLS
    nx_tools = new NxTools(QtCommon::vfs, this);
    connect(nx_tools, &NxTools::RunningChanged, game_list, &GameList::SetNxToolRunning);
    connect(nx_tools, &NxTools::ArchivesChanged, this, &MainWindow::OnGameListRefresh);
    connect(nx_tools, &NxTools::ConvertToNupRequested, this, &MainWindow::OnGameListConvertToNup);
    connect(nx_tools, &NxTools::MetadataWriteFinished, this, [this](bool written) {
        nx_metadata_write_in_progress = false;
        // An icon from Game Settings is in the archive now: the local artwork would hide it.
        if (written && nx_artwork_title_id != 0 &&
            QtCommon::Game::RemoveCustomArtwork(nx_artwork_title_id)) {
            game_list->OnCustomArtworkChanged(nx_artwork_title_id);
        }
        nx_artwork_title_id = 0;
        if (!close_pending) {
            QtCommon::Game::ResetMetadata(false);
            game_list->SetScanEnabled(true);
            game_list->PopulateAsync(UISettings::values.game_dirs);
        }
    });
    connect(game_list, &GameList::ConvertToNxRequested, this,
            [this](const std::string& path) { OnGameListNxTool(path, &NxTools::ConvertToNx); });
    connect(game_list, &GameList::AddToNxRequested, this,
            [this](const std::string& path) { OnGameListNxTool(path, &NxTools::AddToNx); });
    connect(game_list, &GameList::RemoveFromNxRequested, this,
            [this](const std::string& path) { OnGameListNxTool(path, &NxTools::RemoveFromNx); });
    connect(game_list, &GameList::EditNxMetadataRequested, this,
            &MainWindow::OnGameListEditNxMetadata);
#endif
    connect(game_list, &GameList::CopyTIDRequested, this, &MainWindow::OnGameListCopyTID);
    connect(game_list, &GameList::CreateShortcut, this, &MainWindow::OnGameListCreateShortcut);
    connect(game_list, &GameList::AddDirectory, this, &MainWindow::OnGameListAddDirectory);
    connect(game_list_placeholder, &GameListPlaceholder::AddDirectory, this,
            &MainWindow::OnGameListAddDirectory);
    connect(game_list, &GameList::ShowList, this, &MainWindow::OnGameListShowList);
    connect(game_list, &GameList::PopulatingCompleted,
            [this] { multiplayer_state->UpdateGameList(game_list->GetModel()); });
    connect(game_list, &GameList::SaveConfig, this, &MainWindow::OnSaveConfig);

    connect(game_list, &GameList::OpenPerGameGeneralRequested, this,
            &MainWindow::OnGameListOpenPerGameProperties);
    connect(game_list, &GameList::LinkToRyujinxRequested, this, &MainWindow::OnLinkToRyujinx);

    connect(this, &MainWindow::UpdateInstallProgress, this, &MainWindow::IncrementInstallProgress);

    // Software Keyboard Applet
    connect(this, &MainWindow::EmulationStarting, this, &MainWindow::SoftwareKeyboardExit);
    connect(this, &MainWindow::EmulationStopping, this, &MainWindow::SoftwareKeyboardExit);

    connect(&status_bar_update_timer, &QTimer::timeout, this, &MainWindow::UpdateStatusBar);

    connect(this, &MainWindow::UpdateThemedIcons, multiplayer_state,
            &MultiplayerState::UpdateThemedIcons);
}

void MainWindow::ConnectMenuEvents() {
    const auto connect_menu = [&]<typename Fn>(QAction* action, const Fn& event_fn) {
        connect(action, &QAction::triggered, this, event_fn);
        // Add actions to this window so that hiding menus in fullscreen won't disable them
        addAction(action);
        // Add actions to the render window so that they work outside of single window mode
        render_window->addAction(action);
    };

    // File
    connect_menu(ui->action_Load_File, &MainWindow::OnMenuLoadFile);
    connect_menu(ui->action_Load_Folder, &MainWindow::OnMenuLoadFolder);
    connect_menu(ui->action_Install_File_NAND, &MainWindow::OnMenuInstallToNAND);
    connect_menu(ui->action_Exit, &QMainWindow::close);
    connect_menu(ui->action_Load_Amiibo, &MainWindow::OnLoadAmiibo);

    // Emulation
    connect_menu(ui->action_Pause, &MainWindow::OnPauseContinueGame);
    connect_menu(ui->action_Stop, &MainWindow::OnStopGame);
    connect_menu(ui->action_Open_Mods_Page, &MainWindow::OnOpenModsPage);
    connect_menu(ui->action_Open_UserHandbook, &MainWindow::OnOpenUserHandbook);
    connect_menu(ui->action_Restart, &MainWindow::OnRestartGame);
    connect_menu(ui->action_Configure, &MainWindow::OnConfigure);
    connect_menu(ui->action_Configure_Current_Game, &MainWindow::OnConfigurePerGame);

    // View
    connect_menu(ui->action_Fullscreen, &MainWindow::ToggleFullscreen);
    connect_menu(ui->action_Single_Window_Mode, &MainWindow::ToggleWindowMode);
    connect_menu(ui->action_Show_Filter_Bar, &MainWindow::OnFocusLibrarySearch);
    connect_menu(ui->action_Show_Status_Bar, &MainWindow::OnToggleStatusBar);
    connect_menu(ui->action_Show_Performance_Overlay, &MainWindow::OnTogglePerfOverlay);
#ifdef HAS_RESHADE
    connect_menu(ui->action_Post_Processing_Shaders, &MainWindow::OnPostProcessingShaders);
#else
    ui->action_Post_Processing_Shaders->setVisible(false);
#endif

    connect_menu(ui->action_Reset_Window_Size_720, &MainWindow::ResetWindowSize720);
    connect_menu(ui->action_Reset_Window_Size_900, &MainWindow::ResetWindowSize900);
    connect_menu(ui->action_Reset_Window_Size_1080, &MainWindow::ResetWindowSize1080);
    ui->menu_Reset_Window_Size->addActions({ui->action_Reset_Window_Size_720,
                                            ui->action_Reset_Window_Size_900,
                                            ui->action_Reset_Window_Size_1080});

    connect_menu(ui->action_Grid_View, &MainWindow::SetGridView);
    connect_menu(ui->action_Tree_View, &MainWindow::SetTreeView);
    connect_menu(ui->action_Carousel_View, &MainWindow::SetCarouselView);

    game_size_actions = new QActionGroup(this);
    game_size_actions->setExclusive(true);

    for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
        const auto size = default_game_icon_sizes[i].first;
        QAction* action = ui->menuGame_Icon_Size->addAction(GetTranslatedGameIconSize(i));
        action->setCheckable(true);
        game_size_actions->addAction(action);

        connect(action, &QAction::triggered, this, [this, size](bool checked) {
            if (checked) {
                UISettings::CurrentGameIconSize().SetValue(size);
                CheckIconSize();

                game_list->UpdateIconSizes();
                game_list->RefreshGameDirectory();
            }
        });
    }

    CheckIconSize();

    ui->action_Show_Game_Name->setChecked(UISettings::values.show_game_name.GetValue());
    connect(ui->action_Show_Game_Name, &QAction::triggered, this, &MainWindow::ToggleShowGameName);

    // Multiplayer
    connect(ui->action_View_Lobby, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnViewLobby);
    connect(ui->action_Start_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCreateRoom);
    connect(ui->action_Leave_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCloseRoom);
    connect(ui->action_Connect_To_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnDirectConnectToRoom);
    connect(ui->action_Show_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnOpenNetworkRoom);
    connect(multiplayer_state, &MultiplayerState::SaveConfig, this, &MainWindow::OnSaveConfig);

    // Tools
    connect_menu(ui->action_Launch_PhotoViewer, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::PhotoViewer), std::nullopt);
    });
    connect_menu(ui->action_Launch_MiiEdit, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::MiiEdit), std::nullopt);
    });
    connect_menu(ui->action_Launch_Controller, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Controller), std::nullopt);
    });
    connect_menu(ui->action_Launch_QLaunch, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::QLaunch), std::nullopt);
    });
    // Tools (cabinet)
    connect_menu(ui->action_Launch_Cabinet_Nickname_Owner, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartNicknameAndOwnerSettings});
    });
    connect_menu(ui->action_Launch_Cabinet_Eraser, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartGameDataEraser});
    });
    connect_menu(ui->action_Launch_Cabinet_Restorer, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartRestorer});
    });
    connect_menu(ui->action_Launch_Cabinet_Formatter, [this] {
        LaunchFirmwareApplet(u64(Service::AM::AppletProgramId::Cabinet),
                             {Service::NFP::CabinetMode::StartFormatter});
    });

    connect_menu(ui->action_Desktop, &MainWindow::OnCreateHomeMenuDesktopShortcut);
    connect_menu(ui->action_Application_Menu, &MainWindow::OnCreateHomeMenuApplicationMenuShortcut);
    connect_menu(ui->action_Capture_Screenshot, &MainWindow::OnCaptureScreenshot);
    connect_menu(ui->action_Report_Bug, &MainWindow::OnReportBug);

    // TAS
    connect_menu(ui->action_TAS_Start, &MainWindow::OnTasStartStop);
    connect_menu(ui->action_TAS_Record, &MainWindow::OnTasRecord);
    connect_menu(ui->action_TAS_Reset, &MainWindow::OnTasReset);
    connect_menu(ui->action_Configure_Tas, &MainWindow::OnConfigureTas);

    // Help
    connect_menu(ui->action_Root_Data_Folder, &MainWindow::OnOpenRootDataFolder);
    connect_menu(ui->action_NAND_Folder, &MainWindow::OnOpenNANDFolder);
    connect_menu(ui->action_SDMC_Folder, &MainWindow::OnOpenSDMCFolder);
    connect_menu(ui->action_Mod_Folder, &MainWindow::OnOpenModFolder);
    connect_menu(ui->action_Log_Folder, &MainWindow::OnOpenLogFolder);

    connect_menu(ui->action_Verify_installed_contents, &MainWindow::OnVerifyInstalledContents);
    connect_menu(ui->action_Firmware_From_Folder, &MainWindow::OnInstallFirmware);
    connect_menu(ui->action_Firmware_From_ZIP, &MainWindow::OnInstallFirmwareFromZIP);
    connect_menu(ui->action_Install_Keys, &MainWindow::OnInstallDecryptionKeys);
    connect_menu(ui->action_Mod_Store, &MainWindow::OnOpenModStore);
#ifdef ENABLE_UPDATE_CHECKER
    connect_menu(ui->action_Check_For_Updates, [this] { CheckForUpdates(true); });
#else
    ui->action_Check_For_Updates->setVisible(false);
#endif
    connect_menu(ui->action_Whats_New, &MainWindow::OnWhatsNew);
    connect_menu(ui->action_About, &MainWindow::OnAbout);
    connect_menu(ui->action_Eden_Dependencies, &MainWindow::OnEdenDependencies);
    connect_menu(ui->action_Apply_Recommended_Settings, &MainWindow::OnApplyRecommendedSettings);
    connect_menu(ui->action_Restore_Previous_Settings, &MainWindow::OnRestorePreviousSettings);
    connect(ui->menu_Help, &QMenu::aboutToShow, this, [this] {
        ui->action_Apply_Recommended_Settings->setEnabled(!emulation_running);
        ui->action_Restore_Previous_Settings->setEnabled(!emulation_running);
        ui->action_Restore_Previous_Settings->setVisible(
            !Settings::values.recommended_settings_backup.GetValue().empty());
    });
    connect_menu(ui->action_Data_Manager, &MainWindow::OnDataDialog);

#if defined(ENABLE_UPDATE_CHECKER) && (defined(__linux__) || defined(_WIN32))
    auto* restore_action = ui->menu_Help->addAction(tr("Restore Previous Version..."));
    restore_action->setVisible(false);
#ifdef __linux__
    const auto previous_version = [] {
        const std::filesystem::path target{qEnvironmentVariable("APPIMAGE").toStdString()};
        std::error_code ec;
        return !target.empty() && std::filesystem::is_regular_file(
                                      UpdateInstaller::PreviousPath(target), ec)
                   ? target
                   : std::filesystem::path{};
    };
#else
    const auto previous_version = [] {
        const std::filesystem::path backup =
            std::filesystem::path{QCoreApplication::applicationDirPath().toStdWString()} /
            UpdateInstaller::BackupName;
        std::error_code ec;
        return std::filesystem::is_directory(backup, ec) ? backup : std::filesystem::path{};
    };
#endif
    connect(ui->menu_Help, &QMenu::aboutToShow, restore_action,
            [restore_action, previous_version] {
                restore_action->setVisible(!previous_version().empty());
            });
    connect(restore_action, &QAction::triggered, this, [this, previous_version] {
        const auto previous = previous_version();
        if (previous.empty()) {
            return;
        }
        const auto answer = QtCommon::Frontend::Question(
            tr("Restore Previous Version"),
            tr("Go back to the version this one replaced? Your settings and data are not "
               "changed."),
            QtCommon::Frontend::Yes | QtCommon::Frontend::No);
        if (answer != QtCommon::Frontend::Yes) {
            return;
        }
#ifdef __linux__
        const UpdateInstaller::Lock lock{UpdateInstaller::LockPath(previous)};
        std::error_code ec;
        if (!lock.Held()) {
            ec = std::make_error_code(std::errc::device_or_resource_busy);
        }
        if (!lock.Held() || !UpdateInstaller::Rollback(previous, ec)) {
            LOG_ERROR(Frontend, "Could not restore the previous version: {}", ec.message());
            QtCommon::Frontend::Critical(tr("Failed to restore"),
                                         tr("Could not restore the previous version: %1")
                                             .arg(QString::fromStdString(ec.message())));
            return;
        }
        LOG_INFO(Frontend, "Restored the previous version of {}", previous.string());
        UpdateDialog::Restart(this, QString::fromStdString(previous.string()));
#else
        UpdateDialog::LaunchInstaller(this, QString::fromStdWString(previous.wstring()));
#endif
    });
#endif
}

void MainWindow::UpdateMenuState() {
    const bool is_paused = QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning();
    const bool is_firmware_available = CheckFirmwarePresence();

    const std::array running_actions{
        ui->action_Stop,
        ui->action_Restart,
        ui->action_Configure_Current_Game,
        ui->action_Load_Amiibo,
        ui->action_Pause,
    };

    const std::array applet_actions{
        ui->action_Launch_PhotoViewer,       ui->action_Launch_Cabinet_Nickname_Owner,
        ui->action_Launch_Cabinet_Eraser,    ui->action_Launch_Cabinet_Restorer,
        ui->action_Launch_Cabinet_Formatter, ui->action_Launch_MiiEdit,
        ui->action_Launch_QLaunch,           ui->action_Launch_Controller};

    for (QAction* action : running_actions) {
        action->setEnabled(emulation_running);
    }
    speed_limit_action->setEnabled(emulation_running);
    turbo_action->setEnabled(emulation_running);
    slow_action->setEnabled(emulation_running);
    ingame_menu_action->setEnabled(emulation_running);

    ui->action_Firmware_From_Folder->setEnabled(!emulation_running);
    ui->action_Firmware_From_ZIP->setEnabled(!emulation_running);
    ui->action_Install_Keys->setEnabled(!emulation_running);
    ui->action_Mod_Store->setEnabled(!emulation_running);

    for (QAction* action : applet_actions) {
        action->setEnabled(is_firmware_available && !emulation_running);
    }

    ui->action_Capture_Screenshot->setEnabled(emulation_running && !is_paused);
    ui->action_Report_Bug->setEnabled(emulation_running && !is_paused);

    if (emulation_running && is_paused) {
        ui->action_Pause->setText(tr("&Continue"));
    } else {
        ui->action_Pause->setText(tr("&Pause"));
    }

    multiplayer_state->UpdateNotificationStatus();
}

void MainWindow::SetupPrepareForSleep() {
#ifdef __unix__
    if (auto bus = QDBusConnection::systemBus(); bus.isConnected()) {
        // See https://github.com/ConsoleKit2/ConsoleKit2/issues/150
#ifdef __linux__
        const auto dbus_logind_service = QStringLiteral("org.freedesktop.login1");
        const auto dbus_logind_path = QStringLiteral("/org/freedesktop/login1");
        const auto dbus_logind_manager_if = QStringLiteral("org.freedesktop.login1.Manager");
        // const auto dbus_logind_session_if = QStringLiteral("org.freedesktop.login1.Session");
#else
        const auto dbus_logind_service = QStringLiteral("org.freedesktop.ConsoleKit");
        const auto dbus_logind_path = QStringLiteral("/org/freedesktop/ConsoleKit/Manager");
        const auto dbus_logind_manager_if = QStringLiteral("org.freedesktop.ConsoleKit.Manager");
        // const auto dbus_logind_session_if = QStringLiteral("org.freedesktop.ConsoleKit.Session");
#endif
        const bool success = bus.connect(dbus_logind_service, dbus_logind_path,
                                         dbus_logind_manager_if, QStringLiteral("PrepareForSleep"),
                                         QStringLiteral("b"), this, SLOT(OnPrepareForSleep(bool)));
        if (!success)
            LOG_WARNING(Frontend, "Couldn't register PrepareForSleep signal");
    } else {
        LOG_WARNING(Frontend, "QDBusConnection system bus is not connected");
    }
#endif // __unix__
}

void MainWindow::OnPrepareForSleep(bool prepare_sleep) {
    if (QtCommon::emu_thread == nullptr)
        return;

    if (prepare_sleep) {
        if (QtCommon::emu_thread->IsRunning()) {
            auto_paused = true;
            OnPauseGame();
        }
    } else {
        if (!QtCommon::emu_thread->IsRunning() && auto_paused) {
            auto_paused = false;
            OnStartGame();
        }
    }
}

#ifdef __unix__
std::array<int, 3> MainWindow::sig_interrupt_fds{0, 0, 0};

void MainWindow::SetupSigInterrupts() {
    if (sig_interrupt_fds[2] == 1) {
        return;
    }
    socketpair(AF_UNIX, SOCK_STREAM, 0, sig_interrupt_fds.data());
    sig_interrupt_fds[2] = 1;

    struct sigaction sa;
    sa.sa_handler = &MainWindow::HandleSigInterrupt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    sig_interrupt_notifier = new QSocketNotifier(sig_interrupt_fds[1], QSocketNotifier::Read, this);
    connect(sig_interrupt_notifier, &QSocketNotifier::activated, this,
            &MainWindow::OnSigInterruptNotifierActivated);
    connect(this, &MainWindow::SigInterrupt, this, &MainWindow::close);
}

void MainWindow::HandleSigInterrupt(int sig) {
    if (sig == SIGINT) {
        _exit(1);
    }

    // Calling into Qt directly from a signal handler is not safe,
    // so wake up a QSocketNotifier with this hacky write call instead.
    char a = 1;
    int ret = write(sig_interrupt_fds[0], &a, sizeof(a));
    (void)ret;
}

void MainWindow::OnSigInterruptNotifierActivated() {
    sig_interrupt_notifier->setEnabled(false);

    char a;
    int ret = read(sig_interrupt_fds[1], &a, sizeof(a));
    (void)ret;

    sig_interrupt_notifier->setEnabled(true);

    emit SigInterrupt();
}
#endif // __unix__

void MainWindow::PreventOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#elif defined(__APPLE__)
    if (sleep_assertion_id == 0) {
        sleep_assertion_id = MacOS::PreventSleep();
    }
#else
    SDL_DisableScreenSaver();
#endif
}

void MainWindow::AllowOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#elif defined(__APPLE__)
    if (sleep_assertion_id != 0) {
        MacOS::AllowSleep(sleep_assertion_id);
        sleep_assertion_id = 0;
    }
#else
    SDL_EnableScreenSaver();
#endif
}

namespace {

/// A choice in a picker card: an icon tile, a title over a detail line, and a caret.
class ChoiceRow final : public QAbstractButton {
public:
    ChoiceRow(QStringView icon_, const QColor& tint_, const QString& title, const QString& detail_)
        : icon{icon_.toString()}, tint{tint_}, detail{detail_} {
        setText(title);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::StrongFocus);
        setFixedHeight(66);
    }

    QSize sizeHint() const override {
        return {320, 66};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool keyboard = hasFocus() && property("keyboardFocus").toBool();
        painter.setPen(
            QPen(keyboard ? Spotlight::Color::Text() : Spotlight::Color::StrokeSubtle(), 1));
        painter.setBrush(underMouse() || isDown() ? Spotlight::Color::Fill()
                                                  : Spotlight::Color::FillFaint());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);

        const qreal ratio = devicePixelRatioF();
        const QRectF tile(15, (height() - 36) / 2.0, 36, 36);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Spotlight::Color::Fill());
        painter.drawRoundedRect(tile, 12, 12);
        painter.drawPixmap(QPointF(tile.left() + 9, tile.top() + 9),
                           Spotlight::IconPixmap(icon, 18, tint, ratio));
        painter.drawPixmap(
            QPointF(width() - 15 - 14, (height() - 14) / 2.0),
            Spotlight::IconPixmap(u"caret-right", 14, Spotlight::Color::TextSecondary(), ratio));

        const QFont title_font = Spotlight::Font(14, 600);
        const QFont detail_font = Spotlight::Font(12, 400);
        const QFontMetrics title_metrics(title_font);
        const QFontMetrics detail_metrics(detail_font);
        const int left = 63;
        const int right = width() - 15 - 14 - 12;
        const int top = (height() - title_metrics.height() - 2 - detail_metrics.height()) / 2;
        painter.setFont(title_font);
        painter.setPen(Spotlight::Color::Text());
        painter.drawText(QRect(left, top, right - left, title_metrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         title_metrics.elidedText(text(), Qt::ElideRight, right - left));
        painter.setFont(detail_font);
        painter.setPen(Spotlight::Color::TextSecondary());
        painter.drawText(
            QRect(left, top + title_metrics.height() + 2, right - left, detail_metrics.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            detail_metrics.elidedText(detail, Qt::ElideRight, right - left));
    }

private:
    QString icon;
    QColor tint;
    QString detail;
};

enum class FirmwareSource { None, Folder, Zip };

/// The Install firmware card: a folder or a ZIP of the user's own console dump.
FirmwareSource AskFirmwareSource(QWidget* parent) {
    Spotlight::Dialog dialog(parent);
    dialog.setWindowTitle(MainWindow::tr("Install firmware"));
    dialog.SetSubtitle(MainWindow::tr("From your own console dump"));
    dialog.SetIcon(u"cpu", Spotlight::Color::Accent());
    dialog.SetCardWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* folder =
        new ChoiceRow(u"folder", Spotlight::Color::Success(), MainWindow::tr("From a folder"),
                      MainWindow::tr("The folder with the .nca files"));
    auto* zip = new ChoiceRow(u"archive", Spotlight::Color::Purple(), MainWindow::tr("From a ZIP"),
                              MainWindow::tr("A zipped firmware dump"));
    QObject::connect(folder, &QAbstractButton::clicked, &dialog, [&dialog] { dialog.done(1); });
    QObject::connect(zip, &QAbstractButton::clicked, &dialog, [&dialog] { dialog.done(2); });
    layout->addWidget(folder);
    layout->addWidget(zip);
    switch (dialog.exec()) {
    case 1:
        return FirmwareSource::Folder;
    case 2:
        return FirmwareSource::Zip;
    default:
        return FirmwareSource::None;
    }
}

} // namespace

bool MainWindow::LoadROM(const QString& filename, Service::AM::FrontendAppletParameters params) {
    // Shutdown previous session if the emu thread is still active...
    if (QtCommon::emu_thread != nullptr)
        ShutdownGame(tr("The next game starts when this finishes"));

    if (!render_window->InitRenderTarget())
        return false;

    QtCommon::system->SetFilesystem(QtCommon::vfs);

    if (params.launch_type == Service::AM::LaunchType::FrontendInitiated)
        QtCommon::system->GetUserChannel().clear();

    QtCommon::system->SetFrontendAppletSet({
        std::make_unique<QtAmiiboSettings>(*this), // Amiibo Settings
        (Settings::values.controller_applet_mode.GetValue() == Settings::AppletMode::Disabled)
            ? nullptr
            : std::make_unique<QtControllerSelector>(*this), // Controller Selector
        std::make_unique<QtErrorDisplay>(*this),             // Error Display
        nullptr,                                             // Mii Editor
        nullptr,                                             // Parental Controls
        nullptr,                                             // Photo Viewer
        std::make_unique<QtProfileSelector>(*this),          // Profile Selector
        std::make_unique<QtSoftwareKeyboard>(*this),         // Software Keyboard
        std::make_unique<QtWebBrowser>(*this),               // Web Browser
        nullptr,                                             // Net Connect
    });

    /** firmware check */

    if (FirmwareManager::GameRequiresFirmware(params.program_id) && !CheckFirmwarePresence()) {
        QMessageBox box(QMessageBox::Warning, tr("This game needs firmware"),
                        tr("This game may use system files from firmware, and none is installed. "
                           "Install firmware, or start without it."),
                        QMessageBox::NoButton, this);
        QPushButton* start = box.addButton(tr("Start anyway"), QMessageBox::ResetRole);
        start->setProperty("kind", QStringLiteral("ghost"));
        start->setStyleSheet(QStringLiteral("min-height: 34px; font-size: 13px; color: #ffffff;"));
        QPushButton* install = box.addButton(tr("Install firmware\u2026"), QMessageBox::AcceptRole);
        box.setDefaultButton(install);
        box.exec();
        if (box.clickedButton() != start) {
            if (box.clickedButton() == install) {
                switch (AskFirmwareSource(this)) {
                case FirmwareSource::Folder:
                    OnInstallFirmware();
                    break;
                case FirmwareSource::Zip:
                    OnInstallFirmwareFromZIP();
                    break;
                case FirmwareSource::None:
                    break;
                }
            }
            return false;
        }
    }

    /** Exec */
    const Core::SystemResultStatus result{
        QtCommon::system->Load(*render_window, filename.toStdString(), params)};

    if (result != Core::SystemResultStatus::Success) {
        switch (result) {
        case Core::SystemResultStatus::ErrorMemoryAllocation:
            QMessageBox::critical(
                this, tr("Not enough memory to start the game"),
                tr("Lichii could not allocate the game's memory. Close other applications and "
                   "check that your system has enough available memory and paging-file space, "
                   "then try again."));
            break;
        case Core::SystemResultStatus::ErrorGetLoader:
            LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", filename.toStdString());
            QMessageBox::critical(this, tr("Error while loading ROM!"),
                                  tr("The ROM format is not supported."));
            break;
        case Core::SystemResultStatus::ErrorVideoCore:
            QMessageBox::critical(
                this, tr("An error occurred initializing the video core."),
                tr("This is usually caused by outdated GPU drivers. "
                   "Please see the log for more details. See: "
                   "<a href='https://git.eden-emu.dev/eden-emu/eden/src/branch/master/docs/user/HowToAccessLogs.md'>"
                   "How to access log files</a>."));
            break;
        default:
            if (result > Core::SystemResultStatus::ErrorLoader) {
                const u16 loader_id = static_cast<u16>(Core::SystemResultStatus::ErrorLoader);
                const u16 error_id = static_cast<u16>(result) - loader_id;
                const std::string error_code = fmt::format("({:04X}-{:04X})", loader_id, error_id);
                LOG_CRITICAL(Frontend, "Failed to load ROM! {}", error_code);

                const auto title =
                    tr("Error while loading ROM! %1", "%1 signifies a numeric error code.")
                        .arg(QString::fromStdString(error_code));
                const auto status = static_cast<Loader::ResultStatus>(error_id);
                const auto description =
                    (Loader::IsMissingKeyStatus(status)
                         ? tr("%1<br>Dump current keys from your own console and install them "
                              "with Tools > Install Decryption Keys, then start the game again. "
                              "The log names the missing key.",
                              "%1 signifies an error string.")
                         : tr("%1<br>Please redump your files or ask on Discord/Stoat for help.",
                              "%1 signifies an error string."))
                        .arg(QString::fromStdString(GetResultStatusString(status)));

                QMessageBox::critical(this, title, description);
            } else {
                QMessageBox::critical(
                    this, tr("Error while loading ROM!"),
                    tr("An unknown error occurred. Please see the log for more details."));
            }
            break;
        }
        return false;
    }
    current_game_path = filename;

    return true;
}

bool MainWindow::SelectAndSetCurrentUser(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    QtProfileSelectionDialog dialog(*QtCommon::system, this, parameters);
    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::WindowModal);

    if (dialog.exec() == QDialog::Rejected) {
        return false;
    }

    Settings::values.current_user = dialog.GetIndex();
    return true;
}

bool MainWindow::ConfirmLowRamBoot(const QString& filename,
                                   const Service::AM::FrontendAppletParameters& params) {
    const auto ram = Common::GetHostRam();
    if (!UISettings::values.show_low_ram_warning.GetValue() ||
        !Common::IsBelowSwitch2RamMinimum(ram) ||
        !Core::FindSwitch2Program(
            Core::GetGameFileFromPath(QtCommon::vfs, filename.toUtf8().constData()),
            params.program_id, params.program_index)) {
        return true;
    }

    QMessageBox box(QMessageBox::Warning, tr("Not enough RAM"),
                    tr("This PC has %1 GB of RAM. Switch 2 games need at least 16 GB, and 32 GB "
                       "is recommended.\n\nGames can crash, and can even freeze or crash the "
                       "whole PC.\n\nThis setup isn't supported, so please don't open issues or "
                       "ask for help about crashes on it.")
                        .arg(static_cast<double>(ram.bytes) / 1_GiB, 0, 'g', 3),
                    QMessageBox::NoButton, this);
    QPushButton* const continue_button =
        box.addButton(tr("Continue anyway"), QMessageBox::AcceptRole);
    QPushButton* const cancel_button = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel_button);
    box.setEscapeButton(cancel_button);
    QCheckBox* const dont_show = new QCheckBox(tr("Don't show this again"), &box);
    box.setCheckBox(dont_show);
    box.exec();

    if (dont_show->isChecked()) {
        UISettings::values.show_low_ram_warning.SetValue(false);
    }
    return box.clickedButton() == continue_button;
}

void MainWindow::BootGame(const QString& filename, Service::AM::FrontendAppletParameters params,
                          StartGameType type) {
    if (shutdown_game_in_progress) {
        return;
    }

#ifdef HAS_FLYHIGH
    // Frame generation was turned on in the settings, where nothing offers the model: offer it
    // once before the first game.
    static bool flyhigh_offered = false;
    if (!flyhigh_offered && Settings::values.frame_gen.GetValue() &&
        Settings::values.frame_gen_type.GetValue() == Settings::FrameGenType::FlyHigh) {
        flyhigh_offered = true;
        OfferFlyHighModel(this);
    }
#endif
#ifdef LICHII_NX_TOOLS
    if (nx_metadata_write_in_progress) {
        QMessageBox::information(this, tr("NX Archive"),
                                 tr("Wait for the NX metadata edit to finish before starting a game."));
        return;
    }
#endif
    if (nup_running && filename.toStdString() == nup_source_path) {
        QMessageBox::information(this, tr("Conversion in Progress"),
                                 tr("This game cannot be started while its dump is being "
                                    "converted."));
        return;
    }
    if (shader_warm_dialog && shader_warm_dialog->IsRunning()) {
        QMessageBox::information(this, tr("Shader Precache in Progress"),
                                 tr("Games cannot be started while the shader precache is being "
                                    "built. Wait for it to finish or cancel it."));
        shader_warm_dialog->raise();
        shader_warm_dialog->activateWindow();
        return;
    }
    if (boot_in_progress || close_pending || mod_store_open) {
        return;
    }
    if (whats_new_dialog) {
        whats_new_dialog->close();
    }
    disconnect(startup_qlaunch_connection);
    boot_in_progress = true;
    game_list->SetScanEnabled(false);
    QElapsedTimer boot_timer;
    boot_timer.start();
    SCOPE_EXIT {
        boot_in_progress = false;
        LOG_INFO(Frontend, "Launch finished: elapsed_ms={} running={}", boot_timer.elapsed(),
                 emulation_running);
        if (!emulation_running && !close_pending) {
            game_list->SetScanEnabled(true);
            game_list->PopulateAsync(UISettings::values.game_dirs);
            if (startup_whats_new_pending) {
                QTimer::singleShot(0, this, &MainWindow::MaybeShowStartupWhatsNew);
            }
        }
        if (close_pending) {
            QTimer::singleShot(0, this, &QWidget::close);
        }
    };
    if (emulation_running) {
        ShutdownGame();
    }
    if (close_pending) {
        return;
    }
    LOG_INFO(Frontend, "Lichii starting...");
    report_session.reset();
    game_list->SetReportableProgramId(0);

    if (params.program_id == 0 ||
        params.program_id > static_cast<u64>(Service::AM::AppletProgramId::MaxProgramId)) {
        StoreRecentFile(filename); // Put the filename on top of the list
    }

    if (params.launch_type == Service::AM::LaunchType::FrontendInitiated &&
        !ConfirmLowRamBoot(filename, params)) {
        return;
    }

    // Save configurations
    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    config->SaveAllValues();

    u64 title_id{0};

    last_filename_booted = filename;

    QtCommon::Content::configureFilesystemProvider(filename.toStdString());
    const auto v_file = Core::GetGameFileFromPath(QtCommon::vfs, filename.toUtf8().constData());
    const auto loader =
        Loader::GetLoader(*QtCommon::system, v_file, params.program_id, params.program_index);

    if (loader != nullptr && loader->ReadProgramId(title_id) == Loader::ResultStatus::Success &&
        type == StartGameType::Normal) {
        // Load per game settings
        const auto file_path =
            std::filesystem::path{Common::U16StringFromBuffer(filename.utf16(), filename.size())};
        const auto config_file_name = title_id == 0
                                          ? Common::FS::PathToUTF8String(file_path.filename())
                                          : fmt::format("{:016X}", title_id);
        QtConfig per_game_config(config_file_name, Config::ConfigType::PerGameConfig);
        QtCommon::system->HIDCore().ReloadInputDevices();
        QtCommon::system->ApplySettings();
    }

    PrepareShaderPrecache(title_id, std::filesystem::path{filename.toStdU16String()});
    speed_limit_before_hotkeys.reset();
    Settings::LogSettings();

    if (UISettings::values.select_user_on_boot && !user_flag_cmd_line) {
        const Core::Frontend::ProfileSelectParameters parameters{
            .mode = Service::AM::Frontend::UiMode::UserSelector,
            .invalid_uid_list = {},
            .display_options = {},
            .purpose = Service::AM::Frontend::UserSelectionPurpose::General,
        };
        if (SelectAndSetCurrentUser(parameters) == false) {
            return;
        }
    }

    // If the user specifies -u (successfully) on the cmd line, don't prompt for a user on first
    // game startup only. If the user stops emulation and starts a new one, go back to the expected
    // behavior of asking.
    user_flag_cmd_line = false;

    FileSys::VirtualFile prepared_romfs;
    if (loader && loader->GetFileType() == Loader::FileType::DeconstructedRomDirectory) {
        QProgressDialog progress(tr("Preparing game files..."), QString{}, 0, 0, this);
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setWindowFlag(Qt::WindowCloseButtonHint, false);
        progress.setCancelButton(nullptr);
        progress.setMinimumDuration(0);
        QFutureWatcher<FileSys::VirtualFile> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<FileSys::VirtualFile>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&loader] {
            FileSys::VirtualFile image;
            loader->ReadRomFS(image);
            return image;
        }));
        progress.show();
        loop.exec();
        try {
            prepared_romfs = watcher.result();
        } catch (const std::exception& error) {
            LOG_ERROR(Frontend, "Loose RomFS preparation failed: {}", error.what());
            QMessageBox::critical(this, tr("Unable to prepare game files"),
                                  tr("Game preparation failed. See the log for details."));
            return;
        }
        if (close_pending) {
            return;
        }
    }

    if (loader && Common::Nextendo::OnlinePlayActive()) {
        QProgressDialog progress(tr("Checking online patches..."), QString{}, 0, 0, this);
        progress.setWindowModality(Qt::ApplicationModal);
        progress.setWindowFlag(Qt::WindowCloseButtonHint, false);
        progress.setCancelButton(nullptr);
        progress.setMinimumDuration(0);
        QFutureWatcher<void> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([&loader, title_id] {
            Common::NplnPatchCatalog::PrepareForGame(
                title_id, [&] { return loader->ReadMainBuildIdHex(*QtCommon::system); });
            Service::BCAT::Nextendo::PrepareForGame(title_id);
        }));
        progress.show();
        loop.exec();
        if (close_pending) {
            return;
        }
    }

    QElapsedTimer load_timer;
    load_timer.start();
    if (!LoadROM(filename, params)) {
        LOG_ERROR(Frontend, "Core load failed: elapsed_ms={}", load_timer.elapsed());
        return;
    }
    LOG_INFO(Frontend, "Core load completed: elapsed_ms={}", load_timer.elapsed());

    const auto report_program_id = QtCommon::system->GetApplicationProcessProgramID();
    if (Settings::CurrentProgramIsSwitch2() && report_program_id != 0) {
        auto& session = report_session.emplace();
        session.program_id = report_program_id;
        const FileSys::PatchManager pm(report_program_id,
                                       QtCommon::system->GetFileSystemController(),
                                       QtCommon::system->GetContentProvider());
        if (const auto metadata = pm.GetControlMetadata(); metadata.first) {
            session.title = QString::fromStdString(metadata.first->GetApplicationName());
            session.game_version = QString::fromStdString(metadata.first->GetVersionString());
        }
        if (session.title.trimmed().isEmpty()) {
            std::string title;
            if (QtCommon::system->GetGameName(title) == Loader::ResultStatus::Success) {
                session.title = QString::fromStdString(title);
            }
        }
        FileSys::VirtualFile update_raw;
        auto& report_loader = QtCommon::system->GetAppLoader();
        report_loader.ReadUpdateRaw(update_raw);
        QList<CompatibilityReporting::Addon> mods;
        QList<CompatibilityReporting::Addon> updates;
        for (const auto& patch : pm.GetPatches(update_raw)) {
            auto* entries = patch.type == FileSys::PatchType::Mod      ? &mods
                            : patch.type == FileSys::PatchType::Update ? &updates
                                                                       : nullptr;
            if (entries) {
                entries->push_back({QString::fromStdString(patch.name),
                                    QString::fromStdString(patch.version), patch.enabled});
            }
        }
        if (report_loader.GetFileType() == Loader::FileType::NUP) {
            std::string version;
            if (report_loader.ReadDisplayVersion(version) == Loader::ResultStatus::Success &&
                !version.empty()) {
                session.game_version = QString::fromStdString(version);
                updates.push_back(
                    {QStringLiteral("Loaded NUP package"), session.game_version, true});
            }
        }
        session.mods = CompatibilityReporting::SummarizeEnabledAddons(mods);
        session.updates = CompatibilityReporting::SummarizeEnabledAddons(updates);
        session.build = QStringLiteral("Lichii %1 (%2)")
                            .arg(QString::fromUtf8(Common::g_build_version),
                                 QString::fromUtf8(Common::g_scm_rev).left(12));
#ifdef ARCHITECTURE_x86_64
        session.cpu = QString::fromLatin1(Common::g_cpu_caps.cpu_string,
                                          sizeof(Common::g_cpu_caps.cpu_string))
                          .section(QChar{}, 0, 0)
                          .trimmed();
#endif
        session.os = QSysInfo::prettyProductName();
        const auto memory = Common::GetMemInfo().TotalPhysicalMemory;
        if (memory != 0) {
            session.ram =
                QStringLiteral("%1 GiB").arg(double(memory) / (1024 * 1024 * 1024), 0, 'f', 1);
        }
        const auto& device_info = QtCommon::system->Renderer().GetDeviceInfo();
        session.gpu = QString::fromStdString(device_info.model);
        session.driver = QString::fromStdString(device_info.driver);
        session.graphics_api = QString::fromStdString(device_info.api);
        session.settings =
            QStringLiteral("Launch settings: %1 mode; resolution scale %2x; "
                           "GPU accuracy %3; asynchronous shaders %4.")
                .arg(Settings::IsDockedMode() ? QStringLiteral("Docked")
                                              : QStringLiteral("Handheld"))
                .arg(Settings::values.resolution_info.up_factor)
                .arg(Settings::values.gpu_accuracy.GetValue() == Settings::GpuAccuracy::High
                         ? QStringLiteral("High")
                         : QStringLiteral("Low"))
                .arg(Settings::values.use_asynchronous_shaders.GetValue() ? QStringLiteral("on")
                                                                          : QStringLiteral("off"));
    }

    QtCommon::system->SetShuttingDown(false);
    game_list->setDisabled(true);

    // Create and start the emulation thread
    QtCommon::emu_thread = std::make_unique<EmuThread>();
    emit EmulationStarting();
    QtCommon::emu_thread->start();

    // Register an ExecuteProgram callback such that Core can execute a sub-program
    QtCommon::system->RegisterExecuteProgramCallback(
        [this](std::size_t program_index_) { render_window->ExecuteProgram(program_index_); });

    QtCommon::system->RegisterLaunchInNewSessionCallback([this](u64 program_id) {
        QMetaObject::invokeMethod(
            this, [this, program_id] { OnLaunchInNewSession(program_id); },
            Qt::QueuedConnection);
    });

    QtCommon::system->RegisterExitCallback([this] {
        QtCommon::emu_thread->ForceStop();
        render_window->Exit();
    });

    QtCommon::system->RegisterApplicationChangedCallback([this](u64 changed_program_id) {
        if (QtCommon::emu_thread)
            QtCommon::emu_thread->RequestDiskShaderCacheReload(changed_program_id);

        QMetaObject::invokeMethod(
            this, [this, changed_program_id] { this->OnApplicationChanged(changed_program_id); },
            Qt::QueuedConnection);
    });

    connect(render_window, &GRenderWindow::Closed, this, &MainWindow::OnStopGame);
    connect(render_window, &GRenderWindow::MouseActivity, this, &MainWindow::OnMouseActivity);

    connect(QtCommon::emu_thread.get(), &EmuThread::LoadProgress, loading_screen,
            &LoadingScreen::OnLoadProgress, Qt::QueuedConnection);

    connect(
        QtCommon::emu_thread.get(), &EmuThread::ShaderCacheReloadStarted, this,
        [this] {
            loading_screen->Prepare(QtCommon::system->GetAppLoader());
            loading_screen->show();
            render_window->hide();
            OnToggleStatusBar();
        },
        Qt::QueuedConnection);

    connect(
        QtCommon::emu_thread.get(), &EmuThread::ShaderCacheReloadFinished, this,
        [this] {
            loading_screen->OnLoadComplete();
        },
        Qt::QueuedConnection);

    // Update the GUI
    UpdateStatusButtons();
    if (ui->action_Single_Window_Mode->isChecked()) {
        launcher->SetInlineEmulation(true);
    }
    status_bar_update_timer.start(500);
    renderer_status_action->setDisabled(true);
    frame_gen_status_action->setDisabled(true);
    frame_gen_state = VideoCore::FrameGenState::Pending;
    frame_gen_frames = 0;
    frame_gen_generating = false;
    frame_gen_fps = 0.0;
    frame_gen_fps_timer.invalidate();
    refresh_action->setDisabled(true);
    SetFPSSuffix();

    if (UISettings::values.hide_mouse || Settings::values.mouse_panning) {
        render_window->installEventFilter(render_window);
        render_window->setAttribute(Qt::WA_Hover, true);
    }

    if (UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
    }

    render_window->InitializeCamera();

    std::string title_name;
    std::string title_version;
    const auto res = QtCommon::system->GetGameName(title_name);

    const auto metadata = [title_id] {
        const FileSys::PatchManager pm(title_id, QtCommon::system->GetFileSystemController(),
                                       QtCommon::system->GetContentProvider());
        return pm.GetControlMetadata();
    }();
    if (metadata.first != nullptr) {
        title_version = metadata.first->GetVersionString();
        title_name = metadata.first->GetApplicationName();
    } else {
        QtCommon::system->GetAppLoader().ReadDisplayVersion(title_version);
    }
    if (res != Loader::ResultStatus::Success || title_name.empty()) {
        title_name = Common::FS::PathToUTF8String(
            std::filesystem::path{Common::U16StringFromBuffer(filename.utf16(), filename.size())}
                .filename());
    }
    // The name the user gave the game in Properties wins, as in the library.
    if (auto custom_title = QtCommon::Game::ReadCustomTitle(title_id); !custom_title.empty()) {
        title_name = std::move(custom_title);
    }
    const QString display_title = QString::fromStdString(title_name);
    const bool is_64bit = QtCommon::system->Kernel().ApplicationProcess()->Is64Bit();
    const auto instruction_set_suffix = is_64bit ? tr("(64-bit)") : tr("(32-bit)");
    title_name = tr("%1 %2", "%1 is the title name. %2 indicates if the title is 64-bit or 32-bit")
                     .arg(QString::fromStdString(title_name), instruction_set_suffix)
                     .toStdString();
    LOG_INFO(Frontend, "Booting game: {:016X} | {} | {}", title_id, title_name, title_version);
    const auto gpu_vendor = QtCommon::system->GPU().Renderer().GetDeviceVendor();
    UpdateWindowTitle(title_name, title_version, gpu_vendor);
    running_title_bar->SetGameTitle(display_title);
    current_game_title = display_title;

    loading_screen->Prepare(QtCommon::system->GetAppLoader(), display_title,
                            QString::fromStdString(title_version));
    loading_screen->show();

    emulation_running = true;
    fullscreen_hint_shown = false;
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    } else if (!(ui->action_Single_Window_Mode->isChecked() ? isMaximized()
                                                            : render_window->isMaximized())) {
        if (Settings::IsDockedMode()) {
            ResetWindowSize1080();
        } else {
            ResetWindowSize720();
        }
    }
    CheckNextendoOnlineStatus();
    CheckNextendoGameVersion(title_id, title_version);
    nextendo_social_timer.start(Common::Nextendo::FriendsRefreshInterval);
    UpdateNextendoSocial();
    OnStartGame();
}

void MainWindow::BootGameFromList(const QString& filename, StartGameType with_config) {
    BootGame(filename, ApplicationAppletParameters(), with_config);
}

void MainWindow::RememberSpeedLimitBeforeHotkey() {
    if (!speed_limit_before_hotkeys) {
        speed_limit_before_hotkeys = SessionSpeedLimit{
            .value = Settings::values.use_speed_limit.GetValue(),
            .using_global = Settings::values.use_speed_limit.UsingGlobal(),
        };
    }
}

void MainWindow::UndoSessionSpeedChanges() {
    // Undo the session's frame rate limit hotkey and turbo/slow modes. Only what the hotkeys
    // changed is put back, in the scope (global or per-game) it was changed in: forcing the
    // limit on here saved "Limit Speed Percent" as checked after every game, whatever the user
    // had chosen.
    // A configuration applied in a dialog that is still open replaces the hotkeys' change too.
    if (speed_limit_before_hotkeys && !UISettings::values.configuration_applied &&
        Settings::values.use_speed_limit.UsingGlobal() == speed_limit_before_hotkeys->using_global) {
        Settings::values.use_speed_limit.SetValue(speed_limit_before_hotkeys->value);
    }
    speed_limit_before_hotkeys.reset();
    Settings::values.current_speed_mode = Settings::SpeedMode::Standard;
}

bool MainWindow::OnShutdownBegin() {
    if (!emulation_running) {
        return false;
    }

    if (ui->action_Fullscreen->isChecked()) {
        HideFullscreen();
    }
    ingame_bar->hide();
    ingame_menu->hide();

    AllowOSSleep();

    UndoSessionSpeedChanges();

    if (QtCommon::system->IsShuttingDown()) {
        status_bar_update_timer.stop();
        connect(QtCommon::emu_thread.get(), &QThread::finished, this,
                &MainWindow::OnEmulationStopped, Qt::UniqueConnection);
        return false;
    }

    if (perf_overlay) {
        perf_overlay->hide();
        perf_overlay->deleteLater();
        perf_overlay = nullptr;
    }

    QtCommon::system->SetShuttingDown(true);
    status_bar_update_timer.stop();
    LOG_INFO(Frontend, "Shutdown requested; waiting for emulation thread");
    discord_rpc->Pause();

    RequestGameExit();
    QtCommon::emu_thread->disconnect();
    QtCommon::emu_thread->SetRunning(true);

    connect(QtCommon::emu_thread.get(), &QThread::finished, this, &MainWindow::OnEmulationStopped);
    emit EmulationStopping();

    int shutdown_time = 1000;

    if (QtCommon::system->DebuggerEnabled()) {
        shutdown_time = 0;
    } else if (QtCommon::system->GetExitLocked()) {
        shutdown_time = 5000;
    }

    shutdown_timer.setSingleShot(true);
    shutdown_timer.start(shutdown_time);
    connect(&shutdown_timer, &QTimer::timeout, this, &MainWindow::OnEmulationStopTimeExpired,
            Qt::UniqueConnection);

    // Disable everything to prevent anything from being triggered here
    ui->action_Pause->setEnabled(false);
    ui->action_Restart->setEnabled(false);
    ui->action_Stop->setEnabled(false);

    return true;
}

void MainWindow::OnShutdownBeginDialog(const QString& next) {
    shutdown_dialog = new Spotlight::StoppingOverlay(render_window, next, this);
}

/// Ends the process at once: no destructors, no atexit handlers, no flush of C streams.
[[noreturn]] static void EndProcessNow(int exit_code) {
#ifdef _WIN32
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(exit_code));
#endif
    std::_Exit(exit_code);
}

static s64 SteadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// When set (steady clock ms), the exit watchdog ends the process once it is reached.
static std::atomic<s64> exit_deadline_ms{0};

/// Started at the first forced stop, while no lock can be stuck yet: arming it later (see
/// ExitIfEmulationThreadStuck) only stores a deadline and allocates nothing.
static void StartExitWatchdog() {
    static std::once_flag started;
    std::call_once(started, [] {
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const s64 deadline = exit_deadline_ms.load(std::memory_order_relaxed);
                if (deadline != 0 && SteadyNowMs() >= deadline) {
                    EndProcessNow(1);
                }
            }
        }).detach();
    });
}

void MainWindow::OnEmulationStopTimeExpired() {
    if (QtCommon::emu_thread) {
        StartExitWatchdog();
        QtCommon::emu_thread->ForceStop();
        emu_force_stop_clock.start();
        shutdown_progress_seen = Common::ShutdownProgress();
        emu_terminate_timer.setSingleShot(true);
        emu_terminate_timer.start(EmuThreadStopTimeoutMs);
        connect(&emu_terminate_timer, &QTimer::timeout, this, &MainWindow::OnEmulationStopTimeout,
                Qt::UniqueConnection);
    }
}

bool MainWindow::EmulationShutdownProgressing() {
    const u64 progress = Common::ShutdownProgress();
    const bool moved = progress != shutdown_progress_seen;
    shutdown_progress_seen = progress;
    return moved && emu_force_stop_clock.isValid() &&
           emu_force_stop_clock.elapsed() < MaxEmuThreadShutdownMs;
}

void MainWindow::OnEmulationStopTimeout() {
    // While the teardown still moves (pipeline caches written, pipelines destroyed), it gets
    // another timeout. A stalled teardown ends the process (see ExitIfEmulationThreadStuck).
    if (QtCommon::emu_thread && QtCommon::emu_thread->isRunning() &&
        EmulationShutdownProgressing()) {
        LOG_INFO(Frontend, "Emulation thread still shutting down after {} ms, waiting",
                 emu_force_stop_clock.elapsed());
        emu_terminate_timer.start(EmuThreadStopTimeoutMs);
        return;
    }
    ExitIfEmulationThreadStuck();
    OnEmulationStopped();
}

/// Called when the stop wait is over. Returns if the emulation thread has stopped. If it still
/// runs, saves the settings and ends the process with all threads alive, so the program closes
/// from Stop as well as from the window close. Terminating the thread instead can orphan a lock
/// it holds (the process heap's, a driver's), after which every allocation in the process hangs.
void MainWindow::ExitIfEmulationThreadStuck() {
    if (!QtCommon::emu_thread || !QtCommon::emu_thread->isRunning()) {
        return;
    }
    // The stuck thread can hold a lock that the save needs. The save gets this long.
    exit_deadline_ms.store(SteadyNowMs() + 5'000, std::memory_order_relaxed);
    LOG_ERROR(Frontend,
              "Emulation thread failed to stop after {} ms (shutdown progress {}); saving "
              "settings and exiting",
              emu_force_stop_clock.elapsed(), Common::ShutdownProgress());
    play_time_manager->Stop();
    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    UISettings::SaveWindowState();
    hotkey_registry.SaveHotkeys();
    config->SaveAllValues();
    LOG_INFO(Frontend, "Settings saved; exiting without emulation cleanup");
    Common::Log::Stop();
    EndProcessNow(0);
}

void MainWindow::OnEmulationStopped() {
    if (!emulation_running) {
        return;
    }
    if (QtCommon::emu_thread && QtCommon::emu_thread->isRunning()) {
        return;
    }
    if (bug_report_dialog) {
        bug_report_dialog->StopCapture();
    }
    UndoSessionSpeedChanges();
    shutdown_timer.stop();
    emu_terminate_timer.stop();
    if (QtCommon::emu_thread) {
        QtCommon::emu_thread->disconnect();
        QtCommon::emu_thread->wait();
        QtCommon::emu_thread.reset();
    }

    if (shutdown_dialog) {
        shutdown_dialog->deleteLater();
        shutdown_dialog = nullptr;
    }

    emulation_running = false;
    nextendo_version_check.reset();

    nextendo_social_timer.stop();
    if (Common::Nextendo::OnlinePlayActive()) {
        const auto post_offline =
            [client_id = Settings::values.nextendo_client_id.GetValue(),
             profile = Service::Account::Nextendo::CurrentProfile(*QtCommon::system)] {
                void(QtConcurrent::run([client_id, profile] {
                    void(Common::Nextendo::UpdatePresence(client_id, profile, 0));
                }));
            };
        // A refresh still running would post playing after the offline one.
        if (nextendo_social_watcher.isRunning()) {
            connect(&nextendo_social_watcher, &QFutureWatcher<void>::finished, this, post_offline,
                    Qt::SingleShotConnection);
        } else {
            post_offline();
        }
    }
    SyncNextendoProfile(false);

    game_list->SetReportableProgramId(report_session ? report_session->program_id : 0);

    discord_rpc->Update();
    Common::FeralGamemode::Stop();

    // The emulation is stopped, so closing the window or not does not matter anymore
    disconnect(render_window, &GRenderWindow::Closed, this, &MainWindow::OnStopGame);

    // Update the GUI
    UpdateMenuState();

    render_window->hide();
    loading_screen->hide();
    loading_screen->Clear();
    launcher->SetInlineEmulation(false);
    tas_action->setText({});
    input_subsystem->GetTas()->Stop();
    OnTasStateChanged();
    render_window->FinalizeCamera();

    QtCommon::system->GetFrontendAppletHolder().SetCurrentAppletId(Service::AM::AppletId::None);

    // Enable all controllers
    QtCommon::system->HIDCore().SetSupportedStyleTag({Core::HID::NpadStyleSet::All});

    render_window->removeEventFilter(render_window);
    render_window->setAttribute(Qt::WA_Hover, false);

    UpdateWindowTitle();

    // Disable status bar updates
    status_bar_update_timer.stop();
    ingame_bar->hide();
    ingame_menu->hide();
    ingame_bar->Shaders()->SetProgress(0, 0);
    ingame_bar->SetStats(0, 0, 0, false, {});
    session_status_text.clear();
    renderer_status_action->setEnabled(!UISettings::values.has_broken_vulkan);
    frame_gen_status_action->setEnabled(true);
    refresh_action->setEnabled(true);

    current_game_path.clear();

    // When closing the game, destroy the GLWindow to clear the context after the game is closed
    render_window->ReleaseRenderTarget();

    // Enable game list
    game_list->setEnabled(true);

    Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
    QtCommon::system->HIDCore().ReloadInputDevices();
    UpdateStatusButtons();
    LOG_INFO(Frontend, "Emulation cleanup completed");
    if (close_pending) {
        QTimer::singleShot(0, this, &QWidget::close);
    } else if (!boot_in_progress) {
        game_list->SetScanEnabled(true);
        game_list->PopulateAsync(UISettings::values.game_dirs);
        if (startup_whats_new_pending) {
            QTimer::singleShot(0, this, &MainWindow::MaybeShowStartupWhatsNew);
        }
    }
}

void MainWindow::ShutdownGame(const QString& next) {
    if (!emulation_running || shutdown_game_in_progress) {
        return;
    }
    if (!shutdown_dialog) {
        OnShutdownBeginDialog(next);
    }

    shutdown_game_in_progress = true;
    SCOPE_EXIT {
        shutdown_game_in_progress = false;
    };

    if (bug_report_dialog) {
        bug_report_dialog->StopCapture();
    }
    // TODO(crueter): make this common as well (frontend_common?)
    play_time_manager->Stop();
    OnShutdownBegin();
    OnEmulationStopTimeExpired();
    shutdown_timer.stop();
    emu_terminate_timer.stop();
    if (QtCommon::emu_thread && QtCommon::emu_thread->isRunning()) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(QtCommon::emu_thread.get(), &QThread::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        // Waits on while the teardown still makes progress (see OnEmulationStopTimeout).
        do {
            timeout.start(EmuThreadStopTimeoutMs);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
            // OnEmulationStopped (on QThread::finished) may have reset the thread meanwhile.
        } while (QtCommon::emu_thread && QtCommon::emu_thread->isRunning() &&
                 EmulationShutdownProgressing());
    }
    ExitIfEmulationThreadStuck();
    OnEmulationStopped();
}

void MainWindow::StoreRecentFile(const QString& filename) {
    UISettings::values.recent_files.prepend(filename);
    UISettings::values.recent_files.removeDuplicates();
    while (UISettings::values.recent_files.size() > max_recent_files_item) {
        UISettings::values.recent_files.removeLast();
    }

    UpdateRecentFiles();
}

void MainWindow::UpdateRecentFiles() {
    game_list->RefreshLibraryOrder();
    const int num_recent_files =
        (std::min)(static_cast<int>(UISettings::values.recent_files.size()), max_recent_files_item);

    for (int i = 0; i < num_recent_files; i++) {
        const QString text = QStringLiteral("&%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName());
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }

    for (int j = num_recent_files; j < max_recent_files_item; ++j) {
        actions_recent_files[j]->setVisible(false);
    }

    // Enable the recent files menu if the list isn't empty
    ui->menu_recent_files->setEnabled(num_recent_files != 0);
}

void MainWindow::OnGameListLoadFile(QString game_path, u64 program_id) {
    auto params = ApplicationAppletParameters();
    params.program_id = program_id;

    BootGame(game_path, params);
}

// TODO(crueter): Common profile selector
void MainWindow::OnGameListOpenFolder(u64 program_id, GameListOpenTarget target,
                                      const std::string& game_path) {
    std::filesystem::path path;
    QString open_target;

    const auto [user_save_size, device_save_size] = [&game_path, &program_id] {
        const FileSys::PatchManager pm{program_id, QtCommon::system->GetFileSystemController(),
                                       QtCommon::system->GetContentProvider()};
        const auto control = pm.GetControlMetadata().first;
        if (control != nullptr) {
            return std::make_pair(control->GetDefaultNormalSaveSize(),
                                  control->GetDeviceSaveDataSize());
        } else {
            const auto file = Core::GetGameFileFromPath(QtCommon::vfs, game_path);
            const auto loader = Loader::GetLoader(*QtCommon::system, file);

            FileSys::NACP nacp{};
            loader->ReadControlData(nacp);
            return std::make_pair(nacp.GetDefaultNormalSaveSize(), nacp.GetDeviceSaveDataSize());
        }
    }();

    // Some titles (e.g. NX2 games) report no sizes in their NACP yet still create account saves
    // at runtime, so only use the device layout when it is explicitly declared.
    const bool use_device_save{device_save_size > 0 && user_save_size == 0};

    switch (target) {
    case GameListOpenTarget::SaveData: {
        open_target = tr("Save Data");
        const auto save_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::SaveDir);
        auto vfs_save_dir = QtCommon::vfs->OpenDirectory(Common::FS::PathToUTF8String(save_dir),
                                                         FileSys::OpenMode::Read);

        u128 save_user_id{};
        if (!use_device_save) {
            const auto user_id = GetProfileID();
            if (user_id) {
                save_user_id = user_id->AsU128();
            }
        }

        const auto save_data_path = FileSys::SaveDataFactory::GetFullPath(
            {}, vfs_save_dir, FileSys::SaveDataSpaceId::User, FileSys::SaveDataType::Account,
            program_id, save_user_id, 0);

        path = Common::FS::ConcatPathSafe(save_dir, save_data_path);

        // Only create the folder for games that are known to need it; otherwise an empty
        // directory would be left behind in the wrong place.
        if ((user_save_size > 0 || device_save_size > 0) && !Common::FS::CreateDirs(path)) {
            LOG_ERROR(Frontend, "Unable to create the directories for save data");
        }

        break;
    }
    case GameListOpenTarget::ModData: {
        open_target = tr("Mod Data");
        path = Common::FS::GetEdenPath(Common::FS::EdenPath::LoadDir) /
               fmt::format("{:016X}", program_id);
        break;
    }
    default:
        UNIMPLEMENTED();
        break;
    }

    const QString qpath = QString::fromStdString(Common::FS::PathToUTF8String(path));
    const QDir dir(qpath);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("Error Opening %1 Folder").arg(open_target),
                             tr("Folder does not exist!"));
        return;
    }
    LOG_INFO(Frontend, "Opening {} path for program_id={:016x}", open_target.toStdString(),
             program_id);
    QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
}

static bool RomFSRawCopy(size_t total_size, size_t& read_size, QProgressDialog& dialog,
                         const FileSys::VirtualDir& src, const FileSys::VirtualDir& dest,
                         bool full) {
    if (src == nullptr || dest == nullptr || !src->IsReadable() || !dest->IsWritable())
        return false;
    if (dialog.wasCanceled())
        return false;

    std::vector<u8> buffer(CopyBufferSize);
    auto last_timestamp = std::chrono::steady_clock::now();

    const auto QtRawCopy = [&](const FileSys::VirtualFile& src_file,
                               const FileSys::VirtualFile& dest_file) {
        if (src_file == nullptr || dest_file == nullptr) {
            return false;
        }
        if (!dest_file->Resize(src_file->GetSize())) {
            return false;
        }

        for (std::size_t i = 0; i < src_file->GetSize(); i += buffer.size()) {
            if (dialog.wasCanceled()) {
                dest_file->Resize(0);
                return false;
            }

            using namespace std::literals::chrono_literals;
            const auto new_timestamp = std::chrono::steady_clock::now();

            if ((new_timestamp - last_timestamp) > 33ms) {
                last_timestamp = new_timestamp;
                dialog.setValue(
                    static_cast<int>((std::min)(read_size, total_size) * 100 / total_size));
                QCoreApplication::processEvents();
            }

            const auto read = src_file->Read(buffer.data(), buffer.size(), i);
            dest_file->Write(buffer.data(), read, i);

            read_size += read;
        }

        return true;
    };

    if (full) {
        for (const auto& file : src->GetFiles()) {
            const auto out = VfsDirectoryCreateFileWrapper(dest, file->GetName());
            if (!QtRawCopy(file, out))
                return false;
        }
    }

    for (const auto& dir : src->GetSubdirectories()) {
        const auto out = dest->CreateSubdirectory(dir->GetName());
        if (!RomFSRawCopy(total_size, read_size, dialog, dir, out, full))
            return false;
    }

    return true;
}

// TODO(crueter): All this can be transfered to qt_common
// Aldoe I need to decide re: message boxes for QML
// translations_common? strings_common? qt_strings? who knows
void MainWindow::OnGameListRemoveInstalledEntry(u64 program_id,
                                                QtCommon::Game::InstalledEntryType type) {
    const QString entry_question = [type] {
        switch (type) {
        case QtCommon::Game::InstalledEntryType::Game:
            return tr("Remove Installed Game Contents?");
        case QtCommon::Game::InstalledEntryType::Update:
            return tr("Remove Installed Game Update?");
        case QtCommon::Game::InstalledEntryType::AddOnContent:
            return tr("Remove Installed Game DLC?");
        default:
            return QStringLiteral("Remove Installed Game <Invalid Type>?");
        }
    }();

    if (!question(this, tr("Remove Entry"), entry_question, QMessageBox::Yes | QMessageBox::No,
                  QMessageBox::No)) {
        return;
    }

    // TODO(crueter): move this to QtCommon (populate async?)
    switch (type) {
    case QtCommon::Game::InstalledEntryType::Game:
        QtCommon::Game::RemoveBaseContent(program_id, type);
        [[fallthrough]];
    case QtCommon::Game::InstalledEntryType::Update:
        QtCommon::Game::RemoveUpdateContent(program_id, type);
        if (type != QtCommon::Game::InstalledEntryType::Game) {
            break;
        }
        [[fallthrough]];
    case QtCommon::Game::InstalledEntryType::AddOnContent:
        QtCommon::Game::RemoveAddOnContent(program_id, type);
        break;
    }
    Common::FS::RemoveDirRecursively(Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) /
                                     "game_list");
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

void MainWindow::OnGameListRemoveFile(u64 program_id, QtCommon::Game::GameListRemoveTarget target,
                                      const std::string& game_path) {
    const QString question = [target] {
        switch (target) {
        case QtCommon::Game::GameListRemoveTarget::GlShaderCache:
            return tr("Delete OpenGL Transferable Shader Cache?");
        case QtCommon::Game::GameListRemoveTarget::VkShaderCache:
            return tr("Delete Vulkan Transferable Shader Cache?");
        case QtCommon::Game::GameListRemoveTarget::AllShaderCache:
            return tr("Delete All Transferable Shader Caches?");
        case QtCommon::Game::GameListRemoveTarget::CustomConfiguration:
            return tr("Remove Custom Game Configuration?");
        case QtCommon::Game::GameListRemoveTarget::CacheStorage:
            return tr("Remove Cache Storage?");
        default:
            return QString{};
        }
    }();

    if (!MainWindow::question(this, tr("Remove File"), question, QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No)) {
        return;
    }

    switch (target) {
    case QtCommon::Game::GameListRemoveTarget::VkShaderCache:
        QtCommon::Game::RemoveVulkanDriverPipelineCache(program_id);
        [[fallthrough]];
    case QtCommon::Game::GameListRemoveTarget::GlShaderCache:
        QtCommon::Game::RemoveTransferableShaderCache(program_id, target);
        break;
    case QtCommon::Game::GameListRemoveTarget::AllShaderCache:
        QtCommon::Game::RemoveAllTransferableShaderCaches(program_id);
        break;
    case QtCommon::Game::GameListRemoveTarget::CustomConfiguration:
        QtCommon::Game::RemoveCustomConfiguration(program_id, game_path);
        break;
    case QtCommon::Game::GameListRemoveTarget::CacheStorage:
        QtCommon::Game::RemoveCacheStorage(program_id);
        break;
    }
}

void MainWindow::OnGameListSetPlayTime(u64 program_id) {
    const u64 current_play_time = play_time_manager->GetPlayTime(program_id);

    const auto* const library = game_list->GetModel();
    const auto games = library->match(library->index(0, 0), GameListItemPath::ProgramIdRole,
                                      QVariant::fromValue(qulonglong(program_id)), 1,
                                      Qt::MatchExactly | Qt::MatchRecursive);
    const QString title =
        games.isEmpty() ? QString{} : games.first().data(GameListItemPath::TitleRole).toString();
    SetPlayTimeDialog dialog(this, title, current_play_time);

    if (dialog.exec() == QDialog::Accepted) {
        const u64 total_seconds = dialog.GetTotalSeconds();
        play_time_manager->SetPlayTime(program_id, total_seconds);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }
}

void MainWindow::OnGameListRemovePlayTimeData(u64 program_id) {
    if (QMessageBox::question(this, tr("Remove Play Time Data"), tr("Reset play time?"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    play_time_manager->ResetProgramPlayTime(program_id);
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

namespace {

QString GameTitleName(const std::string& game_path);

/// A field of a content card: a muted label over its control.
void AddContentField(QVBoxLayout* layout, const QString& label, QWidget* field) {
    auto* column = new QVBoxLayout;
    column->setSpacing(8);
    auto* title = new QLabel(label);
    title->setFont(Spotlight::Font(13, 600));
    Spotlight::SetTextColor(title, Spotlight::Color::TextSecondary());
    column->addWidget(title);
    column->addWidget(field);
    layout->addLayout(column);
}

/// A content card's buttons: Cancel and the accept button named `accept`.
void AddContentButtons(QDialog* dialog, QVBoxLayout* layout, const QString& accept) {
    auto* buttons = new QDialogButtonBox;
    buttons->addButton(QDialogButtonBox::Cancel);
    auto* accept_button = buttons->addButton(accept, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addSpacing(6);
    layout->addWidget(buttons);
    accept_button->setFocus();
}

struct RomFSContent {
    u64 title_id;
    FileSys::ContentRecordType record_type;
    QString label;
};

/// The installed RomFS of the game, its updates and its DLC that can be dumped.
std::vector<RomFSContent> ListRomFSContents(const FileSys::ContentProvider& installed,
                                            u64 program_id) {
    using ContentInfo = std::tuple<u64, FileSys::TitleType, FileSys::ContentRecordType>;
    boost::container::flat_set<ContentInfo> available_title_ids;

    std::vector<u64> add_on_content;
    for (const auto& entry :
         installed.ListEntriesFilter(FileSys::TitleType::AOC, FileSys::ContentRecordType::Data)) {
        add_on_content.push_back(entry.title_id);
    }
    const u64 add_on_base = FileSys::GetAddOnApplicationID(program_id, add_on_content);

    const auto RetrieveEntries = [&](FileSys::TitleType title_type,
                                     FileSys::ContentRecordType record_type) {
        const u64 base = title_type == FileSys::TitleType::AOC ? add_on_base : program_id;
        const auto entries = installed.ListEntriesFilter(title_type, record_type);
        for (const auto& entry : entries) {
            if (FileSys::GetBaseTitleID(entry.title_id) == base &&
                installed.GetEntry(entry)->GetStatus() == Loader::ResultStatus::Success) {
                available_title_ids.insert({entry.title_id, title_type, record_type});
            }
        }
    };

    RetrieveEntries(FileSys::TitleType::Application, FileSys::ContentRecordType::Program);
    RetrieveEntries(FileSys::TitleType::Application, FileSys::ContentRecordType::HtmlDocument);
    RetrieveEntries(FileSys::TitleType::Application, FileSys::ContentRecordType::LegalInformation);
    RetrieveEntries(FileSys::TitleType::AOC, FileSys::ContentRecordType::Data);

    std::vector<RomFSContent> contents;
    for (const auto& [title_id, title_type, record_type] : available_title_ids) {
        const auto hex_title_id = QString::fromStdString(fmt::format("{:X}", title_id));
        QString label;
        if (record_type == FileSys::ContentRecordType::Program) {
            label = QStringLiteral("Program [%1]").arg(hex_title_id);
        } else if (record_type == FileSys::ContentRecordType::HtmlDocument) {
            label = QStringLiteral("HTML document [%1]").arg(hex_title_id);
        } else if (record_type == FileSys::ContentRecordType::LegalInformation) {
            label = QStringLiteral("Legal information [%1]").arg(hex_title_id);
        } else {
            label = QStringLiteral("DLC %1 [%2]").arg(title_id & 0x7FF).arg(hex_title_id);
        }
        contents.push_back({title_id, record_type, label});
    }
    return contents;
}

struct RomFSDumpChoice {
    std::size_t content;
    bool full;
    DumpRomFSTarget target;
};

/// The Dump RomFS card: which content (when there is more than one), the mode and the
/// destination, which starts on the one the menu asked for.
std::optional<RomFSDumpChoice> AskRomFSDump(QWidget* parent, const QString& game,
                                            const std::vector<RomFSContent>& contents,
                                            DumpRomFSTarget target) {
    Spotlight::Dialog dialog(parent);
    dialog.setWindowTitle(MainWindow::tr("Dump RomFS"));
    dialog.SetSubtitle(game);
    dialog.SetIcon(u"folder-open", Spotlight::Color::Accent());
    dialog.SetCardWidth(440);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    QComboBox* content = nullptr;
    if (contents.size() > 1) {
        content = new QComboBox;
        for (const auto& entry : contents) {
            content->addItem(entry.label);
        }
        AddContentField(layout, MainWindow::tr("Content"), content);
    }
    auto* mode = new Spotlight::SegmentedControl(
        {MainWindow::tr("Full copy"), MainWindow::tr("Folders only")},
        Spotlight::SegmentedControl::Kind::Accent, 38);
    mode->SetCurrentIndex(0);
    mode->setToolTip(MainWindow::tr("Full copy copies every file into the new folder; folders "
                                    "only creates the folder structure."));
    AddContentField(layout, MainWindow::tr("Mode"), mode);
    auto* destination = new Spotlight::SegmentedControl(
        {MainWindow::tr("Dump folder"), MainWindow::tr("SD card (atmosphere)")},
        Spotlight::SegmentedControl::Kind::Accent, 38);
    destination->SetCurrentIndex(target == DumpRomFSTarget::Normal ? 0 : 1);
    AddContentField(layout, MainWindow::tr("Destination"), destination);
    AddContentButtons(&dialog, layout, MainWindow::tr("Dump"));

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return RomFSDumpChoice{
        content ? static_cast<std::size_t>(content->currentIndex()) : 0,
        mode->CurrentIndex() == 0,
        destination->CurrentIndex() == 0 ? DumpRomFSTarget::Normal : DumpRomFSTarget::SDMC,
    };
}

/// The "What is this file?" card for an NCA: the type to install it as, or -1 when cancelled.
int AskNcaType(QWidget* parent, const QStringList& types, int initial) {
    Spotlight::Dialog dialog(parent);
    dialog.setWindowTitle(MainWindow::tr("What is this file?"));
    dialog.SetSubtitle(MainWindow::tr("An .nca needs a type before installing"));
    dialog.SetIcon(u"file", Spotlight::Color::Accent());
    dialog.SetCardWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* type = new QComboBox;
    type->addItems(types);
    type->setCurrentIndex(initial);
    layout->addWidget(type);
    auto* hint = new QLabel(MainWindow::tr("In most instances, the default 'Game' is fine."));
    hint->setWordWrap(true);
    hint->setFont(Spotlight::Font(12, 400));
    Spotlight::SetTextColor(hint, Spotlight::Color::TextMuted());
    layout->addWidget(hint);
    AddContentButtons(&dialog, layout, MainWindow::tr("Install"));

    return dialog.exec() == QDialog::Accepted ? type->currentIndex() : -1;
}

} // namespace

void MainWindow::OnGameListDumpRomFS(u64 program_id, const std::string& game_path,
                                     DumpRomFSTarget target) {
    const auto failed = [this] {
        QMessageBox::warning(this, tr("RomFS Extraction Failed!"),
                             tr("There was an error copying the RomFS files or the user "
                                "cancelled the operation."));
    };

    const auto loader = Loader::GetLoader(
        *QtCommon::system, QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read));
    if (loader == nullptr) {
        failed();
        return;
    }

    const auto& installed = QtCommon::system->GetContentProvider();

    // NX archives and NUPs are never installed: they give their own RomFS.
    const bool archive = loader->GetFileType() == Loader::FileType::NX ||
                         loader->GetFileType() == Loader::FileType::NUP;
    std::vector<RomFSContent> contents;
    if (!archive) {
        contents = ListRomFSContents(installed, program_id);
        if (contents.empty()) {
            failed();
            return;
        }
    }
    const auto choice = AskRomFSDump(this, GameTitleName(game_path), contents, target);
    if (!choice) {
        failed();
        return;
    }

    u64 title_id{};
    FileSys::VirtualFile romfs;
    if (loader->GetFileType() == Loader::FileType::NX) {
        // An NX archive is never installed: it gives its own RomFS, update applied.
        Loader::AppLoader_NX nx_loader{*QtCommon::system,
                                       QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read),
                                       program_id};
        title_id = program_id;
        if (nx_loader.ReadPatchedRomFS(romfs) != Loader::ResultStatus::Success) {
            failed();
            return;
        }
    } else if (loader->GetFileType() == Loader::FileType::NUP) {
        // A NUP is never installed either: its loader gives the RomFS with the newest update
        // already merged over the base.
        title_id = program_id;
        if (loader->ReadRomFS(romfs) != Loader::ResultStatus::Success || romfs == nullptr) {
            failed();
            return;
        }
    } else {
        FileSys::VirtualFile packed_update_raw{};
        loader->ReadUpdateRaw(packed_update_raw);

        title_id = contents[choice->content].title_id;
        const auto type = contents[choice->content].record_type;
        const auto base_nca = installed.GetEntry(title_id, type);
        if (!base_nca) {
            failed();
            return;
        }

        const FileSys::NCA update_nca{packed_update_raw, nullptr};
        if (type != FileSys::ContentRecordType::Program ||
            update_nca.GetStatus() != Loader::ResultStatus::ErrorMissingBKTRBaseRomFS ||
            update_nca.GetTitleId() != FileSys::GetUpdateTitleID(title_id)) {
            packed_update_raw = {};
        }

        const FileSys::PatchManager pm{title_id, QtCommon::system->GetFileSystemController(),
                                       installed};
        romfs = pm.PatchRomFS(base_nca.get(), base_nca->GetRomFS(), type, packed_update_raw, false);
    }

    const auto dump_dir =
        choice->target == DumpRomFSTarget::Normal
            ? Common::FS::GetEdenPath(Common::FS::EdenPath::DumpDir)
            : Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "atmosphere" / "contents";
    const auto romfs_dir = fmt::format("{:016X}/romfs", title_id);

    const auto path = Common::FS::PathToUTF8String(dump_dir / romfs_dir);

    const auto out = VfsFilesystemCreateDirectoryWrapper(path, FileSys::OpenMode::ReadWrite);

    if (out == nullptr) {
        failed();
        QtCommon::vfs->DeleteDirectory(path);
        return;
    }

    const auto extracted = FileSys::ExtractRomFS(romfs);
    if (extracted == nullptr) {
        failed();
        return;
    }

    const bool full = choice->full;

    // The expected required space is the size of the RomFS + 1 GiB
    const auto minimum_free_space = romfs->GetSize() + 0x40000000;

    if (full && Common::FS::GetFreeSpaceSize(path) < minimum_free_space) {
        QMessageBox::warning(this, tr("RomFS Extraction Failed!"),
                             tr("There is not enough free space at %1 to extract the RomFS. Please "
                                "free up space or select a different dump directory at "
                                "Emulation > Configure > System > Filesystem > Dump Root")
                                 .arg(QString::fromStdString(path)));
        return;
    }

    QProgressDialog progress(tr("Extracting RomFS..."), tr("Cancel"), 0, 100, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setAutoClose(false);
    progress.setAutoReset(false);

    size_t read_size = 0;

    if (RomFSRawCopy(romfs->GetSize(), read_size, progress, extracted, out, full)) {
        progress.close();
        QMessageBox::information(this, tr("RomFS Extraction Succeeded!"),
                                 tr("The operation completed successfully."));
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(path)));
    } else {
        progress.close();
        failed();
        QtCommon::vfs->DeleteDirectory(path);
    }
}

// END
void MainWindow::OnGameListVerifyIntegrity(const std::string& game_path) {
    QtCommon::Content::VerifyGameContents(game_path);
}

void MainWindow::OnGameListCopyTID(u64 program_id) {
    QClipboard* clipboard = QGuiApplication::clipboard();
    clipboard->setText(QString::fromStdString(fmt::format("{:016X}", program_id)));
}

void MainWindow::OnGameListCreateShortcut(u64 program_id, const std::string& game_path,
                                          const QtCommon::Game::ShortcutTarget target) {
    // Create shortcu
    std::string arguments = fmt::format("-g \"{:s}\"", game_path);

    QtCommon::Game::CreateShortcut(game_path, program_id, "", target, arguments, true);
}

void MainWindow::OnGameListOpenDirectory(const QString& directory) {
    // TODO(crueter): QtCommon
    std::filesystem::path fs_path;
    if (directory == QStringLiteral("SDMC")) {
        fs_path =
            Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / "Nintendo/Contents/registered";
    } else if (directory == QStringLiteral("UserNAND")) {
        fs_path =
            Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "user/Contents/registered";
    } else if (directory == QStringLiteral("SysNAND")) {
        fs_path =
            Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) / "system/Contents/registered";
    } else {
        fs_path = directory.toStdString();
    }

    const auto qt_path = QString::fromStdString(Common::FS::PathToUTF8String(fs_path));

    if (!Common::FS::IsDir(fs_path)) {
        QMessageBox::critical(this, tr("Error Opening %1").arg(qt_path),
                              tr("Folder does not exist!"));
        return;
    }

    QDesktopServices::openUrl(QUrl::fromLocalFile(qt_path));
}

void MainWindow::OnGameListAddDirectory() {
    const QString dir_path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
    if (dir_path.isEmpty()) {
        return;
    }

    UISettings::GameDir game_dir{dir_path.toStdString(), false, true};
    if (!UISettings::values.game_dirs.contains(game_dir)) {
        UISettings::values.game_dirs.append(game_dir);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    } else {
        LOG_WARNING(Frontend, "Selected directory is already in the game list");
    }

    OnSaveConfig();
}

void MainWindow::OnGameListShowList(bool show) {
    launcher->SetLibraryAvailable(show);
}

void MainWindow::OnGameListOpenPerGameProperties(u64 title_id, const std::string& file) {
    {
        const auto v_file = Core::GetGameFileFromPath(QtCommon::vfs, file);
        const auto loader = Loader::GetLoader(*QtCommon::system, v_file, title_id);

        u64 loaded_id{};
        if (!loader || loader->ReadProgramId(loaded_id) != Loader::ResultStatus::Success ||
            loaded_id != title_id) {
            QMessageBox::information(this, tr("Properties"),
                                     tr("The game properties could not be loaded."));
            return;
        }
    }

    OpenPerGameConfiguration(title_id, file);
}

void MainWindow::OnLinkToRyujinx(const u64& program_id) {
    if (emulation_running) {
        QMessageBox::warning(this, tr("Stop emulation before linking saves"),
                             tr("Close the running game before changing Ryujinx save links."));
        return;
    }

    const auto profile = GetProfileID();
    if (!profile) {
        return;
    }

    auto ryujinx_root =
        UISettings::values.ryujinx_link_paths
            .value(program_id, QDir(Common::FS::GetLegacyPath(Common::FS::RyujinxDir)))
            .filesystemAbsolutePath();

    const auto save = QtCommon::FS::ChooseRyujinxSave(ryujinx_root, program_id, profile->AsU128());
    if (!save) {
        return;
    }

    const bool device = save->type == Common::FS::RyujinxSaveType::Device;
    const auto type = device ? FileSys::SaveDataType::Device : FileSys::SaveDataType::Account;
    const auto user = device ? u128{} : profile->AsU128();
    const auto save_root = Common::FS::GetEdenPath(Common::FS::EdenPath::SaveDir);
    const auto vfs_root = QtCommon::vfs->OpenDirectory(Common::FS::PathToUTF8String(save_root),
                                                       FileSys::OpenMode::Read);
    const auto lichii_dir = Common::FS::ConcatPathSafe(
        save_root, FileSys::SaveDataFactory::GetFullPath(
                       {}, vfs_root, FileSys::SaveDataSpaceId::User, type, program_id, user, 0));

    if (const auto unlinked = QtCommon::FS::CheckUnlink(lichii_dir, save->path)) {
        if (!*unlinked) {
            return;
        }
        UISettings::values.ryujinx_link_paths.remove(program_id);
    } else {
        const auto* const library = game_list->GetModel();
        const auto games = library->match(library->index(0, 0), GameListItemPath::ProgramIdRole,
                                          QVariant::fromValue(qulonglong(program_id)), 1,
                                          Qt::MatchExactly | Qt::MatchRecursive);
        const QModelIndex game = games.isEmpty() ? QModelIndex{} : games.first();
        RyujinxDialog dialog(lichii_dir, save->path,
                             QString::fromStdString(fmt::format("{:016x}", save->save_id)),
                             game.data(GameListItemPath::TitleRole).toString(),
                             game.data(Qt::DecorationRole).value<QPixmap>(), this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        UISettings::values.ryujinx_link_paths.insert(
            program_id, QString::fromStdU16String(ryujinx_root.u16string()));
    }

    config->SaveAllValues();
}

void MainWindow::OnMenuLoadFile() {
    if (is_load_file_select_active) {
        return;
    }

    is_load_file_select_active = true;
    const QString extensions =
        QStringLiteral("*.")
            .append(QtCommon::supported_file_extensions.join(QStringLiteral(" *.")))
            .append(QStringLiteral(" main"));
    const QString file_filter = tr("Switch Executable (%1);;All Files (*.*)",
                                   "%1 is an identifier for the Switch executable file extensions.")
                                    .arg(extensions);
    const QString filename = QFileDialog::getOpenFileName(
        this, tr("Load File"), QString::fromStdString(UISettings::values.roms_path), file_filter);
    is_load_file_select_active = false;

    if (filename.isEmpty()) {
        return;
    }

    UISettings::values.roms_path = QFileInfo(filename).path().toStdString();
    BootGame(filename, ApplicationAppletParameters());
}

void MainWindow::OnMenuLoadFolder() {
    const QString dir_path =
        QFileDialog::getExistingDirectory(this, tr("Open Extracted ROM Directory"));

    if (dir_path.isNull()) {
        return;
    }

    const auto main = Core::GetGameFileFromPath(QtCommon::vfs, dir_path.toStdString());
    if (main) {
        BootGame(QString::fromStdString(main->GetFullPath()), ApplicationAppletParameters());
    } else {
        QMessageBox::warning(this, tr("Invalid Directory Selected"),
                             tr("The directory you have selected does not contain a 'main' file."));
    }
}

void MainWindow::IncrementInstallProgress() {
    install_progress->setValue(install_progress->value() + 1);
}

void MainWindow::OnMenuInstallToNAND() {
    const QString file_filter =
        tr("Installable Switch File (*.nca *.nsp *.nsz *.xci *.xcz);;Nintendo Content Archive "
           "(*.nca);;Nintendo Submission Package (*.nsp *.nsz);;NX Cartridge "
           "Image (*.xci *.xcz)");

    QStringList filenames = QFileDialog::getOpenFileNames(
        this, tr("Install Files"), QString::fromStdString(UISettings::values.roms_path),
        file_filter);

    if (filenames.isEmpty()) {
        return;
    }

    InstallDialog installDialog(this, filenames);
    if (installDialog.exec() == QDialog::Rejected) {
        return;
    }

    const QStringList files = installDialog.GetFiles();

    if (files.isEmpty()) {
        return;
    }

    // Save folder location of the first selected file
    UISettings::values.roms_path = QFileInfo(filenames[0]).path().toStdString();

    int remaining = files.size();

    // This would only overflow above 2^51 bytes (2.252 PB)
    int total_size = 0;
    for (const QString& file : files) {
        total_size += static_cast<int>(QFile(file).size() / CopyBufferSize);
    }
    if (total_size < 0) {
        LOG_CRITICAL(Frontend, "Attempting to install too many files, aborting.");
        return;
    }

    QStringList new_files{};         // Newly installed files that do not yet exist in the NAND
    QStringList overwritten_files{}; // Files that overwrote those existing in the NAND
    QStringList failed_files{};      // Files that failed to install due to errors
    bool detected_base_install{};    // Whether a base game was attempted to be installed

    ui->action_Install_File_NAND->setEnabled(false);

    install_progress = new QProgressDialog(QString{}, tr("Cancel"), 0, total_size, this);
    install_progress->setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    install_progress->setAttribute(Qt::WA_DeleteOnClose, true);
    install_progress->setFixedWidth(installDialog.GetMinimumWidth() + 40);
    install_progress->show();

    for (const QString& file : files) {
        install_progress->setWindowTitle(
            tr("Installing %1 of %2").arg(files.size() - remaining + 1).arg(files.size()));
        install_progress->setLabelText(QFileInfo(file).fileName());

        QFuture<ContentManager::InstallResult> future;
        ContentManager::InstallResult result;

        if (file.endsWith(QStringLiteral("nsp"), Qt::CaseInsensitive) ||
            file.endsWith(QStringLiteral("nsz"), Qt::CaseInsensitive) ||
            file.endsWith(QStringLiteral("xci"), Qt::CaseInsensitive) ||
            file.endsWith(QStringLiteral("xcz"), Qt::CaseInsensitive)) {
            const auto progress_callback = [this](size_t size, size_t progress) {
                emit UpdateInstallProgress();
                if (install_progress->wasCanceled()) {
                    return true;
                }
                return false;
            };
            future = QtConcurrent::run([&file, progress_callback] {
                return ContentManager::InstallNSP(*QtCommon::system, *QtCommon::vfs,
                                                  file.toStdString(), progress_callback);
            });

            while (!future.isFinished()) {
                QCoreApplication::processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            result = future.result();

        } else {
            result = InstallNCA(file);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        switch (result) {
        case ContentManager::InstallResult::Success:
            new_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::Overwrite:
            overwritten_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::Failure:
            failed_files.append(QFileInfo(file).fileName());
            break;
        case ContentManager::InstallResult::BaseInstallAttempted:
            failed_files.append(QFileInfo(file).fileName());
            detected_base_install = true;
            break;
        }

        --remaining;
    }

    install_progress->close();

    if (detected_base_install) {
        QMessageBox::warning(
            this, tr("Install Results"),
            tr("To avoid possible conflicts, we discourage users from installing base games to the "
               "NAND.\nPlease, only use this feature to install updates and DLC."));
    }

    const auto installed_count = static_cast<int>(new_files.size() + overwritten_files.size());
    QMessageBox results(
        failed_files.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
        installed_count == 0 ? tr("Nothing was installed")
                             : tr("Installed %n file(s)", "", installed_count),
        QStringList{tr("%n new", "", static_cast<int>(new_files.size())),
                    tr("%n replaced", "", static_cast<int>(overwritten_files.size())),
                    tr("%n failed", "", static_cast<int>(failed_files.size()))}
            .join(QStringLiteral(" \u00b7 ")),
        QMessageBox::NoButton, this);
    results.addButton(tr("Done"), QMessageBox::AcceptRole);
    if (failed_files.isEmpty()) {
        Spotlight::MarkSuccess(&results);
    }
    results.exec();
    Common::FS::RemoveDirRecursively(Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) /
                                     "game_list");
    game_list->PopulateAsync(UISettings::values.game_dirs);
    ui->action_Install_File_NAND->setEnabled(true);
}

ContentManager::InstallResult MainWindow::InstallNCA(const QString& filename) {
    const QStringList tt_options{tr("System Application"),
                                 tr("System Archive"),
                                 tr("System Application Update"),
                                 tr("Firmware Package (Type A)"),
                                 tr("Firmware Package (Type B)"),
                                 tr("Game"),
                                 tr("Game Update"),
                                 tr("Game DLC"),
                                 tr("Delta Title")};
    auto index = AskNcaType(this, tt_options, 5);
    if (index == -1) {
        QMessageBox::warning(this, tr("Failed to Install"),
                             tr("The title type you selected for the NCA is invalid."));
        return ContentManager::InstallResult::Failure;
    }

    // If index is equal to or past Game, add the jump in TitleType.
    if (index >= 5) {
        index += static_cast<size_t>(FileSys::TitleType::Application) -
                 static_cast<size_t>(FileSys::TitleType::FirmwarePackageB);
    }

    const bool is_application = index >= static_cast<s32>(FileSys::TitleType::Application);
    const auto& fs_controller = QtCommon::system->GetFileSystemController();
    auto* registered_cache = is_application ? fs_controller.GetUserNANDContents()
                                            : fs_controller.GetSystemNANDContents();

    const auto progress_callback = [this](size_t size, size_t progress) {
        emit UpdateInstallProgress();
        if (install_progress->wasCanceled()) {
            return true;
        }
        return false;
    };
    return ContentManager::InstallNCA(*QtCommon::vfs, filename.toStdString(), *registered_cache,
                                      static_cast<FileSys::TitleType>(index), progress_callback);
}

void MainWindow::OnMenuRecentFile() {
    QAction* action = qobject_cast<QAction*>(sender());
    assert(action);

    const QString filename = action->data().toString();
    if (QFileInfo::exists(filename)) {
        BootGame(filename, ApplicationAppletParameters());
    } else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, tr("File not found"),
                                 tr("File \"%1\" not found").arg(filename));

        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void MainWindow::OnStartGame() {
    PreventOSSleep();

    QtCommon::emu_thread->SetRunning(true);

    UpdateMenuState();
    OnTasStateChanged();

    play_time_manager->SetProgramId(QtCommon::system->GetApplicationProcessProgramID());
    play_time_manager->Start();

    discord_rpc->Update();
    Common::FeralGamemode::Start();
}

void MainWindow::OnRestartGame() {
    if (!QtCommon::system->IsPoweredOn()) {
        return;
    }

    if (ConfirmShutdownGame()) {
        // Make a copy since ShutdownGame edits game_path
        const auto current_game = QString(current_game_path);
        ShutdownGame(tr("%1 starts again when this finishes").arg(current_game_title));
        BootGame(current_game, ApplicationAppletParameters());
    }
}

void MainWindow::OnPauseGame() {
    QtCommon::emu_thread->SetRunning(false);
    play_time_manager->Stop();
    UpdateMenuState();
    AllowOSSleep();
    Common::FeralGamemode::Stop();
}

void MainWindow::OnPauseContinueGame() {
    if (emulation_running) {
        if (QtCommon::emu_thread->IsRunning()) {
            OnPauseGame();
        } else {
            OnStartGame();
        }
    }
}

void MainWindow::OnStopGame() {
    if (!emulation_running || shutdown_game_in_progress || shutdown_dialog) {
        return;
    }

    if (ConfirmShutdownGame()) {
        play_time_manager->Stop();
        if (OnShutdownBegin()) {
            OnShutdownBeginDialog(tr("Returning to the library"));
        } else {
            OnEmulationStopped();
        }
    }
}

bool MainWindow::ConfirmShutdownGame() {
    if (UISettings::values.confirm_before_stopping.GetValue() == ConfirmStop::Ask_Always) {
        if (QtCommon::system->GetExitLocked()) {
            if (!ConfirmForceLockedExit()) {
                return false;
            }
        } else {
            if (!ConfirmChangeGame()) {
                return false;
            }
        }
    } else {
        if (UISettings::values.confirm_before_stopping.GetValue() ==
                ConfirmStop::Ask_Based_On_Game &&
            QtCommon::system->GetExitLocked()) {
            if (!ConfirmForceLockedExit()) {
                return false;
            }
        }
    }
    return true;
}

void MainWindow::OnLoadComplete() {
    loading_screen->OnLoadComplete();

    perf_overlay = new PerformanceOverlay(this, render_window);
    perf_overlay->setVisible(ui->action_Show_Performance_Overlay->isChecked());

    connect(perf_overlay, &PerformanceOverlay::closed, perf_overlay,
            [this]() { ui->action_Show_Performance_Overlay->setChecked(false); });
}

void MainWindow::OnExecuteProgram(std::size_t program_index) {
    if (!emulation_running || shutdown_game_in_progress || shutdown_dialog) {
        LOG_WARNING(Frontend, "Dropping ExecuteProgram of index {}: no active session",
                    program_index);
        return;
    }

    const u64 previous_program_id = QtCommon::system->GetApplicationProcessProgramID();

    const auto current_path =
        QString::fromStdString(QtCommon::system->GetCurrentApplicationFilePath());

    LOG_INFO(Frontend, "ExecuteProgram requested, program_index={} previous_program_id={:016X}",
             program_index, previous_program_id);

    ShutdownGame(tr("%1 starts again when this finishes").arg(current_game_title));

    auto params = ApplicationAppletParameters();
    params.program_index = static_cast<s32>(program_index);
    params.launch_type = Service::AM::LaunchType::ApplicationInitiated;

    if (previous_program_id > static_cast<u64>(Service::AM::AppletProgramId::MaxProgramId)) {
        params.previous_program_index =
            static_cast<s32>(previous_program_id - FileSys::GetBaseTitleID(previous_program_id));
        params.program_id = previous_program_id;
    }

    const auto filename = current_path.isEmpty() ? last_filename_booted : current_path;

    LOG_DEBUG(Frontend, "ExecuteProgram booting from path: {}", filename.toStdString());

    BootGame(filename, params);
}

void MainWindow::OnLaunchInNewSession(u64 program_id) {
    // A request queued while its session stops would run inside ShutdownGame's event loop and boot
    // under the outer shutdown, which then takes the new session's thread for a stuck one.
    if (!emulation_running || QtCommon::system->IsShuttingDown()) {
        LOG_WARNING(Frontend, "Dropping HOME Menu launch of {:016X}: its session is stopping",
                    program_id);
        return;
    }
    QString filename;
    if (const auto path = Service::NS::GetGameListApplicationPath(*QtCommon::system, program_id)) {
        if (!Service::NS::OpenGameListApplication(*QtCommon::system, *path, program_id)) {
            LOG_WARNING(Frontend, "No matching program for HOME Menu application {:016X} in {}",
                        program_id, *path);
            return;
        }
        filename = QString::fromStdString(*path);
    } else {
        auto* const model = game_list->GetModel();
        const auto matches =
            model->match(model->index(0, 0), GameListItemPath::ProgramIdRole,
                         QVariant::fromValue(qulonglong{program_id}), 1,
                         Qt::MatchExactly | Qt::MatchRecursive);
        if (!matches.isEmpty()) {
            filename = matches.front().data(GameListItemPath::FullPathRole).toString();
        }
    }
    if (filename.isEmpty()) {
        const auto nca = QtCommon::system->GetContentProviderUnion().GetEntryRaw(
            program_id, FileSys::ContentRecordType::Program);
        if (nca != nullptr) {
            filename = QString::fromStdString(nca->GetFullPath());
        }
    }

    if (filename.isEmpty()) {
        LOG_WARNING(Frontend, "No launch path for HOME Menu application {:016X}", program_id);
        return;
    }
    LOG_INFO(Frontend, "HOME Menu launch of {:016X} in a new session from {}", program_id,
             filename.toStdString());
    ShutdownGame(tr("The next game starts when this finishes"));

    auto params = ApplicationAppletParameters();
    params.program_id = program_id;
    BootGame(filename, params);
}

void MainWindow::OnExit() {
    if (shutdown_dialog) {
        return;
    }
    ShutdownGame();
}

void MainWindow::OnSaveConfig() {
    QtCommon::system->ApplySettings();
    config->SaveAllValues();
}

void MainWindow::ErrorDisplayDisplayError(QString error_code, QString error_text) {
    error_applet = new OverlayDialog(render_window, *QtCommon::system, error_code, error_text,
                                     QString{}, tr("OK"), Qt::AlignLeft | Qt::AlignVCenter);
    SCOPE_EXIT {
        error_applet->deleteLater();
        error_applet = nullptr;
    };
    error_applet->exec();

    emit ErrorDisplayFinished();
}

void MainWindow::ErrorDisplayRequestExit() {
    if (error_applet) {
        error_applet->reject();
    }
}

void MainWindow::OnMenuReportCompatibility() {
    QtCommon::Frontend::Critical(
        tr("Function Disabled"),
        tr("Compatibility list reporting is currently disabled. Check back later!"));

    // #if defined(ARCHITECTURE_x86_64) && !defined(__APPLE__)
    //     const auto& caps = g_cpu_caps;
    //     const bool has_fma = caps.fma;
    //     const auto processor_count = std::thread::hardware_concurrency();
    //     const bool has_4threads = processor_count == 0 || processor_count >= 4;
    //     const bool has_8gb_ram = Common::GetMemInfo().TotalPhysicalMemory >= 8_GiB;
    //     const bool has_broken_vulkan = UISettings::values.has_broken_vulkan;

    //     if (!has_fma || !has_4threads || !has_8gb_ram || has_broken_vulkan) {
    //         QMessageBox::critical(this, tr("Hardware requirements not met"),
    //                               tr("Your system does not meet the recommended hardware
    //                               requirements. "
    //                                  "Compatibility reporting has been disabled."));
    //         return;
    //     }

    //     if (!Settings::values.eden_token.GetValue().empty() &&
    //         !Settings::values.eden_username.GetValue().empty()) {
    //     } else {
    //         QMessageBox::critical(
    //             this, tr("Missing Lichii Account"),
    //             tr("In order to submit a game compatibility test case, you must set up your web
    //             token "
    //                "and "
    //                "username.<br><br/>To link your eden account, go to Emulation &gt;
    //                Configuration "
    //                "&gt; "
    //                "Web."));
    //     }
    // #else
    //     QMessageBox::critical(this, tr("Hardware requirements not met"),
    //                           tr("Your system does not meet the recommended hardware
    //                           requirements. "
    //                              "Compatibility reporting has been disabled."));
    // #endif
}

void MainWindow::OpenURL(const QUrl& url) {
    const bool open = QDesktopServices::openUrl(url);
    if (!open) {
        QMessageBox::warning(this, tr("Error opening URL"),
                             tr("Unable to open the URL \"%1\".").arg(url.toString()));
    }
}

void MainWindow::OnOpenModsPage() {
    OpenURL(QUrl(QStringLiteral("https://github.com/eden-emulator/yuzu-mod-archive")));
}

void MainWindow::OnOpenUserHandbook() {
    OpenURL(QUrl(QStringLiteral("https://git.eden-emu.dev/eden-emu/eden/src/branch/master/docs/user/README.md")));
}

void MainWindow::ToggleFullscreen() {
    if (!emulation_running) {
        return;
    }
    if (ui->action_Fullscreen->isChecked()) {
        ShowFullscreen();
    } else {
        HideFullscreen();
    }
}

// We're going to return the screen that the given window has the most pixels on
static QScreen* GuessCurrentScreen(QWidget* window) {
    const QList<QScreen*> screens = QGuiApplication::screens();
    return *std::max_element(
        screens.cbegin(), screens.cend(), [window](const QScreen* left, const QScreen* right) {
            const QSize left_size = left->geometry().intersected(window->geometry()).size();
            const QSize right_size = right->geometry().intersected(window->geometry()).size();
            return (left_size.height() * left_size.width()) <
                   (right_size.height() * right_size.width());
        });
}

bool MainWindow::UsingExclusiveFullscreen() {
    return Settings::values.fullscreen_mode.GetValue() == Settings::FullscreenMode::Exclusive ||
           QGuiApplication::platformName() == QStringLiteral("wayland") ||
           QGuiApplication::platformName() == QStringLiteral("wayland-egl");
}

void MainWindow::ShowFullscreen() {
    ingame_fullscreen = true;
    ingame_bar->hide();
    ingame_menu->hide();
    const auto show_fullscreen = [this](QWidget* window) {
        if (UsingExclusiveFullscreen()) {
            window->showFullScreen();
            return;
        }
        window->hide();
        // A maximized window keeps the work area and ignores the new geometry, so the taskbar
        // stays visible; the saved geometry restores the maximized state afterwards.
        window->setWindowState(window->windowState() & ~Qt::WindowMaximized);
        window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
        const auto screen_geometry = GuessCurrentScreen(window)->geometry();
        window->setGeometry(screen_geometry.x(), screen_geometry.y(), screen_geometry.width(),
                            screen_geometry.height() + 1);
        window->raise();
        window->showNormal();
    };

    if (ui->action_Single_Window_Mode->isChecked()) {
        UISettings::values.geometry = saveGeometry();

        launcher->SetInlineFullscreen(true);
        if constexpr (!Spotlight::ClientFrame) {
            ui->menubar->hide();
        }

        show_fullscreen(this);
    } else {
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
        show_fullscreen(render_window);
    }
    if (!fullscreen_hint_shown) {
        fullscreen_hint_shown = true;
        fullscreen_hint->ShowControls(
            ui->action_Single_Window_Mode->isChecked() ? static_cast<QWidget*>(this)
                                                       : render_window,
            Spotlight::KeyText(ingame_menu_action->shortcut()), ControllerKeys("In-game Menu"),
            Spotlight::KeyText(ui->action_Fullscreen->shortcut()));
    }
}

void MainWindow::HideFullscreen() {
    ingame_fullscreen = false;
    fullscreen_shader_active = false;
    ingame_menu->hide();
    fullscreen_hint->hide();
    fullscreen_shader_toast->hide();
    if (ui->action_Single_Window_Mode->isChecked()) {
        if (UsingExclusiveFullscreen()) {
            showNormal();
            restoreGeometry(UISettings::values.geometry);
        } else {
            hide();
            setWindowFlag(Qt::FramelessWindowHint, Spotlight::ClientFrame);
            restoreGeometry(UISettings::values.geometry);
            raise();
            show();
        }

        launcher->SetInlineFullscreen(false);
        if constexpr (!Spotlight::ClientFrame) {
            ui->menubar->show();
        }
    } else {
        if (UsingExclusiveFullscreen()) {
            render_window->showNormal();
            render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
        } else {
            render_window->hide();
            render_window->setWindowFlag(Qt::FramelessWindowHint, false);
            render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
            render_window->raise();
            render_window->show();
        }
    }
    OnToggleStatusBar();
}

void MainWindow::ToggleWindowMode() {
    if (ui->action_Single_Window_Mode->isChecked()) {
        // Render in the main window...
        render_window->BackupGeometry();
        ui->horizontalLayout->addWidget(render_window);
        render_window->setFocusPolicy(Qt::StrongFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->setFocus();
            launcher->SetInlineEmulation(true);
        }

    } else {
        // Render in a separate window...
        ui->horizontalLayout->removeWidget(render_window);
        render_window->setParent(nullptr);
        render_window->setFocusPolicy(Qt::NoFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->RestoreGeometry();
            launcher->SetInlineEmulation(false);
        }
    }
}

void MainWindow::ResetWindowSize(u32 width, u32 height) {
    const auto aspect_ratio = Layout::EmulationAspectRatio(Settings::values.aspect_ratio.GetValue(),
                                                           float(height) / width);
    const bool single_window = ui->action_Single_Window_Mode->isChecked();
    // Resizing a maximized window leaves it marked as maximized at the new size, stuck in a corner.
    if (single_window && isMaximized()) {
        showNormal();
    } else if (!single_window && render_window->isMaximized()) {
        // A stopped game hides the render window; showNormal() would show it again.
        if (render_window->isHidden()) {
            render_window->setWindowState(render_window->windowState() & ~Qt::WindowMaximized);
        } else {
            render_window->showNormal();
        }
    }
    const auto status_bar_height = !single_window ? 0
                                   : ui->action_Show_Status_Bar->isChecked()
                                       ? Spotlight::InGameBar::Height
                                       : Spotlight::InGameBar::CollapsedHeight;
    const auto menu_bar_height = single_window && menuBar()->isVisible() ? menuBar()->height() : 0;
    const auto title_bar_height =
        running_title_bar->isVisible() ? Spotlight::RunningTitleBar::Height : 0;
    const auto extra_height = menu_bar_height + title_bar_height + status_bar_height;

    auto* current_screen = this->screen();
    if (!current_screen) {
        current_screen = QGuiApplication::primaryScreen();
    }
    const auto available = current_screen ? current_screen->availableGeometry() : QRect();

    u32 target_h = height;
    u32 target_w = static_cast<u32>(std::round(height / aspect_ratio));

    if (!available.isEmpty()) {
        const int max_w = available.width();
        const int max_h = available.height() - extra_height;
        if (max_w > 0 && max_h > 0 &&
            (target_w > static_cast<u32>(max_w) || target_h > static_cast<u32>(max_h))) {
            const float scale = (std::min)(float(max_w) / target_w, float(max_h) / target_h);
            target_w = (std::max)(1U, static_cast<u32>(std::round(target_w * scale)));
            target_h = (std::max)(1U, static_cast<u32>(std::round(target_h * scale)));
        }
    }

    if (!single_window) {
        render_window->resize(target_w, target_h);
    } else {
        resize(target_w, target_h + extra_height);
    }
}

void MainWindow::ResetWindowSize720() {
    ResetWindowSize(Layout::ScreenUndocked::Width, Layout::ScreenUndocked::Height);
}

void MainWindow::ResetWindowSize900() {
    ResetWindowSize(1600U, 900U);
}

void MainWindow::ResetWindowSize1080() {
    ResetWindowSize(Layout::ScreenDocked::Width, Layout::ScreenDocked::Height);
}

void MainWindow::SetGameListMode(Settings::GameListMode mode) {
    ui->action_Grid_View->setChecked(mode == Settings::GameListMode::GridView);
    ui->action_Tree_View->setChecked(mode == Settings::GameListMode::TreeView);
    ui->action_Carousel_View->setChecked(mode == Settings::GameListMode::CarouselView);

    UISettings::values.game_list_mode = mode;
    ui->action_Show_Game_Name->setEnabled(mode != Settings::GameListMode::TreeView);

    CheckIconSize();
    game_list->ResetViewMode();
}

void MainWindow::SetGridView() {
    SetGameListMode(Settings::GameListMode::GridView);
}

void MainWindow::SetTreeView() {
    SetGameListMode(Settings::GameListMode::TreeView);
}

void MainWindow::SetCarouselView() {
    SetGameListMode(Settings::GameListMode::CarouselView);
}

void MainWindow::CheckIconSize() {
    auto& setting = UISettings::CurrentGameIconSize();

    // When in grid/carousel view mode, with text off
    // there is no point in having icons turned off
    const bool icons_required =
        UISettings::values.game_list_mode.GetValue() != Settings::GameListMode::TreeView &&
        !UISettings::values.show_game_name.GetValue();
    if (icons_required && setting.GetValue() == 0) {
        setting.SetValue(setting.GetDefault());
    }

    auto actions = game_size_actions->actions();
    for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
        if (setting.GetValue() == default_game_icon_sizes[i].first)
            actions.at(i)->setChecked(true);
    }

    // Update this if you add anything before None.
    actions.at(0)->setEnabled(!icons_required);
}

void MainWindow::ToggleShowGameName() {
    auto& setting = UISettings::values.show_game_name;
    const bool newValue = !setting.GetValue();
    ui->action_Show_Game_Name->setChecked(newValue);
    setting.SetValue(newValue);

    CheckIconSize();

    game_list->UpdateIconSizes();
    game_list->RefreshGameDirectory();
}

void MainWindow::OnConfigure() {
    const auto old_theme = UISettings::values.theme;
    const bool old_discord_presence = UISettings::values.enable_discord_presence.GetValue();
    const auto old_language_index = Settings::values.language_index.GetValue();
    const bool old_gamemode = UISettings::values.enable_gamemode.GetValue();
#ifdef __unix__
    const bool old_force_x11 = UISettings::values.gui_force_x11.GetValue();
#endif
    const bool old_nextendo_sync = Settings::values.nextendo_sync_profile.GetValue();
    const auto old_current_user = Settings::values.current_user.GetValue();
    const auto nextendo_pid = [] {
        const auto account = Common::Nextendo::LoadAccount(
            Service::Account::Nextendo::CurrentProfile(*QtCommon::system));
        return account ? account->pid : 0;
    };
    const u64 old_nextendo_pid = nextendo_pid();

    Settings::SetConfiguringGlobal(true);
    UISettings::values.fullscreen = ui->action_Fullscreen->isChecked();
    ConfigureDialog configure_dialog(this, hotkey_registry, input_subsystem.get(),
                                     vk_device_records, *QtCommon::system,
                                     !multiplayer_state->IsHostingPublicRoom());
    connect(&configure_dialog, &ConfigureDialog::LanguageChanged, this,
            &MainWindow::OnLanguageChanged);
    connect(&configure_dialog, &ConfigureDialog::ExternalContentDirsChanged, this,
            &MainWindow::OnGameListRefresh);
    connect(&configure_dialog, &ConfigureDialog::OpenGameFolderRequested, this,
            &MainWindow::OnGameListOpenDirectory);
    connect(&configure_dialog, &ConfigureDialog::FullscreenOnStartChanged,
            ui->action_Fullscreen, &QAction::setChecked);
    connect(&configure_dialog, &ConfigureDialog::ContentInstalled, this, [this] {
        game_list->PopulateAsync(UISettings::values.game_dirs);
        OnCheckFirmwareDecryption(false);
    });

    const auto result = configure_dialog.exec();
    if (result != QDialog::Accepted && !UISettings::values.configuration_applied &&
        !UISettings::values.reset_to_defaults) {
        // Runs if the user hit Cancel or closed the window, and did not ever press the Apply button
        // or `Reset to Defaults` button
        configure_dialog.RestoreInputConfiguration();
        return;
    }
    // Applied settings (OK, Apply or Reset to Defaults) replace the speed limit the hotkeys changed.
    speed_limit_before_hotkeys.reset();
    if (result == QDialog::Accepted) {
        // Only apply new changes if user hit Okay
        // This is here to avoid applying changes if the user hit Apply, made some changes, then hit
        // Cancel
        configure_dialog.ApplyConfiguration();
    } else if (UISettings::values.reset_to_defaults) {
        LOG_INFO(Frontend, "Resetting all settings to defaults");
        if (!Common::FS::RemoveFile(config->GetConfigFilePath())) {
            LOG_WARNING(Frontend, "Failed to remove configuration file");
        }
        if (!Common::FS::RemoveDirContentsRecursively(
                Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "custom")) {
            LOG_WARNING(Frontend, "Failed to remove custom configuration files");
        }
        if (!Common::FS::RemoveDirRecursively(
                Common::FS::GetEdenPath(Common::FS::EdenPath::CacheDir) / "game_list")) {
            LOG_WARNING(Frontend, "Failed to remove game metadata cache files");
        }

        // Explicitly save the game directories, since reinitializing config does not explicitly do
        // so.
        QVector<UISettings::GameDir> old_game_dirs = std::move(UISettings::values.game_dirs);
        QVector<u64> old_favorited_ids = std::move(UISettings::values.favorited_ids);

        Settings::values.disabled_addons.clear();

        config = std::make_unique<QtConfig>();
        UISettings::values.reset_to_defaults = false;

        UISettings::values.game_dirs = std::move(old_game_dirs);
        UISettings::values.favorited_ids = std::move(old_favorited_ids);

        InitializeRecentFileMenuActions();

        SetDefaultUIGeometry();
        RestoreUIState();
    }
    InitializeHotkeys();

    if (UISettings::values.theme != old_theme) {
        UpdateUITheme();
    }
    if (UISettings::values.enable_discord_presence.GetValue() != old_discord_presence) {
        SetDiscordEnabled(UISettings::values.enable_discord_presence.GetValue());
    }
    if (UISettings::values.enable_gamemode.GetValue() != old_gamemode) {
        SetGamemodeEnabled(UISettings::values.enable_gamemode.GetValue());
    }
#ifdef __unix__
    if (UISettings::values.gui_force_x11.GetValue() != old_force_x11) {
        GraphicsBackend::SetForceX11(UISettings::values.gui_force_x11.GetValue());
    }
#endif
    // Just ticked, or a sign-in or another user since.
    if (!old_nextendo_sync || nextendo_pid() != old_nextendo_pid ||
        Settings::values.current_user.GetValue() != old_current_user) {
        SyncNextendoProfile(true);
    }

    if (!multiplayer_state->IsHostingPublicRoom()) {
        multiplayer_state->UpdateCredentials();
    }

    emit UpdateThemedIcons();

    const auto reload = UISettings::values.is_game_list_reload_pending.exchange(false);
    if (reload || Settings::values.language_index.GetValue() != old_language_index) {
        game_list->PopulateAsync(UISettings::values.game_dirs);
    }

    UISettings::values.configuration_applied = false;

    config->SaveAllValues();

    if (result != QDialog::Accepted) {
        // Cancel after Apply: keep what was applied, drop the controller edits made after it
        configure_dialog.RestoreInputConfiguration();
    }

    if ((UISettings::values.hide_mouse || Settings::values.mouse_panning) && emulation_running) {
        render_window->installEventFilter(render_window);
        render_window->setAttribute(Qt::WA_Hover, true);
    } else {
        render_window->removeEventFilter(render_window);
        render_window->setAttribute(Qt::WA_Hover, false);
    }

    if (UISettings::values.hide_mouse) {
        mouse_hide_timer.start();
    }

    // Restart camera config
    if (emulation_running) {
        render_window->FinalizeCamera();
        render_window->InitializeCamera();
    }

    if (!UISettings::values.has_broken_vulkan) {
        renderer_status_action->setEnabled(!emulation_running);
    }

    UpdateStatusButtons();
    controller_dialog->refreshConfiguration();
    QtCommon::system->ApplySettings();

    if (configure_dialog.ReopenRequested()) {
        QTimer::singleShot(0, this, &MainWindow::OnConfigure);
    }
}

void MainWindow::OnConfigureTas() {
    ConfigureTasDialog dialog(this);
    const auto result = dialog.exec();

    if (result != QDialog::Accepted && !UISettings::values.configuration_applied) {
        Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
        return;
    } else if (result == QDialog::Accepted) {
        dialog.ApplyConfiguration();
        OnSaveConfig();
    }
}

void MainWindow::OnTasStartStop() {
    if (!emulation_running) {
        return;
    }

    // Disable system buttons to prevent TAS from executing a hotkey
    auto* controller =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    controller->ResetSystemButtons();

    input_subsystem->GetTas()->StartStop();
    OnTasStateChanged();
}

void MainWindow::OnTasRecord() {
    if (!emulation_running) {
        return;
    }
    if (is_tas_recording_dialog_active) {
        return;
    }

    // Disable system buttons to prevent TAS from recording a hotkey
    auto* controller =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    controller->ResetSystemButtons();

    const bool is_recording = input_subsystem->GetTas()->Record();
    if (!is_recording) {
        if (Settings::values.tas_show_recording_dialog.GetValue()) {
            is_tas_recording_dialog_active = true;

            bool answer = question(this, tr("TAS Recording"), tr("Overwrite file of player 1?"),
                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

            input_subsystem->GetTas()->SaveRecording(answer);
            is_tas_recording_dialog_active = false;
        } else {
            input_subsystem->GetTas()->SaveRecording(true);
        }
    }
    OnTasStateChanged();
}

void MainWindow::OnTasReset() {
    input_subsystem->GetTas()->Reset();
}

void MainWindow::OnToggleDockedMode() {
    const bool is_docked = Settings::IsDockedMode();
    auto* player_1 =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Player1);
    auto* handheld =
        QtCommon::system->HIDCore().GetEmulatedController(Core::HID::NpadIdType::Handheld);

    if (!is_docked && handheld->IsConnected()) {
        QMessageBox::warning(this, tr("Invalid config detected"),
                             tr("Handheld controller can't be used on docked mode. Pro "
                                "controller will be selected."));
        handheld->Disconnect();
        player_1->SetNpadStyleIndex(Core::HID::NpadStyleIndex::Fullkey);
        player_1->Connect();
        controller_dialog->refreshConfiguration();
    }

    Settings::values.use_docked_mode.SetValue(is_docked ? Settings::ConsoleMode::Handheld
                                                        : Settings::ConsoleMode::Docked);
    UpdateDockedButton();
    OnDockedModeChanged(is_docked, !is_docked, *QtCommon::system);
}

void MainWindow::OnToggleGpuAccuracy() {
    switch (Settings::values.gpu_accuracy.GetValue()) {
    case Settings::GpuAccuracy::Low:
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::High);
        break;
    case Settings::GpuAccuracy::High:
        Settings::values.gpu_accuracy.SetValue(Settings::GpuAccuracy::Low);
        break;
    }

    QtCommon::system->ApplySettings();
    UpdateGPUAccuracyButton();
}

void MainWindow::OnMute() {
    Settings::values.audio_muted = !Settings::values.audio_muted;
    UpdateVolumeUI();
}

void MainWindow::OnDecreaseVolume() {
    Settings::values.audio_muted = false;
    const auto current_volume = static_cast<s32>(Settings::values.volume.GetValue());
    int step = 5;
    if (current_volume <= 30) {
        step = 2;
    }
    if (current_volume <= 6) {
        step = 1;
    }
    Settings::values.volume.SetValue((std::max)(current_volume - step, 0));
    UpdateVolumeUI();
}

void MainWindow::OnIncreaseVolume() {
    Settings::values.audio_muted = false;
    const auto current_volume = static_cast<s32>(Settings::values.volume.GetValue());
    int step = 5;
    if (current_volume < 30) {
        step = 2;
    }
    if (current_volume < 6) {
        step = 1;
    }
    Settings::values.volume.SetValue(current_volume + step);
    UpdateVolumeUI();
}

void MainWindow::OnToggleAdaptingFilter() {
    auto filter = Settings::values.scaling_filter.GetValue();
    filter = Settings::ScalingFilter(u32(filter) + 1);
    if (u32(filter) > u32(Settings::EnumMetadata<Settings::ScalingFilter>::GetLast()))
        filter = Settings::EnumMetadata<Settings::ScalingFilter>::GetFirst();
    Settings::values.scaling_filter.SetValue(filter);
    UpdateFilterText();
}

void MainWindow::OnToggleGraphicsAPI() {
    auto api = Settings::values.renderer_backend.GetValue();
    switch (api) {
#ifdef HAS_OPENGL
    case Settings::RendererBackend::Vulkan:
        api = Settings::RendererBackend::OpenGL_GLSL;
        break;
    case Settings::RendererBackend::OpenGL_GLSL:
        api = Settings::RendererBackend::OpenGL_GLASM;
        break;
    case Settings::RendererBackend::OpenGL_GLASM:
        api = Settings::RendererBackend::OpenGL_SPIRV;
        break;
    case Settings::RendererBackend::OpenGL_SPIRV:
        api = Settings::RendererBackend::Null;
        break;
#else
    case Settings::RendererBackend::Vulkan:
        api = Settings::RendererBackend::Null;
        break;
#endif
    case Settings::RendererBackend::Null:
        api = Settings::RendererBackend::Vulkan;
        break;
    default:
        break;
    }
    Settings::values.renderer_backend.SetValue(api);
    UpdateAPIText();
}

void MainWindow::OnConfigurePerGame() {
    const u64 title_id = QtCommon::system->GetApplicationProcessProgramID();
    OpenPerGameConfiguration(title_id, current_game_path.toStdString());
}

static u64 ParseModStoreTitleId(const QString& title_id) {
    if (title_id.size() != 16) {
        return 0;
    }
    const auto id = title_id.toULongLong(nullptr, 16);
    return title_id.compare(QString::fromStdString(fmt::format("{:016X}", id)),
                            Qt::CaseInsensitive) == 0
               ? id
               : 0;
}

void MainWindow::OnOpenModStore() {
    QSet<QString> store_toggled;
    if (OpenModStore(this, 0, {}, store_toggled)) {
        OnGameListRefresh();
    }
}

bool MainWindow::OpenModStore(QWidget* parent, u64 initial_title_id, const std::string& file_name,
                              QSet<QString>& store_toggled) {
    auto& system = *QtCommon::system;
    if (mod_store_open || close_pending || boot_in_progress || emulation_running ||
        system.IsPoweredOn()) {
        return false;
    }
    if (nx_metadata_write_in_progress) {
        QMessageBox::information(parent, tr("Mod Store"),
                                 tr("Wait for the NX metadata edit to finish before browsing mods."));
        return false;
    }
    if (game_list->IsPopulating()) {
        QMessageBox::information(
            parent, tr("Mod Store"), tr("Wait for the game library to finish loading before browsing mods."));
        return false;
    }

    mod_store_open = true;
    game_list->SetScanEnabled(false, false);
    SCOPE_EXIT {
        game_list->SetScanEnabled(true);
        mod_store_open = false;
    };
    const auto lookup = [&](const QString& title_id) -> ModStore::GameIdentity {
        const auto id = ParseModStoreTitleId(title_id);
        if (id == 0) {
            return {};
        }

        const auto open_game = [&](FileSys::VirtualFile file) -> std::unique_ptr<Loader::AppLoader> {
            auto loader = Loader::GetLoader(system, std::move(file), id);
            u64 actual_id{};
            return loader && loader->ReadProgramId(actual_id) == Loader::ResultStatus::Success &&
                           actual_id == id
                       ? std::move(loader)
                       : nullptr;
        };
        const auto open_path = [&](const std::string& path) {
            return open_game(Core::GetGameFileFromPath(QtCommon::vfs, path));
        };
        const bool has_selected_path = id == initial_title_id && !file_name.empty();
        std::unique_ptr<Loader::AppLoader> loader;
        if (has_selected_path) {
            loader = open_path(file_name);
        } else {
            const auto path = Service::NS::GetGameListApplicationPath(system, id);
            if (path) {
                loader = open_path(*path);
            }
            if (!loader) {
                const auto* const model = game_list->GetModel();
                const auto matches =
                    model->match(model->index(0, 0), GameListItemPath::ProgramIdRole,
                                 QVariant::fromValue(qulonglong{id}), -1,
                                 Qt::MatchExactly | Qt::MatchRecursive);
                for (const auto& match : matches) {
                    const auto candidate = match.data(GameListItemPath::FullPathRole).toString()
                                               .toStdString();
                    if (!candidate.empty() && (!path || *path != candidate)) {
                        loader = open_path(candidate);
                        if (loader) {
                            break;
                        }
                    }
                }
            }
            if (!loader) {
                loader = open_game(system.GetContentProvider().GetEntryUnparsed(
                    id, FileSys::ContentRecordType::Program));
            }
        }
        if (!loader) {
            return {};
        }

        ModStore::GameIdentity result{.installed = true};
        if (const auto build = loader->ReadMainBuildIdHex(system);
            build && build->find_first_not_of('0') != std::string::npos) {
            result.build_id = QString::fromStdString(*build);
        }
        const bool updatable = loader->IsRomFSUpdatable();
        const bool native_nx = updatable && loader->GetFileType() == Loader::FileType::NX;
        std::string version;
        if (!native_nx && loader->ReadDisplayVersion(version) == Loader::ResultStatus::Success) {
            result.display_version = QString::fromStdString(version).trimmed();
        }
        if (result.display_version.isEmpty() && updatable) {
            const FileSys::PatchManager patch{id, system.GetFileSystemController(),
                                             system.GetContentProvider()};
            if (const auto control = patch.GetControlMetadata().first) {
                result.display_version = QString::fromStdString(control->GetVersionString()).trimmed();
            }
        }
        return result;
    };

    QList<ModStore::LibraryGame> library_games;
    const auto* const library = game_list->GetModel();
    for (const auto& game :
         library->match(library->index(0, 0), GameListItem::TypeRole,
                        QVariant::fromValue(GameListItemType::Game), -1,
                        Qt::MatchExactly | Qt::MatchRecursive)) {
        const auto id = game.data(GameListItemPath::ProgramIdRole).toULongLong();
        if (id != 0) {
            library_games.append({.title_id = QString::fromStdString(fmt::format("{:016X}", id)),
                                  .name = game.data(GameListItemPath::TitleRole).toString(),
                                  .icon = QIcon(game.data(Qt::DecorationRole).value<QPixmap>())});
        }
    }
    std::ranges::sort(library_games, [](const auto& a, const auto& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    return ModStore::ShowDialog(
        parent, {.base_url = QString::fromStdString(UISettings::values.mod_store_api_url.GetValue()),
                 .initial_title_id =
                     initial_title_id == 0 ? QString{}
                                           : QString::fromStdString(fmt::format("{:016X}", initial_title_id)),
                 .mods_root = QDir(QString::fromStdString(
                                      Common::FS::GetEdenPathString(Common::FS::EdenPath::LoadDir)))
                                  .absolutePath(),
                 .game_identity = lookup,
                 .is_enabled = [](const QString& title_id, const QString& addon) {
                     const auto id = ParseModStoreTitleId(title_id);
                     if (id == 0 || addon.isEmpty()) {
                         return false;
                     }
                     const auto& disabled = Settings::values.disabled_addons;
                     const auto it = disabled.find(id);
                     return it == disabled.end() ||
                            std::ranges::find(it->second, addon.toStdString()) == it->second.end();
                 },
                 .set_enabled = [&](const QString& title_id, const QString& addon, bool enabled) {
                     const auto id = ParseModStoreTitleId(title_id);
                     if (id == 0 || addon.isEmpty() || system.IsPoweredOn() ||
                         !config->SetAddonEnabled(id, addon.toStdString(), enabled)) {
                         return false;
                     }
                     if (id == initial_title_id) {
                         store_toggled.insert(addon);
                     }
                     return true;
                 },
                 .library_games = std::move(library_games)});
}

void MainWindow::OpenPerGameConfiguration(u64 title_id, const std::string& file_name) {
    Settings::SetConfiguringGlobal(false);
    ConfigurePerGame dialog(this, title_id, file_name, vk_device_records, *QtCommon::system);
    dialog.LoadFromFile(Core::GetGameFileFromPath(QtCommon::vfs, file_name));
    // Artwork saved here wins over a SteamGridDB lookup still running for the game.
    connect(&dialog, &ConfigurePerGame::CustomArtworkChanged, game_list,
            &GameList::OnCustomArtworkChanged);
    connect(&dialog, &ConfigurePerGame::OpenSaveDataRequested, this, [this, title_id, file_name] {
        OnGameListOpenFolder(title_id, GameListOpenTarget::SaveData, file_name);
    });
    connect(&dialog, &ConfigurePerGame::BrowseModsRequested, this, [&] {
        QSet<QString> store_toggled;
        const bool changed = OpenModStore(&dialog, title_id, file_name, store_toggled);
        if (changed || !store_toggled.isEmpty()) {
            dialog.ReloadAddonsAfterModStore(store_toggled);
            UISettings::values.is_game_list_reload_pending.exchange(true);
        }
    });

    const auto result = dialog.exec();

    // The game file is written once the dialog is gone and no longer holds it open.
    if (auto archive_icon = dialog.TakeArchiveIcon(); archive_icon || dialog.ArchiveEditRequested()) {
        QTimer::singleShot(0, this, [this, title_id, file_name, archive_icon = std::move(archive_icon),
                                     edit = dialog.ArchiveEditRequested()] {
            WriteArchiveFromSettings(title_id, file_name, archive_icon, edit);
        });
    }

    // A rename only changes the shown name: no rescan of the library.
    if (const auto renamed = dialog.RenamedTitle()) {
        game_list->SetGameTitle(title_id, *renamed);
    }
    // The running game shows the new name in the window title too, also when the custom name was
    // removed from a game that has no name of its own to show in the library.
    if (dialog.TitleChanged() && emulation_running && title_id != 0 &&
        title_id == QtCommon::system->GetApplicationProcessProgramID()) {
        OnApplicationChanged(title_id);
    }

    if (result != QDialog::Accepted && !UISettings::values.configuration_applied) {
        // Artwork saved before a failed save of the rest stays saved after Cancel: show it.
        if (UISettings::values.is_game_list_reload_pending.exchange(false)) {
            OnGameListRefresh();
        }
        Settings::RestoreGlobalState(QtCommon::system->IsPoweredOn());
        return;
    }
    speed_limit_before_hotkeys.reset();
    if (result == QDialog::Accepted) {
        dialog.ApplyConfiguration();
    }

    const auto reload = UISettings::values.is_game_list_reload_pending.exchange(false);
    if (reload) {
        OnGameListRefresh();
    }

    // Do not cause the global config to write local settings into the config file
    const bool is_powered_on = QtCommon::system->IsPoweredOn();
    Settings::RestoreGlobalState(is_powered_on);
    QtCommon::system->HIDCore().ReloadInputDevices();

    UISettings::values.configuration_applied = false;

    if (!is_powered_on) {
        config->SaveAllValues();
    }
}

void MainWindow::OnLoadAmiibo() {
    if (QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning() ||
        is_amiibo_file_select_active)
        return;

    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();

    // Remove amiibo if one is connected
    if (virtual_amiibo->GetCurrentState() == InputCommon::VirtualAmiibo::State::TagNearby) {
        virtual_amiibo->CloseAmiibo();
        QMessageBox::warning(this, tr("Amiibo"), tr("The current amiibo has been removed"));
        return;
    }

    if (virtual_amiibo->GetCurrentState() != InputCommon::VirtualAmiibo::State::WaitingForAmiibo) {
        QMessageBox::warning(this, tr("Error"), tr("The current game is not looking for amiibos"));
        return;
    }

    is_amiibo_file_select_active = true;
    const QString extensions{QStringLiteral("*.bin")};
    const QString file_filter = tr("Amiibo File (%1);; All Files (*.*)").arg(extensions);
    const QString filename = QFileDialog::getOpenFileName(this, tr("Load Amiibo"), {}, file_filter);
    is_amiibo_file_select_active = false;

    if (filename.isEmpty()) {
        return;
    }

    LoadAmiibo(filename);
}

// TODO(crueter): does this need to be ported to QML?
bool MainWindow::question(QWidget* parent, const QString& title, const QString& text,
                          QMessageBox::StandardButtons buttons,
                          QMessageBox::StandardButton defaultButton) {
    QMessageBox* box_dialog = new QMessageBox(parent);
    box_dialog->setWindowTitle(title);
    box_dialog->setText(text);
    box_dialog->setStandardButtons(buttons);
    box_dialog->setDefaultButton(defaultButton);

    ControllerNavigation* controller_navigation =
        new ControllerNavigation(QtCommon::system->HIDCore(), box_dialog);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent,
            [box_dialog](Qt::Key key) {
                QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                QCoreApplication::postEvent(box_dialog, event);
            });
    int res = box_dialog->exec();

    controller_navigation->UnloadController();
    return res == QMessageBox::Yes;
}

void MainWindow::LoadAmiibo(const QString& filename) {
    auto* virtual_amiibo = input_subsystem->GetVirtualAmiibo();
    const QString title = tr("Error loading Amiibo data");
    // Remove amiibo if one is connected
    if (virtual_amiibo->GetCurrentState() == InputCommon::VirtualAmiibo::State::TagNearby) {
        virtual_amiibo->CloseAmiibo();
        QMessageBox::warning(this, tr("Amiibo"), tr("The current amiibo has been removed"));
        return;
    }

    switch (virtual_amiibo->LoadAmiibo(filename.toStdString())) {
    case InputCommon::VirtualAmiibo::Info::NotAnAmiibo:
        QMessageBox::warning(this, title, tr("The selected file is not a valid amiibo"));
        break;
    case InputCommon::VirtualAmiibo::Info::UnableToLoad:
        QMessageBox::warning(this, title, tr("The selected file is already on use"));
        break;
    case InputCommon::VirtualAmiibo::Info::WrongDeviceState:
        QMessageBox::warning(this, title, tr("The current game is not looking for amiibos"));
        break;
    case InputCommon::VirtualAmiibo::Info::Unknown:
        QMessageBox::warning(this, title, tr("An unknown error occurred"));
        break;
    default:
        break;
    }
}

void MainWindow::OnOpenRootDataFolder() {
    QtCommon::Game::OpenRootDataFolder();
}

void MainWindow::OnOpenNANDFolder() {
    QtCommon::Game::OpenNANDFolder();
}

void MainWindow::OnOpenSDMCFolder() {
    QtCommon::Game::OpenSDMCFolder();
}

void MainWindow::OnOpenModFolder() {
    QtCommon::Game::OpenModFolder();
}

void MainWindow::OnOpenLogFolder() {
    QtCommon::Game::OpenLogFolder();
}

void MainWindow::OnVerifyInstalledContents() {
    QtCommon::Content::VerifyInstalledContents();
}

void MainWindow::OnInstallFirmware() {
    QtCommon::Content::InstallFirmware();
    OnCheckFirmwareDecryption();
}

void MainWindow::OnInstallFirmwareFromZIP() {
    QtCommon::Content::InstallFirmwareZip();
    OnCheckFirmwareDecryption();
}

// TODO(crueter): QtCommon this: game list populate can be a signal?
void MainWindow::OnInstallDecryptionKeys() {
    // Don't do this while emulation is running.
    if (QtCommon::emu_thread != nullptr && QtCommon::emu_thread->IsRunning())
        return;

    QtCommon::Content::InstallKeys();

    game_list->PopulateAsync(UISettings::values.game_dirs);
    OnCheckFirmwareDecryption();
}

void MainWindow::MaybeShowStartupWhatsNew() {
    if (!startup_whats_new_pending || !isVisible() || UISettings::values.first_start.GetValue() ||
        boot_in_progress || emulation_running || close_pending) {
        return;
    }
    constexpr auto retry_connection = Qt::ConnectionType(Qt::QueuedConnection |
                                                         Qt::SingleShotConnection |
                                                         Qt::UniqueConnection);
#ifdef ENABLE_UPDATE_CHECKER
    if (update_check_running) {
        connect(&update_watcher, &QFutureWatcher<UpdateChecker::CheckResult>::finished, this,
                &MainWindow::MaybeShowStartupWhatsNew, retry_connection);
        return;
    }
#endif
    if (auto* const modal = QApplication::activeModalWidget()) {
        if (auto* const dialog = qobject_cast<QDialog*>(modal)) {
            connect(dialog, &QDialog::finished, this, &MainWindow::MaybeShowStartupWhatsNew,
                    retry_connection);
        }
        return;
    }
    OnWhatsNew();
}

void MainWindow::OnWhatsNew() {
    startup_whats_new_pending = false;
    if (whats_new_dialog && whats_new_dialog->isVisible()) {
        whats_new_dialog->raise();
        whats_new_dialog->activateWindow();
        return;
    }

    const auto build = WhatsNewDialog::ParseBuildTag(Common::g_build_release_tag);
    const auto date = QDate::fromString(QString::fromLatin1(Common::g_build_date).left(10),
                                       Qt::ISODate);
    whats_new_dialog = new WhatsNewDialog(build, date, LICHII_USE_PENDING_RELEASE_NOTES, this);
    whats_new_dialog->setAttribute(Qt::WA_DeleteOnClose);
    ConfigurationShared::FitToScreen(whats_new_dialog, QSize(442, 576));
    whats_new_dialog->show();

    if (build && build != UISettings::values.last_seen_release.GetValue()) {
        config->SaveLastSeenRelease(build);
    }
}

void MainWindow::OnAbout() {
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}

void MainWindow::OnEdenDependencies() {
    DepsDialog depsDialog(this);
    depsDialog.exec();
}

void MainWindow::OnApplyRecommendedSettings() {
    if (emulation_running) {
        return;
    }
    // A game can start behind the question; leave its settings alone then.
    if (!RecommendedSettingsDialog::Ask(this) || emulation_running) {
        return;
    }
    const auto changes = FrontendCommon::ApplyRecommendedSettings(Settings::values);
    LOG_INFO(Config, "Applied the recommended settings: {} changed", changes.size());
    Settings::UpdateGPUAccuracy();
    OnSaveConfig();
    UpdateStatusButtons();
    RecommendedSettingsDialog::ShowApplied(this, changes);
}

void MainWindow::OnRestorePreviousSettings() {
    if (emulation_running) {
        return;
    }
    const auto changes = FrontendCommon::RestorePreviousSettings(Settings::values);
    LOG_INFO(Config, "Restored the settings from before the recommended ones: {} changed",
             changes.size());
    Settings::UpdateGPUAccuracy();
    OnSaveConfig();
    UpdateStatusButtons();
    RecommendedSettingsDialog::ShowRestored(this, changes);
}

void MainWindow::OnDataDialog() {
    DataDialog dataDialog(this);
    dataDialog.exec();

    // refresh stuff in case it was cleared
    OnGameListRefresh();
}

void MainWindow::OnFocusLibrarySearch() {
    game_list->SetFilterFocus();
}

void MainWindow::OnToggleStatusBar() {
    ingame_bar->SetCollapsed(!ui->action_Show_Status_Bar->isChecked());
    ingame_bar->setVisible(emulation_running && !ingame_fullscreen && !loading_screen->isVisible());
    UpdateLetterboxBars();
}

void MainWindow::UpdateLetterboxBars() {
    int leftover = 0;
    const bool title_shown = running_title_bar->isVisible();
    const int column = height() - (menuBar()->isVisible() ? menuBar()->height() : 0) -
                       (title_shown ? Spotlight::RunningTitleBar::Height : 0) -
                       Spotlight::InGameBar::Height;
    if (emulation_running && !ingame_fullscreen && ingame_bar->isVisible() &&
        ui->action_Show_Status_Bar->isChecked() && ui->action_Single_Window_Mode->isChecked() &&
        width() > 0 && column > 0) {
        const qreal ratio = render_window->windowPixelRatio();
        const auto area = static_cast<u32>(column * ratio);
        const auto layout = Layout::DefaultFrameLayout(static_cast<u32>(width() * ratio), area);
        leftover = static_cast<int>((area - layout.screen.GetHeight()) / ratio);
    }
    constexpr int max_growth = 12;
    const int top = title_shown ? std::min(leftover / 2, max_growth) : 0;
    running_title_bar->SetExtraHeight(top);
    ingame_bar->SetExtraHeight(std::min(leftover - top, max_growth));
}

void MainWindow::OnTogglePerfOverlay() {
    if (perf_overlay)
        perf_overlay->setVisible(ui->action_Show_Performance_Overlay->isChecked());
}

#ifdef HAS_RESHADE
void MainWindow::OnPostProcessingShaders() {
    if (post_processing_dialog == nullptr) {
        post_processing_dialog = new ConfigurePostProcessing(this);
        connect(post_processing_dialog, &QDialog::finished, post_processing_dialog, [this]() {
            post_processing_dialog->deleteLater();
            post_processing_dialog = nullptr;
            UpdatePostShaderText();
        });
    }

    post_processing_dialog->show();
    post_processing_dialog->raise();
    post_processing_dialog->activateWindow();
}
#endif

void MainWindow::OnGameListRefresh() {
    // Resets metadata cache and reloads
    QtCommon::Game::ResetMetadata(false);
    game_list->RefreshGameDirectory();
    game_list->RefreshExternalContent();
    SetFirmwareVersion();
}

#ifdef LICHII_NX_TOOLS
bool MainWindow::CanWriteNxMetadata() {
    if (boot_in_progress || close_pending || nx_metadata_write_in_progress) {
        return false;
    }
    if (emulation_running || QtCommon::system->IsPoweredOn()) {
        ArchiveNotice(this, tr("NX Archive"),
                                 tr("Stop emulation before editing NX metadata."));
        return false;
    }
    return true;
}

bool MainWindow::PrepareNxMetadataWrite() {
    if (!CanWriteNxMetadata()) {
        return false;
    }
    nx_metadata_write_in_progress = true;
    game_list->SetScanEnabled(false);
    // The manual provider keeps NX sessions alive through registered updates and DLC.
    QtCommon::provider->ClearAllEntries();
    return true;
}

void MainWindow::OnGameListEditNxMetadata(const std::string& game_path) {
    if (CanWriteNxMetadata()) {
        nx_tools->EditNxMetadata(game_path, [this] { return PrepareNxMetadataWrite(); });
    }
}

void MainWindow::OnGameListNxTool(const std::string& game_path,
                                  void (NxTools::*tool)(const std::string&)) {
    // An archive edited in place must not be read by a running game.
    if (emulation_running && tool != &NxTools::ConvertToNx &&
        current_game_path == QString::fromStdString(game_path)) {
        ArchiveNotice(this, tr("NX Archive"),
                                 tr("Stop the game before changing its NX archive."));
        return;
    }
    (nx_tools->*tool)(game_path);
}
#endif

void MainWindow::StartNupJob(
    const std::string& game_path, const QString& operation, const QString& title,
    const QPixmap& icon,
    std::function<std::function<void()>(const NupJobProgress&, const std::atomic_bool*)> work) {
    nup_running = true;
    nup_source_path = game_path;
    game_list->SetNupConversionRunning(true);
    nup_cancel = std::make_shared<std::atomic_bool>(false);
    nup_progress = new NupProgressDialog(operation, title, icon, this);
    connect(nup_progress, &NupProgressDialog::CancelRequested, this, [this, cancel = nup_cancel] {
        cancel->store(true);
        if (nup_progress) {
            nup_progress->SetCancelling();
        }
    });
    nup_progress->show();

    nup_watcher.setFuture(QtConcurrent::run([this, work = std::move(work), cancel = nup_cancel] {
        QString last_step;
        std::chrono::steady_clock::time_point last_update{};
        const NupJobProgress report = [&](const QString& step,
                                          const FileSys::Nup::PackProgress& progress) {
            const auto now = std::chrono::steady_clock::now();
            const bool new_step = step != last_step;
            if (!new_step && now - last_update < std::chrono::milliseconds(200)) {
                return;
            }
            last_step = step;
            last_update = now;
            QMetaObject::invokeMethod(
                this,
                [this, step, new_step, progress] {
                    if (!nup_progress) {
                        return;
                    }
                    if (new_step && !step.isEmpty()) {
                        nup_progress->SetStep(step);
                    }
                    nup_progress->SetProgress(progress);
                },
                Qt::QueuedConnection);
        };
        auto done = work(report, cancel.get());
        QMetaObject::invokeMethod(
            this,
            [this, done = std::move(done)] {
                nup_running = false;
                nup_source_path.clear();
                game_list->SetNupConversionRunning(false);
                if (nup_progress) {
                    nup_progress->Finish();
                    nup_progress->deleteLater();
                    nup_progress = nullptr;
                }
                nup_cancel.reset();
                if (done) {
                    done();
                }
            },
            Qt::QueuedConnection);
        return FileSys::Nup::PackResult::Success;
    }));
}

void MainWindow::OnGameListConvertToNup(const std::string& game_path) {
    if (nup_running) {
        return;
    }
    if (const auto root = FileSys::Nup::DumpRootFromMain(
            std::filesystem::path{Common::FS::ToU8String(game_path)})) {
        ConvertDumpToNup(*root, game_path);
    } else {
        ConvertPackageToNup(game_path);
    }
}

void MainWindow::ConvertDumpToNup(const std::filesystem::path& root, const std::string& game_path) {
    const QString convert_title = tr("Convert to NUP");
    auto archive = FileSys::Nup::DefaultArchivePath(root);
    const bool resume = FileSys::Nup::HasResumableJournal(archive);
    bool delete_sources = resume;
    FileSys::Nup::Metadata metadata;
    QString progress_title;
    QPixmap progress_icon;
    if (resume) {
        if (Spotlight::ConfirmDialog(this, tr("Resume conversion"),
                                     tr("An earlier conversion of this game was interrupted. "
                                        "Pick up where it stopped?"),
                                     tr("Resume"), QStringLiteral("archive"))
                .exec() != QDialog::Accepted) {
            return;
        }
    } else {
        NupConvertSummary summary;
        summary.source_name = QString::fromStdString(Common::FS::PathToUTF8String(root));
        summary.archive = archive;
        summary.detected = FileSys::Nup::ProbeMetadata(root);
        summary.offer_low_storage = true;
        // What the packer finds: base/ (or a bare dump), updates/<version>/ and dlc/<id>/<n>/.
        const auto tree_bytes = [](const std::filesystem::path& dir) {
            u64 bytes{};
            std::error_code ec;
            for (std::filesystem::recursive_directory_iterator it(dir, ec), end; !ec && it != end;
                 it.increment(ec)) {
                if (it->is_regular_file(ec)) {
                    bytes += it->file_size(ec);
                }
            }
            return bytes;
        };
        const auto child = [](const std::filesystem::path& dir, std::string_view name) {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (QString::fromStdString(Common::FS::PathToUTF8String(entry.path().filename()))
                        .compare(QString::fromUtf8(name.data(), qsizetype(name.size())),
                                 Qt::CaseInsensitive) == 0) {
                    return entry.path();
                }
            }
            return std::filesystem::path{};
        };
        const auto base = child(root, "base");
        const auto updates = child(root, "updates");
        const auto dlc = child(root, "dlc");
        if (!base.empty() || (updates.empty() && dlc.empty())) {
            summary.packages.push_back({tr("Base game"), tr("ExeFS and RomFS of the dump"),
                                        tree_bytes(base.empty() ? root : base), false});
        }
        std::error_code ec;
        if (!updates.empty()) {
            for (const auto& entry : std::filesystem::directory_iterator(updates, ec)) {
                if (entry.is_directory(ec)) {
                    const auto name = QString::fromStdString(
                        Common::FS::PathToUTF8String(entry.path().filename()));
                    summary.packages.push_back({tr("Update %1").arg(name),
                                                tr("Stored next to the base, kept as dumped"),
                                                tree_bytes(entry.path()), false});
                }
            }
        }
        if (!dlc.empty()) {
            for (const auto& entry : std::filesystem::directory_iterator(dlc, ec)) {
                if (entry.is_directory(ec)) {
                    summary.packages.push_back(
                        {tr("DLC %1").arg(QString::fromStdString(
                             Common::FS::PathToUTF8String(entry.path().filename()))),
                         tr("Add-on content"), tree_bytes(entry.path()), false});
                }
            }
        }
#ifdef LICHII_NX_TOOLS
        summary.offer_nx = NxTools::CanConvert(game_path);
#endif
        NupConvertDialog dialog(std::move(summary), this);
        const int result = dialog.exec();
#ifdef LICHII_NX_TOOLS
        if (result == ArchiveConvertDialog::OtherFormat) {
            OnGameListNxTool(game_path, &NxTools::ConvertToNx);
            return;
        }
#endif
        if (result != QDialog::Accepted) {
            return;
        }
        delete_sources = dialog.DeleteSources();
        metadata = dialog.Metadata();
        archive = dialog.Archive();
        progress_title = dialog.DisplayName();
        progress_icon = dialog.CurrentIcon();
    }

    StartNupJob(game_path, tr("Converting to NUP"), progress_title, progress_icon,
                [this, root, archive, delete_sources, metadata](const NupJobProgress& report,
                                                                const std::atomic_bool* cancel)
                    -> std::function<void()> {
                    std::string error;
                    const auto result = FileSys::Nup::PackDump(
                        root, archive, {.delete_sources = delete_sources, .metadata = metadata},
                        [&](const FileSys::Nup::PackProgress& p) { report({}, p); }, cancel,
                        &error);
                    return [this, result, delete_sources, error] {
                        switch (result) {
                        case FileSys::Nup::PackResult::Success:
                            ArchiveNotice(
                                this, tr("Conversion Complete"),
                                delete_sources ? tr("The NUP was created and the loose folder was "
                                                    "removed.")
                                               : tr("The NUP was created. You can delete the "
                                                    "loose folder if you no longer need it."),
                                                    ArchiveTone::Success);
                            OnGameListRefresh();
                            break;
                        case FileSys::Nup::PackResult::Cancelled:
                            if (delete_sources) {
                                ArchiveNotice(
                                    this, tr("Conversion Cancelled"),
                                    tr("The conversion can be resumed by choosing Convert to NUP "
                                       "again."));
                            }
                            break;
                        case FileSys::Nup::PackResult::InvalidSource:
                        case FileSys::Nup::PackResult::IoError:
                            ArchiveNotice(this, tr("Conversion Failed"),
                                                  QString::fromStdString(error),
                                                  ArchiveTone::Error);
                            break;
                        }
                    };
                });
}

void MainWindow::ConvertPackageToNup(const std::string& game_path) {
    const QString convert_title = tr("Convert to NUP");
    const auto file = QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read);
    const auto loader = Loader::GetLoader(*QtCommon::system, file);
    u64 program_id{};
    if (loader == nullptr || loader->ReadProgramId(program_id) != Loader::ResultStatus::Success ||
        program_id == 0) {
        ArchiveNotice(this, convert_title, tr("This game cannot be read."));
        return;
    }
    std::string error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto source = FileSys::Nup::ReadGameSource(QtCommon::system->GetContentProvider(),
                                               QtCommon::provider.get(), file, program_id,
                                               &error);
    QApplication::restoreOverrideCursor();
    if (!source) {
        ArchiveNotice(this, convert_title,
                             tr("This game cannot be converted: %1")
                                 .arg(QString::fromStdString(error)));
        return;
    }

    auto archive = std::filesystem::path{Common::FS::ToU8String(game_path)};
    archive.replace_extension(".nup");
    NupConvertSummary summary;
    summary.source_name = QFileInfo(QString::fromStdString(game_path)).fileName();
    summary.archive = archive;
    summary.detected = source->metadata;
    summary.extra_bytes = FileSys::Nup::ConvertSpaceNeeded({});
    for (const auto& package : source->packages) {
        const auto id = QStringLiteral("%1").arg(package.title_id, 16, 16, QLatin1Char('0')).toUpper();
        switch (package.kind) {
        case FileSys::Nup::SourceKind::Base:
            summary.packages.push_back({tr("Base game"), id, package.size, false});
            break;
        case FileSys::Nup::SourceKind::Update: {
            QString version;
            if (package.nacp != nullptr) {
                version = QString::fromStdString(FileSys::NACP{package.nacp}.GetVersionString());
            }
            summary.packages.push_back(
                {version.isEmpty() ? tr("Update") : tr("Update %1").arg(version),
                 tr("v%1 · only the files it changes are stored").arg(package.version),
                 package.size, true});
            break;
        }
        case FileSys::Nup::SourceKind::AddOn:
            summary.packages.push_back({tr("DLC"), id, package.size, true});
            break;
        }
    }
#ifdef LICHII_NX_TOOLS
    summary.offer_nx = NxTools::CanConvert(game_path);
#endif
    NupConvertDialog dialog(std::move(summary), this);
    const int result = dialog.exec();
#ifdef LICHII_NX_TOOLS
    if (result == ArchiveConvertDialog::OtherFormat) {
        OnGameListNxTool(game_path, &NxTools::ConvertToNx);
        return;
    }
#endif
    if (result != QDialog::Accepted) {
        return;
    }
    const auto included = dialog.IncludedPackages();
    std::vector<FileSys::Nup::SourcePackage> kept;
    for (size_t i = 0; i < source->packages.size(); ++i) {
        if (included[i]) {
            kept.push_back(std::move(source->packages[i]));
        }
    }
    source->packages = std::move(kept);
    archive = dialog.Archive();
    FileSys::Nup::PackOptions options{.metadata = dialog.Metadata()};

    StartNupJob(
        game_path, tr("Converting to NUP"), dialog.DisplayName(), dialog.CurrentIcon(),
        [this, source = std::make_shared<FileSys::Nup::GameSource>(std::move(*source)), archive,
         options](const NupJobProgress& report,
                  const std::atomic_bool* cancel) -> std::function<void()> {
            std::string error;
            const auto result = FileSys::Nup::ConvertToNup(
                *source, archive, options,
                [&](FileSys::Nup::ConvertStep step, const FileSys::Nup::PackProgress& p) {
                    report(step == FileSys::Nup::ConvertStep::Extract
                               ? tr("Step 1 of 2 · Reading the game")
                               : tr("Step 2 of 2 · Packing the NUP"),
                           p);
                },
                cancel, &error);
            return [this, result, error, archive] {
                switch (result) {
                case FileSys::Nup::PackResult::Success:
                    ArchiveNotice(
                        this, tr("Conversion Complete"),
                        tr("The NUP was created:\n%1\n\nThe original files are untouched; delete "
                           "them if you no longer need them.")
                            .arg(QString::fromStdString(Common::FS::PathToUTF8String(archive))),
                            ArchiveTone::Success);
                    OnGameListRefresh();
                    break;
                case FileSys::Nup::PackResult::Cancelled:
                    break;
                case FileSys::Nup::PackResult::InvalidSource:
                case FileSys::Nup::PackResult::IoError:
                    ArchiveNotice(this, tr("Conversion Failed"),
                                          QString::fromStdString(error), ArchiveTone::Error);
                    break;
                }
            };
        });
}

namespace {
/// The game's title as its loader reports it, else the file name without extension.
QString GameTitleName(const std::string& game_path) {
    if (const auto loader = Loader::GetLoader(
            *QtCommon::system, QtCommon::vfs->OpenFile(game_path, FileSys::OpenMode::Read))) {
        std::string title;
        if (loader->ReadTitle(title) == Loader::ResultStatus::Success) {
            return QString::fromStdString(title);
        }
    }
    return QFileInfo(QString::fromStdString(game_path)).completeBaseName();
}
} // namespace

/// Opens the shader precache dialog for the game and starts the build, or brings a running one to
/// the front. Refused while a game runs or while this game is being converted.
void MainWindow::OnGameListPrepareShaderWarm(u64 program_id, const std::string& game_path) {
    if (shader_warm_dialog) {
        if (shader_warm_dialog->IsRunning()) {
            shader_warm_dialog->raise();
            shader_warm_dialog->activateWindow();
            return;
        }
        shader_warm_dialog->close();
    }
    if (QtCommon::emu_thread != nullptr) {
        QMessageBox::information(this, tr("Precache Shaders"),
                                 tr("Close the running game first."));
        return;
    }
    if (nup_running && game_path == nup_source_path) {
        QMessageBox::information(this, tr("Precache Shaders"),
                                 tr("This game is being converted. Try again when it is done."));
        return;
    }

    auto path = std::filesystem::path{std::u8string{game_path.begin(), game_path.end()}};
    // Loose dumps are listed by their exefs folder (FormatGameName); the loader opens its main.
    if (std::error_code ec; std::filesystem::is_directory(path, ec)) {
        path /= "main";
    }
    shader_warm_dialog =
        new ShaderWarmDialog(*QtCommon::system, path, program_id, GameTitleName(game_path), this);
    connect(shader_warm_dialog, &QDialog::finished, shader_warm_dialog, &QObject::deleteLater);
    shader_warm_dialog->show();
    shader_warm_dialog->Start();
}

/// Precompile All Shaders: turning it on records the request, sets the game's precache to Always
/// and builds the precache file now; the compiling itself runs on the loading screen of the next
/// start. Turning it off keeps the files and the game's precache setting.
void MainWindow::OnGameListPrecompileShaders(u64 program_id, const std::string& game_path,
                                             bool on) {
    if (!SetShaderWarmRequested(program_id, on)) {
        QMessageBox::warning(this, tr("Precompile All Shaders"),
                             tr("The setting could not be saved in the shader folder."));
        return;
    }
    LOG_INFO(Frontend, "Precompile all shaders for {:016x}: {}", program_id, on ? "on" : "off");
    if (on) {
        Config::SaveGameValue(fmt::format("{:016X}", program_id), Settings::values.precache_shaders,
                              std::to_string(static_cast<u32>(Settings::PrecacheShaders::Always)));
        if (!emulation_running) {
            OnGameListPrepareShaderWarm(program_id, game_path);
        }
    }
}

/// With precache_shaders on, or Precompile All Shaders for the game: a title precached before is
/// precached again only when the driver or its precache file changed (the renderer checks). For
/// one never precached, Ask asks whether to precache before the game starts. A title whose
/// precache file is missing or stale (another cache version, an older builder) gets one built
/// from its game files first. A stopped or failed build
/// starts the game with the file it had, or without precaching when there is none.
void MainWindow::PrepareShaderPrecache(u64 title_id, const std::filesystem::path& game_path) {
    namespace ShaderWarm = VideoCommon::ShaderWarm;
    ShaderWarm::RequestWarmBeforeStart(false);
    const auto mode = Settings::values.precache_shaders.GetValue();
    const auto warm_file = ShaderWarm::ComputeWarmPath(title_id);
    const bool requested = title_id != 0 && ShaderWarm::WarmRequested(warm_file);
    // The precache runs where the disk shader cache loads.
    if (title_id == 0 || (mode == Settings::PrecacheShaders::Off && !requested) ||
        !Settings::values.use_disk_shader_cache.GetValue()) {
        return;
    }
    const auto stale = ShaderWarm::WarmFilesStale(warm_file);
    const bool pending = ShaderWarm::WarmPending(title_id);
    if (!pending && !stale) {
        // Precached before: the renderer runs it again only when the driver or the file changed.
        ShaderWarm::RequestWarmBeforeStart(true);
        return;
    }
    if (pending && mode == Settings::PrecacheShaders::Ask && !requested) {
        QMessageBox box(QMessageBox::Question, tr("Precache Shaders"),
                        tr("The shaders of this game are not precached yet. Precache them now?\n\n"
                           "The game starts when they are done. The first time, the game files "
                           "are read and their compute shaders are built on every CPU thread, "
                           "which takes a few minutes. Later launches only build what is new, "
                           "until the GPU driver changes. The loading screen has a Skip "
                           "button.\n\n"
                           "You can change this in Configure, Graphics, Advanced."),
                        QMessageBox::NoButton, this);
        QPushButton* const precache = box.addButton(tr("Precache now"), QMessageBox::AcceptRole);
        box.addButton(tr("Start without"), QMessageBox::RejectRole);
        box.setDefaultButton(precache);
        box.exec();
        const bool now = box.clickedButton() == precache;
        LOG_INFO(Frontend, "Shader precache before the game starts: {}", now ? "yes" : "no");
        if (!now) {
            return;
        }
    }
    std::error_code ec;
    if (stale) {
        LOG_INFO(Frontend, "Shader precache for {:016x}: {}, building it", title_id, *stale);
        ShaderWarmDialog dialog(*QtCommon::system, game_path, title_id,
                                GameTitleName(Common::FS::PathToUTF8String(game_path)), this);
        connect(&dialog, &ShaderWarmDialog::BuildFinished, &dialog, [&dialog](bool ok) {
            if (ok) {
                dialog.accept();
            }
        });
        dialog.Start();
        dialog.exec();
        if (!std::filesystem::exists(warm_file, ec)) {
            return;
        }
    }
    ShaderWarm::RequestWarmBeforeStart(true);
}

/// Deletes the game's precache files after a confirmation, showing the error when it fails.
void MainWindow::OnGameListDeleteShaderWarm(u64 program_id) {
    if (shader_warm_dialog && shader_warm_dialog->IsRunning() &&
        shader_warm_dialog->TitleId() == program_id) {
        // The builder writes this title's files: deleting them under it would lose the result or
        // fail it. Other titles' files are in their own folders.
        QMessageBox::information(this, tr("Delete Shader Precache"),
                                 tr("Shaders are being precached. Try again when it is done."));
        shader_warm_dialog->raise();
        shader_warm_dialog->activateWindow();
        return;
    }
    if (QMessageBox::question(this, tr("Delete Shader Precache"),
                              tr("Delete the shader precache files of this game? They are built "
                                 "again the next time the game is precached."),
                              QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    std::string error;
    if (!DeleteShaderWarmFiles(program_id, error)) {
        QMessageBox::critical(this, tr("Delete Shader Precache"),
                              tr("The files could not be deleted: %1")
                                  .arg(QString::fromStdString(error)));
    }
}

void MainWindow::OnGameListExtractNup(const std::string& game_path) {
    if (nup_running) {
        return;
    }
    const auto archive = std::filesystem::path{Common::FS::ToU8String(game_path)};
    const auto probed = FileSys::Nup::ProbeNupMetadata(archive);
    NupExtractDialog dialog(archive, probed, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const auto out_path = dialog.Target();
    const QString progress_title = QString::fromStdString(probed.title_name).trimmed();
    QPixmap progress_icon;
    if (!probed.icon.empty()) {
        progress_icon.loadFromData(probed.icon.data(), static_cast<uint>(probed.icon.size()));
    }
    StartNupJob(game_path, tr("Extracting NUP"), progress_title, progress_icon,
                [this, archive, out_path](const NupJobProgress& report,
                                          const std::atomic_bool* cancel) -> std::function<void()> {
                    std::string error;
                    const auto result = FileSys::Nup::ExtractNup(
                        archive, out_path,
                        [&](const FileSys::Nup::PackProgress& p) { report({}, p); }, cancel,
                        &error);
                    return [this, result, error] {
                        switch (result) {
                        case FileSys::Nup::ExtractResult::Success:
                            ArchiveNotice(
                                this, tr("Extraction Complete"),
                                tr("The NUP was extracted. You can delete the NUP file if you no "
                                   "longer need it."), ArchiveTone::Success);
                            // Only a new loose dump can appear, so one rescan is enough. The full
                            // refresh also rescans external content and the firmware on the UI
                            // thread.
                            game_list->RefreshGameDirectory();
                            break;
                        case FileSys::Nup::ExtractResult::Cancelled:
                            break;
                        case FileSys::Nup::ExtractResult::InvalidSource:
                        case FileSys::Nup::ExtractResult::IoError:
                            ArchiveNotice(this, tr("Extraction Failed"),
                                                  QString::fromStdString(error),
                                                  ArchiveTone::Error);
                            break;
                        }
                    };
                });
}

bool MainWindow::CanRewriteArchive(const std::string& game_path, const QString& title) {
    if (nup_running || boot_in_progress || close_pending) {
        return false;
    }
    if (emulation_running && current_game_path == QString::fromStdString(game_path)) {
        ArchiveNotice(this, title, tr("Stop the game before changing its file."));
        return false;
    }
    return true;
}

void MainWindow::OnGameListEditNupMetadata(const std::string& game_path) {
    const QString title = tr("Edit title and icon");
    if (!CanRewriteArchive(game_path, title)) {
        return;
    }
    const auto archive = std::filesystem::path{Common::FS::ToU8String(game_path)};
    ArchiveEditDialog dialog(ArchiveEditDialog::Format::Nup, archive,
                             FileSys::Nup::ProbeNupMetadata(archive), {}, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    WriteNupMetadata(game_path, dialog.Edit(), dialog.DisplayName(), dialog.CurrentIcon());
}

void MainWindow::WriteNupMetadata(const std::string& game_path, FileSys::Nup::MetadataEdit edit,
                                  const QString& title, const QPixmap& icon,
                                  std::function<void()> on_success) {
    if (!edit.title_name && !edit.icon && !edit.display_version && !edit.publisher &&
        !edit.localized_titles) {
        return;
    }
    const auto archive = std::filesystem::path{Common::FS::ToU8String(game_path)};
    // The new file is written next to the old one before it replaces it.
    std::error_code ec;
    const u64 size = std::filesystem::file_size(archive, ec);
    const u64 available = Common::FS::GetFreeSpaceSize(archive.parent_path());
    if (!ec && available < size + (16ULL << 20)) {
        ArchiveNotice(this, tr("Edit title and icon"),
                             tr("Saving into the NUP writes a new copy of it next to the old one "
                                "first: %1 MB is needed and %2 MB is free on its drive.")
                                 .arg((size >> 20) + 16)
                                 .arg(available >> 20));
        return;
    }
    // The game list's content provider keeps the add-ons of every NUP it scanned open.
    if (!emulation_running) {
        QtCommon::provider->ClearAllEntries();
    }
    StartNupJob(game_path, tr("Saving into the NUP"), title, icon,
                [this, archive, edit, on_success = std::move(on_success)](
                    const NupJobProgress& report,
                    const std::atomic_bool* cancel) -> std::function<void()> {
                    std::string error;
                    const auto result = FileSys::Nup::EditNupMetadata(
                        archive, edit, &error, cancel,
                        [&](const FileSys::Nup::PackProgress& p) { report({}, p); });
                    return [this, result, error, on_success] {
                        switch (result) {
                        case FileSys::Nup::EditResult::Success:
                            if (on_success) {
                                on_success();
                            }
                            ArchiveNotice(
                                this, tr("Saved into the NUP"),
                                tr("The new details were written into the NUP file, so they show "
                                   "on every PC that opens it."), ArchiveTone::Success);
                            // The cached name and icon are keyed by title, so they must go; the
                            // rest is one rescan instead of the full refresh.
                            QtCommon::Game::ResetMetadata(false);
                            game_list->RefreshGameDirectory();
                            break;
                        case FileSys::Nup::EditResult::NoChange:
                        case FileSys::Nup::EditResult::Cancelled:
                            game_list->RefreshExternalContent();
                            break;
                        case FileSys::Nup::EditResult::InvalidSource:
                        case FileSys::Nup::EditResult::IoError:
                            game_list->RefreshExternalContent();
                            ArchiveNotice(
                                this, tr("Saving into the NUP failed"),
                                tr("%1\n\nThe NUP was not changed.")
                                    .arg(QString::fromStdString(error)), ArchiveTone::Error);
                            break;
                        }
                    };
                });
}

void MainWindow::WriteArchiveFromSettings(u64 title_id, const std::string& game_path,
                                          const std::optional<QByteArray>& icon, bool edit) {
    const bool nup =
        QString::fromStdString(game_path).endsWith(QStringLiteral(".nup"), Qt::CaseInsensitive);
    if (icon) {
        const QString busy =
            tr("Another archive job is running. Pick the icon again when it finishes.");
        QPixmap shown;
        shown.loadFromData(*icon);
        if (nup) {
            if (nup_running) {
                ArchiveNotice(this, tr("Edit title and icon"), busy);
            } else if (CanRewriteArchive(game_path, tr("Edit title and icon"))) {
                FileSys::Nup::MetadataEdit write;
                write.icon = std::vector<u8>(icon->begin(), icon->end());
                WriteNupMetadata(game_path, std::move(write), GameTitleName(game_path), shown,
                                 [this, title_id] {
                                     if (QtCommon::Game::RemoveCustomArtwork(title_id)) {
                                         game_list->OnCustomArtworkChanged(title_id);
                                     }
                                 });
            }
        }
#ifdef LICHII_NX_TOOLS
        else if (NxTools::IsNxArchive(game_path)) {
            if (nx_metadata_write_in_progress) {
                ArchiveNotice(this, tr("NX Archive"), busy);
            } else if (CanWriteNxMetadata()) {
                nx_artwork_title_id = title_id;
                nx_tools->SetNxIcon(game_path, *icon,
                                    [this] { return PrepareNxMetadataWrite(); });
            }
        }
#endif
        return;
    }
    if (edit) {
        if (nup) {
            OnGameListEditNupMetadata(game_path);
        }
#ifdef LICHII_NX_TOOLS
        else if (NxTools::IsNxArchive(game_path)) {
            OnGameListEditNxMetadata(game_path);
        }
#endif
    }
}

void MainWindow::LaunchFirmwareApplet(u64 raw_program_id,
                                      std::optional<Service::NFP::CabinetMode> cabinet_mode) {
    auto const program_id = Service::AM::AppletProgramId(raw_program_id);
    auto result = FirmwareManager::VerifyFirmware(*QtCommon::system.get());
    using namespace QtCommon::StringLookup;
    switch (result) {
    case FirmwareManager::ErrorFirmwareMissing:
        QMessageBox::warning(this, tr("No firmware available"),
                             Lookup(FwCheckErrorFirmwareMissing));
        return;
    case FirmwareManager::ErrorFirmwareCorrupted:
        QMessageBox::warning(this, tr("Firmware Corrupted"), Lookup(FwCheckErrorFirmwareCorrupted));
        return;
    default:
        break;
    }
    auto bis_system = QtCommon::system->GetFileSystemController().GetSystemNANDContents();
    if (auto applet_nca =
            bis_system->GetEntry(u64(program_id), FileSys::ContentRecordType::Program);
        applet_nca) {
        if (auto const applet_id =
                [program_id] {
                    using namespace Service::AM;
                    switch (program_id) {
                    case AppletProgramId::OverlayDisplay:
                        return AppletId::OverlayDisplay;
                    case AppletProgramId::QLaunch:
                        return AppletId::QLaunch;
                    case AppletProgramId::Starter:
                        return AppletId::Starter;
                    case AppletProgramId::Auth:
                        return AppletId::Auth;
                    case AppletProgramId::Cabinet:
                        return AppletId::Cabinet;
                    case AppletProgramId::Controller:
                        return AppletId::Controller;
                    case AppletProgramId::DataErase:
                        return AppletId::DataErase;
                    case AppletProgramId::Error:
                        return AppletId::Error;
                    case AppletProgramId::NetConnect:
                        return AppletId::NetConnect;
                    case AppletProgramId::ProfileSelect:
                        return AppletId::ProfileSelect;
                    case AppletProgramId::SoftwareKeyboard:
                        return AppletId::SoftwareKeyboard;
                    case AppletProgramId::MiiEdit:
                        return AppletId::MiiEdit;
                    case AppletProgramId::Web:
                        return AppletId::Web;
                    case AppletProgramId::Shop:
                        return AppletId::Shop;
                    case AppletProgramId::PhotoViewer:
                        return AppletId::PhotoViewer;
                    case AppletProgramId::Settings:
                        return AppletId::Settings;
                    case AppletProgramId::OfflineWeb:
                        return AppletId::OfflineWeb;
                    case AppletProgramId::LoginShare:
                        return AppletId::LoginShare;
                    case AppletProgramId::WebAuth:
                        return AppletId::WebAuth;
                    case AppletProgramId::MyPage:
                        return AppletId::MyPage;
                    default:
                        return AppletId::None;
                    }
                }();
            applet_id != Service::AM::AppletId::None) {
            QtCommon::system->GetFrontendAppletHolder().SetCurrentAppletId(applet_id);
            if (cabinet_mode)
                QtCommon::system->GetFrontendAppletHolder().SetCabinetMode(*cabinet_mode);
            // ?
            auto const filename = QString::fromStdString((applet_nca->GetFullPath()));
            UISettings::values.roms_path = QFileInfo(filename).path().toStdString();
            BootGame(filename, LibraryAppletParameters(u64(program_id), applet_id));
        } else {
            QMessageBox::warning(this, tr("Unknown applet"),
                                 tr("Applet doesn't map to a known value."));
        }
    } else {
        QMessageBox::warning(this, tr("Record not found"),
                             tr("Applet not found. Please reinstall firmware."));
    }
}

void MainWindow::OnCreateHomeMenuDesktopShortcut() {
    QtCommon::Game::CreateHomeMenuShortcut(QtCommon::Game::ShortcutTarget::Desktop);
}

void MainWindow::OnCreateHomeMenuApplicationMenuShortcut() {
    QtCommon::Game::CreateHomeMenuShortcut(QtCommon::Game::ShortcutTarget::Applications);
}

void MainWindow::OnCaptureScreenshot() {
    if (QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning())
        return;

    const u64 title_id = QtCommon::system->GetApplicationProcessProgramID();
    const auto screenshot_path =
        QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::ScreenshotsDir));
    const auto date =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss-zzz"));
    QString filename = QStringLiteral("%1/%2_%3.png")
                           .arg(screenshot_path)
                           .arg(title_id, 16, 16, QLatin1Char{'0'})
                           .arg(date);

    if (!Common::FS::CreateDir(screenshot_path.toStdString()))
        return;

#ifdef _WIN32
    if (UISettings::values.enable_screenshot_save_as) {
        OnPauseGame();
        filename = QFileDialog::getSaveFileName(this, tr("Capture Screenshot"), filename,
                                                tr("PNG Image (*.png)"));
        OnStartGame();
        if (filename.isEmpty()) {
            return;
        }
    }
#endif
    render_window->CaptureScreenshot(filename);
}

void MainWindow::OnReportBug() {
    if (bug_report_dialog || QtCommon::emu_thread == nullptr || !QtCommon::emu_thread->IsRunning()) {
        return;
    }
    BugReportDialog dialog(*QtCommon::system, this);
    bug_report_dialog = &dialog;
    dialog.exec();
    bug_report_dialog = nullptr;
}

void MainWindow::KeepNextendoSessionAlive() {
    if (!Common::Nextendo::OnlinePlayActive() || nextendo_session_watcher.isRunning()) {
        return;
    }
    // Only the profile a game plays with, or the one the next game boots with: a heartbeat for
    // every signed-in profile would spend the request budget.
    nextendo_session_watcher.setFuture(QtConcurrent::run(
        [client_id = Settings::values.nextendo_client_id.GetValue(),
         profile = Service::Account::Nextendo::CurrentProfile(*QtCommon::system)] {
            return Common::Nextendo::KeepSessionAlive(client_id, profile);
        }));
}

void MainWindow::UpdateNextendoSocial() {
    if (!Common::Nextendo::OnlinePlayActive() || nextendo_social_watcher.isRunning()) {
        return;
    }
    nextendo_social_watcher.setFuture(
        QtConcurrent::run([client_id = Settings::values.nextendo_client_id.GetValue(),
                           profile = Service::Account::Nextendo::CurrentProfile(*QtCommon::system),
                           program_id = QtCommon::system->GetApplicationProcessProgramID()] {
            void(Common::Nextendo::RefreshFriends(client_id, profile));
            void(Common::Nextendo::UpdatePresence(client_id, profile, program_id));
        }));
}

void MainWindow::SyncNextendoProfile(bool force) {
    nextendo_profile_force |= force;
    if (!Common::Nextendo::OnlinePlayActive() ||
        !Settings::values.nextendo_sync_profile.GetValue() || QtCommon::system->IsPoweredOn() ||
        nextendo_profile_watcher.isRunning()) {
        return;
    }
    const auto user = QtCommon::system->GetProfileManager().GetUser(
        static_cast<std::size_t>(Settings::values.current_user.GetValue()));
    if (!user) {
        return;
    }
    nextendo_profile_user = *user;
    nextendo_profile_watcher.setFuture(
        QtConcurrent::run([client_id = Settings::values.nextendo_client_id.GetValue(), user = *user,
                           force = std::exchange(nextendo_profile_force, false)] {
            return Common::Nextendo::PullProfile(client_id, user, force);
        }));
}

void MainWindow::CheckNextendoOnlineStatus() {
    if (!Common::Nextendo::OnlinePlayActive() || nextendo_status_watcher.isRunning()) {
        return;
    }
    nextendo_status_profile = Service::Account::Nextendo::CurrentProfile(*QtCommon::system);
    nextendo_status_watcher.setFuture(
        QtConcurrent::run([client_id = Settings::values.nextendo_client_id.GetValue(),
                           profile = nextendo_status_profile] {
            return Common::Nextendo::CheckOnlineStatus(client_id, profile);
        }));
}

void MainWindow::CheckNextendoGameVersion(u64 title_id, std::string version) {
    if (!Common::Nextendo::OnlinePlayActive()) {
        return;
    }
    nextendo_version_check.emplace(title_id, std::move(version));
    if (nextendo_catalog_watcher.isRunning()) {
        return;
    }
    nextendo_catalog_watcher.setFuture(
        QtConcurrent::run([client_id = Settings::values.nextendo_client_id.GetValue()] {
            return Common::Nextendo::LoadGameCatalog(client_id);
        }));
}

#ifdef ENABLE_UPDATE_CHECKER
void MainWindow::CheckForUpdates(bool manual) {
    if (update_check_running) {
        // Report the check already in flight instead of starting another one.
        update_check_manual |= manual;
        return;
    }
    update_check_running = true;
    update_check_manual = manual;
    update_future = QtConcurrent::run([] { return UpdateChecker::Check(); });
    update_watcher.setFuture(update_future);
}

void MainWindow::OnEmulatorUpdateAvailable() {
    const bool manual = std::exchange(update_check_manual, false);
    update_check_running = false;
    const auto result = update_future.result();
    if (manual && result.failure != UpdateChecker::Failure::None) {
        const auto title = tr("Check for Updates");
        switch (result.failure) {
        case UpdateChecker::Failure::NotReleaseBuild:
            QMessageBox::information(
                this, title,
                tr("This test or development build (%1) has no published release tag. Download "
                   "a release from GitHub to get updates.")
                    .arg(QString::fromUtf8(Common::g_build_version)));
            break;
        case UpdateChecker::Failure::RateLimited:
            QMessageBox::warning(this, title,
                                 tr("GitHub is limiting requests from your network right now. "
                                    "Try again later."));
            break;
        case UpdateChecker::Failure::Http:
            QMessageBox::warning(this, title,
                                 tr("GitHub answered with HTTP error %1. Try again later.")
                                     .arg(result.status));
            break;
        default:
            QMessageBox::warning(
                this, title,
                tr("Could not reach GitHub. Check your internet connection and try again."));
            break;
        }
        return;
    }
    if (result.failure == UpdateChecker::Failure::RateLimited) {
        statusBar()->showMessage(
            tr("Could not check for updates: GitHub rate limit reached. Lichii will try again at "
               "the next start."),
            15000);
        return;
    }
    if (result.failure == UpdateChecker::Failure::Http) {
        statusBar()->showMessage(
            tr("Could not check for updates: GitHub answered with HTTP %1.").arg(result.status),
            15000);
        return;
    }
    if (!result.update) {
        if (manual) {
            QMessageBox::information(this, tr("Check for Updates"),
                                     tr("You're on the latest build (%1).")
                                         .arg(QString::fromUtf8(Common::g_build_release_tag)));
        }
        return;
    }

    const auto& release = *result.update;
    const auto now = static_cast<unsigned long long>(QDateTime::currentSecsSinceEpoch());
    if (!manual && (UISettings::values.skipped_update_tag.GetValue() == release.tag ||
                    UISettings::values.update_remind_at.GetValue() > now)) {
        return;
    }

    UpdateDialog dialog(release, this);
    switch (dialog.exec()) {
    case UpdateDialog::SkipVersion:
        UISettings::values.skipped_update_tag = release.tag;
        break;
    case QDialog::Rejected:
        UISettings::values.update_remind_at = now + 24 * 60 * 60;
        break;
    default:
        return;
    }
    OnSaveConfig();
}
#endif

void MainWindow::OnApplicationChanged(u64 program_id) {
    if (!emulation_running || program_id == 0)
        return;

    if (report_session && report_session->program_id != program_id) {
        report_session.reset();
        game_list->SetReportableProgramId(0);
    }

    std::string title_name;
    std::string title_version;

    // The loader's name stays when the game has no control metadata (a NUP without an installed
    // control record), as on boot.
    if (QtCommon::system->GetGameName(title_name) != Loader::ResultStatus::Success) {
        title_name.clear();
    }

    const FileSys::PatchManager pm(program_id, QtCommon::system->GetFileSystemController(),
                                   QtCommon::system->GetContentProvider());
    if (const auto metadata = pm.GetControlMetadata(); metadata.first != nullptr) {
        title_version = metadata.first->GetVersionString();
        if (auto metadata_name = metadata.first->GetApplicationName(); !metadata_name.empty()) {
            title_name = std::move(metadata_name);
        }
    }
    if (auto custom_title = QtCommon::Game::ReadCustomTitle(program_id); !custom_title.empty()) {
        title_name = std::move(custom_title);
    }
    if (title_name.empty()) {
        title_name = fmt::format("{:016X}", program_id);
    }

    if (const auto* process = QtCommon::system->Kernel().ApplicationProcess(); process != nullptr) {
        const auto instruction_set_suffix = process->Is64Bit() ? tr("(64-bit)") : tr("(32-bit)");
        title_name =
            tr("%1 %2", "%1 is the title name. %2 indicates if the title is 64-bit or 32-bit")
                .arg(QString::fromStdString(title_name), instruction_set_suffix)
                .toStdString();
    }

    LOG_INFO(Frontend, "Now running: {:016X} | {} | {}", program_id, title_name, title_version);

    UpdateWindowTitle(title_name, title_version,
                      QtCommon::system->GPU().Renderer().GetDeviceVendor());
    CheckNextendoGameVersion(program_id, std::move(title_version));

    // Switch to record playtime for the running program
    if (play_time_manager)
        play_time_manager->SetProgramId(program_id);
}

void MainWindow::UpdateWindowTitle(std::string_view title_name, std::string_view title_version,
                                   std::string_view gpu_vendor) {
    static const std::string build_id = std::string{Common::g_build_id};
    static const std::string lichii_title =
        fmt::format("{} | {} | {}", std::string{Common::g_build_name},
                    std::string{Common::g_build_version}, std::string{Common::g_compiler_id});

    const auto override_title =
        fmt::format(fmt::runtime(std::string(Common::g_title_bar_format_idle)), build_id);
    const auto window_title = override_title.empty() ? lichii_title : override_title;

    if (title_name.empty()) {
        setWindowTitle(QString::fromStdString(window_title));
    } else {
        running_title_bar->SetGameTitle(QString::fromUtf8(title_name.data(), title_name.size()));
        const auto run_title = [window_title, title_name, title_version, gpu_vendor]() {
            if (title_version.empty()) {
                return fmt::format("{} | {} | {}", window_title, title_name, gpu_vendor);
            }
            return fmt::format("{} | {} | {} | {}", window_title, title_name, title_version,
                               gpu_vendor);
        }();
        setWindowTitle(QString::fromStdString(run_title));
    }
}

std::string MainWindow::CreateTASFramesString(
    std::array<size_t, InputCommon::TasInput::PLAYER_NUMBER> frames) const {
    std::string string = "";
    size_t maxPlayerIndex = 0;
    for (size_t i = 0; i < frames.size(); i++) {
        if (frames[i] != 0) {
            if (maxPlayerIndex != 0)
                string += ", ";
            while (maxPlayerIndex++ != i)
                string += "0, ";
            string += std::to_string(frames[i]);
        }
    }
    return string;
}

QString MainWindow::GetTasStateDescription() const {
    auto [tas_status, current_tas_frame, total_tas_frames] = input_subsystem->GetTas()->GetStatus();
    std::string tas_frames_string = CreateTASFramesString(total_tas_frames);
    switch (tas_status) {
    case InputCommon::TasInput::TasState::Running:
        return tr("TAS state: Running %1/%2")
            .arg(current_tas_frame)
            .arg(QString::fromStdString(tas_frames_string));
    case InputCommon::TasInput::TasState::Recording:
        return tr("TAS state: Recording %1").arg(total_tas_frames[0]);
    case InputCommon::TasInput::TasState::Stopped:
        return tr("TAS state: Idle %1/%2")
            .arg(current_tas_frame)
            .arg(QString::fromStdString(tas_frames_string));
    default:
        return tr("TAS State: Invalid");
    }
}

void MainWindow::OnTasStateChanged() {
    bool is_running = false;
    bool is_recording = false;
    if (emulation_running) {
        const InputCommon::TasInput::TasState tas_status =
            std::get<0>(input_subsystem->GetTas()->GetStatus());
        is_running = tas_status == InputCommon::TasInput::TasState::Running;
        is_recording = tas_status == InputCommon::TasInput::TasState::Recording;
    }

    ui->action_TAS_Start->setText(is_running ? tr("&Stop Running") : tr("&Start"));
    ui->action_TAS_Record->setText(is_recording ? tr("Stop R&ecording") : tr("R&ecord"));

    ui->action_TAS_Start->setEnabled(emulation_running);
    ui->action_TAS_Record->setEnabled(emulation_running);
    ui->action_TAS_Reset->setEnabled(emulation_running);
}

void MainWindow::UpdateStatusBar() {
    if (QtCommon::emu_thread == nullptr || !QtCommon::system->IsPoweredOn()) {
        status_bar_update_timer.stop();
        return;
    }

    tas_action->setText(Settings::values.tas_enable.GetValue() ? GetTasStateDescription()
                                                               : QString{});

    auto results = QtCommon::system->GetAndResetPerfStats();
    auto& shader_notify = QtCommon::system->GPU().ShaderNotify();
    const int shaders_building = shader_notify.ShadersBuilding();

    // Before statsUpdated, so the performance overlay reads this update's generated frame rate.
    const auto frame_gen = QtCommon::system->GPU().Renderer().GetFrameGenStatus();
    const u64 new_generated_frames = frame_gen.generated_frames - frame_gen_frames;
    const qint64 frame_gen_ms = frame_gen_fps_timer.isValid() ? frame_gen_fps_timer.restart() : 0;
    if (!frame_gen_fps_timer.isValid()) {
        frame_gen_fps_timer.start();
    }
    frame_gen_fps = frame_gen_ms > 0 ? static_cast<double>(new_generated_frames) * 1000.0 /
                                           static_cast<double>(frame_gen_ms)
                                     : 0.0;
    frame_gen_generating = frame_gen.generated_frames > frame_gen_frames;
#ifdef EGLANTINE_STREAMLINE
    // NVIDIA DLSS Frame Generation adds its frames after the renderer, at present.
    if (Vulkan::Streamline::FrameGenActive()) {
        const auto rates = Vulkan::Streamline::FrameGenRecentRates();
        frame_gen_fps = std::max(rates.shown_fps - rates.real_fps, 0.0);
        frame_gen_generating = rates.generating;
    }
#endif
    frame_gen_frames = frame_gen.generated_frames;
    frame_gen_state = frame_gen.state;

    const auto dlss = QtCommon::system->GPU().Renderer().GetDlssStatus();
    dlss_status_text.clear();
    if (dlss.evaluations > dlss_evaluations) {
        const auto mode_text = [this](Settings::NvidiaDlssQuality mode) {
            return ConfigurationShared::TranslateMapText(
                       translator, ConfigurationShared::nvidia_dlss_quality_texts_map.at(mode))
                .toUpper();
        };
        dlss_status_text =
            tr("DLSS %1 %2", "NVIDIA DLSS mode and model preset, e.g. DLSS DLAA M")
                .arg(mode_text(dlss.mode),
                     ConfigurationShared::TranslateMapText(
                         translator,
                         ConfigurationShared::nvidia_dlss_preset_texts_map.at(dlss.preset))
                         .toUpper());
        if (dlss.requested != Settings::NvidiaDlssQuality::Automatic &&
            dlss.requested != dlss.mode) {
            dlss_status_text += tr(" (%1 DOESN'T FIT)", "The DLSS mode the user chose")
                                    .arg(mode_text(dlss.requested));
        }
    }
    dlss_evaluations = dlss.evaluations;
    const int left = std::max(shader_notify.ShadersLeft(), 0);
    ingame_bar->Shaders()->SetProgress(shaders_building, left);
    if (ingame_fullscreen && left > 0) {
        if (!fullscreen_shader_active) {
            fullscreen_shader_active = true;
            fullscreen_shader_toast->ShowShaders(ui->action_Single_Window_Mode->isChecked()
                                                     ? static_cast<QWidget*>(this)
                                                     : render_window,
                                                 shaders_building > 0 ? shaders_building : left);
        } else {
            fullscreen_shader_toast->SetShaderCount(shaders_building > 0 ? shaders_building : left);
        }
    } else {
        fullscreen_shader_active = false;
    }

    emit statsUpdated(results, shader_notify);

    QString fpsText = tr("Game: %1 FPS").arg(std::round(results.average_game_fps), 0, 'f', 0);
    QString fpsSuffix = m_fpsSuffix;
#ifdef EGLANTINE_STREAMLINE
    if (Vulkan::Streamline::FrameGenActive()) {
        QString frameGen = Vulkan::Streamline::FrameGenRecentRates().generating
                               ? tr("DLSS FG")
                               : tr("DLSS FG paused");
        if (!fpsSuffix.isEmpty())
            frameGen = frameGen % QStringLiteral(", ") % fpsSuffix;
        fpsSuffix = frameGen;
    }
#endif
    if (frame_gen_fps >= 0.5) {
        fpsText = fpsText % tr(" + %1 generated").arg(std::round(frame_gen_fps), 0, 'f', 0);
    }
    if (!fpsSuffix.isEmpty())
        fpsText = fpsText % QStringLiteral(" (%1)").arg(fpsSuffix);

    QStringList status{fpsText, tr("Frame: %1 ms").arg(results.frametime * 1000.0, 0, 'f', 2),
                       tr("Scale: %1×").arg(Settings::values.resolution_info.up_factor)};
    const bool limited = Settings::values.use_speed_limit.GetValue();
    const auto speed_limit = Settings::SpeedLimit();
    if (!Settings::values.use_multi_core.GetValue()) {
        const double speed = results.emulation_speed * 100.0;
        status << (limited
                       ? tr("Speed: %1% / %2%").arg(speed, 0, 'f', 0).arg(speed_limit)
                       : tr("Speed: %1%").arg(speed, 0, 'f', 0));
    }
    if (!dlss_status_text.isEmpty()) {
        status << dlss_status_text;
    }
    session_status_text = status.join(QStringLiteral(" · "));
    const bool at_target = limited && std::abs(results.emulation_speed * 100.0 - speed_limit) < 3.0;
    ingame_bar->SetSpeedStatus(!limited           ? tr("Speed unlocked")
                               : speed_limit != 100 ? tr("Speed %1%").arg(speed_limit)
                                                    : QString{});
    ingame_bar->SetStats(results.average_game_fps, frame_gen_fps, results.frametime * 1000.0,
                         at_target, session_status_text);
    UpdateFrameGenButton();
    UpdateInGameGraphicsSummary();
}

void MainWindow::ToggleInGameMenu() {
    if (ingame_menu->isVisible()) {
        ingame_menu->hide();
        return;
    }
    if (!emulation_running || !QtCommon::system->IsPoweredOn() ||
        QtCommon::system->IsShuttingDown() || !render_window->isVisible()) {
        return;
    }

    const u64 program = QtCommon::system->GetApplicationProcessProgramID();
    auto name = QtCommon::Game::ReadCustomTitle(program);
    if (name.empty() && QtCommon::system->GetGameName(name) != Loader::ResultStatus::Success) {
        name.clear();
    }
    const QString title = name.empty() ? QFileInfo(current_game_path).completeBaseName()
                                       : QString::fromStdString(name);
    auto& loader = QtCommon::system->GetAppLoader();
    auto image = QtCommon::Game::ReadCustomArtwork(program);
    QPixmap icon;
    if (!image.empty()) {
        icon.loadFromData(image.data(), static_cast<uint>(image.size()));
    }
    if (icon.isNull() && loader.ReadIcon(image) == Loader::ResultStatus::Success) {
        icon.loadFromData(image.data(), static_cast<uint>(image.size()));
    }
    const u64 minutes = play_time_manager->GetPlayTime(program) / 60;
    const QString play_time = minutes >= 60
                                  ? tr("%1h %2m played").arg(minutes / 60).arg(minutes % 60)
                                  : tr("%1m played").arg(minutes);
    UpdateStatusButtons();
    UpdateInGameGraphicsSummary();
    ingame_menu->Open(render_window, ingame_menu_action->shortcut(), icon, title, play_time,
                      !QtCommon::emu_thread->IsRunning());
}

void MainWindow::UpdateInGameGraphicsSummary() {
    auto& values = Settings::values;
    const auto text = [this](const auto& texts, auto value) {
        return ConfigurationShared::TranslateMapText(translator, texts.at(value));
    };
    QString effects = values.post_shader_enabled.GetValue() ? tr("On") : tr("Off");
#ifdef HAS_RESHADE
    if (const auto preset = VideoCore::GetActiveFxPreset();
        values.post_shader_enabled.GetValue() && !preset.empty()) {
        effects = QString::fromStdString(preset);
    }
#endif
    const int preset = Spotlight::PerformancePresetIndex(
        {values.resolution_setup.GetValue(), values.scaling_filter.GetValue(),
         values.anti_aliasing.GetValue(), values.frame_gen.GetValue()});
    const u32 device = values.vulkan_device.GetValue();
    const int auto_tier = Spotlight::SuggestedPresetTier(
        device < vk_device_records.size() ? &vk_device_records[device] : nullptr);
    const Spotlight::InGameSummary summary{
        .renderer = text(ConfigurationShared::renderer_backend_texts_map,
                         values.renderer_backend.GetValue()),
        .resolution = tr("%1×").arg(values.resolution_info.up_factor),
        .filter =
            text(ConfigurationShared::scaling_filter_texts_map, values.scaling_filter.GetValue()),
        .frame_gen = frame_gen_status_text,
        .accuracy =
            text(ConfigurationShared::gpu_accuracy_texts_map, values.gpu_accuracy.GetValue()),
        .anti_aliasing =
            text(ConfigurationShared::anti_aliasing_texts_map, values.anti_aliasing.GetValue()),
        .effects = effects,
        .preset = preset == -1          ? -1
                  : preset == auto_tier ? 0
                                        : preset + 1,
        .preset_global = values.resolution_setup.UsingGlobal() &&
                         values.scaling_filter.UsingGlobal() &&
                         values.anti_aliasing.UsingGlobal() && values.frame_gen.UsingGlobal(),
    };
    ingame_menu->SetSummary(summary);
}

void MainWindow::UpdateGPUAccuracyButton() {
    const auto gpu_accuracy = Settings::values.gpu_accuracy.GetValue();
    const auto gpu_accuracy_text = ConfigurationShared::TranslateMapText(
        translator, ConfigurationShared::gpu_accuracy_texts_map.find(gpu_accuracy)->second);
    gpu_accuracy_action->setText(tr("GPU accuracy: %1").arg(gpu_accuracy_text));
}

void MainWindow::UpdateDockedButton() {
    const bool docked = Settings::IsDockedMode();
    const QString mode = docked ? tr("Docked") : tr("Handheld");
    dock_status_action->setText(tr("Console mode: %1").arg(mode));
    ingame_menu->SetConsoleMode(docked);
    UpdateAPIText();
}

void MainWindow::UpdateAPIText() {
    const auto api = Settings::values.renderer_backend.GetValue();
    const auto renderer_status_text = ConfigurationShared::TranslateMapText(
        translator, ConfigurationShared::renderer_backend_texts_map.find(api)->second);
    renderer_status_action->setText(tr("Renderer: %1").arg(renderer_status_text));
    ingame_bar->SetConfiguration(renderer_status_text,
                                 Settings::IsDockedMode() ? tr("Docked") : tr("Handheld"));
}

void MainWindow::UpdateFilterText() {
    const auto filter = Settings::values.scaling_filter.GetValue();
    const auto filter_text = ConfigurationShared::TranslateMapText(
        translator, ConfigurationShared::scaling_filter_texts_map.find(filter)->second);
    filter_status_action->setText(tr("Scaling filter: %1").arg(filter_text));
}

void MainWindow::UpdateAAText() {
    const auto aa_mode = Settings::values.anti_aliasing.GetValue();
    const auto aa_text = ConfigurationShared::TranslateMapText(
        translator, ConfigurationShared::anti_aliasing_texts_map.find(aa_mode)->second);
    aa_status_action->setText(tr("Anti-aliasing: %1").arg(aa_text));
}

#ifdef HAS_RESHADE
void MainWindow::UpdatePostShaderText() {
    const bool enabled = Settings::values.post_shader_enabled.GetValue();
    post_shader_status_action->setChecked(enabled);
    const std::string preset = enabled ? VideoCore::GetActiveFxPreset() : std::string{};
    post_shader_status_action->setToolTip(preset.empty() ? QString{}
                                                         : QString::fromStdString(preset));
}
#endif

void MainWindow::UpdateFrameGenButton() {
    const bool enabled = Settings::values.frame_gen.GetValue();
    frame_gen_status_action->setChecked(enabled);
    if (!enabled) {
        frame_gen_status_text = tr("Off");
        frame_gen_status_action->setToolTip(tr("Frame generation is off."));
        return;
    }

    const Settings::FrameGenType selected_type = Settings::values.frame_gen_type.GetValue();
    // Once a game runs, NVIDIA DLSS Frame Generation that isn't this session's generator gives
    // way to Lichii Optical Flow.
    const bool dlssg_fallback = selected_type == Settings::FrameGenType::NvidiaDlssG &&
                                emulation_running &&
                                frame_gen_state == VideoCore::FrameGenState::Running;
    const bool flyhigh_fallback = selected_type == Settings::FrameGenType::FlyHigh &&
                                  emulation_running &&
                                  frame_gen_state == VideoCore::FrameGenState::MissingFlyHigh;
    const Settings::FrameGenType shown_type =
        dlssg_fallback || flyhigh_fallback ? Settings::FrameGenType::OpticalFlow : selected_type;
    const QString& type_text =
        ConfigurationShared::frame_gen_type_texts_map.find(shown_type)->second;
    const QString type = shown_type == Settings::FrameGenType::NvidiaDlssG
                             ? QStringLiteral("DLSS-G")
                             : ConfigurationShared::TranslateMapText(translator, type_text).toUpper();
    const u32 target_rate = Settings::values.frame_gen_target_rate.GetValue();
    const QString mode = target_rate == 0
                             ? tr("%1 ×%2").arg(type).arg(Settings::FrameGenMultiplier())
                             : tr("%1 Auto").arg(type);
    const QString target = target_rate == 0
                               ? QString()
                               : tr(" It adds up to %1 frames per game frame to reach %2 FPS.")
                                     .arg(Settings::MAX_FRAME_GEN_MULTIPLIER - 1)
                                     .arg(target_rate);

    if (!emulation_running) {
        frame_gen_status_text = mode;
        frame_gen_status_action->setToolTip(
            tr("Frame generation is on.%1 Whether it runs shows here once a game starts.")
                .arg(target));
        return;
    }

    QString unavailable;
    switch (frame_gen_state) {
    case VideoCore::FrameGenState::Pending:
    case VideoCore::FrameGenState::Running:
        break;
    case VideoCore::FrameGenState::UnsupportedRenderer:
        unavailable = tr("Frame generation needs the Vulkan renderer.");
        break;
    case VideoCore::FrameGenState::UnsupportedBuild:
        unavailable = tr("This build was compiled without frame generation.");
        break;
    case VideoCore::FrameGenState::UnsupportedGpu:
        unavailable = tr("This GPU lacks Vulkan features LSFG needs (Vulkan memory model and null "
                         "descriptors). Lichii Optical Flow Frame Generation may still work.");
        break;
    case VideoCore::FrameGenState::UnsupportedSurface:
        unavailable = tr("This GPU can't write to the window's swapchain format from a shader, so "
                         "generated frames can't be presented.");
        break;
    case VideoCore::FrameGenState::MissingLosslessDll:
        unavailable = tr("LSFG needs Lossless.dll from Lossless Scaling in the \"lossless\" folder "
                         "of the user directory. It is missing or could not be loaded.");
        break;
    case VideoCore::FrameGenState::NvidiaFrameGen:
        break;
    case VideoCore::FrameGenState::MissingFlyHigh:
        break;
    }

    if (!unavailable.isEmpty()) {
        frame_gen_status_action->setChecked(false);
        frame_gen_status_text = tr("Unavailable");
        frame_gen_status_action->setToolTip(
            tr("Frame generation is on but not running. %1").arg(unavailable));
        return;
    }

    const QString fallback =
        dlssg_fallback ? tr(" NVIDIA DLSS Frame Generation can't run for this game or setup, so "
                            "Lichii Optical Flow Frame Generation runs instead; the log says why.")
        : flyhigh_fallback
            ? tr(" FlyHigh can't run yet (its model is not downloaded, or the log says why), so "
                 "Lichii Optical Flow runs instead. Use the gear next to FlyHigh to open its "
                 "settings.")
            : QString();
    if (frame_gen_generating) {
        frame_gen_status_text = mode;
        frame_gen_status_action->setToolTip(
            tr("Frame generation is running.%1%2").arg(target, fallback));
        return;
    }

    const QString target_met =
        target_rate == 0
            ? QString()
            : tr(" It also pauses while the game already reaches %1 FPS.").arg(target_rate);
    frame_gen_status_text = tr("%1 (idle)").arg(mode);
    frame_gen_status_action->setToolTip(
        tr("Frame generation is on but generated no frames in the last half second. It pauses "
           "while it warms up, for a second after a frame takes longer than 100 ms, and while "
           "frames arrive in bursts.%1%2")
            .arg(target_met, fallback));
}

void MainWindow::UpdateVolumeUI() {
    const int value = Settings::values.volume.GetValue();
    const bool muted = Settings::values.audio_muted.GetValue();
    const QSignalBlocker blocker(volume_slider);
    volume_slider->setValue(value);
    mute_action->setText(muted ? tr("Unmute") : tr("Mute"));
    mute_action->setChecked(muted);
    ingame_menu->SetVolume(value, muted);
}

void MainWindow::UpdateStatusButtons() {
    UpdateLetterboxBars();
    UpdateGPUAccuracyButton();
    UpdateDockedButton();
    UpdateFilterText();
    UpdateAAText();
    UpdateFrameGenButton();
    UpdateVolumeUI();
#ifdef HAS_RESHADE
    UpdatePostShaderText();
#endif
}

// TODO(crueter): Use this for game list stuff
void MainWindow::UpdateUISettings() {
    if (!ui->action_Fullscreen->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
    }
    UISettings::values.state = saveState();
    UISettings::values.single_window_mode = ui->action_Single_Window_Mode->isChecked();
    UISettings::values.fullscreen = ui->action_Fullscreen->isChecked();
    UISettings::values.show_status_bar = ui->action_Show_Status_Bar->isChecked();
    UISettings::values.show_perf_overlay = ui->action_Show_Performance_Overlay->isChecked();

    UISettings::values.first_start = false;

    Settings::values.enable_overlay = ui->action_Enable_Overlay_Applet->isChecked();
}

void MainWindow::UpdateInputDrivers() {
    if (!input_subsystem)
        return;
    input_subsystem->PumpEvents();
}

void MainWindow::HideMouseCursor() {
    if (QtCommon::emu_thread == nullptr && UISettings::values.hide_mouse) {
        mouse_hide_timer.stop();
        ShowMouseCursor();
        return;
    }
    render_window->setCursor(QCursor(Qt::BlankCursor));
}

void MainWindow::ShowMouseCursor() {
    render_window->unsetCursor();
    if (QtCommon::emu_thread != nullptr && UISettings::values.hide_mouse)
        mouse_hide_timer.start();
}

void MainWindow::OnMouseActivity() {
    if (!Settings::values.mouse_panning) {
        ShowMouseCursor();
    }
}

void MainWindow::OnCheckFirmwareDecryption(bool prompt) {
    if (!ContentManager::AreKeysPresent() && !prompt) {
        // At startup only log it: loose ExeFS/RomFS dumps (all Switch 2 games so far) need no
        // keys, and a modal question blocked every unattended launch. Tools > Install Decryption
        // Keys still installs them.
        LOG_WARNING(Frontend, "Decryption keys are missing from {}: NCA, NSP and XCI games need them",
                    Common::FS::PathToUTF8String(
                        Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir)));
    } else if (!ContentManager::AreKeysPresent()) {
        Spotlight::StartupPrompt keys_prompt(this, Spotlight::StartupPrompt::Tone::Warning,
                                             tr("Derivation Components Missing"),
                                             tr("Decryption keys are missing. Install them now?"));
        keys_prompt.AddButton(tr("No"), false);
        auto* yes = keys_prompt.AddButton(tr("Yes"), true);
        keys_prompt.exec();
        if (keys_prompt.ClickedButton() == yes) {
            OnInstallDecryptionKeys();
        }
    }

    SetFirmwareVersion();
    UpdateMenuState();
}

#ifdef __unix__
void MainWindow::OnCheckGraphicsBackend() {
    const QString platformName = QGuiApplication::platformName();
    const QByteArray qtPlatform = qgetenv("QT_QPA_PLATFORM");

    if (platformName == QStringLiteral("xcb") || qtPlatform == "xcb")
        return;

    const bool isWayland =
        platformName.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive) ||
        qtPlatform.startsWith("wayland");
    if (!isWayland)
        return;

    const bool currently_hidden = UISettings::values.gui_hide_backend_warning.GetValue();
    if (currently_hidden)
        return;

    Spotlight::StartupPrompt prompt(
        this, Spotlight::StartupPrompt::Tone::Warning, tr("Wayland Detected!"),
        tr("Wayland can cause performance problems. Use X11 for future launches?"));
    auto* hide = prompt.AddCheckBox(tr("Don't show again"));
    prompt.AddButton(tr("Continue with Wayland"), false);
    auto* use_x11 = prompt.AddButton(tr("Use X11"), true);
    prompt.exec();

    if (hide->isChecked()) {
        UISettings::values.gui_hide_backend_warning.SetValue(true);
    }

    if (prompt.ClickedButton() == use_x11) {
        UISettings::values.gui_force_x11.SetValue(true);
        GraphicsBackend::SetForceX11(true);
        Spotlight::StartupPrompt::Notify(this, Spotlight::StartupPrompt::Tone::Info,
                                         tr("Restart Required"),
                                         tr("Restart Lichii to apply the X11 backend."));
    }
}
#endif

bool MainWindow::CheckFirmwarePresence() {
    return FirmwareManager::CheckFirmwarePresence(*QtCommon::system.get());
}

void MainWindow::SetFirmwareVersion() {
    const auto pair = FirmwareManager::GetFirmwareVersion(*QtCommon::system.get());
    const auto firmware_data = pair.first;
    const auto result = pair.second;

    if (result.IsError() || !CheckFirmwarePresence()) {
        LOG_INFO(Frontend, "Installed firmware: No firmware available");
        ui->menu_Applets->setEnabled(false);
        ui->menu_Create_Shortcuts->setEnabled(false);
        firmware_action->setText(tr("Firmware not installed"));
        firmware_action->setToolTip({});
        return;
    }

    ui->menu_Applets->setEnabled(true);
    ui->menu_Create_Shortcuts->setEnabled(true);

    const std::string display_version(firmware_data.display_version.data());
    const std::string display_title(firmware_data.display_title.data());

    LOG_INFO(Frontend, "Installed firmware: {}", display_version);

    firmware_action->setText(tr("Firmware: %1").arg(QString::fromStdString(display_version)));
    firmware_action->setToolTip(QString::fromStdString(display_title));
}

void MainWindow::SetFPSSuffix() {
    const auto mode = Settings::values.current_speed_mode.GetValue();
    turbo_action->setChecked(mode == Settings::SpeedMode::Turbo);
    slow_action->setChecked(mode == Settings::SpeedMode::Slow);
    switch (mode) {
    case Settings::SpeedMode::Slow:
        m_fpsSuffix = tr("Slow");
        break;
    case Settings::SpeedMode::Turbo:
        m_fpsSuffix = tr("Turbo");
        break;
    case Settings::SpeedMode::Standard:
        const bool limited = Settings::values.use_speed_limit.GetValue();
        m_fpsSuffix = limited ? QString{} : tr("Unlocked");
        break;
    }
}

bool MainWindow::ConfirmClose() {
    if (QtCommon::emu_thread == nullptr ||
        UISettings::values.confirm_before_stopping.GetValue() == ConfirmStop::Ask_Never)
        return true;

    if (!QtCommon::system->GetExitLocked() &&
        UISettings::values.confirm_before_stopping.GetValue() == ConfirmStop::Ask_Based_On_Game)
        return true;

    return ConfirmCard(this, tr("Close Lichii?"),
                       tr("The game is still running. Any unsaved progress will be lost."),
                       tr("Close"), QStringLiteral("x"));
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!close_pending && !ConfirmClose()) {
        event->ignore();
        return;
    }
    close_pending = true;
    game_list->SetScanEnabled(false);
    if (boot_in_progress) {
        event->ignore();
        return;
    }
    if (QtCommon::emu_thread && emulation_running) {
        event->ignore();
        if (!QtCommon::emu_thread->isRunning()) {
            OnEmulationStopped();
            return;
        }
        if (OnShutdownBegin()) {
            OnShutdownBeginDialog({});
        }
        return;
    }
    LOG_INFO(Frontend, "Closing frontend after emulation cleanup");

    UpdateUISettings();
    game_list->SaveInterfaceLayout();
    UISettings::SaveWindowState();
    hotkey_registry.SaveHotkeys();

    // Unload controllers early
    controller_dialog->UnloadController();
    game_list->UnloadController();

    // Shutdown session if the emu thread is active...
    if (QtCommon::emu_thread != nullptr)
        ShutdownGame();

    render_window->close();
    multiplayer_state->Close();
    QtCommon::system->HIDCore().UnloadInputDevices();
    Network::Shutdown();

    QWidget::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    emit sizeChanged(event->size());
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == this && event->type() == QEvent::Resize) {
        UpdateLetterboxBars();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::moveEvent(QMoveEvent* event) {
    auto window_frame_height = frameGeometry().height() - geometry().height();
    emit positionChanged(event->pos() - QPoint{0, window_frame_height});
}

static bool IsSingleFileDropEvent(const QMimeData* mime) {
    return mime->hasUrls() && mime->urls().length() == 1;
}

void MainWindow::AcceptDropEvent(QDropEvent* event) {
    if (IsSingleFileDropEvent(event->mimeData())) {
        event->setDropAction(Qt::DropAction::LinkAction);
        event->accept();
    }
}

bool MainWindow::DropAction(QDropEvent* event) {
    if (!IsSingleFileDropEvent(event->mimeData())) {
        return false;
    }

    const QMimeData* mime_data = event->mimeData();
    const QString& filename = mime_data->urls().at(0).toLocalFile();

    if (emulation_running && QFileInfo(filename).suffix() == QStringLiteral("bin")) {
        // Amiibo
        LoadAmiibo(filename);
    } else {
        // Game
        if (ConfirmChangeGame()) {
            BootGame(filename, ApplicationAppletParameters());
        }
    }
    return true;
}

void MainWindow::dropEvent(QDropEvent* event) {
    DropAction(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    AcceptDropEvent(event);
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    AcceptDropEvent(event);
}

bool MainWindow::ConfirmChangeGame() {
    if (QtCommon::emu_thread == nullptr)
        return true;

    return ConfirmCard(this, tr("Stop the game?"), tr("Any unsaved progress will be lost."),
                       tr("Stop"), QStringLiteral("stop-fill"));
}

bool MainWindow::ConfirmForceLockedExit() {
    if (QtCommon::emu_thread == nullptr)
        return true;

    return ConfirmCard(this, tr("Exit anyway?"),
                       tr("The running game asked Lichii not to exit. Any unsaved progress will "
                          "be lost."),
                       tr("Exit anyway"), QStringLiteral("stop-fill"));
}

void MainWindow::RequestGameExit() {
    if (!QtCommon::system->IsPoweredOn())
        return;

    QtCommon::system->SetExitRequested(true);
    QtCommon::system->GetAppletManager().RequestExit();
}

static void AdjustLinkColor() {
    QPalette new_pal(qApp->palette());

    if (UISettings::values.theme == "lichii")
        new_pal.setColor(QPalette::Link, Spotlight::Color::AccentLight());
    else if (UISettings::IsDarkTheme())
        new_pal.setColor(QPalette::Link, QColor(0, 190, 255, 255));
    else
        new_pal.setColor(QPalette::Link, QColor(0, 140, 200, 255));

    if (qApp->palette().color(QPalette::Link) != new_pal.color(QPalette::Link))
        qApp->setPalette(new_pal);
}

// Lichii Dark styles most widgets through its stylesheet; the palette covers what Qt draws
// without one: item view text, disabled text, links and the native parts of dialogs.
static QPalette LichiiPalette() {
    using namespace Spotlight::Color;
    QPalette palette;
    palette.setColor(QPalette::Window, Window());
    palette.setColor(QPalette::WindowText, Text());
    palette.setColor(QPalette::Base, PanelDeep());
    palette.setColor(QPalette::AlternateBase, Panel());
    palette.setColor(QPalette::ToolTipBase, Panel());
    palette.setColor(QPalette::ToolTipText, Text());
    palette.setColor(QPalette::PlaceholderText, TextMuted());
    palette.setColor(QPalette::Text, Text());
    palette.setColor(QPalette::Button, Control());
    palette.setColor(QPalette::ButtonText, Text());
    palette.setColor(QPalette::BrightText, Text());
    palette.setColor(QPalette::Light, Stroke());
    palette.setColor(QPalette::Midlight, Fill());
    palette.setColor(QPalette::Mid, Panel());
    palette.setColor(QPalette::Dark, PanelDeep());
    palette.setColor(QPalette::Shadow, Qt::black);
    palette.setColor(QPalette::Highlight, Accent());
    palette.setColor(QPalette::HighlightedText, OnAccent());
    palette.setColor(QPalette::Link, AccentLight());
    palette.setColor(QPalette::LinkVisited, Accent());
    palette.setColor(QPalette::Disabled, QPalette::WindowText, TextFaint());
    palette.setColor(QPalette::Disabled, QPalette::Text, TextFaint());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, TextFaint());
    return palette;
}

void MainWindow::UpdateMenuIcons() {
    const std::array<std::pair<QAction*, const char*>, 56> icons{{
        {ui->action_Install_File_NAND, "download"},
        {ui->action_Load_File, "file"},
        {ui->action_Load_Folder, "folder"},
        {ui->menu_recent_files->menuAction(), "clock"},
        {ui->action_Load_Amiibo, "wave"},
        {ui->menuOpen_Eden_Folders->menuAction(), "folder"},
        {ui->action_Exit, "x"},
        {ui->action_Root_Data_Folder, "folder"},
        {ui->action_NAND_Folder, "hdd"},
        {ui->action_SDMC_Folder, "sd"},
        {ui->action_Mod_Folder, "puzzle"},
        {ui->action_Log_Folder, "list"},
        {ui->action_Pause, "pause"},
        {ui->action_Stop, "stop"},
        {ui->action_Restart, "restart"},
        {ui->action_Configure, "sliders"},
        {ui->action_Configure_Current_Game, "gamepad"},
        {ui->action_Post_Processing_Shaders, "wand"},
        {ui->menu_Reset_Window_Size->menuAction(), "monitor"},
        {ui->menu_View_Debugging->menuAction(), "bug"},
        {ui->menu_Game_List_Mode->menuAction(), "grid"},
        {ui->menuGame_Icon_Size->menuAction(), "image"},
        {ui->action_View_Lobby, "search"},
        {ui->action_Start_Room, "plus"},
        {ui->action_Connect_To_Room, "link"},
        {ui->action_Show_Room, "chat"},
        {ui->action_Leave_Room, "sign_out"},
        {ui->action_Install_Keys, "key"},
        {ui->menuInstall_Firmware->menuAction(), "cpu"},
        {ui->action_Firmware_From_Folder, "folder"},
        {ui->action_Firmware_From_ZIP, "archive"},
        {ui->action_Verify_installed_contents, "check"},
        {ui->action_Data_Manager, "hdd"},
        {ui->action_Mod_Store, "puzzle"},
        {ui->menu_cabinet_applet->menuAction(), "star"},
        {ui->menu_Applets->menuAction(), "apps"},
        {ui->menu_Create_Shortcuts->menuAction(), "share"},
        {ui->action_Desktop, "monitor"},
        {ui->action_Application_Menu, "apps"},
        {ui->action_Capture_Screenshot, "image"},
        {ui->menuTAS->menuAction(), "play"},
        {ui->action_TAS_Start, "play"},
        {ui->action_TAS_Record, "record"},
        {ui->action_TAS_Reset, "restart"},
        {ui->action_Configure_Tas, "sliders"},
        {ui->action_Launch_QLaunch, "apps"},
        {ui->action_Launch_MiiEdit, "user"},
        {ui->action_Launch_Controller, "gamepad"},
        {ui->action_Launch_PhotoViewer, "image"},
        {ui->action_Open_UserHandbook, "help"},
        {ui->action_Apply_Recommended_Settings, "wand"},
        {ui->action_Restore_Previous_Settings, "restart"},
        {ui->action_Check_For_Updates, "download"},
        {ui->action_Whats_New, "sparkles"},
        {ui->action_About, "info"},
        {ui->action_Eden_Dependencies, "list"},
    }};
    for (const auto& [action, name] : icons) {
        action->setIcon(MenuIcon(name));
    }
    ui->action_Open_Mods_Page->setIcon(
        UISettings::values.theme == "lichii"
            ? Spotlight::Icon(u"arrow-square-out", Spotlight::Color::MenuInk(), 16)
            : QIcon{});
}

void MainWindow::UpdateUITheme() {
    const QString default_theme = QString::fromUtf8(UISettings::themes[size_t(UISettings::default_theme)].second);
    QString current_theme = QString::fromStdString(UISettings::values.theme);

    if (current_theme.isEmpty())
        current_theme = default_theme;

    static const QFont default_font = qApp->font();
    static bool lichii_palette = false;
    Spotlight::ApplyAppStyle(current_theme == QStringLiteral("lichii"));
    if (current_theme == QStringLiteral("lichii")) {
        qApp->setPalette(LichiiPalette());
        qApp->setFont(Spotlight::Font(13));
        lichii_palette = true;
    } else if (lichii_palette) {
        qApp->setPalette(QPalette());
        qApp->setFont(default_font);
        lichii_palette = false;
    }

#ifdef _WIN32
    QIcon::setThemeName(current_theme);
    AdjustLinkColor();
#else
    if (current_theme == QStringLiteral("default") || current_theme == QStringLiteral("colorful")) {
        QIcon::setThemeName(current_theme == QStringLiteral("colorful") ? current_theme : startup_icon_theme);
        QIcon::setThemeSearchPaths(QStringList(default_theme_paths));
        if (isDarkMode()) {
            current_theme = QStringLiteral("default_dark");
        }
    } else {
        QIcon::setThemeName(current_theme);
        QIcon::setThemeSearchPaths(QStringList(QStringLiteral(":/icons")));
        AdjustLinkColor();
    }
#endif

    if (current_theme != default_theme) {
        QString theme_uri{QStringLiteral(":%1/style.qss").arg(current_theme)};
        QFile f(theme_uri);
        if (!f.open(QFile::ReadOnly | QFile::Text)) {
            LOG_ERROR(Frontend, "Unable to open style \"{}\", fallback to the default theme",
                      UISettings::values.theme);
            current_theme = default_theme;
        }
    }

    QString theme_uri{QStringLiteral(":%1/style.qss").arg(current_theme)};
    QFile f(theme_uri);
    if (f.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&f);
        qApp->setStyleSheet(ts.readAll());
        setStyleSheet(ts.readAll());
    } else {
        LOG_ERROR(Frontend, "Unable to set style \"{}\", stylesheet file not found",
                  UISettings::values.theme);
        qApp->setStyleSheet({});
        setStyleSheet({});
    }

    UpdateMenuIcons();

#ifdef _WIN32
    RemoveTitlebarFilter();
    ApplyGlobalDarkTitlebar(UISettings::IsDarkTheme());
#endif
}

void MainWindow::LoadTranslation() {
    bool loaded;

    const std::string language = UISettings::values.language.GetValue();
    if (language.empty()) {
        // If the selected language is empty, use system locale
        loaded = translator.load(QLocale(), {}, {}, QStringLiteral(":/languages/"));
    } else {
        // Otherwise load from the specified file
        loaded = translator.load(QString::fromStdString(language), QStringLiteral(":/languages/"));
    }

    if (loaded) {
        qApp->installTranslator(&translator);
    } else if (!language.empty() && language != "en") {
        // A language this build has no translation for shows English, but the setting is kept
        // (the language list marks it as missing), so a build that has the translation shows it
        // again. Likewise <System> stays when the system's language has no translation. English
        // works without its file (en.qm only holds plurals, and builds without translations lack
        // it), so it gets no warning.
        LOG_WARNING(Frontend, "No UI translation for \"{}\" in this build, showing English",
                    language);
    }
}

void MainWindow::OnLanguageChanged(const QString& locale) {
    if (UISettings::values.language.GetValue() != std::string("en"))
        qApp->removeTranslator(&translator);

    UISettings::values.language = locale.toStdString();
    LoadTranslation();

    // After the new translator is in: renamed before it, the sizes stayed in English.
    QList<QAction*> actions = game_size_actions->actions();
    for (size_t i = 0; i < default_game_icon_sizes.size(); i++) {
        actions.at(i)->setText(GetTranslatedGameIconSize(i));
    }

    ui->retranslateUi(this);
    multiplayer_state->retranslateUi();
    UpdateWindowTitle();
    // The status bar's buttons are named in code, so retranslateUi leaves them as they were.
    UpdateStatusButtons();
#ifdef HAS_RESHADE
    UpdatePostShaderText();
#endif
}

void MainWindow::SetDiscordEnabled([[maybe_unused]] bool state) {
#ifdef USE_DISCORD_PRESENCE
    if (state)
        discord_rpc = std::make_unique<DiscordRPC::DiscordImpl>(*QtCommon::system);
    else
        discord_rpc = std::make_unique<DiscordRPC::NullImpl>();
#else
    discord_rpc = std::make_unique<DiscordRPC::NullImpl>();
#endif
    discord_rpc->Update();
}

void MainWindow::SetGamemodeEnabled(bool state) {
    if (emulation_running) {
        if (state)
            Common::FeralGamemode::Start();
        else
            Common::FeralGamemode::Stop();
    }
}

void MainWindow::changeEvent(QEvent* event) {
#ifdef __unix__
    // PaletteChange event appears to only reach so far into the GUI, explicitly asking to
    // UpdateUITheme is a decent work around
    if (event->type() == QEvent::PaletteChange) {
        const QPalette test_palette(qApp->palette());
        const QString current_theme = QString::fromStdString(UISettings::values.theme);
        // Keeping eye on QPalette::Window to avoid looping. QPalette::Text might be useful too
        static QColor last_window_color;
        const QColor window_color = test_palette.color(QPalette::Active, QPalette::Window);
        if (last_window_color != window_color && (current_theme == QStringLiteral("default") ||
                                                  current_theme == QStringLiteral("colorful"))) {
            UpdateUITheme();
        }
        last_window_color = window_color;
    }
#endif // __unix__
    QWidget::changeEvent(event);
}

Service::AM::FrontendAppletParameters MainWindow::ApplicationAppletParameters() {
    return Service::AM::FrontendAppletParameters{
        .applet_id = Service::AM::AppletId::Application,
        .applet_type = Service::AM::AppletType::Application,
    };
}

Service::AM::FrontendAppletParameters MainWindow::LibraryAppletParameters(
    u64 program_id, Service::AM::AppletId applet_id) {
    return Service::AM::FrontendAppletParameters{
        .program_id = program_id,
        .applet_id = applet_id,
        .applet_type = Service::AM::AppletType::LibraryApplet,
    };
}
#ifdef main
#undef main
#endif

#if !defined(QT_STATICPLUGIN) || defined(__APPLE__)
#define VMA_IMPLEMENTATION 1
#include "video_core/vulkan_common/vma.h"
#endif
