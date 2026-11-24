// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/confirm.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {

ConfirmDialog::ConfirmDialog(QWidget* parent, const QString& title, const QString& text,
                             const QString& action, const QString& action_icon)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("background: transparent;"));
    setWindowTitle(title);

    card = new QWidget(this);
    card->setFixedWidth(420);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(0);

    auto* heading = new QLabel(title, card);
    heading->setFont(Font(18, 700));
    heading->setWordWrap(true);
    SetTextColor(heading, Color::Text());
    layout->addWidget(heading);
    layout->addSpacing(8);

    auto* body = new QLabel(text, card);
    body->setFont(Font(13));
    body->setWordWrap(true);
    SetTextColor(body, Color::TextSecondary());
    layout->addWidget(body);
    layout->addSpacing(22);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    cancel_button = new PillButton(tr("Cancel"), {}, PillButton::Kind::Control, 38, card);
    action_button = new PillButton(action, action_icon, PillButton::Kind::Accent, 38, card);
    buttons->addWidget(cancel_button);
    buttons->addWidget(action_button);
    layout->addLayout(buttons);
    connect(cancel_button, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(action_button, &QAbstractButton::clicked, this, &QDialog::accept);

    auto* center = new QVBoxLayout(this);
    center->addWidget(card, 0, Qt::AlignCenter);
}

void ConfirmDialog::showEvent(QShowEvent* event) {
    if (auto* window = parentWidget() ? parentWidget()->window() : nullptr) {
        setGeometry(window->geometry());
    }
    QDialog::showEvent(event);
    action_button->setFocus(Qt::TabFocusReason);
}

void ConfirmDialog::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0x06, 0x07, 0x0a, 0x8c));
    painter.setPen(QPen(Color::StrokeSubtle(), 1));
    painter.setBrush(Color::Popover());
    painter.drawRoundedRect(QRectF(card->geometry()).adjusted(0.5, 0.5, -0.5, -0.5), Radius::Card,
                            Radius::Card);
}

void ConfirmDialog::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left:
        cancel_button->setFocus(Qt::TabFocusReason);
        return;
    case Qt::Key_Right:
        action_button->setFocus(Qt::TabFocusReason);
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        (cancel_button->hasFocus() ? cancel_button : action_button)->click();
        return;
    default:
        QDialog::keyPressEvent(event);
    }
}

} // namespace Spotlight
