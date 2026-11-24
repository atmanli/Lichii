// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QAbstractButton;

namespace Spotlight {

/// A confirmation card over the dimmed window: title, text, Cancel and an accent action pill.
/// exec() returns Accepted for the action. Enter confirms, Escape cancels and the arrow keys move
/// between the pills, so controller navigation can drive it with key events.
class ConfirmDialog final : public QDialog {
public:
    ConfirmDialog(QWidget* parent, const QString& title, const QString& text, const QString& action,
                  const QString& action_icon);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QWidget* card;
    QAbstractButton* cancel_button;
    QAbstractButton* action_button;
};

} // namespace Spotlight
