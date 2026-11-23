// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <utility>
#include "common/settings.h"
#include "ui_configure_touchscreen_advanced.h"
#include "lichii/configuration/configure_touchscreen_advanced.h"
#include "lichii/spotlight/theme.h"

ConfigureTouchscreenAdvanced::ConfigureTouchscreenAdvanced(QWidget* parent)
    : Spotlight::Dialog(parent), ui(std::make_unique<Ui::ConfigureTouchscreenAdvanced>()) {
    ui->setupUi(this);
    SetIcon(QStringLiteral("hand-tap"), Spotlight::Color::Info());

    connect(ui->restore_defaults_button, &QPushButton::clicked, this,
            &ConfigureTouchscreenAdvanced::RestoreDefaults);

    LoadConfiguration();

    const std::pair<QSlider*, QLabel*> sliders[] = {
        {ui->diameter_x_box, ui->diameter_x_value},
        {ui->diameter_y_box, ui->diameter_y_value},
        {ui->angle_box, ui->angle_value},
    };
    for (const auto& [slider, label] : sliders) {
        connect(slider, &QSlider::valueChanged, label, qOverload<int>(&QLabel::setNum));
        label->setNum(slider->value());
    }
}

ConfigureTouchscreenAdvanced::~ConfigureTouchscreenAdvanced() = default;

void ConfigureTouchscreenAdvanced::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    Spotlight::Dialog::changeEvent(event);
}

void ConfigureTouchscreenAdvanced::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureTouchscreenAdvanced::ApplyConfiguration() {
    Settings::values.touchscreen.diameter_x = ui->diameter_x_box->value();
    Settings::values.touchscreen.diameter_y = ui->diameter_y_box->value();
    Settings::values.touchscreen.rotation_angle = ui->angle_box->value();
}

void ConfigureTouchscreenAdvanced::LoadConfiguration() {
    ui->diameter_x_box->setValue(Settings::values.touchscreen.diameter_x);
    ui->diameter_y_box->setValue(Settings::values.touchscreen.diameter_y);
    ui->angle_box->setValue(Settings::values.touchscreen.rotation_angle);
}

void ConfigureTouchscreenAdvanced::RestoreDefaults() {
    ui->diameter_x_box->setValue(15);
    ui->diameter_y_box->setValue(15);
    ui->angle_box->setValue(0);
}
