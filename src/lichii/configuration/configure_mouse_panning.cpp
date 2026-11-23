// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include <QCloseEvent>
#include <QMessageBox>

#include "common/settings.h"
#include "ui_configure_mouse_panning.h"
#include "lichii/configuration/configure_mouse_panning.h"
#include "lichii/spotlight/theme.h"

ConfigureMousePanning::ConfigureMousePanning(QWidget* parent,
                                             InputCommon::InputSubsystem* input_subsystem_,
                                             float right_stick_deadzone, float right_stick_range)
    : Spotlight::Dialog(parent), input_subsystem{input_subsystem_},
      ui(std::make_unique<Ui::ConfigureMousePanning>()) {
    ui->setupUi(this);
    SetIcon(QStringLiteral("hand-pointing"), Spotlight::Color::Warning());
    SetSubtitle(tr("Move the camera with the mouse"));
    SetConfiguration(right_stick_deadzone, right_stick_range);
    ConnectEvents();
}

ConfigureMousePanning::~ConfigureMousePanning() = default;

void ConfigureMousePanning::closeEvent(QCloseEvent* event) {
    event->accept();
}

void ConfigureMousePanning::SetConfiguration(float right_stick_deadzone, float right_stick_range) {
    ui->enable->setChecked(Settings::values.mouse_panning.GetValue());
    ui->x_sensitivity->setValue(Settings::values.mouse_panning_x_sensitivity.GetValue());
    ui->y_sensitivity->setValue(Settings::values.mouse_panning_y_sensitivity.GetValue());
    ui->deadzone_counterweight->setValue(
        Settings::values.mouse_panning_deadzone_counterweight.GetValue());
    ui->decay_strength->setValue(Settings::values.mouse_panning_decay_strength.GetValue());
    ui->min_decay->setValue(Settings::values.mouse_panning_min_decay.GetValue());

    if (right_stick_deadzone > 0.0f || right_stick_range != 1.0f) {
        const QString right_stick_deadzone_str =
            QString::fromStdString(std::to_string(static_cast<int>(right_stick_deadzone * 100.0f)));
        const QString right_stick_range_str =
            QString::fromStdString(std::to_string(static_cast<int>(right_stick_range * 100.0f)));

        ui->warning_label->setText(
            tr("Mouse panning works better with a deadzone of 0% and a range of 100%.\nCurrent "
               "values are %1% and %2% respectively.")
                .arg(right_stick_deadzone_str, right_stick_range_str));
    }

    if (Settings::values.mouse_enabled) {
        ui->warning_label->setText(
            tr("Emulated mouse is enabled. This is incompatible with mouse panning."));
    }
    ui->warning_label->setVisible(!ui->warning_label->text().isEmpty());
}

void ConfigureMousePanning::SetDefaultConfiguration() {
    ui->x_sensitivity->setValue(Settings::values.mouse_panning_x_sensitivity.GetDefault());
    ui->y_sensitivity->setValue(Settings::values.mouse_panning_y_sensitivity.GetDefault());
    ui->deadzone_counterweight->setValue(
        Settings::values.mouse_panning_deadzone_counterweight.GetDefault());
    ui->decay_strength->setValue(Settings::values.mouse_panning_decay_strength.GetDefault());
    ui->min_decay->setValue(Settings::values.mouse_panning_min_decay.GetDefault());
}

void ConfigureMousePanning::ConnectEvents() {
    const std::pair<QSlider*, QLabel*> sliders[] = {
        {ui->x_sensitivity, ui->x_sensitivity_value},
        {ui->y_sensitivity, ui->y_sensitivity_value},
        {ui->deadzone_counterweight, ui->deadzone_counterweight_value},
        {ui->decay_strength, ui->decay_strength_value},
        {ui->min_decay, ui->min_decay_value},
    };
    for (const auto& [slider, label] : sliders) {
        const auto show = [slider, label] {
            label->setText(QStringLiteral("%1%").arg(slider->value()));
        };
        connect(slider, &QSlider::valueChanged, label, show);
        show();
    }
    connect(ui->default_button, &QPushButton::clicked, this,
            &ConfigureMousePanning::SetDefaultConfiguration);
    connect(ui->button_box, &QDialogButtonBox::accepted, this,
            &ConfigureMousePanning::ApplyConfiguration);
    connect(ui->button_box, &QDialogButtonBox::rejected, this, [this] { reject(); });
}

void ConfigureMousePanning::ApplyConfiguration() {
    Settings::values.mouse_panning = ui->enable->isChecked();
    Settings::values.mouse_panning_x_sensitivity = static_cast<float>(ui->x_sensitivity->value());
    Settings::values.mouse_panning_y_sensitivity = static_cast<float>(ui->y_sensitivity->value());
    Settings::values.mouse_panning_deadzone_counterweight =
        static_cast<float>(ui->deadzone_counterweight->value());
    Settings::values.mouse_panning_decay_strength = static_cast<float>(ui->decay_strength->value());
    Settings::values.mouse_panning_min_decay = static_cast<float>(ui->min_decay->value());

    if (Settings::values.mouse_enabled && Settings::values.mouse_panning) {
        Settings::values.mouse_panning = false;
        QMessageBox::critical(
            this, tr("Emulated mouse is enabled"),
            tr("Real mouse input and mouse panning are incompatible. Please disable the "
               "emulated mouse in input advanced settings to allow mouse panning."));
        return;
    }

    accept();
}
