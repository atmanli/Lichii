// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace SettingsCard {

using namespace Spotlight;

inline QLabel* Text(const QString& text, int size, int weight = 500,
                    const QColor& tint = Color::Text()) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setFont(Font(size, weight));
    SetTextColor(label, tint);
    return label;
}

inline QLabel* IconTile(const QString& name, int size, int glyph_size, int radius,
                        const QColor& glyph = Color::Text(), const QColor& fill = Color::Fill()) {
    auto* tile = new QLabel;
    tile->setFixedSize(size, size);
    tile->setAlignment(Qt::AlignCenter);
    tile->setPixmap(IconPixmap(name, glyph_size, glyph, tile->devicePixelRatioF()));
    tile->setStyleSheet(QStringLiteral("background: rgba(%1,%2,%3,%4); border-radius: %5px;")
                            .arg(fill.red())
                            .arg(fill.green())
                            .arg(fill.blue())
                            .arg(fill.alpha())
                            .arg(radius));
    return tile;
}

enum class ButtonKind { Control, Accent, Destructive, SoftDestructive };

class Button final : public Spotlight::PillButton {
public:
    Button(const QString& text, ButtonKind kind_, int height_ = 40, QString icon_ = {})
        : PillButton(text, {}, Kind::Control, height_), kind(kind_), height(height_),
          icon(std::move(icon_)) {
        setFont(Font(height == 32 ? 12 : height == 34 ? 13 : 14, 600));
        setFixedWidth(sizeHint().width());
    }

    QSize sizeHint() const override {
        const int padding = height == 32 ? 12 : height == 34 ? 13 : 18;
        return {fontMetrics().horizontalAdvance(text()) + padding * 2 + (icon.isEmpty() ? 0 : 24),
                height};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = Color::Fill();
        QColor ink = Color::Text();
        if (kind == ButtonKind::Accent || kind == ButtonKind::Destructive) {
            fill = kind == ButtonKind::Accent ? Color::Accent() : QColor(0xff, 0x5c, 0x7a);
            ink = Color::OnAccent();
        } else if (kind == ButtonKind::SoftDestructive) {
            fill = QColor(0xff, 0x8f, 0xae, 0x1f);
            ink = Color::AccentLight();
        }
        fill = Mix(fill, QColor(0xff, 0xff, 0xff, 0x66), isDown() ? 0.35 : 0.2 * hover.Value());
        if (!isEnabled()) {
            fill.setAlphaF(fill.alphaF() * 0.4f);
            ink.setAlphaF(0.45f);
        }
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(HasKeyboardFocus(this) ? QPen(Color::Text(), 1) : Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(bounds, Radius::Pill(height), Radius::Pill(height));
        QRect text_rect = rect();
        if (!icon.isEmpty()) {
            const int x = (width() - 24 - fontMetrics().horizontalAdvance(text())) / 2;
            painter.drawPixmap(x, (height - 16) / 2,
                               IconPixmap(icon, 16, ink, devicePixelRatioF()));
            text_rect.setLeft(24);
        }
        painter.setPen(ink);
        painter.setFont(font());
        painter.drawText(text_rect, Qt::AlignCenter, text());
    }

    void enterEvent(QEnterEvent*) override {
        hover.SetHovered(true);
    }

    void leaveEvent(QEvent*) override {
        hover.SetHovered(false);
    }

private:
    ButtonKind kind;
    int height;
    QString icon;
    HoverFade hover{this};
};

inline QVBoxLayout* Layout(QDialog& dialog, int width, int spacing = 20) {
    auto* layout = new QVBoxLayout(&dialog);
    const int border = 1;
    const int padding = 24 + border;
    layout->setContentsMargins(padding, padding, padding, padding);
    layout->setSpacing(spacing);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->addStrut(width - 2 * padding);
    dialog.setFixedWidth(width + dialog.contentsMargins().left() +
                         dialog.contentsMargins().right());
    return layout;
}

inline QLabel* Header(QDialog& dialog, QVBoxLayout& layout, const QString& title,
                      const QString& icon, const QString& subtitle, bool closable = true,
                      const QColor& tint = Color::Text()) {
    auto* header = new QHBoxLayout;
    header->setSpacing(14);
    header->addWidget(IconTile(icon, 44, 22, Radius::Tile, tint), 0, Qt::AlignTop);
    auto* copy = new QVBoxLayout;
    copy->setSpacing(4);
    auto* heading = Text(title, 22, 400);
    heading->setFont(DisplayFont(22));
    heading->setFixedHeight(26);
    copy->addWidget(heading);
    auto* description = Text(subtitle, 14, 400, Color::TextSecondary());
    description->setWordWrap(true);
    copy->addWidget(description);
    header->addLayout(copy, 1);
    if (closable) {
        auto* close = new CircleButton(QStringLiteral("x"), 32);
        close->setAccessibleName(QDialog::tr("Close"));
        close->setToolTip(QDialog::tr("Close"));
        close->setFocusPolicy(Qt::NoFocus);
        header->addWidget(close, 0, Qt::AlignTop);
        QObject::connect(close, &QAbstractButton::clicked, &dialog, &QDialog::reject);
    }
    layout.addLayout(header);
    dialog.setWindowTitle(title);
    return description;
}

class Message final : public Dialog {
public:
    Message(QWidget* parent, int width, const QString& heading, const QString& body,
            const QString& action, const QString& cancel = {},
            const QString& icon = QStringLiteral("warning-circle"),
            const QColor& tint = QColor(0xff, 0xd3, 0x7a))
        : Dialog(parent, Chrome::Content) {
        setWindowTitle(heading);
        auto* layout = Layout(*this, width, 18);
        auto* header = new QHBoxLayout;
        header->setSpacing(16);
        QColor fill = tint;
        fill.setAlpha(tint == Color::Accent() ? 0x24 : 0x1f);
        header->addWidget(IconTile(icon, 44, 22, 22, tint, fill), 0, Qt::AlignTop);
        auto* copy = new QVBoxLayout;
        copy->setSpacing(6);
        auto* title = Text(heading, 20, 400);
        title->setFont(DisplayFont(20));
        title->setWordWrap(true);
        copy->addWidget(title);
        auto* description = Text(body, 14, 400, Color::TextSecondary());
        description->setWordWrap(true);
        copy->addWidget(description);
        header->addLayout(copy, 1);
        layout->addLayout(header);

        auto* footer = new QHBoxLayout;
        footer->setSpacing(10);
        footer->addStretch();
        if (!cancel.isEmpty()) {
            cancel_button = new Button(cancel, ButtonKind::Control);
            footer->addWidget(cancel_button);
            connect(cancel_button, &QAbstractButton::clicked, this, &QDialog::reject);
        }
        action_button =
            new Button(action, cancel_button ? ButtonKind::Destructive : ButtonKind::Accent);
        footer->addWidget(action_button);
        layout->addLayout(footer);
        connect(action_button, &QAbstractButton::clicked, this, &QDialog::accept);
        (cancel_button ? cancel_button : action_button)->setFocus(Qt::OtherFocusReason);
        adjustSize();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
            (event->key() == Qt::Key_Left && cancel_button ? cancel_button : action_button)
                ->setFocus(Qt::TabFocusReason);
            return;
        case Qt::Key_Enter:
        case Qt::Key_Return:
            (cancel_button && cancel_button->hasFocus() ? cancel_button : action_button)->click();
            return;
        default:
            Dialog::keyPressEvent(event);
        }
    }

private:
    Button* cancel_button = nullptr;
    Button* action_button;
};

} // namespace SettingsCard
