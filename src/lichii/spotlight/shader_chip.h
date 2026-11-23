// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractButton>

class QLabel;

namespace Spotlight {

class ToggleSwitch;

/// The bottom bar's "Shaders 12" chip and its popover.
class ShaderChip final : public QAbstractButton {
    Q_OBJECT

public:
    explicit ShaderChip(QWidget* parent);
    void SetProgress(int building, int remaining);
    void SetPrecompile(bool enabled, bool checked);
    QSize sizeHint() const override;

signals:
    void AboutToOpen();
    void PrecompileChanged(bool checked);
    void ShaderSettingsRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void Open();
    QWidget* popup;
    QLabel* summary;
    ToggleSwitch* precompile;
};

} // namespace Spotlight
