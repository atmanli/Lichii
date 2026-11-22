// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#include "lichii/game/search_field.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {

namespace {

void ToggleMaximized(QWidget* window) {
    if (window->isMaximized()) {
        window->showNormal();
    } else {
        window->showMaximized();
    }
}

class WindowButton final : public QAbstractButton {
public:
    enum class Kind { Minimize, Maximize, Close };

    WindowButton(Kind kind_, QWidget* parent) : QAbstractButton(parent), kind(kind_) {
        setFixedSize(20, 28);
        setFocusPolicy(Qt::TabFocus);
        setMouseTracking(true);
        connect(this, &QAbstractButton::clicked, this, [this] {
            switch (kind) {
            case Kind::Minimize:
                window()->showMinimized();
                break;
            case Kind::Maximize:
                ToggleMaximized(window());
                break;
            case Kind::Close:
                window()->close();
                break;
            }
        });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hover = underMouse();
        auto ink = Color::Text();
        ink.setAlphaF(hover ? 1.0f : 0.72f);
        if (hover || hasFocus()) {
            painter.setPen(hasFocus() ? Color::Accent() : Qt::transparent);
            painter.setBrush(hover && kind == Kind::Close ? QColor(0xe5, 0x48, 0x4d)
                                                          : Color::Fill());
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        }
        const auto icon = kind == Kind::Minimize    ? u"minus"
                          : kind == Kind::Close     ? u"x"
                          : window()->isMaximized() ? u"copy"
                                                    : u"square";
        const int size = kind == Kind::Maximize ? 14 : 16;
        painter.drawPixmap((width() - size) / 2, (height() - size) / 2,
                           IconPixmap(icon, size, ink, devicePixelRatioF()));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }

private:
    Kind kind;
};

} // namespace

WindowDragBar::WindowDragBar(QWidget* parent) : QWidget(parent) {}

void WindowDragBar::AddWindowControls(QHBoxLayout* row) {
    if constexpr (!ClientFrame) {
        return;
    }
    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(10, 0, 0, 0);
    controls->setSpacing(9);
    using enum WindowButton::Kind;
    const auto add = [&](WindowButton::Kind kind, const QString& description) {
        auto* button = new WindowButton(kind, this);
        button->setToolTip(description);
        button->setAccessibleName(description);
        controls->addWidget(button);
    };
    add(Minimize, tr("Minimize window"));
    add(Maximize, tr("Maximize or restore window"));
    add(Close, tr("Close window"));
    row->addLayout(controls);
}

void WindowDragBar::mousePressEvent(QMouseEvent* event) {
    if (ClientFrame && event->button() == Qt::LeftButton) {
        drag_origin = event->position().toPoint();
        drag_pending = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WindowDragBar::mouseMoveEvent(QMouseEvent* event) {
    if (drag_pending && (event->buttons() & Qt::LeftButton) &&
        (event->position().toPoint() - drag_origin).manhattanLength() >=
            QApplication::startDragDistance()) {
        drag_pending = false;
        if (window()->windowHandle() && window()->windowHandle()->startSystemMove()) {
            event->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void WindowDragBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_pending = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void WindowDragBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (ClientFrame && event->button() == Qt::LeftButton) {
        drag_pending = false;
        ToggleMaximized(window());
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

TopBar::TopBar(GameListSearchField* search, QMenu* add, QAction* settings, QMenu* more,
               QWidget* parent)
    : WindowDragBar(parent) {
    setObjectName(QStringLiteral("spotlightTopBar"));
    setFixedHeight(64);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(22, 0, 20, 0);
    row->setSpacing(10);

    auto* logo = new QLabel(this);
    logo->setPixmap(
        QIcon(QStringLiteral(":/img/lichii.ico")).pixmap({26, 26}, devicePixelRatioF()));
    logo->setFixedSize(26, 26);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(logo);

    auto* wordmark = new QLabel(tr("Lichii"), this);
    wordmark->setFont(DisplayFont(18));
    wordmark->setStyleSheet(QStringLiteral("color: white;"));
    wordmark->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(wordmark);
    row->addSpacing(21);

    auto* platforms = new SegmentedControl({tr("All"), tr("Switch 2"), tr("Switch 1")},
                                           SegmentedControl::Kind::Light, 40, this);
    platforms->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    platforms->setAccessibleName(tr("Filter by console"));
    connect(platforms, &SegmentedControl::CurrentChanged, this, [this](int index) {
        emit PlatformChanged(static_cast<Lichii::LibraryPlatform>(index));
    });
    row->addWidget(platforms);
    row->addStretch();
    row->addWidget(search);

    const auto add_button = [&](QString icon, const QString& description) {
        auto* button = new CircleButton(std::move(icon), 38, this);
        button->setToolTip(description);
        button->setAccessibleName(description);
        row->addWidget(button);
        return button;
    };
    const auto link_menu = [this](CircleButton* button, QMenu* menu) {
        connect(button, &QAbstractButton::clicked, this, [button, menu] {
            menu->popup(button->mapToGlobal(
                QPoint(button->width() - menu->sizeHint().width(), button->height() + 5)));
        });
    };
    link_menu(add_button(QStringLiteral("plus"), tr("Add or load games")), add);
    auto* configure = add_button(QStringLiteral("gear-six"), tr("Settings"));
    configure->setEnabled(settings->isEnabled());
    connect(settings, &QAction::changed, configure,
            [configure, settings] { configure->setEnabled(settings->isEnabled()); });
    connect(configure, &QAbstractButton::clicked, settings, &QAction::trigger);
    link_menu(add_button(QStringLiteral("dots-three"), tr("More menus")), more);
    AddWindowControls(row);
}

RunningTitleBar::RunningTitleBar(QWidget* parent) : WindowDragBar(parent), title(new QLabel(this)) {
    setFixedHeight(36);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(14, 0, 16, 0);
    row->setSpacing(9);
    title->setFont(Font(13, 600));
    title->setStyleSheet(QStringLiteral("color: white;"));
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(title, 1);
    AddWindowControls(row);
    hide();
}

void RunningTitleBar::SetGameTitle(const QString& value) {
    title->setText(value);
}

void RunningTitleBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Color::Panel());
    painter.setPen(Color::StrokeSubtle());
    painter.drawLine(rect().bottomLeft(), rect().bottomRight());
}

} // namespace Spotlight
