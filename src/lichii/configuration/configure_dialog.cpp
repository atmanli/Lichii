// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <tuple>
#include <utility>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"
#include "lichii/configuration/configuration_shared.h"
#include "lichii/configuration/configure_applets.h"
#include "lichii/configuration/configure_audio.h"
#include "lichii/configuration/configure_cpu.h"
#include "lichii/configuration/configure_debug_tab.h"
#include "lichii/configuration/configure_dialog.h"
#include "lichii/configuration/configure_filesystem.h"
#include "lichii/configuration/configure_general.h"
#include "lichii/configuration/configure_graphics.h"
#include "lichii/configuration/configure_graphics_advanced.h"
#include "lichii/configuration/configure_graphics_extensions.h"
#include "lichii/configuration/configure_hotkeys.h"
#include "lichii/configuration/configure_input.h"
#include "lichii/configuration/configure_network.h"
#include "lichii/configuration/configure_profile_manager.h"
#include "lichii/configuration/configure_system.h"
#include "lichii/configuration/configure_ui.h"
#include "lichii/configuration/configure_web.h"
#include "lichii/configuration/shared_widget.h"
#include "lichii/hotkeys.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace {
using Spotlight::SettingsPage;
namespace Color = Spotlight::Color;
namespace Tints = Spotlight::SettingsColor;

int SuggestedTier(const VkDeviceInfo::Record* device) {
    if (device == nullptr) {
        return 0;
    }
    const QString name = QString::fromStdString(device->name);
    static const QRegularExpression high(
        QStringLiteral(R"(RTX\s+[345]\d{3}\b|RX\s+[67][789]\d{2}\b)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mid(
        QStringLiteral(
            R"(RTX\s+\d{4}\b|GTX\s+\d{4}\b|RX\s+\d{3,4}\b|Arc\s+[AB]\d{3}\b|Apple M[1-9])"),
        QRegularExpression::CaseInsensitiveOption);
    if (device->fsr4_capable || high.match(name).hasMatch()) {
        return 2;
    }
    return mid.match(name).hasMatch() ? 1 : 0;
}

bool IsBuiltInFolder(const UISettings::GameDir& folder) {
    return folder.path == "SDMC" || folder.path == "UserNAND" || folder.path == "SysNAND";
}

class PresetCard final : public QAbstractButton {
public:
    PresetCard(const QString& title, QString description_) : description{std::move(description_)} {
        setText(title);
        setCheckable(true);
        setProperty("spotlightTransparent", true);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(98);
    }

    void SetSuggestion(QString device, QString tier) {
        suggestion = std::move(device);
        suggested_tier = std::move(tier);
        setToolTip(suggestion + suggested_tier);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool active = isChecked();
        painter.setBrush(active         ? Color::Accent()
                         : underMouse() ? Color::Fill()
                                        : Color::FillFaint());
        painter.setPen(hasFocus() ? Color::Text()
                       : active   ? Color::Accent()
                                  : Color::StrokeSubtle());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);

        const QColor ink = !isEnabled() ? Color::TextFaint()
                           : active     ? Color::OnAccent()
                                        : Color::Text();
        painter.setPen(ink);
        painter.setFont(Spotlight::Font(15, 600));
        painter.drawText(QRect(14, 13, width() - 28, 21), Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, width() - 28));
        if (active) {
            painter.drawPixmap(
                width() - 28, 15,
                Spotlight::IconPixmap(QStringLiteral("check"), 16, ink, devicePixelRatioF()));
        }
        painter.setFont(Spotlight::Font(12));
        painter.setPen(active ? ink : Color::TextSecondary());
        painter.drawText(QRect(14, 36, width() - 28, 34), Qt::TextWordWrap | Qt::AlignLeft,
                         description);
        if (!suggestion.isEmpty()) {
            painter.setPen(active ? ink : Color::AccentLight());
            painter.setFont(Spotlight::Font(12, 600));
            const int remaining =
                width() - 28 - painter.fontMetrics().horizontalAdvance(suggested_tier);
            painter.drawText(
                QRect(14, 69, width() - 28, 16), Qt::AlignVCenter | Qt::AlignLeft,
                painter.fontMetrics().elidedText(suggestion, Qt::ElideRight, remaining) +
                    suggested_tier);
        }
    }

private:
    QString description;
    QString suggestion;
    QString suggested_tier;
};
} // namespace

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);
    input_tab->Initialize(input_subsystem);
    general_tab->SetResetCallback([this] { close(); });
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);

    if (system.IsPoweredOn()) {
        auto* apply = ui->buttonBox->addButton(QDialogButtonBox::Apply);
        connect(apply, &QPushButton::clicked, this, &ConfigureDialog::HandleApplyButtonClicked);
    }
    ui->label->setVisible(system.IsPoweredOn());
    BuildSettings(vk_device_records);
    ui->buttonBox->setFocus();
    ConfigurationShared::FitToScreen(this, QSize(1280, 800));
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    input_tab->ApplyConfiguration();
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    if (check_updates != nullptr) {
        UISettings::values.check_for_updates.SetValue(check_updates->isChecked());
    }
    if (start_fullscreen->isEnabled() &&
        UISettings::values.fullscreen.GetValue() != start_fullscreen->isChecked()) {
        UISettings::values.fullscreen.SetValue(start_fullscreen->isChecked());
        emit FullscreenOnStartChanged(start_fullscreen->isChecked());
    }
    if (game_dirs_changed) {
        UISettings::values.game_dirs = game_dirs;
        UISettings::values.is_game_list_reload_pending = true;
        game_dirs_changed = false;
    }
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::RestoreInputConfiguration() {
    input_tab->RestoreConfiguration();
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    QDialog::changeEvent(event);
}

void ConfigureDialog::HandleApplyButtonClicked() {
    UISettings::values.configuration_applied = true;
    ApplyConfiguration();
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    UISettings::values.is_game_list_reload_pending = true;
    ApplyConfiguration();
    reopen_requested = true;
    accept();
}

void ConfigureDialog::BuildSettings(const std::vector<VkDeviceInfo::Record>& devices) {
    settings_page = new SettingsPage(this, false);
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->addWidget(settings_page);
    settings_page->SetFooter(ui->footer);
    connect(settings_page, &SettingsPage::BackRequested, this, &QDialog::reject);

    const int general =
        settings_page->AddCategory(tr("General"), tr("Preset, look, folders and updates"),
                                   QStringLiteral("globe"), Color::Accent());
    const int library = settings_page->AddCategory(
        tr("Library"), tr("Choose where Lichii looks for games and what the library shows"),
        QStringLiteral("folder"), Tints::Green());
    const int graphics = settings_page->AddCategory(
        tr("Graphics"), tr("Set a preset or choose individual picture settings"),
        QStringLiteral("monitor"), Tints::Gold());
    const int audio = settings_page->AddCategory(
        tr("Audio"), tr("Choose where game audio plays and how loud it is"),
        QStringLiteral("speaker-high"), Tints::Blue());
    const int controls = settings_page->AddCategory(
        tr("Controls"), tr("One controller per player; hotkeys work with keyboard and pad"),
        QStringLiteral("game-controller"), Tints::Lavender());
    const int system_page = settings_page->AddCategory(
        tr("System"), tr("Choose the emulated console's language, region and storage"),
        QStringLiteral("cpu"), Color::Accent());
    const int multiplayer = settings_page->AddCategory(
        tr("Multiplayer"), tr("Configure local networking and online services"),
        QStringLiteral("users"), Tints::Green());
    const int shortcuts =
        settings_page->AddCategory(tr("Shortcuts"), tr("Edit keyboard and controller shortcuts"),
                                   QStringLiteral("keyboard"), Tints::Gold());

    auto* preset_group = new QWidget;
    settings_page->AddBlock(general, preset_group, tr("Performance preset"));
    auto* view = ui_tab.get();
    auto* options = settings_page->AddSection(general);
    auto* language = view->findChild<QComboBox*>(QStringLiteral("language_combobox"));
    const auto locale_name = [this](const QString& name) {
        return name.startsWith(QChar::fromLatin1('<')) ? tr("System language") : name;
    };
    auto* locale_description = SettingsPage::AddRow(
        options, tr("Language"), locale_name(language->currentText()), QStringLiteral("globe"),
        Color::Accent(),
        SettingsPage::BindCombo(language, {}, [](const QComboBox& combo, int index) {
            const auto locale = combo.itemData(index).toString();
            if (locale.isEmpty()) {
                return tr("System");
            }
            const QLocale parsed(locale);
            return parsed.language() == QLocale::C ? combo.itemText(index)
                                                   : QLocale::languageToString(parsed.language());
        }));
    connect(language, &QComboBox::currentTextChanged, locale_description,
            [locale_description, locale_name](const QString& name) {
                locale_description->setText(locale_name(name));
            });

    auto* theme = view->findChild<QComboBox*>(QStringLiteral("theme_combobox"));
    const int dark_index = theme->findData(QStringLiteral("lichii"));
    const int light_index = theme->findData(QStringLiteral("default"));
    const QStringList looks{tr("Dark"), tr("Light")};
    auto* look =
        new Spotlight::SegmentedControl(looks, Spotlight::SegmentedControl::Kind::Accent, 36);
    const auto sync_theme = [look, theme, dark_index, light_index, looks, this] {
        const QSignalBlocker blocker(look);
        const int selected = theme->currentIndex() == dark_index    ? 0
                             : theme->currentIndex() == light_index ? 1
                                                                    : 2;
        QStringList items = looks;
        if (selected == 2) {
            items.append(tr("Other"));
        }
        look->SetOptions(items);
        look->SetCurrentIndex(selected);
        look->SetOptionEnabled(2, false);
    };
    sync_theme();
    connect(theme, &QComboBox::currentIndexChanged, look, sync_theme);
    connect(look, &Spotlight::SegmentedControl::CurrentChanged, theme,
            [theme, dark_index, light_index](int index) {
                SettingsPage::SetComboIndex(theme, index == 0 ? dark_index : light_index);
            });
    SettingsPage::AddRow(options, tr("Look"), tr("Choose the window's color theme"),
                         QStringLiteral("palette"), Tints::Lavender(), look);
    auto* manage_folders = SettingsPage::Manage(tr("Manage"));
    connect(manage_folders, &QAbstractButton::clicked, settings_page,
            [this, library] { settings_page->SetCategory(library); });
    folder_summary = SettingsPage::AddRow(options, tr("Game folders"), tr("No folders added"),
                                          QStringLiteral("folder"), Tints::Green(), manage_folders);
#ifdef ENABLE_UPDATE_CHECKER
    check_updates = new Spotlight::ToggleSwitch;
    check_updates->setChecked(UISettings::values.check_for_updates.GetValue());
    SettingsPage::AddRow(options, tr("Updates"), tr("Check for new Lichii builds at start"),
                         QStringLiteral("arrow-circle-up"), Tints::Gold(), check_updates);
#endif

    auto* quick = settings_page->AddSection(graphics, tr("Quick"));
    auto* picture = settings_page->AddSection(graphics, tr("Picture"));
    auto& values = Settings::values;
    auto* gfx = graphics_tab.get();
    auto* resolution = SettingsPage::Editor(gfx, values.resolution_setup);
    auto* filter = SettingsPage::Editor(gfx, values.scaling_filter);
    auto* aa = SettingsPage::Editor(gfx, values.anti_aliasing);
    auto* frame_gen = SettingsPage::Editor(gfx, values.frame_gen);
    auto* preset_cards = new QWidget;
    preset_cards->setProperty("spotlightTransparent", true);
    auto* cards_layout = new QHBoxLayout(preset_cards);
    cards_layout->setContentsMargins(0, 0, 0, 0);
    cards_layout->setSpacing(10);
    const QStringList preset_names{Spotlight::PerformancePresetName(0),
                                   Spotlight::PerformancePresetName(1),
                                   Spotlight::PerformancePresetName(2)};
    const std::array cards{
        new PresetCard(tr("Auto"), tr("Picks for your GPU")),
        new PresetCard(preset_names[0], tr("Older or integrated GPUs")),
        new PresetCard(preset_names[1], tr("Most gaming laptops")),
        new PresetCard(preset_names[2], tr("Desktop GPUs")),
    };
    for (auto* card : cards) {
        cards_layout->addWidget(card, 1);
    }
    auto* presets_layout = new QVBoxLayout(preset_group);
    presets_layout->setContentsMargins(0, 0, 0, 0);
    presets_layout->setSpacing(6);
    auto* presets_label = SettingsPage::Text(
        tr("Preset · changes inherited settings only; game overrides stay"), 13, 600, true);
    presets_label->setContentsMargins(6, 3, 6, 0);
    presets_layout->addWidget(presets_label);
    presets_layout->addWidget(preset_cards);

    const QStringList preset_choices{tr("Auto"), tr("Low"), tr("Mid"), tr("High")};
    auto* presets_control = new Spotlight::SegmentedControl(
        preset_choices, Spotlight::SegmentedControl::Kind::Accent, 36);
    auto* preset_summary =
        SettingsPage::AddRow(quick, tr("Preset"), tr("Custom graphics settings"),
                             QStringLiteral("lightning"), Color::Accent(), presets_control);
    if (resolution && filter && aa && frame_gen) {
        auto* device_editor = SettingsPage::Editor(gfx, values.vulkan_device);
        auto* device_combo = device_editor ? device_editor->combobox : nullptr;
        const auto selected_gpu = [device_combo, &devices]() -> const VkDeviceInfo::Record* {
            const int index = device_combo ? device_combo->currentIndex() : -1;
            return index >= 0 && std::cmp_less(index, devices.size()) ? &devices[index] : nullptr;
        };
        const auto sync_gpu = [cards, selected_gpu, this] {
            const auto* gpu = selected_gpu();
            cards[0]->SetSuggestion(
                gpu ? QString::fromStdString(gpu->name) : tr("No GPU identified"),
                tr(" → %1").arg(Spotlight::PerformancePresetName(SuggestedTier(gpu))));
        };
        sync_gpu();
        if (device_combo != nullptr) {
            connect(device_combo, &QComboBox::currentIndexChanged, this, sync_gpu);
        }

        auto* res_combo = resolution->combobox;
        auto* filter_combo = filter->combobox;
        auto* aa_combo = aa->combobox;
        auto* frame_check = frame_gen->checkbox;
        const bool can_apply = res_combo->isEnabled() && filter_combo->isEnabled() &&
                               aa_combo->isEnabled() && frame_check->isEnabled();
        for (auto* card : cards) {
            card->setEnabled(can_apply);
        }
        presets_control->setEnabled(can_apply);
        const auto sync_preset = [=, this] {
            const int selected = Spotlight::PerformancePresetIndex({
                static_cast<Settings::ResolutionSetup>(res_combo->currentData().toUInt()),
                static_cast<Settings::ScalingFilter>(filter_combo->currentData().toUInt()),
                static_cast<Settings::AntiAliasing>(aa_combo->currentData().toUInt()),
                frame_check->isChecked(),
            });
            for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
                cards[i]->setChecked(selected >= 0 && i == selected + 1);
            }
            const QSignalBlocker blocker(presets_control);
            QStringList choices = preset_choices;
            if (selected == -1) {
                choices.append(tr("Custom"));
            }
            presets_control->SetOptions(choices);
            presets_control->SetOptionEnabled(4, false);
            presets_control->SetCurrentIndex(selected == -1 ? 4 : selected + 1);
            preset_summary->setText(
                selected == -1 ? tr("Custom graphics settings")
                               : tr("%1: %2 resolution, %3, %4. Game overrides stay.")
                                     .arg(preset_names[selected],
                                          SettingsPage::ClosedSettingLabel(
                                              res_combo, Settings::values.resolution_setup.Id(),
                                              res_combo->currentIndex()),
                                          SettingsPage::ClosedSettingLabel(
                                              filter_combo, Settings::values.scaling_filter.Id(),
                                              filter_combo->currentIndex()),
                                          aa_combo->currentText()));
        };
        const auto apply_preset = [=](int selection) {
            if (selection == 4) {
                return;
            }
            const auto& preset =
                Spotlight::PerformancePresets[selection == 0 ? SuggestedTier(selected_gpu())
                                                             : selection - 1];
            SettingsPage::SetComboIndex(res_combo,
                                        res_combo->findData(static_cast<u32>(preset.resolution)));
            SettingsPage::SetComboIndex(filter_combo,
                                        filter_combo->findData(static_cast<u32>(preset.filter)));
            SettingsPage::SetComboIndex(aa_combo,
                                        aa_combo->findData(static_cast<u32>(preset.anti_aliasing)));
            if (frame_check->isChecked() != preset.frame_generation) {
                frame_check->click();
            }
            sync_preset();
        };
        for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
            connect(cards[i], &QAbstractButton::clicked, this,
                    [apply_preset, i] { apply_preset(i); });
        }
        connect(presets_control, &Spotlight::SegmentedControl::CurrentChanged, this, apply_preset);
        for (auto* combo : {res_combo, filter_combo, aa_combo}) {
            connect(combo, &QComboBox::currentIndexChanged, this, sync_preset);
        }
        connect(frame_check, &QCheckBox::toggled, this, sync_preset);
        sync_preset();
    }

    const auto add_setting = [](QVBoxLayout* group, QWidget* page,
                                const Settings::BasicSetting& setting, const QString& title,
                                const QString& detail, const QString& icon, const QColor& color) {
        if (auto* source = SettingsPage::Editor(page, setting)) {
            if (auto* control = SettingsPage::BindSetting(source)) {
                SettingsPage::AddRow(group, title, detail, icon, color, control);
            }
        }
    };
    add_setting(quick, gfx, values.fps_limit, tr("Guest FPS cap"),
                tr("Limits how often supported games present; 0 follows the game"),
                QStringLiteral("timer"), Tints::Green());
    add_setting(picture, gfx, values.resolution_setup, tr("Resolution"),
                tr("Choose the game's internal rendering scale"), QStringLiteral("monitor"),
                Tints::Blue());
    add_setting(picture, gfx, values.scaling_filter, tr("Scaling filter"),
                tr("How the image fits the window"), QStringLiteral("sliders-horizontal"),
                Tints::Lavender());
    auto* frame_generation = SettingsPage::BindFrameGeneration(gfx);
    if (frame_generation != nullptr) {
        SettingsPage::AddRow(picture, tr("Frame generation"),
                             tr("Adds displayed frames between game frames"),
                             QStringLiteral("film-strip"), Tints::Gold(), frame_generation);
    }
    start_fullscreen = new Spotlight::ToggleSwitch;
    start_fullscreen->setChecked(UISettings::values.fullscreen.GetValue());
    start_fullscreen->setEnabled(!system.IsPoweredOn());
    SettingsPage::AddRow(picture, tr("Fullscreen when a game starts"), {},
                         QStringLiteral("arrows-out"), Tints::Green(), start_fullscreen);

    folders_layout = settings_page->AddSection(library, tr("Game folders"));
    game_dirs = UISettings::values.game_dirs;
    RebuildGameFolders();
    auto* display = settings_page->AddSection(library, tr("Display"));
    for (const auto& [name, label] : std::array{
             std::pair{"folder_icon_size_combobox", tr("Folder icon size")},
             std::pair{"row_1_text_combobox", tr("First title line")},
             std::pair{"row_2_text_combobox", tr("Second title line")},
         }) {
        SettingsPage::AddRow(
            display, label, {}, QStringLiteral("list"), Tints::Green(),
            SettingsPage::BindCombo(view->findChild<QComboBox*>(QString::fromLatin1(name))));
    }
    for (const auto& [name, label] : std::array{
             std::pair{"show_add_ons", tr("Show add-ons")},
             std::pair{"show_size", tr("Show size")},
             std::pair{"show_types", tr("Show file formats")},
             std::pair{"show_play_time", tr("Show play time")},
         }) {
        SettingsPage::AddRow(
            display, label, {}, QStringLiteral("eye"), Tints::Blue(),
            SettingsPage::BindToggle(view->findChild<QCheckBox*>(QString::fromLatin1(name))));
    }

    auto* output = settings_page->AddSection(audio, tr("Output"));
    add_setting(output, audio_tab.get(), values.volume, tr("Volume"), tr("Game audio level"),
                QStringLiteral("speaker-high"), Tints::Blue());
    add_setting(output, audio_tab.get(), values.sink_id, tr("Audio engine"),
                tr("Backend for game audio"), QStringLiteral("sliders-horizontal"),
                Tints::Lavender());
    add_setting(output, audio_tab.get(), values.audio_output_device_id, tr("Output device"),
                tr("Speaker or headphone destination"), QStringLiteral("speaker-high"),
                Tints::Gold());

    BuildControls(controls, shortcuts);

    auto* console = settings_page->AddSection(system_page, tr("Console"));
    add_setting(console, system_tab.get(), values.language_index, tr("System language"),
                tr("Language reported to games"), QStringLiteral("translate"), Tints::Blue());
    add_setting(console, system_tab.get(), values.region_index, tr("Region"),
                tr("Region reported to games"), QStringLiteral("globe"), Color::Accent());
    auto* profile = SettingsPage::Manage(tr("Manage"));
    connect(profile, &QAbstractButton::clicked, settings_page, [this, system_page] {
        settings_page->ShowAdvancedPage(system_page, profile_tab.get());
    });
    SettingsPage::AddRow(console, tr("Profiles"), tr("Manage local console profiles"),
                         QStringLiteral("user"), Tints::Lavender(), profile);

    auto* networking = settings_page->AddSection(multiplayer, tr("Local network"));
    SettingsPage::AddRow(networking, tr("Network interface"),
                         tr("Connection to use for multiplayer"), QStringLiteral("wifi-high"),
                         Tints::Blue(),
                         SettingsPage::BindCombo(network_tab->findChild<QComboBox*>(
                             QStringLiteral("network_interface"))));
    SettingsPage::AddRow(
        networking, tr("LAN play"), tr("Enable local network play"), QStringLiteral("users"),
        Tints::Green(),
        SettingsPage::BindToggle(network_tab->findChild<QCheckBox*>(QStringLiteral("lan_play"))));

    for (const auto& [category, widget, name] :
         std::initializer_list<std::tuple<int, QWidget*, QString>>{
             {general, general_tab.get(), tr("General")},
             {general, debug_tab_tab.get(), tr("Debug")},
             {library, ui_tab.get(), tr("UI / Game List")},
             {graphics, graphics_tab.get(), tr("Graphics")},
             {graphics, graphics_advanced_tab.get(), tr("Advanced")},
             {graphics, graphics_extensions_tab.get(), tr("Extras")},
             {audio, audio_tab.get(), tr("Audio")},
             {system_page, system_tab.get(), tr("System")},
             {system_page, profile_tab.get(), tr("Profiles")},
             {system_page, cpu_tab.get(), tr("CPU")},
             {system_page, filesystem_tab.get(), tr("Filesystem")},
             {system_page, applets_tab.get(), tr("Applets")},
             {multiplayer, network_tab.get(), tr("Network")},
             {multiplayer, web_tab.get(), tr("Web")},
         }) {
        ConfigurationShared::UseToggles(widget);
        settings_page->AddPage(category, widget, name);
    }
    settings_page->AddPage(shortcuts, hotkeys_tab.get(), tr("Hotkeys"), false);

    int advanced_count = 0;
    const bool has_frame_gen_choices = qobject_cast<QComboBox*>(frame_generation) != nullptr;
    for (auto* tab : std::initializer_list<QWidget*>{
             graphics_tab.get(),
             graphics_advanced_tab.get(),
             graphics_extensions_tab.get(),
         }) {
        for (auto* editor : tab->findChildren<ConfigurationShared::Widget*>()) {
            const auto id = editor->SettingId();
            if (has_frame_gen_choices &&
                (id == values.frame_gen_type.Id() || id == values.frame_gen_multiplier.Id())) {
                continue;
            }
            if (editor->Valid() && editor->isVisibleTo(tab) && id != values.resolution_setup.Id() &&
                id != values.scaling_filter.Id() && id != values.frame_gen.Id() &&
                id != values.fps_limit.Id()) {
                ++advanced_count;
            }
        }
    }
    auto* footnote =
        SettingsPage::Text(tr("VSync, speed limit and more are under Show advanced (%n more).",
                              nullptr, advanced_count),
                           13, 500, true);
    footnote->setWordWrap(true);
    settings_page->AddBlock(graphics, footnote, tr("Show advanced"));
    settings_page->SetCategory(general);
}

void ConfigureDialog::BuildControls(int category, int shortcuts) {
    const auto pages = input_tab->GetSubTabs();
    auto* input_tabs = input_tab->findChild<QTabWidget*>(QStringLiteral("tabWidget"));
    auto* player_pages = new QStackedWidget;
    player_pages->setProperty("spotlightTransparent", true);
    QStringList player_names;
    QList<QCheckBox*> connected;
    for (int i = 0; i < 8; ++i) {
        player_names.append(tr("P%1").arg(i + 1));
        auto* state = input_tab->findChild<QCheckBox*>(
            QStringLiteral("checkboxPlayer%1Connected").arg(i + 1));
        connected.append(state);
        auto* type = pages[i]->findChild<QComboBox*>(QStringLiteral("comboControllerType"));
        auto* profile = pages[i]->findChild<QComboBox*>(QStringLiteral("comboProfiles"));
        auto* container = new QWidget;
        container->setProperty("spotlightTransparent", true);
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* card = SettingsPage::Card();
        auto* card_layout = new QHBoxLayout(card);
        card_layout->setContentsMargins(18, 20, 18, 20);
        card_layout->setSpacing(16);
        card_layout->addWidget(SettingsPage::Card(QStringLiteral("game-controller"), 102), 0,
                               Qt::AlignVCenter);
        auto* detail = new QVBoxLayout;
        detail->setSpacing(2);
        detail->addWidget(SettingsPage::Text(tr("Player %1").arg(i + 1), 13, 500, true));
        auto* controller = SettingsPage::Text(type->currentText(), 24, 600);
        controller->setFont(Spotlight::DisplayFont(24));
        connect(type, &QComboBox::currentTextChanged, controller, &QLabel::setText);
        detail->addWidget(controller);
        auto* connected_text = SettingsPage::Text({}, 12, 500, true);
        const auto sync_state = [connected_text, state, this] {
            connected_text->setText(state->isChecked() ? tr("Connected") : tr("Disconnected"));
        };
        sync_state();
        connect(state, &QAbstractButton::toggled, connected_text, sync_state);
        detail->addWidget(connected_text);
        auto* actions = new QHBoxLayout;
        actions->setSpacing(8);
        auto* mapping = new Spotlight::PillButton(tr("Map buttons"), QStringLiteral("keyboard"),
                                                  Spotlight::PillButton::Kind::Accent);
        connect(mapping, &QAbstractButton::clicked, this, [this, i, category, input_tabs] {
            input_tabs->setCurrentIndex(i);
            settings_page->ShowAdvancedPage(category, input_tab.get());
        });
        actions->addWidget(mapping);
        auto* profiles = SettingsPage::BindCombo(profile, tr("No profile selected"));
        profiles->setAccessibleName(tr("Controller profile"));
        actions->addWidget(profiles);
        actions->addStretch();
        detail->addLayout(actions);
        card_layout->addLayout(detail, 1);
        layout->addWidget(card);
        auto* type_card = SettingsPage::Card();
        auto* type_rows = new QVBoxLayout(type_card);
        type_rows->setContentsMargins(1, 1, 1, 1);
        SettingsPage::AddRow(type_rows, tr("Controller type"), tr("What the game sees"),
                             QStringLiteral("game-controller"), Tints::Green(),
                             SettingsPage::BindCombo(type));
        layout->addWidget(type_card);
        player_pages->addWidget(container);
    }
    auto* picker = new QWidget;
    picker->setProperty("spotlightTransparent", true);
    auto* picker_layout = new QHBoxLayout(picker);
    picker_layout->setContentsMargins(0, 0, 0, 0);
    auto* players = new Spotlight::SegmentedControl(player_names,
                                                    Spotlight::SegmentedControl::Kind::Accent, 36);
    players->setAccessibleName(tr("Player"));
    picker_layout->addWidget(players, 0, Qt::AlignLeft);
    picker_layout->addStretch();
    auto* count = SettingsPage::Text({}, 13, 500, true);
    count->setWordWrap(true);
    picker_layout->addWidget(count);
    const auto sync_count = [this, connected, count] {
        const int active = std::count_if(connected.cbegin(), connected.cend(),
                                         [](const auto* player) { return player->isChecked(); });
        count->setText(tr("Connected controllers: %1").arg(active));
    };
    sync_count();
    for (auto* state : connected) {
        connect(state, &QAbstractButton::toggled, count, sync_count);
    }
    connect(players, &Spotlight::SegmentedControl::CurrentChanged, player_pages,
            &QStackedWidget::setCurrentIndex);
    settings_page->AddBlock(category, picker, tr("Players"));
    settings_page->AddBlock(category, player_pages, tr("Controller and profile"));

    auto* controllers = settings_page->AddSection(category);
    auto* motion = input_tab->findChild<QGroupBox*>(QStringLiteral("motionGroup"));
    SettingsPage::AddRow(controllers, tr("Motion"), tr("Use motion controls"),
                         QStringLiteral("hand-pointing"), Tints::Lavender(),
                         SettingsPage::BindToggle(motion));
    auto* vibration = input_tab->findChild<QPushButton*>(QStringLiteral("vibrationButton"));
    auto* vibration_group = input_tab->findChild<QGroupBox*>(QStringLiteral("vibrationGroup"));
    auto* configure_vibration = SettingsPage::Manage(tr("Configure"));
    configure_vibration->setEnabled(vibration_group->isChecked());
    connect(vibration_group, &QGroupBox::toggled, configure_vibration, &QWidget::setEnabled);
    connect(configure_vibration, &QAbstractButton::clicked, vibration, &QPushButton::click);
    auto* vibration_controls = new QWidget;
    vibration_controls->setProperty("spotlightTransparent", true);
    auto* vibration_layout = new QHBoxLayout(vibration_controls);
    vibration_layout->setContentsMargins(0, 0, 0, 0);
    vibration_layout->setSpacing(12);
    vibration_layout->addWidget(configure_vibration);
    vibration_layout->addWidget(SettingsPage::BindToggle(vibration_group));
    SettingsPage::AddRow(controllers, tr("Vibration"), tr("Adjust rumble strength per controller"),
                         QStringLiteral("vibrate"), Tints::Gold(), vibration_controls);

    auto* docked = input_tab->findChild<QRadioButton*>(QStringLiteral("radioDocked"));
    auto* handheld = input_tab->findChild<QRadioButton*>(QStringLiteral("radioUndocked"));
    auto* mode = new Spotlight::SegmentedControl({tr("Docked"), tr("Handheld")},
                                                 Spotlight::SegmentedControl::Kind::Accent, 36);
    const auto sync_mode = [mode, docked] {
        const QSignalBlocker blocker(mode);
        mode->SetCurrentIndex(docked->isChecked() ? 0 : 1);
    };
    sync_mode();
    connect(docked, &QAbstractButton::toggled, mode, sync_mode);
    connect(mode, &Spotlight::SegmentedControl::CurrentChanged, this,
            [docked, handheld, sync_mode](int index) {
                auto* source = index == 0 ? docked : handheld;
                if (source->isEnabled()) {
                    source->click();
                }
                sync_mode();
            });
    SettingsPage::AddRow(controllers, tr("Console mode"), tr("Choose docked or handheld mode"),
                         QStringLiteral("game-controller"), Tints::Blue(), mode);
    auto* edit_hotkeys = SettingsPage::Manage();
    connect(edit_hotkeys, &QAbstractButton::clicked, settings_page,
            [this, shortcuts] { settings_page->SetCategory(shortcuts); });
    SettingsPage::AddRow(controllers, tr("Hotkeys"), tr("Edit keyboard and controller shortcuts"),
                         QStringLiteral("keyboard"), Tints::Gold(), edit_hotkeys);
    settings_page->AddPage(category, input_tab.get(), tr("Players and input settings"));
}

void ConfigureDialog::RebuildGameFolders() {
    while (auto* item = folders_layout->takeAt(0)) {
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    int custom_count = 0;
    for (int i = 0; i < game_dirs.size(); ++i) {
        const auto& folder = game_dirs[i];
        const bool built_in = IsBuiltInFolder(folder);
        custom_count += !built_in;
        const QString path = QString::fromStdString(folder.path);
        auto* menu_button = new Spotlight::CircleButton(QStringLiteral("dots-three"), 36);
        auto* menu = new QMenu(menu_button);
        const auto changed = [this] {
            game_dirs_changed = true;
            RebuildGameFolders();
        };
        menu->addAction(tr("Open location"), this,
                        [this, path] { emit OpenGameFolderRequested(path); });
        auto* up = menu->addAction(tr("Move up"), this, [this, i, changed] {
            game_dirs.swapItemsAt(i, i - 1);
            changed();
        });
        up->setEnabled(i > 0);
        auto* down = menu->addAction(tr("Move down"), this, [this, i, changed] {
            game_dirs.swapItemsAt(i, i + 1);
            changed();
        });
        down->setEnabled(i + 1 < game_dirs.size());
        if (!built_in) {
            auto* scan = menu->addAction(tr("Scan subfolders"));
            scan->setCheckable(true);
            scan->setChecked(folder.deep_scan);
            connect(scan, &QAction::toggled, this, [this, i, changed](bool enabled) {
                game_dirs[i].deep_scan = enabled;
                changed();
            });
            menu->addAction(tr("Remove"), this, [this, i, changed] {
                game_dirs.removeAt(i);
                changed();
            });
        }
        connect(menu_button, &QAbstractButton::clicked, menu, [menu, menu_button] {
            menu->popup(menu_button->mapToGlobal(QPoint(0, menu_button->height())));
        });
        const QString title = folder.path == "SDMC"       ? tr("SD card")
                              : folder.path == "UserNAND" ? tr("User NAND")
                              : folder.path == "SysNAND"  ? tr("System NAND")
                                                          : QDir(path).dirName();
        SettingsPage::AddRow(folders_layout, title, built_in ? tr("Built-in location") : path,
                             QStringLiteral("folder"), Tints::Green(), menu_button);
    }
    auto* add = new Spotlight::PillButton(tr("Add folder"), QStringLiteral("plus"));
    connect(add, &QAbstractButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Select game folder"));
        const UISettings::GameDir dir{path.toStdString(), false, true};
        if (!path.isEmpty() && !game_dirs.contains(dir)) {
            game_dirs.append(dir);
            game_dirs_changed = true;
            RebuildGameFolders();
        }
    });
    SettingsPage::AddRow(folders_layout, tr("Add a game folder"), tr("Scan a folder for games"),
                         QStringLiteral("folder-open"), Tints::Green(), add);
    folder_summary->setText(tr("Game folders added: %1").arg(custom_count));
}
