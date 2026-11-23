// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include "lichii/spotlight/dialog.h"

class QCheckBox;
class QLabel;
class QSlider;

namespace Ui {
class ConfigureVibration;
}

namespace Core::HID {
enum class ControllerTriggerType;
class HIDCore;
} // namespace Core::HID

class ConfigureVibration : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit ConfigureVibration(QWidget* parent, Core::HID::HIDCore& hid_core_);
    ~ConfigureVibration() override;

    void ApplyConfiguration();

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();
    void VibrateController(Core::HID::ControllerTriggerType type, std::size_t player_index);
    void StopVibrations();

    std::unique_ptr<Ui::ConfigureVibration> ui;

    static constexpr std::size_t NUM_PLAYERS = 8;

    std::array<QCheckBox*, NUM_PLAYERS> vibration_checkboxes;
    std::array<QSlider*, NUM_PLAYERS> vibration_sliders;
    std::array<QLabel*, NUM_PLAYERS> vibration_labels;

    /// Callback index to stop the controllers events
    std::array<int, NUM_PLAYERS> controller_callback_key;

    Core::HID::HIDCore& hid_core;
};
