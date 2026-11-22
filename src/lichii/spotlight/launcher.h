// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QImage>
#include <QVariantAnimation>
#include <QWidget>

class QAction;
class GameList;
class GameListPlaceholder;
class QMainWindow;
class QMenu;
class QModelIndex;
class QStackedLayout;

namespace PlayTime {
class PlayTimeManager;
}

namespace Spotlight {

class Hero;
class LibraryHeader;
class RunningTitleBar;

struct LauncherActions {
    QAction* load_file;
    QAction* load_folder;
    QAction* install;
    QAction* settings;
    QAction* search;
    QAction* grid;
    QAction* list;
    QMenu* tools;
    QMenu* multiplayer;
    QMenu* help;
};

class Launcher final : public QWidget {
    Q_OBJECT

public:
    Launcher(QMainWindow* window, GameList* games, GameListPlaceholder* placeholder,
             RunningTitleBar* running_title, const PlayTime::PlayTimeManager& playtime,
             LauncherActions actions, QWidget* parent);
    ~Launcher() override;

    void SetLibraryAvailable(bool available);
    void SetInlineEmulation(bool running);
    void SetInlineFullscreen(bool fullscreen);

signals:
    void RescanRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void UpdateChrome();
    void UpdateSelection(const QModelIndex& game, int count);

    QMainWindow* owner;
    GameList* games;
    RunningTitleBar* running_title;
    const PlayTime::PlayTimeManager& playtime;
    Hero* hero;
    LibraryHeader* library;
    QStackedLayout* library_stack;
    QWidget* edges = nullptr;
    QAction* pin_menu;
    bool library_available = false;
    bool inline_emulation = false;
    bool inline_fullscreen = false;
    bool alt_pressed = false;
    bool temporary_menu = false;

    QImage previous_art;
    QImage art;
    qint64 art_key = 0;
    QVariantAnimation fade;
};

} // namespace Spotlight
