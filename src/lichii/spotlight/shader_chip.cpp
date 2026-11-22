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

namespace Spotlight {
namespace {

class ShaderPopover final : public QWidget {
public:
    explicit ShaderPopover(QWidget* parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedWidth(340);
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

class ShaderSettingsRow final : public QAbstractButton {
public:
    ShaderSettingsRow(const QString& text, QWidget* parent) : QAbstractButton(parent) {
        setText(text);
        setFont(Font(13));
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
            painter.setBrush(underMouse() ? Color::Fill() : Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        }
        painter.setFont(font());
        painter.setPen(Color::Text());
        painter.drawText(QRect{4, 0, width() - 24, height()}, Qt::AlignVCenter, text());
        painter.drawPixmap(
            width() - 18, (height() - 14) / 2,
            IconPixmap(u"caret-right", 14, Color::TextSecondary(), devicePixelRatioF()));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }
};

} // namespace

ShaderChip::ShaderChip(QWidget* parent)
    : PillButton({}, QStringLiteral("circle-notch"), Kind::Control, 26, parent),
      popup(new ShaderPopover(this)) {
    SetForegroundColor(Color::Warning());
    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    const auto add_label = [&](const QString& text, int size, int weight, const QColor& color) {
        auto* label = new QLabel(text, popup);
        label->setFont(Font(size, weight));
        auto palette = label->palette();
        palette.setColor(QPalette::WindowText, color);
        label->setPalette(palette);
        label->setWordWrap(true);
        return label;
    };
    auto* heading = new QHBoxLayout;
    auto* icon = new QLabel(popup);
    icon->setPixmap(IconPixmap(u"circle-notch", 16, Color::Warning(), devicePixelRatioF()));
    heading->addWidget(icon);
    summary = add_label({}, 14, 600, Color::Text());
    heading->addWidget(summary, 1);
    layout->addLayout(heading);
    layout->addWidget(add_label(
        tr("Shaders are built as you play. You may notice brief stutters while they finish."), 12,
        500, Color::TextSecondary()));

    auto* row = new QHBoxLayout;
    auto* label = new QLabel(tr("Precompile All Shaders for this game"), popup);
    label->setFont(Font(13, 600));
    auto palette = label->palette();
    palette.setColor(QPalette::WindowText, Color::Text());
    label->setPalette(palette);
    label->setWordWrap(true);
    precompile = new ToggleSwitch(popup);
    precompile->setAccessibleName(label->text());
    row->setSpacing(10);
    row->addWidget(label, 1);
    row->addWidget(precompile);
    layout->addLayout(row);

    auto* settings = new ShaderSettingsRow(tr("Shader settings"), popup);
    layout->addWidget(settings);
    connect(settings, &QAbstractButton::clicked, this, [this] {
        popup->hide();
        emit ShaderSettingsRequested();
    });
    connect(precompile, &QAbstractButton::clicked, this, [this](bool checked) {
        popup->hide();
        emit PrecompileChanged(checked);
    });
    connect(this, &QAbstractButton::clicked, this, &ShaderChip::Open);
    hide();
}

void ShaderChip::SetProgress(int building, int remaining) {
    if (remaining <= 0) {
        popup->hide();
        hide();
        return;
    }
    setText(tr("Shaders %1").arg(building));
    if (remaining == building) {
        summary->setText(building == 1 ? tr("Building: 1 shader")
                                       : tr("Building: %n shaders", "", building));
    } else {
        summary->setText(tr("Building: %n shaders, %1 left", "", building).arg(remaining));
    }
    show();
}

void ShaderChip::SetPrecompile(bool enabled, bool checked) {
    precompile->setEnabled(enabled);
    precompile->setChecked(checked);
}

void ShaderChip::Open() {
    if (popup->isVisible()) {
        popup->hide();
        return;
    }
    emit AboutToOpen();
    popup->adjustSize();
    const QPoint anchor = mapToGlobal(QPoint{0, 0});
    const QRect area = screen()->availableGeometry();
    popup->move(std::clamp(anchor.x(), area.left(),
                           std::max(area.left(), area.right() - popup->width() + 1)),
                std::max(area.top(), anchor.y() - popup->height() - 8));
    popup->show();
    precompile->setFocus(Qt::PopupFocusReason);
}

} // namespace Spotlight
