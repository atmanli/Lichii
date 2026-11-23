// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/fullscreen_hint.h"

#include <algorithm>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>

#include "lichii/spotlight/theme.h"

namespace Spotlight {
namespace {

constexpr auto OverlayFlags = Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;

Qt::WindowFlags Flags(FullscreenHint::Kind kind) {
    return kind == FullscreenHint::Kind::Shaders
               ? OverlayFlags | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
               : OverlayFlags;
}

int KeyWidth(const QString& key) {
    return QFontMetrics(Font(11, 600)).horizontalAdvance(key) + 16;
}

} // namespace

FullscreenHint::FullscreenHint(Kind kind, QWidget* parent)
    : QWidget(parent, Flags(kind)), kind(kind), timer(new QTimer(this)),
      fade(new QPropertyAnimation(this, "windowOpacity", this)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    if (kind == Kind::Controls) {
        setFixedHeight(77);
        setMouseTracking(true);
    } else {
        setFixedHeight(36);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    timer->setSingleShot(true);
    fade->setDuration(400);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    connect(timer, &QTimer::timeout, this, [this] { fade->start(); });
    connect(fade, &QPropertyAnimation::finished, this, &QWidget::hide);
}

void FullscreenHint::ShowControls(QWidget* widget, const QString& menu, const QString& controller,
                                  const QString& fullscreen) {
    menu_key = menu;
    controller_key = controller;
    exit_key = fullscreen;
    ShowAt(widget);
}

void FullscreenHint::ShowShaders(QWidget* widget, int count) {
    SetShaderCount(count);
    ShowAt(widget);
}

void FullscreenHint::SetShaderCount(int count) {
    const QString label =
        count == 1 ? tr("Building 1 shader") : tr("Building %1 shaders").arg(count);
    if (label == text) {
        return;
    }
    text = label;
    setFixedWidth(14 + 16 + 8 + QFontMetrics(Font(13, 600)).horizontalAdvance(text) + 14);
    Position();
    update();
}

void FullscreenHint::ShowAt(QWidget* widget) {
    if (anchor) {
        anchor->removeEventFilter(this);
    }
    anchor = widget;
    if (parentWidget() != anchor->window()) {
        setParent(anchor->window(), Flags(kind));
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    anchor->installEventFilter(this);
    fade->stop();
    setWindowOpacity(1);
    Position();
    show();
    timer->start(kind == Kind::Controls ? 7600 : 2600);
}

void FullscreenHint::Position() {
    if (!anchor) {
        return;
    }
    if (kind == Kind::Controls) {
        setFixedWidth(std::min(640, anchor->width() - 40));
        move(anchor->mapToGlobal(
            QPoint{(anchor->width() - width()) / 2, anchor->height() - height() - 47}));
    } else {
        move(anchor->mapToGlobal(QPoint{anchor->width() - width() - 20, 20}));
    }
}

QRect FullscreenHint::DismissRect() const {
    const int button = QFontMetrics(Font(13, 700)).horizontalAdvance(tr("Got it")) + 28;
    return {width() - 16 - button, (height() - 32) / 2, button, 32};
}

bool FullscreenHint::eventFilter(QObject* watched, QEvent* event) {
    if (watched == anchor && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        Position();
    }
    return QWidget::eventFilter(watched, event);
}

void FullscreenHint::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const qreal ratio = devicePixelRatioF();
    if (kind == Kind::Shaders) {
        painter.setBrush(Color::Hud());
        painter.drawRoundedRect(rect(), 18, 18);
        painter.drawPixmap(14, 10, IconPixmap(u"circle-notch", 16, Color::Warning(), ratio));
        painter.setFont(Font(13, 600));
        painter.setPen(Color::Text());
        painter.drawText(QRect{38, 0, width() - 52, height()}, Qt::AlignVCenter, text);
        return;
    }

    painter.setBrush(Color::HudSolid());
    painter.drawRoundedRect(rect(), 18, 18);
    painter.drawPixmap(16, (height() - 24) / 2, IconPixmap(u"keyboard", 24, Color::Text(), ratio));
    painter.setFont(Font(14, 700));
    painter.setPen(Color::Text());
    painter.drawText(QRect{52, 14, width() - 52, 19}, Qt::AlignVCenter,
                     tr("Open the in-game menu"));

    int x = 52;
    const int y = 39;
    const QColor note(0xff, 0xff, 0xff, 0xbf);
    const auto key = [&](const QString& label) {
        const QRectF cap{qreal(x), qreal(y), qreal(KeyWidth(label)), 24};
        painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 0x33), 1));
        painter.setBrush(QColor(0xff, 0xff, 0xff, 0x24));
        painter.drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), Radius::KeyCap, Radius::KeyCap);
        painter.setFont(Font(11, 600));
        painter.setPen(Color::Text());
        painter.drawText(cap, Qt::AlignCenter, label);
        x += KeyWidth(label) + 6;
    };
    const auto words = [&](const QString& label) {
        painter.setFont(Font(12));
        painter.setPen(note);
        const int advance = painter.fontMetrics().horizontalAdvance(label);
        painter.drawText(QRect{x, y, advance + 1, 24}, Qt::AlignVCenter, label);
        x += advance + 6;
    };
    if (!menu_key.isEmpty()) {
        key(menu_key);
    }
    if (!controller_key.isEmpty()) {
        if (!menu_key.isEmpty()) {
            words(tr("or"));
        }
        key(controller_key);
    }
    if (!exit_key.isEmpty()) {
        words(menu_key.isEmpty() && controller_key.isEmpty() ? tr("Leave fullscreen")
                                                             : tr("· leave fullscreen"));
        key(exit_key);
    }

    const QRect dismiss = DismissRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(dismiss_hovered ? QColor(0xf2, 0xf3, 0xf7) : Color::Text());
    painter.drawRoundedRect(dismiss, 16, 16);
    painter.setFont(Font(13, 700));
    painter.setPen(QColor(0x14, 0x15, 0x1b));
    painter.drawText(dismiss, Qt::AlignCenter, tr("Got it"));
}

void FullscreenHint::mousePressEvent(QMouseEvent* event) {
    if (DismissRect().contains(event->position().toPoint())) {
        hide();
    }
}

void FullscreenHint::mouseMoveEvent(QMouseEvent* event) {
    const bool hovered = DismissRect().contains(event->position().toPoint());
    if (hovered != dismiss_hovered) {
        dismiss_hovered = hovered;
        setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FullscreenHint::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer->stop();
    fade->stop();
}

} // namespace Spotlight
