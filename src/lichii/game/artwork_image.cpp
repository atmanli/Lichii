// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/game/artwork_image.h"

#include <algorithm>
#include <QBuffer>
#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

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
    QPainterPath clip;
    clip.addRoundedRect(target, radius, radius);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setClipPath(clip, Qt::IntersectClip);
    painter.drawPixmap(target, source, source.rect());
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
