// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

#include "lichii/spotlight/library_header.h"
#include "lichii/spotlight/theme.h"

namespace Spotlight {

namespace {

class SortButton final : public QAbstractButton {
public:
    explicit SortButton(QWidget* parent) : QAbstractButton(parent) {
        setFixedHeight(34);
        setFont(Font(13, 600));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

    QSize sizeHint() const override {
        return {fontMetrics().horizontalAdvance(text()) + 68, 34};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? Color::Accent() : Color::Stroke());
        painter.setBrush(underMouse() ? Color::ControlHover() : Color::Control());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 17, 17);
        painter.drawPixmap(12, 9,
                           IconPixmap(u"sort-ascending", 16, Color::Text(), devicePixelRatioF()));
        painter.drawPixmap(
            width() - 26, 9,
            IconPixmap(u"caret-down", 16, Color::TextSecondary(), devicePixelRatioF()));
        painter.setPen(Color::Text());
        painter.setFont(font());
        painter.drawText(QRect(35, 0, width() - 68, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }
};

class ViewButtons final : public QWidget {
public:
    ViewButtons(QAction* grid, QAction* list, QWidget* parent) : QWidget(parent) {
        setFixedSize(74, 34);
        setStyleSheet(QStringLiteral("background: transparent;"));
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(3, 3, 3, 3);
        row->setSpacing(2);
        const auto add = [&](QAction* action, QStringView icon_name, const QString& label) {
            auto* button = new QToolButton(this);
            button->setFixedSize(33, 28);
            button->setIconSize({15, 15});
            button->setToolTip(label);
            button->setAccessibleName(label);
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setFocusPolicy(Qt::TabFocus);
            button->setStyleSheet(
                QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 9px; "
                               "padding: 0; } "
                               "QToolButton:hover { background: rgba(255,255,255,20); } "
                               "QToolButton:checked { background: white; } "
                               "QToolButton:focus { border-color: %1; }")
                    .arg(Color::Accent().name()));
            const auto sync = [button, action, name = icon_name.toString()] {
                button->setChecked(action->isChecked());
                button->setIcon(
                    Icon(name, action->isChecked() ? Color::OnLight() : Color::Text(), 15));
            };
            sync();
            connect(action, &QAction::changed, button, sync);
            connect(button, &QToolButton::clicked, action, [action, sync] {
                action->trigger();
                sync();
            });
            row->addWidget(button);
        };
        add(grid, u"squares-four", tr("Grid view"));
        add(list, u"list", tr("List view"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Color::Stroke());
        painter.setBrush(Color::Control());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 17, 17);
    }
};

} // namespace

LibraryHeader::LibraryHeader(QAction* grid, QAction* list, QWidget* parent)
    : QWidget(parent), count(new QLabel(this)), sort(new SortButton(this)),
      sort_choices(new QActionGroup(this)) {
    setObjectName(QStringLiteral("spotlightLibraryHeader"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(40, 0, 40, 10);
    row->setSpacing(10);
    auto* label = new QLabel(tr("Library"), this);
    label->setFont(DisplayFont(20));
    label->setStyleSheet(QStringLiteral("color: white;"));
    row->addWidget(label);
    count->setFont(Font(13));
    count->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 184);"));
    row->addWidget(count);
    row->addStretch();
    SetCount(0);

    sort->setToolTip(tr("Sort library"));
    sort->setText(tr("Recently played"));
    row->addWidget(sort);

    auto* menu = new QMenu(this);
    using enum Lichii::LibrarySort;
    const std::array options{
        std::pair{Recent, tr("Recently played")},
        std::pair{Name, tr("Name")},
        std::pair{PlayTime, tr("Play time")},
        std::pair{Size, tr("Size")},
    };
    for (const auto& [key, text] : options) {
        auto* choice = menu->addAction(text);
        choice->setCheckable(true);
        choice->setChecked(key == Recent);
        sort_choices->addAction(choice);
        connect(choice, &QAction::triggered, this, [this, key] { emit SortChanged(key); });
    }
    connect(sort, &QAbstractButton::clicked, menu, [this, menu] {
        menu->popup(sort->mapToGlobal(
            QPoint(sort->width() - menu->sizeHint().width(), sort->height() + 5)));
    });
    row->addWidget(new ViewButtons(grid, list, this));
}

void LibraryHeader::SetCount(int games) {
    count->setText(games == 1 ? tr("1 game") : tr("%1 games").arg(games));
}

void LibraryHeader::SetSortLabel(const QString& column_name) {
    const auto label = column_name.isEmpty() ? tr("Recently played") : column_name;
    sort->setText(label);
    for (auto* choice : sort_choices->actions()) {
        choice->setChecked(choice->text() == label);
    }
}

} // namespace Spotlight
