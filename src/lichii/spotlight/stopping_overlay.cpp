// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/stopping_overlay.h"

#include <QEvent>
#include <QPainter>
#include <QTimer>
#include <QVariantAnimation>

#include "lichii/spotlight/theme.h"

namespace Spotlight {

namespace {

constexpr int SpinnerSize = 44;
constexpr int Gap = 14;
constexpr int TitleHeight = 26;
constexpr int NextHeight = 17;

} // namespace

StoppingOverlay::StoppingOverlay(QWidget* game_, const QString& next_, QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), game(game_), next(next_),
      spin(new QVariantAnimation(this)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowModality(Qt::WindowModal);
    setCursor(Qt::BusyCursor);
    spin->setStartValue(0.0);
    spin->setEndValue(360.0);
    spin->setDuration(900);
    spin->setLoopCount(-1);
    connect(spin, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        angle = value.toReal();
        update(Spinner());
    });
    game->installEventFilter(this);
    parent->installEventFilter(this);
    QTimer::singleShot(150, this, [this] {
        if (parentWidget()->isVisible()) {
            Follow();
            show();
        }
    });
}

void StoppingOverlay::Follow() {
    const QWidget* area = game->isVisible() ? game : parentWidget();
    setGeometry(QRect{area->mapToGlobal(QPoint{}), area->size()});
}

bool StoppingOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (isVisible() && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        Follow();
    }
    return QWidget::eventFilter(watched, event);
}

void StoppingOverlay::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    spin->start();
}

void StoppingOverlay::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    spin->stop();
}

QRect StoppingOverlay::Spinner() const {
    const int content = SpinnerSize + Gap + TitleHeight + (next.isEmpty() ? 0 : Gap + NextHeight);
    return QRect{(width() - SpinnerSize) / 2, (height() - content) / 2, SpinnerSize, SpinnerSize};
}

void StoppingOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0x07, 0x08, 0x0b, 0xc7));

    const QRect spinner = Spinner();
    const QRectF ring = QRectF(spinner).adjusted(4, 4, -4, -4);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Color::Stroke(), 4));
    painter.drawEllipse(ring);
    painter.setPen(QPen(Color::Accent(), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring, static_cast<int>((90 - angle) * 16), -126 * 16);

    int top = spinner.bottom() + 1 + Gap;
    painter.setFont(Font(20, 600));
    painter.setPen(Color::Text());
    painter.drawText(QRect{0, top, width(), TitleHeight}, Qt::AlignCenter, tr("Closing software…"));
    if (!next.isEmpty()) {
        top += TitleHeight + Gap;
        painter.setFont(Font(13));
        painter.setPen(Color::TextSecondary());
        painter.drawText(QRect{0, top, width(), NextHeight}, Qt::AlignCenter, next);
    }
}

} // namespace Spotlight
