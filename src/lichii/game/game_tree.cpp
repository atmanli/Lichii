// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>

#include "lichii/game/artwork_image.h"
#include "lichii/game/game_tree.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/nextendo_live.h"
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
            const bool missing = is_game && !index.data(GameListItemPath::HasIconRole).toBool();
            if (side > 0 && (missing || !image.isNull())) {
                const QRect square(rect.x(), rect.center().y() - side / 2, side, side);
                if (missing) {
                    ArtworkImage::PaintMissingArt(*painter, square, 8);
                } else {
                    ArtworkImage::PaintIcon(*painter, square, image, is_game ? 8 : 0);
                }
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
        const QSize online = name_column && is_game ? Spotlight::OnlineBadgeSize(id) : QSize{};
        const int extra = (favorite ? 21 : 0) + (online.isEmpty() ? 0 : online.width() + 10);
        const QString label = painter->fontMetrics().elidedText(
            lines.value(0), Qt::ElideRight, std::max(0, primary.width() - extra));
        QRect label_rect;
        painter->drawText(primary,
                          Qt::AlignLeft | (lines.size() > 1 ? Qt::AlignBottom : Qt::AlignVCenter),
                          label, &label_rect);
        if (favorite) {
            painter->drawPixmap(label_rect.right() + 7, label_rect.center().y() - 7,
                                IconPixmap(u"star-fill", 14, Color::Accent(),
                                           painter->device()->devicePixelRatioF()));
        }
        if (!online.isEmpty()) {
            const QPoint corner(rect.right() - online.width(),
                                rect.center().y() - online.height() / 2);
            PaintOnlineBadge(*painter, QRect(corner, online), id);
        }
        if (lines.size() > 1) {
            rect.setTop(primary.bottom() + 1);
            painter->setFont(Font(12));
            painter->setPen(IsLichiiTheme() ? Color::TextMuted() : native_text);
            painter->drawText(rect, Qt::AlignLeft | Qt::AlignTop,
                              painter->fontMetrics().elidedText(
                                  lines.value(1).trimmed(), Qt::ElideRight,
                                  rect.width() - (online.isEmpty() ? 0 : online.width() + 10)));
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

class LibraryTreeHeader final : public QHeaderView {
public:
    explicit LibraryTreeHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {}

protected:
    QSize sizeHint() const override {
        return {QHeaderView::sizeHint().width(), 36};
    }

    void paintSection(QPainter* painter, const QRect& rect, int section) const override {
        using namespace Spotlight;
        if (!IsLichiiTheme()) {
            QHeaderView::paintSection(painter, rect, section);
            return;
        }
        painter->save();
        painter->setPen(QColor(255, 255, 255, 0x12));
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        const bool sorted = isSortIndicatorShown() && sortIndicatorSection() == section;
        painter->setFont(Font(12, 600));
        painter->setPen(sorted ? Color::Text() : Color::TextMuted());
        const QRect text = rect.adjusted(6, 0, -4, 0);
        const QString label = painter->fontMetrics().elidedText(
            model()->headerData(section, orientation()).toString(), Qt::ElideRight,
            text.width() - (sorted ? 16 : 0));
        painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft, label);
        if (sorted) {
            painter->drawPixmap(text.left() + painter->fontMetrics().horizontalAdvance(label) + 4,
                                rect.center().y() - 5,
                                IconPixmap(sortIndicatorOrder() == Qt::DescendingOrder
                                               ? u"caret-down"
                                               : u"caret-up",
                                           12, Color::Text(), devicePixelRatioF()));
        }
        painter->restore();
    }
};

} // namespace

GameTree::GameTree(QWidget* parent) : QTreeView(parent) {
    setHeader(new LibraryTreeHeader(this));
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
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QHeaderView::customContextMenuRequested, this, [this](const QPoint& point) {
        Spotlight::Menu menu(this);
        menu.setMinimumWidth(220);
        menu.AddHeading(tr("Columns"), Spotlight::Menu::Heading::Caps);
        const auto add = [&](int column, Settings::Setting<bool>& setting) {
            auto* action = menu.addAction(model()->headerData(column, Qt::Horizontal).toString());
            action->setCheckable(true);
            action->setChecked(setting.GetValue());
            connect(action, &QAction::toggled, this, [this, &setting](bool shown) {
                setting.SetValue(shown);
                UpdateColumnVisibility();
            });
        };
        add(GameListModel::COLUMN_ADD_ONS, UISettings::values.show_add_ons);
        add(GameListModel::COLUMN_FILE_TYPE, UISettings::values.show_types);
        add(GameListModel::COLUMN_SIZE, UISettings::values.show_size);
        add(GameListModel::COLUMN_PLAY_TIME, UISettings::values.show_play_time);
        menu.exec(header()->mapToGlobal(point));
    });
    setObjectName(QStringLiteral("spotlightTree"));
    setRootIsDecorated(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(&Spotlight::NextendoLive::Instance(), &Spotlight::NextendoLive::Changed, viewport(),
            qOverload<>(&QWidget::update));
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

void GameTree::SaveInterfaceLayout() {
    UISettings::values.gamelist_header_state = header()->saveState();
}

void GameTree::LoadInterfaceLayout() {
    if (!header()->restoreState(UISettings::values.gamelist_header_state)) {
        header()->resizeSection(GameListModel::COLUMN_NAME, 540);
        header()->setSortIndicator(-1, Qt::AscendingOrder);
    }
    header()->setSectionResizeMode(GameListModel::COLUMN_NAME, QHeaderView::Stretch);
}

void GameTree::UpdateColumnVisibility() {
    setColumnHidden(GameListModel::COLUMN_ADD_ONS, !UISettings::values.show_add_ons);
    setColumnHidden(GameListModel::COLUMN_FILE_TYPE, !UISettings::values.show_types);
    setColumnHidden(GameListModel::COLUMN_SIZE, !UISettings::values.show_size);
    setColumnHidden(GameListModel::COLUMN_PLAY_TIME, !UISettings::values.show_play_time);
}
