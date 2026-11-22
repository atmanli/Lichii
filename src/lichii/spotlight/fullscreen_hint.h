// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QKeySequence;
class QLabel;
class QPropertyAnimation;
class QTimer;

namespace Spotlight {

class KeyCap;

class FullscreenHint final : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Controls, Shaders };
    explicit FullscreenHint(Kind kind, QWidget* parent);
    void ShowControls(QWidget* anchor, const QKeySequence& menu, const QKeySequence& fullscreen);
    void ShowShaders(QWidget* anchor, int count);
    void SetShaderCount(int count);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void hideEvent(QHideEvent* event) override;

private:
    void ShowAt(QWidget* anchor);
    void Position();

    Kind kind;
    QWidget* anchor = nullptr;
    QLabel* text = nullptr;
    QLabel* exit_text = nullptr;
    KeyCap* menu_key = nullptr;
    KeyCap* exit_key = nullptr;
    QTimer* timer;
    QPropertyAnimation* fade;
};

} // namespace Spotlight
