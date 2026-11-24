// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <tuple>

#include <QActionGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

#include "lichii/spotlight/library_header.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

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
        return {fontMetrics().horizontalAdvance(text()) + 69, 34};
    }

    void SetOpen(bool open_) {
        open = open_;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(HasKeyboardFocus(this) ? Color::Accent() : Color::Stroke());
        painter.setBrush(underMouse() || open ? Color::ControlHover()
                                              : QColor(255, 255, 255, 0x24));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 17, 17);
        const auto ratio = devicePixelRatioF();
        painter.drawPixmap(12, 10, IconPixmap(u"sort-ascending", 15, Color::Text(), ratio));
        painter.drawPixmap(
            width() - 26, 10,
            IconPixmap(open ? u"caret-up" : u"caret-down", 14, Color::TextSecondary(), ratio));
        painter.setPen(Color::Text());
        painter.setFont(font());
        painter.drawText(QRect(35, 0, width() - 69, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }

private:
    bool open = false;
};

class ViewButtons final : public QWidget {
public:
    ViewButtons(QAction* grid, QAction* list, QWidget* parent) : QWidget(parent) {
        setFixedSize(72, 34);
        setStyleSheet(QStringLiteral("background: transparent;"));
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(3, 3, 3, 3);
        row->setSpacing(2);
        const auto add = [&](QAction* action, QStringView icon_name, const QString& label) {
            auto* button = new QToolButton(this);
            button->setFixedSize(32, 28);
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
                               "QToolButton:checked { background: rgba(255,255,255,46); } "
                               "QToolButton:focus { border-color: %1; }")
                    .arg(Color::Accent().name()));
            button->setIcon(Icon(icon_name, Color::Text(), 15));
            const auto sync = [button, action] { button->setChecked(action->isChecked()); };
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
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(rect(), 12, 12);
    }
};

} // namespace

LibraryHeader::LibraryHeader(QAction* grid, QAction* list, QWidget* parent)
    : QWidget(parent), count(new QLabel(this)), sort(new SortButton(this)),
      sort_choices(new QActionGroup(this)) {
    setObjectName(QStringLiteral("spotlightLibraryHeader"));
    auto* row = new QHBoxLayout(this);
    setFixedHeight(48);
    row->setContentsMargins(40, 0, 40, 14);
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

    auto* menu = new Menu(this);
    menu->setMinimumWidth(260);
    menu->AddHeading(tr("Sort by"), Menu::Heading::Label);
    using enum Lichii::LibrarySort;
    const std::array options{
        std::tuple{Recent, tr("Recently played"), tr("Recently played")},
        std::tuple{Name, tr("Name"), tr("Name (A–Z)")},
        std::tuple{PlayTime, tr("Play time"), tr("Play time (most first)")},
        std::tuple{Size, tr("Size"), tr("Size (largest first)")},
    };
    for (const auto& [key, sort_label, text] : options) {
        auto* choice = menu->addAction(text);
        choice->setData(sort_label);
        choice->setCheckable(true);
        choice->setChecked(key == Recent);
        sort_choices->addAction(choice);
        connect(choice, &QAction::triggered, this, [this, key] { emit SortChanged(key); });
    }
    auto* sort_button = static_cast<SortButton*>(sort);
    connect(menu, &QMenu::aboutToShow, sort, [sort_button] { sort_button->SetOpen(true); });
    connect(menu, &QMenu::aboutToHide, sort, [sort_button] { sort_button->SetOpen(false); });
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
        choice->setChecked(choice->data().toString() == label);
    }
}

} // namespace Spotlight
