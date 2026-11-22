// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QListView>
#include <QPersistentModelIndex>

class GameCard;

class GameGrid : public QListView {
    Q_OBJECT

public:
    enum class Mode { Grid, Carousel };

    explicit GameGrid(QWidget* parent = nullptr, Mode mode = Mode::Grid);

    void UpdateIconSize();

    QModelIndex indexAt(const QPoint& point) const override;

signals:
    void PlayRequested(const QModelIndex& game);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    GameCard* m_gameCard = nullptr;
    QPersistentModelIndex pending_play;
    Mode mode;
};
