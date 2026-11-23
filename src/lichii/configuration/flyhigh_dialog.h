// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QComboBox;
class QSpinBox;
namespace Spotlight {
class ToggleSwitch;
}

class FlyHighDialog : public QDialog {
    Q_OBJECT

public:
    explicit FlyHighDialog(QWidget* parent);

    /// Writes the choices to the settings and makes FlyHigh the running frame generation.
    void Apply();

private:
    QComboBox* multiplier;
    QSpinBox* target_rate;
    Spotlight::ToggleSwitch* protect_hud;
};
