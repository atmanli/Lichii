// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/ingame_bar.h"

#include <cmath>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QVBoxLayout>

#include "lichii/spotlight/shader_chip.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

class ConfigurationButton final : public QAbstractButton {
public:
    explicit ConfigurationButton(QWidget* parent) : QAbstractButton(parent) {
        setFont(Font(12, 600));
        setFixedHeight(26);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

    QSize sizeHint() const override {
        return {fontMetrics().horizontalAdvance(text()) + 50, 26};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
        painter.setBrush(underMouse() || isDown() ? Color::ControlHover() : Color::Fill());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Success());
        painter.drawEllipse(QRect{10, 9, 8, 8});
        painter.setFont(font());
        painter.setPen(Color::Text());
        painter.drawText(QRect{24, 0, width() - 50, height()}, Qt::AlignVCenter, text());
        painter.drawPixmap(
            width() - 22, 6,
            IconPixmap(u"caret-down", 14, Color::TextSecondary(), devicePixelRatioF()));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }
};

} // namespace

class MenuTooltip final : public QWidget {
public:
    explicit MenuTooltip(QWidget* parent)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                              Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setAttribute(Qt::WA_TranslucentBackground);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(6);
        setMinimumWidth(180);
        const auto label = [this](const QString& text, const QFont& font, QColor color) {
            auto* label = new QLabel(text, this);
            label->setFont(font);
            auto palette = label->palette();
            palette.setColor(QPalette::WindowText, color);
            label->setPalette(palette);
            return label;
        };
        layout->addWidget(label(InGameBar::tr("In-game menu"), Font(13, 600), Color::Text()));
        keyboard_row = new QWidget(this);
        auto* row = new QHBoxLayout(keyboard_row);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        auto* icon = new QLabel(keyboard_row);
        icon->setPixmap(IconPixmap(u"keyboard", 15, Color::TextSecondary(), devicePixelRatioF()));
        row->addWidget(icon);
        row->addWidget(label(InGameBar::tr("Keyboard"), Font(12), Color::TextSecondary()));
        row->addStretch();
        key = new KeyCap({}, keyboard_row);
        row->addWidget(key);
        layout->addWidget(keyboard_row);
    }

    void SetKey(const QString& text) {
        keyboard_row->setVisible(!text.isEmpty());
        key->SetText(text);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Menu());
        painter.drawRoundedRect(rect(), 12, 12);
    }

private:
    QWidget* keyboard_row;
    KeyCap* key;
};

InGameBar::InGameBar(QWidget* parent) : QStatusBar(parent), contents(new QWidget(this)) {
    setFixedHeight(36);
    setSizeGripEnabled(false);
    auto* layout = new QHBoxLayout(contents);
    layout->setContentsMargins(6, 1, 6, 0);
    layout->setSpacing(8);

    configuration = new ConfigurationButton(contents);
    configuration->setToolTip(tr("Quick controls"));
    layout->addWidget(configuration);
    connect(configuration, &QAbstractButton::clicked, this, &InGameBar::QuickControlsRequested);

    shaders = new ShaderChip(contents);
    layout->addWidget(shaders);

    speed =
        new PillButton({}, QStringLiteral("lightning"), PillButton::Kind::Control, 26, contents);
    speed->SetForegroundColor(Color::Info());
    speed->hide();
    layout->addWidget(speed);
    connect(speed, &QAbstractButton::clicked, this, &InGameBar::QuickControlsRequested);

    notice = new QLabel(contents);
    notice->setFont(Font(12));
    notice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    notice->setTextFormat(Qt::PlainText);
    layout->addWidget(notice, 1);
    connect(this, &QStatusBar::messageChanged, this, [this](const QString& message) {
        notice->setToolTip(message);
        notice->setText(notice->fontMetrics().elidedText(message, Qt::ElideRight, notice->width()));
    });

    metrics = new QLabel(contents);
    metrics->setFont(Font(13, 600));
    metrics->setTextFormat(Qt::RichText);
    layout->addWidget(metrics);

    menu_button = new PillButton(tr("Menu"), QStringLiteral("sliders-horizontal"),
                                 PillButton::Kind::Control, 26, contents);
    menu_button->setFont(Font(12, 700));
    menu_button->installEventFilter(this);
    menu_tooltip = new MenuTooltip(menu_button);
    connect(menu_button, &QAbstractButton::clicked, this, &InGameBar::MenuRequested);
    layout->addWidget(menu_button);
}

void InGameBar::SetConfiguration(const QString& renderer, const QString& mode) {
    configuration->setText(tr("%1 · %2").arg(renderer, mode));
}

void InGameBar::SetStats(double fps, double generated, double frame_ms, bool at_target,
                         const QString& details) {
    const bool fg = std::isfinite(fps) && std::isfinite(generated) && generated >= 0.5;
    const QString game = std::isfinite(fps) ? QString::number(fps, 'f', 0) : QStringLiteral("—");
    const QString rate = fg ? game : tr("%1 FPS").arg(game);
    const QString shown = fg ? tr(" → <b>%1 FPS</b>").arg(fps + generated, 0, 'f', 0) : QString{};
    const QString time =
        std::isfinite(frame_ms) ? tr("%1 ms").arg(frame_ms, 0, 'f', 1) : QStringLiteral("—");
    metrics->setText(QStringLiteral("<span style='color:%1'><b>%2</b></span>"
                                    "<span style='color:%3'>%4</span>"
                                    "<span style='color:%5'> · %6</span>")
                         .arg((at_target ? Color::Success() : Color::Text()).name(), rate,
                              Color::Text().name(), shown,
                              Color::TextSecondary().name(QColor::HexArgb), time));
    metrics->setToolTip(details);
}

void InGameBar::SetSpeedStatus(const QString& description) {
    speed->setText(description);
    speed->setVisible(!description.isEmpty());
}

void InGameBar::SetMenuShortcut(const QKeySequence& shortcut) {
    const QString key = shortcut.toString(QKeySequence::NativeText);
    menu_tooltip->SetKey(key);
    menu_button->setToolTip(key.isEmpty() ? tr("In-game menu") : tr("In-game menu (%1)").arg(key));
}

void InGameBar::SetMenuOpen(bool open) {
    menu_tooltip->hide();
    menu_button->SetKind(open ? PillButton::Kind::Accent : PillButton::Kind::Control);
}

void InGameBar::OpenQuickControls(QMenu* menu) {
    const auto anchor = configuration->mapToGlobal(QPoint{0, 0});
    menu->popup(anchor - QPoint{0, menu->sizeHint().height() + 4});
}

void InGameBar::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Color::Panel());
    painter.fillRect(QRect{0, 0, width(), 1}, Color::StrokeSubtle());
}

void InGameBar::resizeEvent(QResizeEvent* event) {
    QStatusBar::resizeEvent(event);
    contents->setGeometry(rect());
    notice->setText(
        notice->fontMetrics().elidedText(currentMessage(), Qt::ElideRight, notice->width()));
}

bool InGameBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched != menu_button) {
        return QStatusBar::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::ToolTip: {
        menu_tooltip->adjustSize();
        const auto corner = menu_button->mapToGlobal(menu_button->rect().topRight());
        menu_tooltip->move(corner - QPoint{menu_tooltip->width() - 1, menu_tooltip->height() + 8});
        menu_tooltip->show();
        return true;
    }
    case QEvent::Leave:
    case QEvent::Hide:
    case QEvent::MouseButtonPress:
        menu_tooltip->hide();
        break;
    default:
        break;
    }
    return QStatusBar::eventFilter(watched, event);
}

} // namespace Spotlight
