// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/game/artwork_image.h"
#include "lichii/spotlight/theme.h"

#include <algorithm>
#include <QBuffer>
#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>

namespace ArtworkImage {

QImage Decode(const QByteArray& bytes, QSize fit) {
    if (bytes.isEmpty() || bytes.size() > MaxBytes) {
        return {};
    }
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg" && format != "bmp" && format != "webp") {
        return {};
    }
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > MaxDimension || size.height() > MaxDimension ||
        qint64{size.width()} * size.height() > MaxPixels) {
        return {};
    }
    if (fit.isValid() && (size.width() > fit.width() || size.height() > fit.height())) {
        reader.setScaledSize(size.scaled(fit, Qt::KeepAspectRatio).expandedTo(QSize{1, 1}));
    }
    return reader.read();
}

QImage SquareIcon(const QImage& source) {
    if (source.isNull()) {
        return {};
    }
    const int side = std::min(source.width(), source.height());
    const int x = (source.width() - side) / 2;
    const int y = (source.height() - side) / (source.height() > source.width() ? 3 : 2);
    QImage image = source.copy(x, y, side, side).convertToFormat(QImage::Format_RGBA8888);
    if (side != IconSize) {
        image = image.scaled(IconSize, IconSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void PaintIcon(QPainter& painter, const QRectF& bounds, const QPixmap& source, qreal radius) {
    if (source.isNull() || bounds.isEmpty()) {
        return;
    }
    const QSizeF size = QSizeF(source.size()).scaled(bounds.size(), Qt::KeepAspectRatio);
    const QRectF target(bounds.center() - QPointF(size.width() / 2, size.height() / 2), size);
    const qreal ratio = painter.device()->devicePixelRatioF();
    const QSize pixels = (size * ratio).toSize().expandedTo({1, 1});
    const QString key = QStringLiteral("artwork-icon:%1:%2x%3:%4")
                            .arg(source.cacheKey())
                            .arg(pixels.width())
                            .arg(pixels.height())
                            .arg(radius * ratio);
    QPixmap rounded;
    if (!QPixmapCache::find(key, &rounded)) {
        rounded = QPixmap(pixels);
        rounded.fill(Qt::transparent);
        {
            QPainter canvas(&rounded);
            canvas.setRenderHint(QPainter::Antialiasing);
            canvas.setPen(Qt::NoPen);
            canvas.setBrush(source.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
            canvas.drawRoundedRect(QRectF(rounded.rect()), radius * ratio, radius * ratio);
        }
        rounded.setDevicePixelRatio(ratio);
        QPixmapCache::insert(key, rounded);
    }
    painter.drawPixmap(target.topLeft(), rounded);
}

void PaintMissingArt(QPainter& painter, const QRectF& bounds, qreal radius) {
    if (bounds.isEmpty()) {
        return;
    }
    using namespace Spotlight;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF tile = bounds.adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Color::Panel());
    painter.drawRoundedRect(tile, radius, radius);
    painter.setPen(QPen(Color::StrokeSubtle(), 1));
    painter.setBrush(Color::Fill());
    painter.drawRoundedRect(tile, radius, radius);
    const int glyph = std::max(12, qRound(bounds.height() * 0.3));
    painter.drawPixmap(bounds.center() - QPointF(glyph / 2.0, glyph / 2.0),
                       IconPixmap(u"game-controller", glyph, Color::TextFaint(),
                                  painter.device()->devicePixelRatioF()));
    painter.restore();
}

QIcon Icon(const QPixmap& source, int size, qreal radius) {
    if (source.isNull()) {
        return {};
    }
    QIcon icon;
    for (const qreal ratio : {1.0, 1.5, 2.0}) {
        QPixmap pixmap(qRound(size * ratio), qRound(size * ratio));
        pixmap.setDevicePixelRatio(ratio);
        pixmap.fill(Qt::transparent);
        {
            QPainter painter(&pixmap);
            PaintIcon(painter, QRectF(0, 0, size, size), source, radius);
        }
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace ArtworkImage
