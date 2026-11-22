// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lichii/spotlight/widgets.h"

class QLabel;

namespace Spotlight {

class ShaderChip final : public PillButton {
    Q_OBJECT

public:
    explicit ShaderChip(QWidget* parent);
    void SetProgress(int building, int remaining);
    void SetPrecompile(bool enabled, bool checked);

signals:
    void AboutToOpen();
    void PrecompileChanged(bool checked);
    void ShaderSettingsRequested();

private:
    void Open();
    QWidget* popup;
    QLabel* summary;
    ToggleSwitch* precompile;
};

} // namespace Spotlight
