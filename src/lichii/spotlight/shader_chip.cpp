// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/shader_chip.h"

#include <algorithm>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

class ShaderPopover final : public QWidget {
public:
    explicit ShaderPopover(QWidget* parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoMouseReplay);
        setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
        setFocusPolicy(Qt::StrongFocus);
        setFixedWidth(340);
        FadeInOnShow(this, Motion::Hover);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Color::StrokeSubtle());
        painter.setBrush(Color::Popover());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
    }
};

class Hairline final : public QWidget {
public:
    explicit Hairline(QWidget* parent) : QWidget(parent) {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter(this).fillRect(rect(), QColor(0xff, 0xff, 0xff, 0x12));
    }
};

class SettingsRow final : public QAbstractButton {
public:
    SettingsRow(const QString& text, QWidget* parent) : QAbstractButton(parent) {
        setText(text);
        setAccessibleName(text);
        setFixedHeight(40);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus()) {
            painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
            painter.setBrush(underMouse() ? Color::FillFaint() : Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(-7.5, 0.5, 7.5, -0.5), 10, 10);
        }
        painter.setFont(Font(13));
        painter.setPen(Color::Text());
        painter.drawText(QRect{0, 0, width() - 26, height()}, Qt::AlignVCenter, text());
        painter.drawPixmap(
            width() - 16, 12,
            IconPixmap(u"caret-right", 16, Color::TextSecondary(), devicePixelRatioF()));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }
};

QLabel* Label(const QString& text, const QFont& font, const QColor& color, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(font);
    label->setWordWrap(true);
    SetTextColor(label, color);
    return label;
}

} // namespace

ShaderChip::ShaderChip(QWidget* parent) : QAbstractButton(parent), popup(new ShaderPopover(this)) {
    setFont(Font(12, 700));
    setFixedHeight(26);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);

    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    auto* heading = new QHBoxLayout;
    heading->setSpacing(8);
    auto* icon = new QLabel(popup);
    icon->setPixmap(IconPixmap(u"circle-notch", 16, Color::Warning(), devicePixelRatioF()));
    heading->addWidget(icon);
    summary = Label({}, Font(14, 700), Color::Text(), popup);
    heading->addWidget(summary, 1);
    layout->addLayout(heading);
    layout->addWidget(Label(tr("Short stutters are normal while this runs. The next launch "
                               "precaches them."),
                            Font(12, 400), Color::TextSecondary(), popup));
    layout->addWidget(new Hairline(popup));

    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    auto* label = Label(tr("Precompile All Shaders for this game"), Font(13), Color::Text(), popup);
    row->addWidget(label, 1);
    precompile = new ToggleSwitch(popup);
    precompile->setAccessibleName(label->text());
    row->addWidget(precompile);
    auto* holder = new QWidget(popup);
    holder->setFixedHeight(40);
    row->setContentsMargins(0, 0, 0, 0);
    holder->setLayout(row);
    layout->addWidget(holder);

    auto* settings = new SettingsRow(tr("Shader settings"), popup);
    layout->addWidget(settings);
    connect(settings, &QAbstractButton::clicked, this, [this] {
        popup->hide();
        emit ShaderSettingsRequested();
    });
    connect(precompile, &QAbstractButton::clicked, this, &ShaderChip::PrecompileChanged);
    connect(this, &QAbstractButton::clicked, this, &ShaderChip::Open);
    hide();
}

QSize ShaderChip::sizeHint() const {
    return {10 + 16 + 6 + fontMetrics().horizontalAdvance(text()) + 10, 26};
}

void ShaderChip::SetProgress(int building, int remaining) {
    if (remaining <= 0) {
        popup->hide();
        hide();
        return;
    }
    const QString label = tr("Shaders %1").arg(building);
    if (label != text()) {
        setText(label);
        updateGeometry();
    }
    const QString count =
        building == 1 ? tr("Building: 1 shader") : tr("Building: %1 shaders").arg(building);
    summary->setText(remaining == building ? count : tr("%1, %2 left").arg(count).arg(remaining));
    show();
}

void ShaderChip::SetPrecompile(bool enabled, bool checked) {
    precompile->setEnabled(enabled);
    precompile->setChecked(checked);
}

void ShaderChip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(hasFocus() ? QPen(Color::Warning()) : Qt::NoPen);
    painter.setBrush(underMouse() || isDown() ? Color::ControlHover() : Color::Fill());
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
    painter.drawPixmap(10, 5,
                       IconPixmap(u"circle-notch", 16, Color::Warning(), devicePixelRatioF()));
    painter.setFont(font());
    painter.setPen(Color::Warning());
    painter.drawText(QRect{32, 0, width() - 32, height()}, Qt::AlignVCenter, text());
}

void ShaderChip::enterEvent(QEnterEvent*) {
    update();
}

void ShaderChip::leaveEvent(QEvent*) {
    update();
}

void ShaderChip::Open() {
    emit AboutToOpen();
    popup->adjustSize();
    const QPoint anchor = mapToGlobal(QPoint{0, 0});
    const QRect area = screen()->availableGeometry();
    popup->move(std::clamp(anchor.x() - 16, area.left(),
                           std::max(area.left(), area.right() - popup->width() + 1)),
                std::max(area.top(), anchor.y() - popup->height() - 11));
    popup->show();
    popup->setFocus(Qt::PopupFocusReason);
}

} // namespace Spotlight
