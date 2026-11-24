// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>

class QAbstractButton;
class QHBoxLayout;
class QVBoxLayout;

namespace Spotlight {

/// A warning or question Lichii asks while it starts (Paper: Startup warnings and setup prompts):
/// a card with a tinted icon, a title, text, an optional check box and pill buttons. Enter clicks
/// the focused button or else the accent one; Escape rejects with no button clicked.
class StartupPrompt final : public QDialog {
    Q_OBJECT

public:
    enum class Tone { Warning, Info, Error };

    StartupPrompt(QWidget* parent, Tone tone, const QString& title, const QString& text);

    /// Adds a pill button after the others that closes the prompt.
    QAbstractButton* AddButton(const QString& label, bool accent);
    QAbstractButton* AddCheckBox(const QString& label);

    QAbstractButton* ClickedButton() const {
        return clicked;
    }

    /// Shows a prompt with only an OK button and waits for it.
    static void Notify(QWidget* parent, Tone tone, const QString& title, const QString& text);

protected:
    void showEvent(QShowEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QVBoxLayout* column;
    QHBoxLayout* buttons;
    QAbstractButton* default_button = nullptr;
    QAbstractButton* clicked = nullptr;
};

} // namespace Spotlight
