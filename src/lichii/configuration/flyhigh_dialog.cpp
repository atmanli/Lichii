// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QComboBox>
#include <QSpinBox>

#include "common/settings.h"
#include "lichii/configuration/flyhigh_dialog.h"
#include "lichii/configuration/flyhigh_panel.h"
#include "lichii/configuration/graphics_panel_widgets.h"

FlyHighDialog::FlyHighDialog(QWidget* parent) : QDialog(parent) {
    using namespace GraphicsPanel;
    auto* root = DialogLayout(this, tr("FlyHigh configuration"), 450,
                              tr("In-game menu → Frame Generation"), QStringLiteral("lightning"));

    auto* section = new QVBoxLayout;
    section->setSpacing(8);
    auto* heading = Label(tr("Frame generation"), 15, 600);
    heading->setContentsMargins(6, 0, 6, 0);
    section->addWidget(heading);
    auto* group = Group();
    auto* rows = new QVBoxLayout(group);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    section->addWidget(group);
    root->addLayout(section);

    auto* multiplier_row = Row(rows, tr("Multiplier"), tr("One generated frame per game frame."));
    auto* multiplier_title = multiplier_row->title;
    auto* multiplier_description = multiplier_row->detail;
    multiplier = new QComboBox;
    multiplier->setAccessibleName(tr("Multiplier"));
    StyleCombo(multiplier);
    for (u32 factor = Settings::MIN_FRAME_GEN_MULTIPLIER;
         factor <= Settings::MAX_FRAME_GEN_MULTIPLIER; ++factor) {
        multiplier->addItem(tr("%1×").arg(factor), factor);
    }
    multiplier->setCurrentIndex(multiplier->findData(Settings::FrameGenMultiplier()));
    multiplier_row->addWidget(multiplier);
    const auto update_description = [this, multiplier_description] {
        const int frames = multiplier->currentData().toInt() - 1;
        multiplier_description->setText(
            frames == 1 ? tr("One generated frame per game frame.")
                        : tr("%n generated frames per game frame.", nullptr, frames));
    };
    update_description();
    connect(multiplier, &QComboBox::currentIndexChanged, this, update_description);

    Divider(rows);
    auto* target_row =
        Row(rows, tr("Target frame rate"), tr("0–240 FPS. Off uses the multiplier."));
    target_rate = new QSpinBox;
    target_rate->setAccessibleName(tr("Target frame rate"));
    target_rate->setButtonSymbols(QAbstractSpinBox::NoButtons);
    target_rate->setFont(Spotlight::Font(13, 600));
    target_rate->setFixedHeight(34);
    target_rate->setMinimumWidth(62);
    target_rate->setStyleSheet(QStringLiteral("QSpinBox { %1 } QSpinBox:focus { border-color: "
                                              "#ff6b95; }")
                                   .arg(FieldStyle()));
    target_rate->setRange(0, 240);
    target_rate->setSuffix(tr(" FPS"));
    target_rate->setSpecialValueText(tr("Off"));
    target_rate->setValue(static_cast<int>(Settings::values.frame_gen_target_rate.GetValue()));
    target_rate->setToolTip(tr("Generates up to %1 frames per game frame, as many as reach this "
                               "frame rate.")
                                .arg(Settings::MAX_FRAME_GEN_MULTIPLIER - 1));
    target_row->addWidget(target_rate);
    const auto update_multiplier = [this, multiplier_title, multiplier_description] {
        const bool enabled = target_rate->value() == 0;
        multiplier->setEnabled(enabled);
        Spotlight::SetTextColor(multiplier_title, enabled ? Text() : QColor(255, 255, 255, 115));
        Spotlight::SetTextColor(multiplier_description,
                                enabled ? TextSecondary() : QColor(255, 255, 255, 71));
        target_rate->setFixedWidth(target_rate->value() == 0 ? 62 : 88);
    };
    update_multiplier();
    connect(target_rate, &QSpinBox::valueChanged, this, update_multiplier);

    Divider(rows);
    protect_hud = new Spotlight::ToggleSwitch;
    protect_hud->setAccessibleName(tr("Keep HUD still"));
    protect_hud->setChecked(Settings::values.frame_gen_flyhigh_protect_hud.GetValue());
    protect_hud->setToolTip(tr("Pixels that have not changed for several frames, such as a HUD, "
                               "are shown as they are instead of being interpolated."));
    Row(rows, tr("Keep HUD still"))->addWidget(protect_hud);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 6, 0, 0);
    footer->setSpacing(10);
    footer->addStretch();
    auto* cancel = new PillButton(tr("Cancel"), {}, PillButton::Kind::Control, 40);
    auto* use = new PillButton(tr("Use FlyHigh"), {}, PillButton::Kind::Accent, 40);
    footer->addWidget(cancel);
    footer->addWidget(use);
    root->addLayout(footer);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(use, &QAbstractButton::clicked, this, &QDialog::accept);
    resize(width(), sizeHint().height());
}

void FlyHighDialog::Apply() {
    auto& settings = Settings::values;
    if (settings.frame_gen_multiplier.GetValue() != multiplier->currentData().toUInt()) {
        settings.frame_gen_multiplier.SetValue(multiplier->currentData().toUInt());
    }
    if (settings.frame_gen_target_rate.GetValue() != static_cast<u32>(target_rate->value())) {
        settings.frame_gen_target_rate.SetValue(static_cast<u32>(target_rate->value()));
    }
    if (settings.frame_gen_flyhigh_protect_hud.GetValue() != protect_hud->isChecked()) {
        settings.frame_gen_flyhigh_protect_hud.SetValue(protect_hud->isChecked());
    }
    if (settings.frame_gen_type.GetValue() != Settings::FrameGenType::FlyHigh) {
        settings.frame_gen_type.SetValue(Settings::FrameGenType::FlyHigh);
    }
    if (!settings.frame_gen.GetValue()) {
        settings.frame_gen.SetValue(true);
    }
    OfferFlyHighModel(parentWidget());
}
