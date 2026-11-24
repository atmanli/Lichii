// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QPainter>
#include <QPixmapCache>

#include "lichii/game/artwork_image.h"
#include "lichii/game/game_card.h"
#include "lichii/spotlight/nextendo_live.h"
#include "lichii/spotlight/theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

using namespace Spotlight;

namespace {

QPixmap SoftShadow(QSizeF size, qreal radius, int blur, const QColor& color, qreal ratio) {
    const QString key = QStringLiteral("card-shadow:%1x%2:%3:%4:%5:%6")
                            .arg(size.width())
                            .arg(size.height())
                            .arg(radius)
                            .arg(blur)
                            .arg(color.rgba(), 0, 16)
                            .arg(ratio);
    QPixmap shadow;
    if (QPixmapCache::find(key, &shadow)) {
        return shadow;
    }
    const QSize canvas = ((size + QSizeF(blur * 2, blur * 2)) * ratio).toSize();
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(QPointF(blur, blur) * ratio, size * ratio), radius * ratio,
                                radius * ratio);
    }
    const int step = std::max(1, qRound(blur * ratio / 2));
    image = image.scaled(canvas / step, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .scaled(canvas, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    shadow = QPixmap::fromImage(std::move(image));
    shadow.setDevicePixelRatio(ratio);
    QPixmapCache::insert(key, shadow);
    return shadow;
}

void PaintShadow(QPainter& painter, const QRectF& rect, qreal radius, int blur, int offset,
                 const QColor& color) {
    painter.drawPixmap(rect.topLeft() + QPointF(-blur, offset - blur),
                       SoftShadow(rect.size(), radius, blur, color,
                                  painter.device()->devicePixelRatioF()));
}

} // namespace

GameCard::GameCard(QObject* parent) : QStyledItemDelegate(parent) {}

QSize GameCard::UpdateSize(int viewport_height) {
    const int setting = int(UISettings::values.game_cover_size.GetValue());
    icon_size = setting == 128 ? 124 : setting;
    show_name = UISettings::values.show_game_name.GetValue();
    tile_size = icon_size == 0 ? 132 : icon_size + 8;
    const int height = icon_size == 0 ? 48 : tile_size;
    cell_size = {tile_size + 20, std::max(viewport_height, height + (show_name ? 22 : 0) + 16)};
    return cell_size;
}

QSize GameCard::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return cell_size;
}

QRect GameCard::CardRect(const QRect& cell) const {
    const int height = icon_size == 0 ? 48 : tile_size;
    const int content_height = height + (show_name && icon_size > 0 ? 22 : 0) + 16;
    return {cell.x() + TileOffset,
            cell.y() + TileOffset + std::max(0, (cell.height() - content_height - TileOffset) / 2),
            tile_size, height};
}

QRect GameCard::PlayRect(const QRect& cell) const {
    const QRect tile = CardRect(cell);
    const int side = std::min(34, tile.height() - 12);
    return {tile.right() - 7 - side, tile.bottom() - 7 - side, side, side};
}

QRect GameCard::OnlineBadgeRect(const QRect& tile, u64 program_id, bool favorite) const {
    const QSize size = icon_size >= 60 ? Spotlight::OnlineBadgeSize(program_id) : QSize{};
    if (size.isEmpty()) {
        return {};
    }
    return {QPoint(tile.right() - 12 - (favorite ? 22 : 0) - size.width(), tile.top() + 12), size};
}

QString GameCard::OnlineToolTip(const QPoint& point, const QRect& cell,
                                const QModelIndex& index) const {
    const auto program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    const bool favorite = program_id != 0 && UISettings::values.favorited_ids.contains(program_id);
    return OnlineBadgeRect(CardRect(cell), program_id, favorite).contains(point)
               ? Spotlight::OnlineBadgeToolTip(program_id)
               : QString{};
}

bool GameCard::HitTest(const QPoint& point, const QRect& cell) const {
    QRect target = CardRect(cell);
    if (show_name && icon_size > 0) {
        target.adjust(0, 0, 0, 22);
    }
    return target.contains(point);
}

void GameCard::paint(QPainter* painter, const QStyleOptionViewItem& option,
                     const QModelIndex& index) const {
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const bool focused = option.state & QStyle::State_HasFocus;
    const QRect tile = CardRect(option.rect);
    const qreal radius = std::min<qreal>(22, tile.height() / 4.0);
    auto light = Color::Text();
    light.setAlpha(0xf2);
    auto shade = Color::Text();
    shade.setAlpha(0x1a);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    if (selected) {
        auto glow = Color::Accent();
        glow.setAlpha(0x8c);
        PaintShadow(*painter, tile, radius, GlowBlur, 0, glow);
        painter->setPen(QPen(Color::Accent(), 3));
        painter->setBrush(light);
        painter->drawRoundedRect(QRectF(tile).adjusted(-1.5, -1.5, 1.5, 1.5), radius, radius);
    } else if (hover || focused) {
        if (hover) {
            PaintShadow(*painter, tile, radius, 30, 14, QColor(0, 0, 0, 0x8c));
        }
        painter->setPen(focused ? Color::Accent() : Qt::transparent);
        painter->setBrush(shade);
        painter->drawRoundedRect(tile, radius, radius);
    }

    const QString title = index.data(Qt::DisplayRole).toString();
    if (icon_size > 0) {
        const QRect art = tile.adjusted(4, 4, -4, -4);
        if (index.data(GameListItemPath::HasIconRole).toBool()) {
            ArtworkImage::PaintIcon(*painter, art, index.data(Qt::DecorationRole).value<QPixmap>(),
                                    Radius::Tile);
        } else {
            ArtworkImage::PaintMissingArt(*painter, art, Radius::Tile);
            if (!show_name && !hover) {
                painter->setFont(Font(12, 600));
                painter->setPen(Color::TextSecondary());
                const QRect label = art.adjusted(8, art.height() - 30, -8, -8);
                painter->drawText(label, Qt::AlignCenter,
                                  painter->fontMetrics().elidedText(title, Qt::ElideRight,
                                                                    label.width()));
            }
        }
    }

    const auto program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    const bool favorite = program_id != 0 && UISettings::values.favorited_ids.contains(program_id);
    if (favorite && icon_size > 0) {
        const QPoint corner(tile.right() - 21, tile.top() + 8);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Color::Panel());
        painter->drawEllipse(QRect(corner - QPoint(2, 2), QSize(18, 18)));
        painter->drawPixmap(corner, IconPixmap(u"star-fill", 14, Color::Accent(),
                                               painter->device()->devicePixelRatioF()));
    }
    const QRect online = OnlineBadgeRect(tile, program_id, favorite);
    PaintOnlineBadge(*painter, online, program_id);
    painter->setFont(Font(12));
    if (show_name || icon_size == 0) {
        const QRect name = icon_size > 0 ? QRect(tile.x(), tile.bottom() + 4, tile.width(), 20)
                                         : tile.adjusted(4, 0, -4, 0);
        painter->setPen(IsLichiiTheme() ? Color::TextSecondary()
                                        : option.palette.color(QPalette::Text));
        painter->drawText(name, Qt::AlignCenter,
                          painter->fontMetrics().elidedText(title, Qt::ElideRight, name.width()));
    }

    if (hover && icon_size >= 60) {
        painter->setFont(Font(12, 600));
        const int max_width =
            (online.isEmpty() ? tile.right() - 9 : online.left() - 6) - tile.x() - 30;
        const QString label = painter->fontMetrics().elidedText(title, Qt::ElideRight, max_width);
        const QRect title_pill(tile.x() + 10, tile.y() + 10,
                               painter->fontMetrics().horizontalAdvance(label) + 20, 26);
        painter->setPen(Qt::NoPen);
        painter->setBrush(light);
        painter->drawRoundedRect(title_pill, Radius::Row, Radius::Row);
        painter->setPen(Color::OnLight());
        painter->drawText(title_pill, Qt::AlignCenter, label);

        const QRect play = PlayRect(option.rect);
        PaintShadow(*painter, play, play.width() / 2.0, 12, 4, QColor(0, 0, 0, 0x59));
        painter->setPen(Qt::NoPen);
        painter->setBrush(Color::Accent());
        painter->drawEllipse(play);
        painter->drawPixmap(play.center() - QPoint(6, 6),
                            IconPixmap(u"play-fill", 14, Color::OnAccent(),
                                       painter->device()->devicePixelRatioF()));
    }
    painter->restore();
}
