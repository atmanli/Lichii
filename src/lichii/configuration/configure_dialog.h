// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
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
class ConfigureMicrophone;
class ConfigureProfileManager;
class ConfigureSystem;
class ConfigureNetwork;
class ConfigureUi;
class ConfigureWeb;

class HotkeyRegistry;
class QAbstractButton;
class QCheckBox;
class QColor;
class QComboBox;
class QLabel;
class QVBoxLayout;

namespace ConfigurationShared {
class Builder;
}

namespace Settings {
class BasicSetting;
}

namespace Spotlight {
class PresetCard;
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
                             InputCommon::InputSubsystem* input_subsystem_,
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
    /// The Get Started wizard installed keys or firmware.
    void ContentInstalled();

private:
    void changeEvent(QEvent* event) override;
    void reject() override;
    struct PresetSources {
        QComboBox* resolution = nullptr;
        QComboBox* filter = nullptr;
        QComboBox* aa = nullptr;
        QCheckBox* frame_generation = nullptr;
        QComboBox* device = nullptr;

        bool CanApply() const;
    };

    void BuildSettings();
    void BuildGeneral(int general, int library, int controls);
    void BuildLibrary(int library);
    void BuildGraphics(int graphics);
    void BuildControls(int category, int shortcuts);
    void BuildShortcuts(int shortcuts);
    void BuildSystem(int system_page);
    void BuildProfiles(int profiles);
    void RebuildProfiles();
    void EnsureInput();
    void AddSettingRow(QVBoxLayout* group, QWidget* page, const Settings::BasicSetting& setting,
                       const QString& title, const QString& detail, const QString& icon,
                       const QColor& color);
    PresetSources FindPresetSources() const;
    const VkDeviceInfo::Record* SelectedGpu(const PresetSources& sources) const;
    int CurrentPreset(const PresetSources& sources) const;
    void ApplyPreset(const PresetSources& sources, int selection);
    void WatchPreset(const PresetSources& sources, QObject* context,
                     const std::function<void()>& sync);
    void BuildMicrophone(int category, ConfigureMicrophone* microphone);
    void RebuildGameFolders();
    QString GameFolderSummary() const;
    void AddGameFolder();
    void OpenGetStarted(int controls, const QList<Spotlight::PresetCard*>& presets);

    std::unique_ptr<Ui::ConfigureDialog> ui;
    HotkeyRegistry& registry;

    Core::System& system;
    const std::vector<VkDeviceInfo::Record>& devices;
    InputCommon::InputSubsystem* input_subsystem;
    int mapping_page = -1;
    QVBoxLayout* profile_rows = nullptr;
    QAbstractButton* add_profile = nullptr;
    bool input_initialized = false;
    QList<int> vibration_strength;
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
    QVector<UISettings::GameDir> game_dirs;
    QVBoxLayout* folders_layout = nullptr;
    QLabel* folder_summary = nullptr;
    bool game_dirs_changed = false;
    bool reopen_requested = false;
};
