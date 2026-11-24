// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QPropertyAnimation;
class QTimer;

namespace Spotlight {

/// The fullscreen overlays of Paper 1307-0: the first-fullscreen hint and the shader toast.
class FullscreenHint final : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Controls, Shaders };
    explicit FullscreenHint(Kind kind, QWidget* parent);
    void ShowControls(QWidget* anchor, const QString& menu, const QString& controller,
                      const QString& fullscreen);
    void ShowShaders(QWidget* anchor, int count);
    void SetShaderCount(int count);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent*) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void ShowAt(QWidget* anchor);
    void Position();
    QRect DismissRect() const;

    Kind kind;
    QWidget* anchor = nullptr;
    QString text;
    QString menu_key;
    QString controller_key;
    QString exit_key;
    bool dismiss_hovered = false;
    QTimer* timer;
    QPropertyAnimation* fade;
};

} // namespace Spotlight
