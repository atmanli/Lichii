// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/notice.h"

#include <QPainter>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {

namespace {

constexpr int PaddingX = 14;
constexpr int PaddingY = 12;
constexpr int IconSize = 16;
constexpr int IconGap = 10;

struct Palette {
    QColor tint;
    QColor fill;
    QColor stroke;
    QColor text;
    const char16_t* icon;
};

QColor Tinted(QColor color, int alpha) {
    color.setAlpha(alpha);
    return color;
}

Palette ToneColors(Notice::Tone tone) {
    switch (tone) {
    case Notice::Tone::Success:
        return {Color::Success(), Tinted(Color::Success(), 0x14), Tinted(Color::Success(), 0x33),
                QColor(0xff, 0xff, 0xff, 0xb8), u"check-circle"};
    case Notice::Tone::Error:
        return {Color::Destructive(), Tinted(Color::Destructive(), 0x1f),
                Tinted(Color::Destructive(), 0x40), QColor(0xff, 0xc2, 0xd2), u"warning-circle"};
    case Notice::Tone::Warning:
        return {Color::Warning(), Tinted(Color::Warning(), 0x14), Tinted(Color::Warning(), 0x33),
                QColor(0xff, 0xe7, 0xb3), u"warning-circle"};
    case Notice::Tone::Neutral:
        break;
    }
    return {Color::TextSecondary(), QColor(0xff, 0xff, 0xff, 0x0a), Color::StrokeSubtle(),
            QColor(0xff, 0xff, 0xff, 0xb8), u"info"};
}

} // namespace

Notice::Notice(QWidget* parent) : QLabel(parent) {
    setWordWrap(true);
    setTextFormat(Qt::PlainText);
    setFont(Font(13, 400));
    setContentsMargins(PaddingX + IconSize + IconGap, PaddingY, PaddingX, PaddingY);
    SetTone(Tone::Neutral);
}

void Notice::SetTone(Tone tone_) {
    tone = tone_;
    SetTextColor(this, ToneColors(tone).text);
    update();
}

void Notice::paintEvent(QPaintEvent* event) {
    const auto colors = ToneColors(tone);
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(colors.stroke, 1));
        painter.setBrush(colors.fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        const int line = fontMetrics().height();
        painter.drawPixmap(PaddingX, PaddingY + (line - IconSize) / 2,
                           IconPixmap(QStringView{colors.icon}, IconSize, colors.tint,
                                      devicePixelRatioF()));
    }
    QLabel::paintEvent(event);
}

} // namespace Spotlight
