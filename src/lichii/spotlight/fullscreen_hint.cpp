// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/fullscreen_hint.h"

#include <algorithm>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

constexpr auto OverlayFlags = Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;

Qt::WindowFlags Flags(FullscreenHint::Kind kind) {
    return kind == FullscreenHint::Kind::Shaders
               ? OverlayFlags | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus
               : OverlayFlags;
}

} // namespace

FullscreenHint::FullscreenHint(Kind kind, QWidget* parent)
    : QWidget(parent, Flags(kind)), kind(kind), timer(new QTimer(this)),
      fade(new QPropertyAnimation(this, "windowOpacity", this)) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(8);

    const auto label = [&](QBoxLayout* row, const QString& caption, int size, int weight,
                           const QColor& ink) {
        auto* result = new QLabel(caption, this);
        result->setFont(Font(size, weight));
        auto palette = result->palette();
        palette.setColor(QPalette::WindowText, ink);
        result->setPalette(palette);
        row->addWidget(result);
        return result;
    };
    if (kind == Kind::Controls) {
        setFixedHeight(78);
        layout->setContentsMargins(16, 10, 16, 10);
        auto* description = new QVBoxLayout;
        description->setSpacing(6);
        label(description, tr("Open the in-game menu"), 14, 700, Color::Text());
        auto* keys = new QHBoxLayout;
        keys->setSpacing(6);
        menu_key = new KeyCap({}, this);
        keys->addWidget(menu_key);
        exit_text = label(keys, tr("·  Leave fullscreen"), 13, 500, Color::TextSecondary());
        exit_key = new KeyCap({}, this);
        keys->addWidget(exit_key);
        keys->addStretch();
        description->addLayout(keys);
        layout->addLayout(description);
        layout->addStretch();
        auto* dismiss = new PillButton(tr("Got it"), {}, PillButton::Kind::Light, 32, this);
        layout->addWidget(dismiss);
        connect(dismiss, &QAbstractButton::clicked, this, &QWidget::hide);
    } else {
        setFixedHeight(36);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* icon = new QLabel(this);
        icon->setPixmap(IconPixmap(u"circle-notch", 16, Color::Warning(), devicePixelRatioF()));
        layout->addWidget(icon);
        text = label(layout, {}, 13, 600, Color::Text());
    }
    timer->setSingleShot(true);
    fade->setDuration(400);
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    connect(timer, &QTimer::timeout, this, [this] { fade->start(); });
    connect(fade, &QPropertyAnimation::finished, this, &QWidget::hide);
}

void FullscreenHint::ShowControls(QWidget* widget, const QKeySequence& menu,
                                  const QKeySequence& fullscreen) {
    menu_key->setVisible(!menu.isEmpty());
    menu_key->SetText(menu.toString(QKeySequence::NativeText));
    exit_key->setVisible(!fullscreen.isEmpty());
    exit_text->setVisible(!fullscreen.isEmpty());
    exit_key->SetText(fullscreen.toString(QKeySequence::NativeText));
    ShowAt(widget);
}

void FullscreenHint::ShowShaders(QWidget* widget, int count) {
    SetShaderCount(count);
    ShowAt(widget);
}

void FullscreenHint::SetShaderCount(int count) {
    text->setText(count == 1 ? tr("Building 1 shader") : tr("Building %n shaders", "", count));
    if (isVisible()) {
        adjustSize();
        Position();
    }
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
    adjustSize();
    Position();
    show();
    timer->start(kind == Kind::Controls ? 7600 : 2600);
}

void FullscreenHint::Position() {
    if (!anchor) {
        return;
    }
    if (kind == Kind::Controls) {
        resize(std::min(640, std::max(minimumSizeHint().width(), anchor->width() - 40)), height());
    }
    const QPoint offset = kind == Kind::Controls ? QPoint{(anchor->width() - width()) / 2,
                                                          anchor->height() - height() - 46}
                                                 : QPoint{anchor->width() - width() - 20, 20};
    move(anchor->mapToGlobal(offset));
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
    painter.setBrush(kind == Kind::Controls ? Color::HudSolid() : Color::Hud());
    painter.drawRoundedRect(rect(), 18, 18);
}

void FullscreenHint::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    timer->stop();
    fade->stop();
}

} // namespace Spotlight
