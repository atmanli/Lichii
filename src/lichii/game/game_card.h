// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStyledItemDelegate>

class GameCard : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit GameCard(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    QRect CardRect(const QRect& cell) const;
    QRect PlayRect(const QRect& cell) const;
    bool HitTest(const QPoint& point, const QRect& cell) const;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize UpdateSize(int viewport_height = 0);

private:
    QSize cell_size;
    int tile_size = 132;
    int icon_size = 124;
    bool show_name = false;
};
