// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTreeView>

class QPainter;

class GameTree : public QTreeView {
    Q_OBJECT

public:
    explicit GameTree(QWidget* parent = nullptr);

    void SetModel(QAbstractItemModel* model);

    void SaveInterfaceLayout();
    void LoadInterfaceLayout();

    void UpdateColumnVisibility();

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;
};
