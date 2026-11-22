// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>
#include <QDialog>
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"

namespace Core {
class System;
}

class ConfigureApplets;
class ConfigureAudio;
class ConfigureCpu;
class ConfigureDebugTab;
class ConfigureFilesystem;
class ConfigureGeneral;
class ConfigureGraphics;
class ConfigureGraphicsAdvanced;
class ConfigureGraphicsExtensions;
class ConfigureHotkeys;
class ConfigureInput;
class ConfigureProfileManager;
class ConfigureSystem;
class ConfigureNetwork;
class ConfigureUi;
class ConfigureWeb;

class HotkeyRegistry;
class QLabel;
class QVBoxLayout;

namespace ConfigurationShared {
class Builder;
}

namespace Spotlight {
class SettingsPage;
class ToggleSwitch;
}

namespace InputCommon {
class InputSubsystem;
}

namespace Ui {
class ConfigureDialog;
}

class ConfigureDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                             InputCommon::InputSubsystem* input_subsystem,
                             std::vector<VkDeviceInfo::Record>& vk_device_records,
                             Core::System& system_, bool enable_web_config = true);
    ~ConfigureDialog() override;

    void ApplyConfiguration();

    /// Drop the controller edits that were not applied; the Controls tab changes them live.
    void RestoreInputConfiguration();

    /// A new interface language was applied: the dialog closed to be opened again translated.
    bool ReopenRequested() const {
        return reopen_requested;
    }

private slots:
    void OnLanguageChanged(const QString& locale);

signals:
    void LanguageChanged(const QString& locale);
    void ExternalContentDirsChanged();
    void FullscreenOnStartChanged(bool enabled);
    void OpenGameFolderRequested(const QString& folder);

private:
    void changeEvent(QEvent* event) override;
    void HandleApplyButtonClicked();
    void BuildSettings(const std::vector<VkDeviceInfo::Record>& devices);
    void BuildControls(int category, int shortcuts);
    void RebuildGameFolders();

    std::unique_ptr<Ui::ConfigureDialog> ui;
    HotkeyRegistry& registry;

    Core::System& system;
    std::unique_ptr<ConfigurationShared::Builder> builder;

    std::unique_ptr<ConfigureApplets> applets_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureDebugTab> debug_tab_tab;
    std::unique_ptr<ConfigureFilesystem> filesystem_tab;
    std::unique_ptr<ConfigureGeneral> general_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphicsExtensions> graphics_extensions_tab;
    std::unique_ptr<ConfigureUi> ui_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureHotkeys> hotkeys_tab;
    std::unique_ptr<ConfigureInput> input_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureProfileManager> profile_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureWeb> web_tab;

    Spotlight::SettingsPage* settings_page = nullptr;
    Spotlight::ToggleSwitch* start_fullscreen = nullptr;
    Spotlight::ToggleSwitch* check_updates = nullptr;
    QVector<UISettings::GameDir> game_dirs;
    QVBoxLayout* folders_layout = nullptr;
    QLabel* folder_summary = nullptr;
    bool game_dirs_changed = false;
    bool reopen_requested = false;
};
