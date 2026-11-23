// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <QColor>
#include <QDialog>
#include <QList>
#include <QStringList>

class QLabel;
class QVBoxLayout;

namespace Core {
class System;
}

namespace Spotlight {

class PresetCard;

/// The Get Started wizard of Settings > General: keys, firmware, game folders, controller and
/// preset, one step each.
class GetStarted final : public QDialog {
    Q_OBJECT

public:
    struct Setup {
        std::function<QStringList()> game_folders;
        std::function<void()> add_game_folder;
        QString controller;
        QString controller_device;
        bool controller_connected;
        QList<PresetCard*> presets;
    };

    GetStarted(QWidget* parent, Core::System& system, Setup setup);

    /// Map buttons was chosen and the wizard finished: open player 1's controls next.
    bool MapButtonsRequested() const {
        return map_buttons && result() == Accepted;
    }

signals:
    /// Keys or firmware were installed.
    void ContentInstalled();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    class Step;
    class Card;
    class Button;
    class Progress;

    struct Option {
        QString icon;
        QColor icon_color;
        QString title;
        QString description;
        QString status;
        bool done;
        /// Shown dimmed with a gold status: a game is running.
        bool locked;
        std::function<void()> action;
    };

    void SetStep(int step);
    void ShowStep();
    QWidget* Options(const QList<Option>& options);
    bool StepDone(int step) const;

    Core::System& system;
    Setup setup;
    QList<Step*> steps;
    Progress* progress;
    QLabel* counter;
    QLabel* title;
    QLabel* description;
    QWidget* page = nullptr;
    QVBoxLayout* page_slot;
    QList<QWidget*> footer;
    Button* continue_button;
    QWidget* title_bar;
    QWidget* edges = nullptr;
    int current = 0;
    bool map_buttons = false;
};

} // namespace Spotlight
