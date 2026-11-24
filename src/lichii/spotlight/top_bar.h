// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "lichii/game/common.h"

class QAbstractButton;
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

/// Resize handles along the edges of a frameless window, masked to leave the inside clickable.
class FrameEdges final : public QWidget {
public:
    explicit FrameEdges(QWidget* parent);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    Qt::Edges EdgesAt(QPoint point) const;

    static constexpr int Size = 6;
};

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
    static constexpr int Height = 36;

    explicit RunningTitleBar(QWidget* parent);
    void SetGameTitle(const QString& value);
    /// Floats only the window controls over `screen` while it is shown, without a bar or title.
    void FloatOver(QWidget* screen);
    /// Adds a dropdown button after the game title that opens `menu` below it.
    void AddMenu(const QString& name, QMenu* menu);
    /// Grows the docked bar past its base height, with its row centred, to take up letterboxing.
    void SetExtraHeight(int extra);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void SetFloating(bool value);

    QLabel* title;
    QWidget* menus;
    QHBoxLayout* menu_row;
    QHBoxLayout* row;
    QWidget* floating_screen = nullptr;
    bool floating = false;
    int extra_height = 0;
};

} // namespace Spotlight
