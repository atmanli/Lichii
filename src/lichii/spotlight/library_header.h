// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "lichii/game/common.h"

class QAction;
class QAbstractButton;
class QActionGroup;
class QLabel;

namespace Spotlight {

class LibraryHeader final : public QWidget {
    Q_OBJECT

public:
    LibraryHeader(QAction* grid, QAction* list, QWidget* parent = nullptr);
    void SetCount(int games);
    void SetSortLabel(const QString& column_name);

signals:
    void SortChanged(Lichii::LibrarySort sort);

private:
    QLabel* count;
    QAbstractButton* sort;
    QActionGroup* sort_choices;
};

} // namespace Spotlight
