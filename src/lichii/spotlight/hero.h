// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "common/common_types.h"
#include "lichii/game/common.h"

class QLabel;

namespace Spotlight {

class CircleButton;
class NextendoGameLine;
class PillButton;

class Hero final : public QWidget {
    Q_OBJECT

public:
    explicit Hero(QWidget* parent = nullptr);
    void SetGame(const QString& name, Lichii::LibraryPlatform platform, u64 seconds,
                 u64 program_id);
    void SetEmpty(bool has_library, bool filtered);

signals:
    void PlayRequested();
    void MenuRequested(const QPoint& position);
    void AddDirectoryRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void SetTitle(const QString& text);
    void UpdateTitle();

    QLabel* platform;
    QLabel* playtime;
    NextendoGameLine* online;
    QLabel* title;
    PillButton* play;
    CircleButton* menu;
    PillButton* add;
    QString title_text;
};

} // namespace Spotlight
