// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <QKeySequence>
#include <QPixmap>
#include <QWidget>

class QAction;
class QLabel;
class QMenu;
class QScrollArea;
class QSlider;

namespace Spotlight {

class KeyCap;
class PillButton;
class SegmentedControl;

class InGameMenu final : public QWidget {
    Q_OBJECT

public:
    struct Actions {
        QAction* pause;
        QAction* restart;
        QAction* stop;
        QAction* capture;
        QAction* fullscreen;
        QAction* settings;
        QAction* frame_gen;
        QMenu* frame_gen_options;
        QMenu* quick_controls;
        QAction* turbo;
        QAction* effects;
        QAction* decrease_volume;
        QAction* increase_volume;
    };

    explicit InGameMenu(const Actions& actions, QWidget* parent);
    void Open(QWidget* anchor, const QKeySequence& shortcut, const QPixmap& icon,
              const QString& title, const QString& play_time);
    void SetConsoleMode(bool docked);
    void SetVolume(int volume, bool muted);
    void SetGraphicsSummary(const QString& resolution, const QString& frame_gen);

signals:
    void Dismissed();
    void DockedChanged(bool docked);
    void VolumeChanged(int volume);

protected:
    void paintEvent(QPaintEvent*) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    bool IsCloseKey(const QKeyEvent& event) const;
    QAction* ActionForKey(const QKeyEvent& event) const;

    QKeySequence menu_shortcut;
    std::array<QAction*, 6> popup_actions;
    QPixmap game_icon;
    QWidget* close_hint;
    QScrollArea* scroll;
    QWidget* panel;
    QWidget* game_art;
    QLabel* title;
    QLabel* play_time;
    KeyCap* menu_key;
    PillButton* resolution_button;
    PillButton* frame_gen_button;
    SegmentedControl* console_mode;
    QSlider* volume;
    QLabel* volume_value;
};

} // namespace Spotlight
