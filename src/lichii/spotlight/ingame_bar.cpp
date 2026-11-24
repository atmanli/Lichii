// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/ingame_bar.h"

#include <cmath>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "lichii/spotlight/nextendo_live.h"
#include "lichii/spotlight/shader_chip.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {

class ConfigurationStatus final : public QWidget {
public:
    explicit ConfigurationStatus(QWidget* parent) : QWidget(parent) {
        setFixedHeight(26);
    }

    void SetText(QString value) {
        text = std::move(value);
        setFixedWidth(QFontMetrics(Font(12, 600)).horizontalAdvance(text) + 32);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Success());
        painter.drawEllipse(QRect{10, 9, 8, 8});
        painter.setFont(Font(12, 600));
        painter.setPen(Color::Text());
        painter.drawText(QRect{24, 0, width() - 24, height()}, Qt::AlignVCenter, text);
    }

private:
    QString text;
};

class BarChip final : public QWidget {
public:
    BarChip(QString icon, const QColor& ink, QWidget* parent)
        : QWidget(parent), icon_name(std::move(icon)), ink(ink) {
        setFixedHeight(26);
    }

    QString Text() const {
        return text;
    }

    void SetText(QString value) {
        text = std::move(value);
        setFixedWidth(10 + 16 + 6 + QFontMetrics(Font(12, 700)).horizontalAdvance(text) + 10);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(QRectF(rect()), 13, 13);
        painter.drawPixmap(10, 5, IconPixmap(icon_name, 16, ink, devicePixelRatioF()));
        painter.setFont(Font(12, 700));
        painter.setPen(ink);
        painter.drawText(QRect{32, 0, width() - 32, height()}, Qt::AlignVCenter, text);
    }

private:
    QString icon_name;
    QColor ink;
    QString text;
};

/// "60 → 120 FPS · 16.6 ms", painted so that a stats update only repaints.
class BarMetrics final : public QWidget {
public:
    explicit BarMetrics(QWidget* parent) : QWidget(parent) {
        setFixedHeight(26);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }

    void Set(QString game, QString shown, QString time, bool at_target) {
        if (game == game_text && shown == shown_text && time == time_text &&
            at_target == on_target) {
            return;
        }
        game_text = std::move(game);
        shown_text = std::move(shown);
        time_text = std::move(time);
        on_target = at_target;
        const int width = Width();
        if (width != this->width()) {
            setFixedWidth(width);
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        int x = 4;
        const auto span = [&](const QString& text, const QFont& font, const QColor& ink) {
            painter.setFont(font);
            painter.setPen(ink);
            const int advance = QFontMetrics(font).horizontalAdvance(text);
            painter.drawText(QRect{x, 0, advance + 1, height()}, Qt::AlignVCenter, text);
            x += advance + 5;
        };
        span(shown_text.isEmpty() ? InGameBar::tr("%1 FPS").arg(game_text) : game_text,
             Font(13, 700), on_target ? Color::Success() : Color::Text());
        if (!shown_text.isEmpty()) {
            span(QStringLiteral("→"), Font(12, 600), Color::TextSecondary());
            span(InGameBar::tr("%1 FPS").arg(shown_text), Font(13, 700), Color::Text());
        }
        span(QStringLiteral("·"), Font(12, 600), Color::TextSecondary());
        span(time_text, Font(13, 600), Color::TextSecondary());
    }

private:
    int Width() const {
        const QFontMetrics bold(Font(13, 700));
        const QFontMetrics small(Font(12, 600));
        int width = 8 + 5 + small.horizontalAdvance(QStringLiteral("·")) + 5 +
                    QFontMetrics(Font(13, 600)).horizontalAdvance(time_text);
        if (shown_text.isEmpty()) {
            width += bold.horizontalAdvance(InGameBar::tr("%1 FPS").arg(game_text));
        } else {
            width += bold.horizontalAdvance(game_text) + 5 +
                     small.horizontalAdvance(QStringLiteral("→")) + 5 +
                     bold.horizontalAdvance(InGameBar::tr("%1 FPS").arg(shown_text));
        }
        return width;
    }

    QString game_text;
    QString shown_text;
    QString time_text;
    bool on_target = false;
};

InGameBar::InGameBar(QWidget* parent) : QStatusBar(parent), contents(new QWidget(this)) {
    setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    setFixedHeight(Height);
    setSizeGripEnabled(false);
    auto* layout = new QHBoxLayout(contents);
    layout->setContentsMargins(6, 1, 6, 0);
    layout->setSpacing(8);

    configuration = new ConfigurationStatus(contents);
    layout->addWidget(configuration);

    shaders = new ShaderChip(contents);
    layout->addWidget(shaders);

    speed = new BarChip(QStringLiteral("lightning"), Color::Info(), contents);
    speed->hide();
    layout->addWidget(speed);

    layout->addWidget(new NextendoGameChip(this));

    notice = new QLabel(contents);
    notice->setFont(Font(12));
    notice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    notice->setTextFormat(Qt::PlainText);
    SetTextColor(notice, Color::TextSecondary());
    layout->addWidget(notice, 1);
    connect(this, &QStatusBar::messageChanged, this, [this](const QString& message) {
        notice->setToolTip(message);
        notice->setText(notice->fontMetrics().elidedText(message, Qt::ElideRight, notice->width()));
    });

    metrics = new BarMetrics(contents);
    layout->addWidget(metrics);
    layout->addSpacing(4);
}

void InGameBar::SetConfiguration(const QString& renderer, const QString& mode) {
    configuration->SetText(tr("%1 · %2").arg(renderer, mode));
}

void InGameBar::SetStats(double fps, double generated, double frame_ms, bool at_target,
                         const QString& details) {
    const bool fg = std::isfinite(fps) && std::isfinite(generated) && generated >= 0.5;
    metrics->Set(std::isfinite(fps) ? QString::number(fps, 'f', 0) : QStringLiteral("—"),
                 fg ? QString::number(fps + generated, 'f', 0) : QString{},
                 std::isfinite(frame_ms) && frame_ms > 0 ? tr("%1 ms").arg(frame_ms, 0, 'f', 1)
                                                         : QStringLiteral("—"),
                 at_target);
    metrics->setToolTip(details);
}

void InGameBar::SetSpeedStatus(const QString& description) {
    if (speed->Text() != description) {
        speed->SetText(description);
    }
    speed->setVisible(!description.isEmpty());
}

void InGameBar::SetCollapsed(bool collapsed_) {
    if (collapsed == collapsed_) {
        return;
    }
    collapsed = collapsed_;
    contents->setVisible(!collapsed);
    setFixedHeight(collapsed ? CollapsedHeight : Height + extra_height);
    setCursor(collapsed ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(collapsed ? tr("Show the bar") : QString{});
    update();
}

void InGameBar::SetExtraHeight(int extra) {
    extra_height = extra;
    if (!collapsed) {
        setFixedHeight(Height + extra_height);
    }
}

void InGameBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QColor border(0xff, 0xff, 0xff, 0x12);
    if (!collapsed) {
        painter.fillRect(rect(), Color::Panel());
        painter.fillRect(QRect{0, 0, width(), 1}, border);
        return;
    }
    painter.fillRect(rect(), Color::Window());
    painter.fillRect(QRect{0, 10, width(), 6}, Color::Panel());
    painter.fillRect(QRect{0, 10, width(), 1}, border);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF handle{(width() - 48) / 2 + 0.5, 0.5, 47, 20};
    QPainterPath tab;
    tab.addRoundedRect(handle, 6, 6);
    painter.setClipRect(QRect{0, 0, width(), 11});
    painter.setPen(QPen(border, 1));
    painter.setBrush(Color::Panel());
    painter.drawPath(tab);
}

void InGameBar::mousePressEvent(QMouseEvent* event) {
    if (collapsed && event->button() == Qt::LeftButton) {
        emit ExpandRequested();
        return;
    }
    QStatusBar::mousePressEvent(event);
}

void InGameBar::resizeEvent(QResizeEvent* event) {
    QStatusBar::resizeEvent(event);
    contents->setGeometry(rect());
    notice->setText(
        notice->fontMetrics().elidedText(currentMessage(), Qt::ElideRight, notice->width()));
}

} // namespace Spotlight
