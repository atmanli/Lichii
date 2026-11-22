// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QStatusBar>

class QAbstractButton;
class QLabel;
class QMenu;

namespace Spotlight {

class PillButton;
class ShaderChip;
class MenuTooltip;

class InGameBar final : public QStatusBar {
    Q_OBJECT

public:
    explicit InGameBar(QWidget* parent);
    void SetConfiguration(const QString& renderer, const QString& mode);
    void SetStats(double game_fps, double generated_fps, double frame_ms, bool at_target,
                  const QString& details);
    void SetSpeedStatus(const QString& description);
    void SetMenuShortcut(const QKeySequence& shortcut);
    void SetMenuOpen(bool open);
    ShaderChip* Shaders() const {
        return shaders;
    }
    void OpenQuickControls(QMenu* menu);

signals:
    void MenuRequested();
    void QuickControlsRequested();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* contents;
    QAbstractButton* configuration;
    ShaderChip* shaders;
    PillButton* speed;
    QLabel* notice;
    QLabel* metrics;
    PillButton* menu_button;
    MenuTooltip* menu_tooltip;
};

} // namespace Spotlight
