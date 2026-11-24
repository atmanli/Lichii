// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

#include "common/common_types.h"

class GameCard : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameCard(QObject* parent = nullptr);

    static constexpr int GlowBlur = 32;
    /// Where a tile starts in its cell, leaving the first row and column room for the glow. A tile
    /// reaches up to HitShift past its cell's right and bottom edges.
    static constexpr int TileOffset = 36;
    static constexpr int HitShift = 20;
    /// How far the glow and the hover shadow reach outside a cell.
    static constexpr int EffectMargin = 48 + TileOffset;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QRect CardRect(const QRect& cell) const;
    QRect PlayRect(const QRect& cell) const;
    bool HitTest(const QPoint& point, const QRect& cell) const;
    /// The Nextendo player pill in the tile's top-right corner; empty when it shows none.
    QRect OnlineBadgeRect(const QRect& tile, u64 program_id, bool favorite) const;
    QString OnlineToolTip(const QPoint& point, const QRect& cell, const QModelIndex& index) const;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize UpdateSize(int viewport_height = 0);

private:
    QSize cell_size;
    int tile_size = 132;
    int icon_size = 124;
    bool show_name = false;
};
