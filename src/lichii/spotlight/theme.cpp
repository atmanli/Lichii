// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/theme.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>

namespace Spotlight {

void LoadFonts() {
    for (const char* face : {"Lexend-Regular", "Lexend-Medium", "Lexend-SemiBold", "Lexend-Bold",
                             "CalSans-Regular"}) {
        QFontDatabase::addApplicationFont(QStringLiteral(":/spotlight/fonts/%1.ttf").arg(QLatin1String(face)));
    }
}

QFont Font(int pixel_size, int weight) {
    QFont font(QStringLiteral("Lexend"));
    font.setPixelSize(pixel_size);
    font.setWeight(QFont::Weight(weight));
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QFont DisplayFont(int pixel_size) {
    QFont font(QStringLiteral("Cal Sans"));
    font.setPixelSize(pixel_size);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QPixmap IconPixmap(QStringView name, int size, const QColor& color, qreal device_pixel_ratio) {
    const QString cache_key = QStringLiteral("spotlight-icon:%1:%2:%3:%4")
                                  .arg(name)
                                  .arg(size)
                                  .arg(color.rgba(), 0, 16)
                                  .arg(device_pixel_ratio);
    if (QPixmap cached; QPixmapCache::find(cache_key, &cached)) {
        return cached;
    }
    static QHash<QString, QImage> masks;
    const QString key = name.toString();
    auto it = masks.find(key);
    if (it == masks.end()) {
        it = masks.insert(key, QImage(QStringLiteral(":/spotlight/icons/%1.png").arg(key))
                                   .convertToFormat(QImage::Format_ARGB32_Premultiplied));
    }
    const int pixels = qMax(1, qRound(size * device_pixel_ratio));
    QImage image = it->isNull() ? QImage(pixels, pixels, QImage::Format_ARGB32_Premultiplied)
                                : it->scaled(pixels, pixels, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
    if (it->isNull()) {
        image.fill(Qt::transparent);
    }
    {
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), color);
    }
    QPixmap pixmap = QPixmap::fromImage(std::move(image));
    pixmap.setDevicePixelRatio(device_pixel_ratio);
    QPixmapCache::insert(cache_key, pixmap);
    return pixmap;
}

QIcon Icon(QStringView name, const QColor& color, int size) {
    QColor disabled = color;
    disabled.setAlphaF(color.alphaF() * 0.35f);
    QIcon icon;
    for (const qreal ratio : {1.0, 1.5, 2.0}) {
        icon.addPixmap(IconPixmap(name, size, color, ratio), QIcon::Normal);
        icon.addPixmap(IconPixmap(name, size, color, ratio), QIcon::Active);
        icon.addPixmap(IconPixmap(name, size, color, ratio), QIcon::Selected);
        icon.addPixmap(IconPixmap(name, size, disabled, ratio), QIcon::Disabled);
    }
    return icon;
}

void PaintWholeIcon(QPainter& painter, const QRectF& rect, const QPixmap& icon, qreal radius) {
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    painter.setClipPath(clip, Qt::IntersectClip);
    painter.fillRect(rect, Color::Panel());
    if (icon.isNull()) {
        const int size = qRound(rect.width() / 2);
        painter.drawPixmap(rect.center() - QPointF{size / 2.0, size / 2.0},
                           IconPixmap(u"game-controller", size, Color::TextMuted(),
                                      painter.device()->devicePixelRatioF()));
    } else {
        QSizeF size = icon.deviceIndependentSize();
        size.scale(rect.size(), Qt::KeepAspectRatio);
        painter.drawPixmap(
            QRectF{rect.center() - QPointF{size.width() / 2, size.height() / 2}, size}, icon,
            QRectF{icon.rect()});
    }
    painter.restore();
}

} // namespace Spotlight
