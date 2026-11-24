// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class QVariantAnimation;

namespace Spotlight {

/// Covers the game while its session shuts down (Paper 2CIY-1): a spinner, "Closing software…" and
/// what comes next. It blocks the window as the stop dialog did, and shows only after a short
/// delay so a quick stop never flashes it.
class StoppingOverlay final : public QWidget {
    Q_OBJECT

public:
    StoppingOverlay(QWidget* game, const QString& next, QWidget* parent);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void Follow();
    QRect Spinner() const;

    QWidget* game;
    QString next;
    QVariantAnimation* spin;
    qreal angle = 0;
};

} // namespace Spotlight
