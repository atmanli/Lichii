// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDialog>
#include <QFutureWatcher>
#include <QImage>
#include <QList>
#include <QPixmap>
#include <QSet>
#include "configuration/shared_widget.h"
#include "core/file_sys/vfs/vfs_types.h"
#include "frontend_common/config.h"
#include "qt_common/config/qt_config.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/util/vk.h"
#include "lichii/configuration/configuration_shared.h"

namespace Core {
class System;
}

namespace InputCommon {
class InputSubsystem;
}

class ConfigurePerGameAddons;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureGraphicsExtensions;
class ConfigureInputPerGame;
class ConfigureSystem;
class ConfigureNetwork;
class ConfigureApplets;

class QLabel;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QVBoxLayout;

namespace Ui {
class ConfigurePerGame;
}

namespace Spotlight {
class SettingsPage;
}

class ConfigurePerGame : public QDialog {
    Q_OBJECT

public:
    // Cannot use std::filesystem::path due to https://bugreports.qt.io/browse/QTBUG-73263
    explicit ConfigurePerGame(QWidget* parent, u64 title_id_, const std::string& file_name,
                              std::vector<VkDeviceInfo::Record>& vk_device_records,
                              Core::System& system_);
    ~ConfigurePerGame() override;

    /// Save all button configurations to settings file
    void ApplyConfiguration();

    void LoadFromFile(FileSys::VirtualFile file_);
    void ReloadAddonsAfterModStore(const QSet<QString>& store_toggled);

    /// The name the library should show when the user renamed the game in this dialog: the custom
    /// name, or the game's own name after the field was cleared.
    std::optional<QString> RenamedTitle() const {
        return renamed_title;
    }

    /// True when a new custom name was saved or the custom name was removed, even when there is no
    /// name to show in place and the library reads it again.
    bool TitleChanged() const {
        return title_changed;
    }

    /// An icon picked for a NUP or NX game, as a 256x256 JPEG: it is written into the game file
    /// once Game Settings has closed and released the file.
    std::optional<QByteArray> TakeArchiveIcon() {
        return std::exchange(archive_icon, std::nullopt);
    }

    /// The user asked to edit the title and icon stored in the game file.
    bool ArchiveEditRequested() const {
        return archive_edit_requested;
    }

signals:
    void BrowseModsRequested();
    void OpenSaveDataRequested();

    /// The custom artwork was saved or removed.
    void CustomArtworkChanged(u64 program_id);

private:
    void done(int result) override;
    void reject() override;
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void BuildSettings();
    void BuildAddons(int mods, int updates);
    void RebuildAddons();
    void AddPresetRow(QVBoxLayout* section);
    void BuildNativeModes(int category);
    void BuildInfoPage(int category);
    void BuildArtworkPage(int category);
    /// Shown on the Artwork page: a warning when detection finds nothing, an error when saving
    /// fails.
    void ShowArtworkProblem(const QString& text, bool error);
    void UpdateModsSummary();
    void UpdateSpotlightIdentity();

    void LoadConfiguration();
    void UpdateIconPreview(const QPixmap& map);
    void StageCustomIcon(QImage icon);
    void OnUploadNupIcon();
    void OnDetectNupIcon();
    void OnBrowseIiDB();
    void OnRemoveCustomIcon();
    void UpdateRemoveIconButton();
    bool SaveCustomMetadata(bool show_error = false);

    struct DetectIconResult {
        QImage icon;
        std::string resolved_title;
        std::string error;
    };

    std::unique_ptr<Ui::ConfigurePerGame> ui;
    FileSys::VirtualFile file;
    std::filesystem::path file_path;
    const u64 title_id;
    bool is_nup_file{false};
    /// NUP and NX files take the icon inside them; other formats keep it for this PC only.
    bool icon_in_file{false};
    std::optional<QByteArray> archive_icon;
    bool archive_edit_requested{false};
    QLabel* icon_destination = nullptr;
    std::string original_title;
    std::string initial_title;
    std::optional<QString> renamed_title;
    bool title_changed{false};
    QImage pending_custom_icon;
    /// The game's own icon, shown again when the custom artwork is removed.
    QPixmap file_icon;
    bool has_custom_icon{false};
    /// The saved custom artwork is deleted when Game Settings closes.
    bool remove_custom_icon{false};
    QFutureWatcher<DetectIconResult> detect_watcher;
    std::shared_ptr<std::atomic_bool> detect_cancel;

    Spotlight::SettingsPage* settings_page = nullptr;
    QLabel* game_banner_icon = nullptr;
    QLabel* game_banner_title = nullptr;
    QLabel* game_banner_summary = nullptr;
    QLabel* game_banner_version = nullptr;
    int artwork_category = -1;
    QLabel* artwork_image = nullptr;
    QLabel* info_image = nullptr;
    QWidget* artwork_detecting = nullptr;
    QWidget* artwork_problem = nullptr;
    QLabel* artwork_problem_text = nullptr;
    QLabel* update_summary = nullptr;
    QList<QLabel*> mods_summaries;
    QVBoxLayout* store_rows = nullptr;
    QLabel* store_header = nullptr;
    QLabel* own_header = nullptr;
    QVBoxLayout* own_rows = nullptr;
    QVBoxLayout* content_rows = nullptr;
    bool addons_queued = false;
    QPixmap shown_icon;

    std::unique_ptr<QtConfig> game_config;

    Core::System& system;
    const std::vector<VkDeviceInfo::Record>& devices;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::shared_ptr<std::vector<ConfigurationShared::Tab*>> tab_group;

    std::unique_ptr<ConfigurePerGameAddons> addons_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphicsExtensions> graphics_extensions_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureInputPerGame> input_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureApplets> applets_tab;
};
