// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

#include "lichii/game/artwork_image.h"
#include "lichii/game/game_tree.h"
#include "lichii/spotlight/theme.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"

namespace {

class LibraryRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        using namespace Spotlight;
        const auto type =
            index.siblingAtColumn(0).data(GameListItem::TypeRole).value<GameListItemType>();
        const bool name_column = index.column() == GameListModel::COLUMN_NAME;
        const bool is_game = type == GameListItemType::Game;
        const auto lines = index.data().toString().split(QLatin1Char('\n'));
        auto rect = option.rect.adjusted(6, 0, -8, 0);
        painter->save();
        painter->setClipRect(option.rect);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);

        if (name_column) {
            const QPixmap image = index.data(Qt::DecorationRole).value<QPixmap>();
            const int side = is_game ? GameIconSize() : 16;
            if (side > 0 && !image.isNull()) {
                const QRect square(rect.x(), rect.center().y() - side / 2, side, side);
                ArtworkImage::PaintIcon(*painter, square, image, is_game ? 8 : 0);
                rect.setLeft(square.right() + 13);
            }
        }

        const u64 id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
        const bool favorite =
            name_column && is_game && UISettings::values.favorited_ids.contains(id);
        painter->setFont(name_column ? Font(14, 600) : Font(13));
        const QColor native_text = option.palette.color(
            option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text);
        painter->setPen(IsLichiiTheme() ? (name_column ? Color::Text() : Color::TextSecondary())
                                        : native_text);
        QRect primary = rect;
        if (lines.size() > 1) {
            primary.setBottom(rect.center().y() + 1);
        }
        const QString label = painter->fontMetrics().elidedText(
            lines.value(0), Qt::ElideRight, std::max(0, primary.width() - (favorite ? 21 : 0)));
        QRect label_rect;
        painter->drawText(primary,
                          Qt::AlignLeft | (lines.size() > 1 ? Qt::AlignBottom : Qt::AlignVCenter),
                          label, &label_rect);
        if (favorite) {
            painter->drawPixmap(label_rect.right() + 7, label_rect.center().y() - 7,
                                IconPixmap(u"star-fill", 14, Color::Accent(),
                                           painter->device()->devicePixelRatioF()));
        }
        if (lines.size() > 1) {
            rect.setTop(primary.bottom() + 1);
            painter->setFont(Font(12));
            painter->setPen(IsLichiiTheme() ? Color::TextMuted() : native_text);
            painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop,
                              painter->fontMetrics().elidedText(lines.value(1).trimmed(),
                                                                Qt::ElideRight, rect.width()));
        }
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
        using namespace Spotlight;
        const auto type =
            index.siblingAtColumn(0).data(GameListItem::TypeRole).value<GameListItemType>();
        const bool name_column = index.column() == GameListModel::COLUMN_NAME;
        const bool game = type == GameListItemType::Game;
        const auto lines = index.data().toString().split(QLatin1Char('\n'));
        const int width = std::max(
            QFontMetrics(name_column ? Font(14, 600) : Font(13)).horizontalAdvance(lines.value(0)),
            QFontMetrics(Font(12)).horizontalAdvance(lines.value(1)));
        const int icon_width = name_column ? (game ? GameIconSize() : 16) + 12 : 0;
        return {width + icon_width + 14, game ? std::max(52, GameIconSize() + 12) : 34};
    }

private:
    static int GameIconSize() {
        const int size = int(UISettings::values.game_icon_size.GetValue());
        return size == 44 ? 40 : size;
    }
};

} // namespace

GameTree::GameTree(QWidget* parent) : QTreeView(parent) {
    setItemDelegate(new LibraryRowDelegate(this));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setContextMenuPolicy(Qt::CustomContextMenu);
    header()->setSectionsClickable(true);
    header()->setSortIndicatorShown(true);
    header()->setSectionsMovable(true);
    setObjectName(QStringLiteral("spotlightTree"));

    connect(this, &QTreeView::expanded, this, &GameTree::OnItemExpanded);
    connect(this, &QTreeView::collapsed, this, &GameTree::OnItemExpanded);
}

void GameTree::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                       const QModelIndex& index) const {
    using namespace Spotlight;
    if (!IsLichiiTheme()) {
        QTreeView::drawRow(painter, option, index);
        return;
    }
    const bool selected = selectionModel()->isSelected(index);
    const bool focus = hasFocus() && index == currentIndex().siblingAtColumn(0);
    const bool hover = option.state & QStyle::State_MouseOver;
    if (selected || hover || focus) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(focus ? Color::Accent() : Qt::transparent);
        painter->setBrush(selected ? Color::Fill() : Color::FillFaint());
        painter->drawRoundedRect(QRectF(option.rect).adjusted(2, 1, -2, -1), Radius::Row,
                                 Radius::Row);
        painter->restore();
    }
    auto row = option;
    row.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
    QTreeView::drawRow(painter, row, index);
}

void GameTree::SetModel(QAbstractItemModel* model) {
    QTreeView::setModel(model);
    LoadInterfaceLayout();
    UpdateColumnVisibility();
}

void GameTree::OnItemExpanded(const QModelIndex& item) {
    const auto type = item.data(GameListItem::TypeRole).value<GameListItemType>();
    switch (type) {
    case GameListItemType::Favorites:
        UISettings::values.favorites_expanded = isExpanded(item);
        break;
    case GameListItemType::CustomDir:
    case GameListItemType::SdmcDir:
    case GameListItemType::UserNandDir:
    case GameListItemType::SysNandDir: {
        const int dir = item.data(GameListDir::GameDirRole).toInt();
        if (dir >= 0 && dir < UISettings::values.game_dirs.size()) {
            UISettings::values.game_dirs[dir].expanded = isExpanded(item);
        }
        break;
    }
    default:
        break;
    }
}

void GameTree::SaveInterfaceLayout() {
    UISettings::values.gamelist_header_state = header()->saveState();
}

void GameTree::LoadInterfaceLayout() {
    if (!header()->restoreState(UISettings::values.gamelist_header_state)) {
        header()->resizeSection(GameListModel::COLUMN_NAME, 540);
        header()->setSortIndicator(-1, Qt::AscendingOrder);
    }
}

void GameTree::UpdateColumnVisibility() {
    setColumnHidden(GameListModel::COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    setColumnHidden(GameListModel::COLUMN_FILE_TYPE, !UISettings::values.show_types);
    setColumnHidden(GameListModel::COLUMN_SIZE, !UISettings::values.show_size);
    setColumnHidden(GameListModel::COLUMN_PLAY_TIME, !UISettings::values.show_play_time);
}
