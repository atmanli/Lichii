// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "lichii/game/common.h"

class QAction;
class GameListSearchField;
class QLabel;
class QHBoxLayout;
class QMenu;

namespace Spotlight {

#ifdef Q_OS_MACOS
inline constexpr bool ClientFrame = false;
#else
inline constexpr bool ClientFrame = true;
#endif

class WindowDragBar : public QWidget {
public:
    explicit WindowDragBar(QWidget* parent);
    void AddWindowControls(QHBoxLayout* row);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QPoint drag_origin;
    bool drag_pending = false;
};

class TopBar final : public WindowDragBar {
    Q_OBJECT

public:
    TopBar(GameListSearchField* search, QMenu* add, QAction* settings, QMenu* more,
           QWidget* parent = nullptr);

signals:
    void PlatformChanged(Lichii::LibraryPlatform platform);
};

class RunningTitleBar final : public WindowDragBar {
    Q_OBJECT

public:
    explicit RunningTitleBar(QWidget* parent);
    void SetGameTitle(const QString& value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* title;
};

} // namespace Spotlight
