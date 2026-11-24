// SPDX-License-Identifier: GPL-2.0-or-later

#include <QFont>
#include <QLayout>
#include "common/settings.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "ui_configure_vibration.h"
#include "lichii/configuration/configure_vibration.h"
#include "lichii/spotlight/theme.h"

namespace {
void ShowPercent(QSlider* slider, QLabel* label) {
    const auto update = [label](int value) { label->setText(QStringLiteral("%1%").arg(value)); };
    QObject::connect(slider, &QSlider::valueChanged, label, update);
    update(slider->value());
}

void LinkEnabled(QCheckBox* box, QSlider* slider, QLabel* label) {
    const auto update = [slider, label](bool on) {
        slider->setEnabled(on);
        label->setEnabled(on);
    };
    QObject::connect(box, &QCheckBox::toggled, slider, update);
    update(box->isChecked());
}
} // namespace

ConfigureVibration::ConfigureVibration(QWidget* parent, Core::HID::HIDCore& hid_core_)
    : Spotlight::Dialog(parent), ui(std::make_unique<Ui::ConfigureVibration>()), hid_core{hid_core_} {
    ui->setupUi(this);
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    SetIcon(QStringLiteral("wave-sine"), Spotlight::Color::Accent());
    for (auto* section : {ui->labelVibrationSection, ui->labelSettingsSection}) {
        QFont font = section->font();
        font.setCapitalization(QFont::AllUppercase);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        section->setFont(font);
    }

    vibration_checkboxes = {
        ui->checkBoxPlayer1, ui->checkBoxPlayer2, ui->checkBoxPlayer3, ui->checkBoxPlayer4,
        ui->checkBoxPlayer5, ui->checkBoxPlayer6, ui->checkBoxPlayer7, ui->checkBoxPlayer8,
    };

    vibration_sliders = {
        ui->sliderPlayer1, ui->sliderPlayer2, ui->sliderPlayer3, ui->sliderPlayer4,
        ui->sliderPlayer5, ui->sliderPlayer6, ui->sliderPlayer7, ui->sliderPlayer8,
    };

    vibration_labels = {
        ui->labelPlayer1, ui->labelPlayer2, ui->labelPlayer3, ui->labelPlayer4,
        ui->labelPlayer5, ui->labelPlayer6, ui->labelPlayer7, ui->labelPlayer8,
    };

    const auto& players = Settings::values.players.GetValue();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        auto controller = hid_core.GetEmulatedControllerByIndex(i);
        Core::HID::ControllerUpdateCallback engine_callback{
            .on_change = [this,
                          i](Core::HID::ControllerTriggerType type) { VibrateController(type, i); },
            .is_npad_service = false,
        };
        controller_callback_key[i] = controller->SetCallback(engine_callback);
        vibration_checkboxes[i]->setChecked(players[i].vibration_enabled);
        vibration_sliders[i]->setValue(players[i].vibration_strength);
        ShowPercent(vibration_sliders[i], vibration_labels[i]);
        LinkEnabled(vibration_checkboxes[i], vibration_sliders[i], vibration_labels[i]);
    }

    ui->checkBoxAccurateVibration->setChecked(
        Settings::values.enable_accurate_vibrations.GetValue());
    ui->checkBoxDualSenseHaptics->setChecked(Settings::values.enable_dualsense_haptics.GetValue());
    ui->checkBoxFrequencyRumble->setChecked(Settings::values.rumble_frequency_mapping.GetValue());
    // (0 in the setting is off: the box unticked, the field keeps its own default)
    const u8 trigger_rumble = Settings::values.trigger_rumble_strength.GetValue();
    ui->checkBoxTriggerRumble->setChecked(trigger_rumble > 0);
    if (trigger_rumble > 0) {
        ui->sliderTriggerRumble->setValue(trigger_rumble);
    }
    ShowPercent(ui->sliderTriggerRumble, ui->labelTriggerRumble);
    LinkEnabled(ui->checkBoxTriggerRumble, ui->sliderTriggerRumble, ui->labelTriggerRumble);

    if (!Settings::IsConfiguringGlobal()) {
        ui->checkBoxAccurateVibration->setDisabled(true);
        ui->checkBoxDualSenseHaptics->setDisabled(true);
        ui->checkBoxFrequencyRumble->setDisabled(true);
        ui->checkBoxTriggerRumble->setDisabled(true);
        ui->sliderTriggerRumble->setDisabled(true);
        ui->labelTriggerRumble->setDisabled(true);
    }

    RetranslateUI();
}

ConfigureVibration::~ConfigureVibration() {
    StopVibrations();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        auto controller = hid_core.GetEmulatedControllerByIndex(i);
        controller->DeleteCallback(controller_callback_key[i]);
    }
};

void ConfigureVibration::ApplyConfiguration() {
    auto& players = Settings::values.players.GetValue();

    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        players[i].vibration_enabled = vibration_checkboxes[i]->isChecked();
        players[i].vibration_strength = vibration_sliders[i]->value();
    }

    Settings::values.enable_accurate_vibrations.SetValue(
        ui->checkBoxAccurateVibration->isChecked());
    Settings::values.enable_dualsense_haptics.SetValue(ui->checkBoxDualSenseHaptics->isChecked());
    Settings::values.rumble_frequency_mapping.SetValue(ui->checkBoxFrequencyRumble->isChecked());
    Settings::values.trigger_rumble_strength.SetValue(
        ui->checkBoxTriggerRumble->isChecked() ? static_cast<u8>(ui->sliderTriggerRumble->value())
                                               : u8{0});
}

void ConfigureVibration::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    Spotlight::Dialog::changeEvent(event);
}

void ConfigureVibration::RetranslateUI() {
    ui->retranslateUi(this);
    SetSubtitle(tr("Press any controller button to vibrate the controller."));
}

void ConfigureVibration::VibrateController(Core::HID::ControllerTriggerType type,
                                           std::size_t player_index) {
    if (type != Core::HID::ControllerTriggerType::Button) {
        return;
    }

    auto& player = Settings::values.players.GetValue()[player_index];
    auto controller = hid_core.GetEmulatedControllerByIndex(player_index);
    const int vibration_strength = vibration_sliders[player_index]->value();
    const auto& buttons = controller->GetButtonsValues();

    bool button_is_pressed = false;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i].value) {
            button_is_pressed = true;
            break;
        }
    }

    if (!button_is_pressed) {
        StopVibrations();
        return;
    }

    const bool old_vibration_enabled = player.vibration_enabled;
    const int old_vibration_strength = player.vibration_strength;
    // (the trigger rumble shown in the dialog, not yet applied)
    const u8 old_trigger_rumble = Settings::values.trigger_rumble_strength.GetValue();
    player.vibration_enabled = true;
    player.vibration_strength = vibration_strength;
    Settings::values.trigger_rumble_strength.SetValue(
        ui->checkBoxTriggerRumble->isChecked() ? static_cast<u8>(ui->sliderTriggerRumble->value())
                                               : u8{0});

    const Core::HID::VibrationValue vibration{
        .low_amplitude = 1.0f,
        .low_frequency = 160.0f,
        .high_amplitude = 1.0f,
        .high_frequency = 320.0f,
    };
    controller->SetVibration(Core::HID::DeviceIndex::Left, vibration);
    controller->SetVibration(Core::HID::DeviceIndex::Right, vibration);

    // Restore previous values
    player.vibration_enabled = old_vibration_enabled;
    player.vibration_strength = old_vibration_strength;
    Settings::values.trigger_rumble_strength.SetValue(old_trigger_rumble);
}

void ConfigureVibration::StopVibrations() {
    for (std::size_t i = 0; i < NUM_PLAYERS; ++i) {
        auto controller = hid_core.GetEmulatedControllerByIndex(i);
        controller->SetVibration(Core::HID::DeviceIndex::Left, Core::HID::DEFAULT_VIBRATION_VALUE);
        controller->SetVibration(Core::HID::DeviceIndex::Right, Core::HID::DEFAULT_VIBRATION_VALUE);
    }
}
