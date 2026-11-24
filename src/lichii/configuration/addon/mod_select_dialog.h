// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QStringList>

#include "lichii/spotlight/dialog.h"

class QCheckBox;

class ModSelectDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit ModSelectDialog(const QStringList& mods, QWidget* parent = nullptr);
    ~ModSelectDialog() override;

signals:
    void modsSelected(const QStringList& mods);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QStringList mods;
    QList<QCheckBox*> checks;
};
