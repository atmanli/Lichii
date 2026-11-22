// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QPainter>

#include "lichii/game/artwork_image.h"
#include "lichii/game/game_card.h"
#include "lichii/spotlight/theme.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"

using namespace Spotlight;

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
    return {cell.x() + 4, cell.y() + 4 + std::max(0, (cell.height() - content_height) / 2),
            tile_size, height};
}

QRect GameCard::PlayRect(const QRect& cell) const {
    const QRect tile = CardRect(cell);
    const int side = std::min(34, tile.height() - 12);
    return {tile.right() - 7 - side, tile.bottom() - 7 - side, side, side};
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
        painter->setPen(Qt::NoPen);
        for (int spread = 12; spread >= 4; --spread) {
            auto glow = Color::Accent();
            glow.setAlpha(4 + (12 - spread) * 2);
            painter->setBrush(glow);
            painter->drawRoundedRect(tile.adjusted(-spread, -spread, spread, spread),
                                     radius + spread, radius + spread);
        }
        painter->setPen(QPen(Color::Accent(), 3));
        painter->setBrush(light);
        painter->drawRoundedRect(QRectF(tile).adjusted(-1.5, -1.5, 1.5, 1.5), radius, radius);
    } else if (hover || focused) {
        painter->setPen(focused ? Color::Accent() : Qt::transparent);
        painter->setBrush(shade);
        painter->drawRoundedRect(tile, radius, radius);
    }

    if (icon_size > 0) {
        ArtworkImage::PaintIcon(*painter, tile.adjusted(4, 4, -4, -4),
                                index.data(Qt::DecorationRole).value<QPixmap>(), Radius::Tile);
    }

    const QString title = index.data(Qt::DisplayRole).toString();
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
        const int max_width = tile.width() - 40;
        const QString label = painter->fontMetrics().elidedText(title, Qt::ElideRight, max_width);
        const QRect title_pill(tile.x() + 10, tile.y() + 10,
                               painter->fontMetrics().horizontalAdvance(label) + 20, 26);
        painter->setPen(Qt::NoPen);
        painter->setBrush(light);
        painter->drawRoundedRect(title_pill, Radius::Row, Radius::Row);
        painter->setPen(Color::OnLight());
        painter->drawText(title_pill, Qt::AlignCenter, label);

        const QRect play = PlayRect(option.rect);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 0x59));
        painter->drawEllipse(play.translated(0, 3));
        painter->setBrush(Color::Accent());
        painter->drawEllipse(play);
        painter->drawPixmap(play.center() - QPoint(6, 6),
                            IconPixmap(u"play-fill", 14, Color::OnAccent(),
                                       painter->device()->devicePixelRatioF()));
    }
    painter->restore();
}
