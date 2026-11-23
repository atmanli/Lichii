// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <QAbstractButton>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QWindow>

#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/startup_prompt.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

constexpr int CardWidth = 470;
constexpr int CardRadius = 22;
constexpr int Padding = 24;
constexpr int BadgeSize = 44;
constexpr int BadgeGap = 16;
constexpr QMargins ShadowMargins{32, 20, 32, 44};
constexpr int ShadowBlur = 24;
constexpr int ShadowOffset = 14;

QColor CardColor() {
    return QColor(0x15, 0x16, 0x1e);
}

class Pill final : public QAbstractButton {
public:
    Pill(const QString& label, bool accent_, QWidget* parent)
        : QAbstractButton(parent), accent{accent_} {
        setText(label);
        setFont(Font(14, 600));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setFixedSize(sizeHint());
    }

    QSize sizeHint() const override {
        return {18 + fontMetrics().horizontalAdvance(text()) + 18, 40};
    }

protected:
    void enterEvent(QEnterEvent*) override {
        hover.SetHovered(true);
    }

    void leaveEvent(QEvent*) override {
        hover.SetHovered(false);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(HasKeyboardFocus(this) ? QPen(Color::Text(), 2) : QPen(Qt::NoPen));
        painter.setBrush(accent ? Mix(Color::Accent(), Color::AccentLight(), hover.Value())
                                : Mix(Color::Fill(), Color::ControlHover(), hover.Value()));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 19, 19);
        painter.setPen(accent ? Color::OnAccent() : Color::Text());
        painter.setFont(font());
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool accent;
    HoverFade hover{this};
};

class Check final : public QAbstractButton {
public:
    Check(const QString& label, QWidget* parent) : QAbstractButton(parent) {
        setText(label);
        setCheckable(true);
        setFont(Font(14, 500));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setFixedSize(sizeHint());
    }

    QSize sizeHint() const override {
        return {20 + 10 + fontMetrics().horizontalAdvance(text()) + 1, 20};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box(0.75, 0.75, 18.5, 18.5);
        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Color::Accent());
            painter.drawRoundedRect(QRectF(0, 0, 20, 20), Radius::KeyCap, Radius::KeyCap);
            painter.drawPixmap(3, 3,
                               IconPixmap(u"check", 14, Color::OnAccent(), devicePixelRatioF()));
        } else {
            painter.setPen(QPen(Color::Stroke(), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, Radius::KeyCap - 0.75, Radius::KeyCap - 0.75);
        }
        if (HasKeyboardFocus(this)) {
            painter.setPen(QPen(Color::Text(), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(box, Radius::KeyCap - 0.75, Radius::KeyCap - 0.75);
        }
        painter.setPen(Color::Text());
        painter.setFont(font());
        painter.drawText(QRect(30, 0, width() - 30, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
    }
};

QLabel* Text(const QString& text, const QFont& font, const QColor& color, int line_height) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setFont(font);
    label->setWordWrap(true);
    label->setFixedWidth(CardWidth - Padding * 2 - BadgeSize - BadgeGap);
    label->setMinimumHeight(line_height);
    SetTextColor(label, color);
    return label;
}

} // namespace

StartupPrompt::StartupPrompt(QWidget* parent, Tone tone, const QString& title, const QString& text)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("background: transparent;"));
    setWindowTitle(title);

    column = new QVBoxLayout(this);
    column->setContentsMargins(ShadowMargins + QMargins(Padding, Padding, Padding, Padding));
    column->setSpacing(18);
    column->setSizeConstraint(QLayout::SetFixedSize);

    const auto [icon, tint] = [tone]() -> std::pair<QStringView, QColor> {
        switch (tone) {
        case Tone::Info:
            return {u"info", Color::Info()};
        case Tone::Error:
            return {u"warning-circle", Color::AccentLight()};
        case Tone::Warning:
            break;
        }
        return {u"warning-circle", SettingsColor::Gold()};
    }();
    auto* header = new QHBoxLayout;
    header->setSpacing(BadgeGap);
    const qreal ratio = devicePixelRatioF();
    QPixmap circle(qRound(BadgeSize * ratio), qRound(BadgeSize * ratio));
    circle.setDevicePixelRatio(ratio);
    circle.fill(Qt::transparent);
    {
        QPainter painter(&circle);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        QColor fill = tint;
        fill.setAlpha(0x1f);
        painter.setBrush(fill);
        painter.drawEllipse(QRectF(0, 0, BadgeSize, BadgeSize));
        painter.drawPixmap(11, 11, IconPixmap(icon, 22, tint, ratio));
    }
    auto* badge = new QLabel(this);
    badge->setPixmap(circle);
    badge->setFixedSize(BadgeSize, BadgeSize);
    header->addWidget(badge, 0, Qt::AlignTop);
    auto* copy = new QVBoxLayout;
    copy->setSpacing(6);
    copy->addWidget(Text(title, DisplayFont(20), Color::Text(), 24));
    copy->addWidget(Text(text, Font(14, 400), Color::TextSecondary(), 20));
    header->addLayout(copy, 1);
    column->addLayout(header);

    buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    column->addLayout(buttons);
}

QAbstractButton* StartupPrompt::AddButton(const QString& label, bool accent) {
    auto* button = new Pill(label, accent, this);
    buttons->addWidget(button);
    if (accent) {
        default_button = button;
    }
    connect(button, &QAbstractButton::clicked, this, [this, button] {
        clicked = button;
        accept();
    });
    return button;
}

QAbstractButton* StartupPrompt::AddCheckBox(const QString& label) {
    auto* box = new Check(label, this);
    column->insertWidget(column->count() - 1, box);
    return box;
}

void StartupPrompt::Notify(QWidget* parent, Tone tone, const QString& title, const QString& text) {
    StartupPrompt prompt(parent, tone, title, text);
    prompt.AddButton(tr("OK"), true);
    prompt.exec();
}

void StartupPrompt::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (default_button != nullptr) {
        default_button->setFocus(Qt::OtherFocusReason);
    }
}

void StartupPrompt::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect().marginsRemoved(ShadowMargins)).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    QColor shadow(0, 0, 0);
    for (int step = ShadowBlur; step > 0; --step) {
        const qreal t = 1.0 - qreal(step) / ShadowBlur;
        shadow.setAlphaF(0.07f * float(t * t));
        painter.setBrush(shadow);
        painter.drawRoundedRect(
            card.translated(0, ShadowOffset * t).adjusted(-step, -step, step, step),
            CardRadius + step, CardRadius + step);
    }
    painter.setPen(QPen(Color::StrokeSubtle(), 1));
    painter.setBrush(CardColor());
    painter.drawRoundedRect(card, CardRadius, CardRadius);
}

void StartupPrompt::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && windowHandle() != nullptr &&
        windowHandle()->startSystemMove()) {
        return;
    }
    QDialog::mousePressEvent(event);
}

void StartupPrompt::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        auto* focused = qobject_cast<QAbstractButton*>(focusWidget());
        auto* target = focused != nullptr && !focused->isCheckable() ? focused : default_button;
        if (target != nullptr) {
            target->click();
        }
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace Spotlight
