// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/ingame_menu.h"

#include <algorithm>
#include <array>
#include <QAction>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

constexpr auto PopupFlags = Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;

class MenuPanel final : public QScrollArea {
public:
    explicit MenuPanel(QWidget* parent) : QScrollArea(parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QScrollArea::paintEvent(event);
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Color::StrokeSubtle());
        painter.setBrush(Color::Popover());
        painter.drawRoundedRect(QRectF(viewport()->rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                Radius::InGamePanel, Radius::InGamePanel);
    }
};

class GameArt final : public QWidget {
public:
    GameArt(const QPixmap& image, QWidget* parent) : QWidget(parent), image(image) {
        setFixedSize(52, 52);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        PaintWholeIcon(painter, rect(), image, 6);
    }

private:
    const QPixmap& image;
};

class ActionTile final : public QAbstractButton {
public:
    ActionTile(QAction* action, QString icon_name, const QString& label, QWidget* parent)
        : QAbstractButton(parent), action(action), icon_name(std::move(icon_name)) {
        setText(label);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(82);
        setMinimumWidth(54);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF tile{(width() - 44) / 2.0, 0.5, 44, 44};
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
        painter.setBrush(underMouse() ? Color::ControlHover() : Color::Fill());
        painter.drawRoundedRect(tile, 15, 15);
        const QColor ink = isEnabled() ? Color::Text() : Color::TextFaint();
        painter.drawPixmap(tile.center() - QPointF{10, 10},
                           IconPixmap(icon_name, 20, ink, devicePixelRatioF()));
        painter.setFont(Font(12, 600));
        painter.setPen(ink);
        painter.drawText(QRect{0, 47, width(), 16}, Qt::AlignCenter,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, width()));
        painter.setFont(Font(11));
        painter.setPen(Color::TextSecondary());
        painter.drawText(QRect{0, 64, width(), 16}, Qt::AlignCenter,
                         action->shortcut().toString(QKeySequence::NativeText));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }

private:
    QAction* action;
    QString icon_name;
};

QLabel* Label(const QString& text, const QFont& font, QColor color, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setFont(font);
    auto palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
    return label;
}

} // namespace

InGameMenu::InGameMenu(const Actions& actions, QWidget* parent)
    : QWidget(parent, PopupFlags),
      popup_actions{actions.pause,   actions.restart,    actions.stop,
                    actions.capture, actions.fullscreen, actions.settings} {
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    scroll = new MenuPanel(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(
        QStringLiteral("QScrollArea, QWidget#InGameMenuContent { background: transparent; }"));
    scroll->viewport()->setAutoFillBackground(false);
    panel = new QWidget(scroll);
    panel->setObjectName(QStringLiteral("InGameMenuContent"));
    scroll->setWidget(panel);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(19, 15, 19, 17);
    layout->setSpacing(10);

    close_hint = new QWidget(this);
    auto* close_row = new QHBoxLayout(close_hint);
    close_row->setContentsMargins(0, 0, 0, 0);
    close_row->setSpacing(6);
    auto* close =
        new PillButton(tr("Close"), QStringLiteral("x"), PillButton::Kind::Control, 26, close_hint);
    connect(close, &QAbstractButton::clicked, this, &QWidget::hide);
    close_row->addWidget(close);
    close_row->addWidget(new KeyCap(tr("Esc"), close_hint));
    menu_key = new KeyCap({}, close_hint);
    close_row->addWidget(menu_key);

    auto* title_row = new QHBoxLayout;
    title_row->setSpacing(12);
    game_art = new GameArt(game_icon, panel);
    title_row->addWidget(game_art);
    auto* descriptions = new QVBoxLayout;
    descriptions->setSpacing(2);
    title = Label({}, DisplayFont(19), Color::Text(), panel);
    title->setWordWrap(true);
    descriptions->addStretch();
    descriptions->addWidget(title);
    play_time = Label({}, Font(12), Color::TextSecondary(), panel);
    descriptions->addWidget(play_time);
    descriptions->addStretch();
    title_row->addLayout(descriptions, 1);
    layout->addLayout(title_row);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(4);
    const auto tile = [&](QAction* action, QString icon, const QString& name) {
        auto* button = new ActionTile(action, std::move(icon), name, panel);
        const auto sync = [button, action] {
            button->setEnabled(action->isEnabled());
            button->setToolTip(action->toolTip());
            button->update();
        };
        connect(action, &QAction::changed, button, sync);
        connect(button, &QAbstractButton::clicked, this, [this, action] {
            hide();
            action->trigger();
        });
        tiles->addWidget(button, 1);
        sync();
        return button;
    };
    auto* pause = tile(actions.pause, QStringLiteral("pause-fill"), tr("Pause"));
    const auto update_pause = [pause, action = actions.pause] {
        QString text = action->text();
        pause->setText(text.remove(QLatin1Char('&')));
    };
    connect(actions.pause, &QAction::changed, pause, update_pause);
    update_pause();
    tile(actions.restart, QStringLiteral("arrow-clockwise"), tr("Restart"));
    tile(actions.stop, QStringLiteral("stop-fill"), tr("Stop"));
    tile(actions.capture, QStringLiteral("camera"), tr("Capture"));
    tile(actions.fullscreen, QStringLiteral("arrows-out"), tr("Fullscreen"));
    layout->addLayout(tiles);

    layout->addWidget(Label(tr("On now"), Font(12, 600), Color::TextSecondary(), panel));
    auto* features = new QHBoxLayout;
    features->setSpacing(6);
    const auto chip = [&](const QString& name, QString icon, QAction* action) {
        if (!action) {
            return;
        }
        auto* button = new PillButton(name, std::move(icon), PillButton::Kind::Control, 30, panel);
        const auto sync = [button, action] {
            button->setVisible(action->isEnabled());
            button->SetKind(action->isChecked() ? PillButton::Kind::Accent
                                                : PillButton::Kind::Control);
            button->setToolTip(action->toolTip());
        };
        connect(action, &QAction::changed, button, sync);
        connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
        features->addWidget(button);
        sync();
    };
    chip(tr("Frame gen"), QStringLiteral("lightning"), actions.frame_gen);
    chip(tr("Turbo"), QStringLiteral("timer"), actions.turbo);
    chip(tr("FX"), QStringLiteral("sparkle"), actions.effects);
    features->addStretch();
    layout->addLayout(features);

    const auto row = [&](const QString& name, QWidget* control) {
        auto* item = new QHBoxLayout;
        item->setSpacing(8);
        item->addWidget(Label(name, Font(14, 600), Color::Text(), panel));
        item->addStretch();
        control->setAccessibleName(name);
        item->addWidget(control);
        layout->addLayout(item);
    };

    const auto dropdown = [&](const QString& label, QAction* action, QMenu* menu) {
        auto* button =
            new PillButton({}, QStringLiteral("caret-down"), PillButton::Kind::Control, 34, panel);
        row(label, button);
        connect(button, &QAbstractButton::clicked, this, [this, button, action, menu] {
            const QPoint anchor = button->mapToGlobal(button->rect().bottomLeft());
            hide();
            if (menu) {
                menu->popup(anchor);
            } else {
                action->trigger();
            }
        });
        return button;
    };
    resolution_button = dropdown(tr("Resolution"), actions.settings, nullptr);
    frame_gen_button =
        dropdown(tr("Frame generation"), actions.frame_gen, actions.frame_gen_options);
    connect(actions.frame_gen, &QAction::changed, this, [this, action = actions.frame_gen] {
        frame_gen_button->setToolTip(action->toolTip());
    });
    console_mode = new SegmentedControl({tr("Docked"), tr("Handheld")},
                                        SegmentedControl::Kind::Accent, 34, panel);
    row(tr("Console mode"), console_mode);
    connect(console_mode, &SegmentedControl::CurrentChanged, this,
            [this](int index) { emit DockedChanged(index == 0); });

    auto* audio = new QWidget(panel);
    auto* audio_layout = new QHBoxLayout(audio);
    audio_layout->setContentsMargins(0, 0, 0, 0);
    audio_layout->setSpacing(5);
    auto* minus = new CircleButton(QStringLiteral("minus"), 26, audio);
    minus->setAccessibleName(tr("Volume down"));
    audio_layout->addWidget(minus);
    connect(minus, &QAbstractButton::clicked, actions.decrease_volume, &QAction::trigger);

    volume = new QSlider(Qt::Horizontal, audio);
    volume->setAccessibleName(tr("Volume"));
    volume->setRange(0, 200);
    volume->setPageStep(5);
    volume->setFixedWidth(120);
    volume->setStyleSheet(
        QStringLiteral(
            "QSlider::groove:horizontal { height: 5px; border-radius: 2px; background: %1; }"
            "QSlider::sub-page:horizontal { border-radius: 2px; background: %2; }"
            "QSlider::handle:horizontal { width: 13px; margin: -4px 0; border-radius: 6px; "
            "background: %2; }"
            "QSlider:focus { outline: 0; }"
            "QSlider::handle:horizontal:focus { border: 1px solid %3; }")
            .arg(Color::Stroke().name(QColor::HexArgb), Color::Accent().name(),
                 Color::Text().name()));
    audio_layout->addWidget(volume);
    connect(volume, &QSlider::valueChanged, this, &InGameMenu::VolumeChanged);

    auto* plus = new CircleButton(QStringLiteral("plus"), 26, audio);
    plus->setAccessibleName(tr("Volume up"));
    audio_layout->addWidget(plus);
    connect(plus, &QAbstractButton::clicked, actions.increase_volume, &QAction::trigger);

    volume_value = Label({}, Font(13, 600), Color::Text(), audio);
    volume_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volume_value->setMinimumWidth(48);
    audio_layout->addWidget(volume_value);
    row(tr("Volume"), audio);

    auto* settings = new PillButton(tr("All game settings"), QStringLiteral("gear-six"),
                                    PillButton::Kind::Control, 42, panel);
    const auto sync_settings = [settings, action = actions.settings] {
        const QString key = action->shortcut().toString(QKeySequence::NativeText);
        settings->setText(key.isEmpty() ? tr("All game settings")
                                        : tr("All game settings  %1").arg(key));
        settings->setEnabled(action->isEnabled());
    };
    connect(actions.settings, &QAction::changed, settings, sync_settings);
    sync_settings();
    connect(settings, &QAbstractButton::clicked, this, [this, action = actions.settings] {
        hide();
        action->trigger();
    });
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addWidget(settings, 1);
    auto* more = new CircleButton(QStringLiteral("dots-three"), 38, panel);
    more->setToolTip(tr("Quick controls"));
    more->setAccessibleName(more->toolTip());
    footer->addWidget(more);
    connect(more, &QAbstractButton::clicked, this, [this, more, menu = actions.quick_controls] {
        const QPoint anchor = more->mapToGlobal(more->rect().topRight());
        hide();
        menu->popup(anchor - QPoint{menu->sizeHint().width(), menu->sizeHint().height()});
    });
    layout->addLayout(footer);
    layout->addStretch();
}

void InGameMenu::Open(QWidget* anchor, const QKeySequence& shortcut, const QPixmap& icon,
                      const QString& name, const QString& played) {
    if (parentWidget() != anchor->window()) {
        setParent(anchor->window(), PopupFlags);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    menu_shortcut = shortcut;
    menu_key->setVisible(!shortcut.isEmpty());
    menu_key->SetText(shortcut.toString(QKeySequence::NativeText));
    game_icon = icon;
    game_art->update();
    title->setText(name);
    play_time->setText(played);
    play_time->setVisible(!played.isEmpty());
    constexpr int margin = 20;
    close_hint->adjustSize();
    close_hint->move(margin, margin);
    const int panel_width = std::min(420, anchor->width() - 2 * margin);
    const int left = anchor->width() - panel_width - margin;
    const int top = left < close_hint->width() + 2 * margin ? 60 : margin;
    const int panel_height =
        std::min(panel->layout()->sizeHint().height() + 4, anchor->height() - top - margin);
    scroll->setGeometry(left, top, panel_width, panel_height);
    setGeometry(QRect{anchor->mapToGlobal(QPoint{}), anchor->size()});
    show();
    setFocus(Qt::PopupFocusReason);
}

void InGameMenu::SetConsoleMode(bool docked) {
    console_mode->SetCurrentIndex(docked ? 0 : 1);
}

void InGameMenu::SetVolume(int value, bool muted) {
    const QSignalBlocker blocker(volume);
    volume->setValue(value);
    volume_value->setText(muted ? tr("Muted") : tr("%1%").arg(value));
}

void InGameMenu::SetGraphicsSummary(const QString& resolution, const QString& frame_gen) {
    if (resolution_button->text() != resolution) {
        resolution_button->setText(resolution);
    }
    if (frame_gen_button->text() != frame_gen) {
        frame_gen_button->setText(frame_gen);
    }
}

void InGameMenu::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    auto scrim = Color::Scrim();
    scrim.setAlpha(89);
    painter.fillRect(rect(), scrim);
}

void InGameMenu::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    emit Dismissed();
}

bool InGameMenu::IsCloseKey(const QKeyEvent& event) const {
    return event.key() == Qt::Key_Escape ||
           menu_shortcut.matches(QKeySequence(event.keyCombination())) == QKeySequence::ExactMatch;
}

QAction* InGameMenu::ActionForKey(const QKeyEvent& event) const {
    const auto key = QKeySequence(event.keyCombination());
    for (auto* action : popup_actions) {
        if (action->isEnabled() && action->shortcut().matches(key) == QKeySequence::ExactMatch) {
            return action;
        }
    }
    return nullptr;
}

void InGameMenu::mousePressEvent(QMouseEvent* event) {
    if (!scroll->geometry().contains(event->position().toPoint())) {
        hide();
    }
    event->accept();
}

void InGameMenu::keyPressEvent(QKeyEvent* event) {
    if (IsCloseKey(*event)) {
        event->accept();
        hide();
        return;
    }
    if (auto* action = ActionForKey(*event)) {
        event->accept();
        hide();
        action->trigger();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool InGameMenu::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        const auto& key = *static_cast<QKeyEvent*>(event);
        if (IsCloseKey(key) || ActionForKey(key)) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

} // namespace Spotlight
