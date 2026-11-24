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

    /// How far the viewport reaches up under the library header, so the selection glow of the
    /// first row is not cut off.
    static constexpr int HeaderOverlap = 36;

    void UpdateIconSize();

    QModelIndex indexAt(const QPoint& point) const override;

signals:
    void PlayRequested(const QModelIndex& game);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command) override;
    void currentChanged(const QModelIndex& current, const QModelIndex& previous) override;
    void dataChanged(const QModelIndex& top_left, const QModelIndex& bottom_right,
                     const QList<int>& roles = QList<int>()) override;

private:
    void UpdateEffects(const QModelIndex& index);
    void ClipUnderHeader();

    GameCard* m_gameCard = nullptr;
    QPersistentModelIndex pending_play;
    QPersistentModelIndex hovered;
    Mode mode;
};
