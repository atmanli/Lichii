// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/spotlight/ingame_menu.h"

#include <algorithm>
#include <iterator>
#include <QAbstractSlider>
#include <QAction>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

constexpr auto PopupFlags = Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;

QColor HairlineColor() {
    return QColor(0xff, 0xff, 0xff, 0x12);
}

QLabel* Label(const QString& text, const QFont& font, const QColor& color, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setFont(font);
    SetTextColor(label, color);
    return label;
}

void PaintKey(QPainter& painter, const QRect& rect, const QString& key, const QColor& fill,
              const QColor& stroke) {
    painter.setPen(QPen(stroke, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), Radius::KeyCap,
                            Radius::KeyCap);
    painter.setFont(Font(11, 600));
    painter.setPen(Color::Text());
    painter.drawText(rect, Qt::AlignCenter, key);
}

int KeyWidth(const QString& key) {
    return QFontMetrics(Font(11, 600)).horizontalAdvance(key) + 16;
}

void PopupMenu(QMenu* menu, QWidget* anchor) {
    const QSize size = menu->sizeHint();
    const QRect area = anchor->screen()->availableGeometry();
    const QPoint corner = anchor->mapToGlobal(QPoint{anchor->width(), anchor->height() + 4});
    QPoint position{corner.x() - size.width(), corner.y()};
    if (position.y() + size.height() > area.bottom()) {
        position.setY(anchor->mapToGlobal(QPoint{}).y() - size.height() - 4);
    }
    position.setX(std::clamp(position.x(), area.left(),
                             std::max(area.left(), area.right() - size.width() + 1)));
    menu->popup(position);
}

class Hairline final : public QWidget {
public:
    explicit Hairline(QWidget* parent) : QWidget(parent) {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter(this).fillRect(rect(), HairlineColor());
    }
};

class FlowLayout final : public QLayout {
public:
    FlowLayout(QWidget* parent, int gap) : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(gap);
    }
    ~FlowLayout() override {
        while (auto* item = FlowLayout::takeAt(0)) {
            delete item;
        }
    }

    void addItem(QLayoutItem* item) override {
        items.append(item);
    }
    int count() const override {
        return static_cast<int>(items.size());
    }
    QLayoutItem* itemAt(int index) const override {
        return items.value(index);
    }
    QLayoutItem* takeAt(int index) override {
        return index >= 0 && index < items.size() ? items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override {
        return {};
    }
    bool hasHeightForWidth() const override {
        return true;
    }
    int heightForWidth(int width) const override {
        return Arrange(QRect{0, 0, width, 0}, false);
    }
    QSize sizeHint() const override {
        return minimumSize();
    }
    QSize minimumSize() const override {
        QSize size;
        for (const auto* item : items) {
            size = size.expandedTo(item->minimumSize());
        }
        return size;
    }
    void setGeometry(const QRect& rect) override {
        QLayout::setGeometry(rect);
        Arrange(rect, true);
    }

private:
    int Arrange(const QRect& rect, bool apply) const {
        int x = rect.x();
        int y = rect.y();
        int line = 0;
        for (auto* item : items) {
            if (item->isEmpty()) {
                continue;
            }
            const QSize size = item->sizeHint();
            if (x > rect.x() && x + size.width() > rect.right() + 1) {
                x = rect.x();
                y += line + spacing();
                line = 0;
            }
            if (apply) {
                item->setGeometry(QRect{QPoint{x, y}, size});
            }
            x += size.width() + spacing();
            line = std::max(line, size.height());
        }
        return y + line - rect.y();
    }

    QList<QLayoutItem*> items;
};

class HoverButton : public QAbstractButton {
public:
    explicit HoverButton(QWidget* parent) : QAbstractButton(parent) {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

protected:
    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }
    QColor Fill() const {
        return isDown() ? Color::Stroke() : underMouse() ? Color::ControlHover() : Color::Fill();
    }
};

class CloseHint final : public HoverButton {
public:
    explicit CloseHint(QWidget* parent) : HoverButton(parent) {
        setText(InGameMenu::tr("Close"));
        setAccessibleName(text());
    }

    void SetKey(QString key) {
        menu_key = std::move(key);
        resize(sizeHint());
        update();
    }

    QSize sizeHint() const override {
        int width = 20 + QFontMetrics(Font(12, 600)).horizontalAdvance(text()) + 6 +
                    KeyWidth(InGameMenu::tr("Esc"));
        if (!menu_key.isEmpty()) {
            width += 6 + KeyWidth(menu_key);
        }
        return {width, 32};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
        painter.setBrush(underMouse() ? QColor(0x08, 0x09, 0x0d, 0xcc)
                                      : QColor(0x08, 0x09, 0x0d, 0x99));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
        painter.setFont(Font(12, 600));
        painter.setPen(Color::Text());
        const int label = painter.fontMetrics().horizontalAdvance(text());
        painter.drawText(QRect{10, 0, label, 32}, Qt::AlignVCenter, text());
        int x = 10 + label + 6;
        const QColor fill(0xff, 0xff, 0xff, 0x24);
        const QColor stroke(0xff, 0xff, 0xff, 0x33);
        for (const QString& key : {InGameMenu::tr("Esc"), menu_key}) {
            if (key.isEmpty()) {
                continue;
            }
            PaintKey(painter, QRect{x, 4, KeyWidth(key), 24}, key, fill, stroke);
            x += KeyWidth(key) + 6;
        }
    }

private:
    QString menu_key;
};

class ActionTile final : public HoverButton {
public:
    ActionTile(QAction* action, QString icon, const QString& label, QWidget* parent)
        : HoverButton(parent), action(action), icon_name(std::move(icon)) {
        setText(label);
        setAccessibleName(label);
        setFixedSize(62, 81);
    }

    void SetIcon(QString icon) {
        icon_name = std::move(icon);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF tile{9, 0, 44, 44};
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
        painter.setBrush(Fill());
        painter.drawRoundedRect(hasFocus() ? tile.adjusted(0.5, 0.5, -0.5, -0.5) : tile, 15, 15);
        const QColor ink = isEnabled() ? Color::Text() : Color::TextFaint();
        painter.drawPixmap(QPointF{21, 12}, IconPixmap(icon_name, 20, ink, devicePixelRatioF()));
        painter.setFont(Font(12, 600));
        painter.setPen(ink);
        painter.drawText(QRect{0, 47, width(), 16}, Qt::AlignCenter,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, width()));
        painter.setFont(Font(11));
        painter.setPen(Color::TextSecondary());
        painter.drawText(QRect{0, 66, width(), 15}, Qt::AlignCenter, KeyText(action->shortcut()));
    }

private:
    QAction* action;
    QString icon_name;
};

class FeatureChip final : public HoverButton {
public:
    FeatureChip(QAction* action, QString icon, const QString& label, QWidget* parent)
        : HoverButton(parent), action(action), icon_name(std::move(icon)) {
        setText(label);
        setAccessibleName(label);
        setFont(Font(13, 600));
        setFixedHeight(32);
    }

    QSize sizeHint() const override {
        return {12 + 16 + 6 + fontMetrics().horizontalAdvance(text()) +
                    (action->isChecked() ? 6 + 16 : 0) + 12,
                32};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool on = action->isChecked();
        QColor fill = on ? (underMouse() ? Color::AccentLight() : Color::Accent()) : Fill();
        const QColor ink = on ? Color::OnAccent() : Color::Text();
        painter.setPen(hasFocus() ? QPen(Color::Text()) : Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 16, 16);
        const qreal ratio = devicePixelRatioF();
        painter.drawPixmap(12, 8, IconPixmap(icon_name, 16, ink, ratio));
        painter.setFont(font());
        painter.setPen(ink);
        const int label = fontMetrics().horizontalAdvance(text());
        painter.drawText(QRect{34, 0, label + 1, 32}, Qt::AlignVCenter, text());
        if (on) {
            painter.drawPixmap(34 + label + 6, 8, IconPixmap(u"check", 16, ink, ratio));
        }
    }

private:
    QAction* action;
    QString icon_name;
};

class SettingsRow final : public HoverButton {
public:
    SettingsRow(QAction* action, QWidget* parent) : HoverButton(parent), action(action) {
        setText(InGameMenu::tr("All game settings"));
        setAccessibleName(text());
        setFixedHeight(46);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
        painter.setBrush(Fill());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 15, 15);
        const qreal ratio = devicePixelRatioF();
        painter.drawPixmap(14, 15, IconPixmap(u"sliders-horizontal", 16, Color::Text(), ratio));
        painter.setFont(Font(14, 600));
        painter.setPen(Color::Text());
        painter.drawText(QRect{39, 0, width() - 39, height()}, Qt::AlignVCenter, text());
        int right = width() - 14 - 16;
        painter.drawPixmap(right, 15,
                           IconPixmap(u"caret-right", 16, Color::TextSecondary(), ratio));
        const QString key = KeyText(action->shortcut());
        if (!key.isEmpty()) {
            right -= 9 + KeyWidth(key);
            PaintKey(painter, QRect{right, 11, KeyWidth(key), 24}, key, Color::Fill(),
                     Color::Fill());
        }
    }

private:
    QAction* action;
};

class PanelFrame final : public QWidget {
public:
    PanelFrame(int radius, QWidget* parent) : QWidget(parent), radius(radius) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Color::StrokeSubtle());
        painter.setBrush(Color::Popover());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

private:
    int radius;
};

class GlyphButton final : public HoverButton {
public:
    GlyphButton(const QString& glyph, const QString& name, QWidget* parent) : HoverButton(parent) {
        setText(glyph);
        setAccessibleName(name);
        setFont(Font(15, 600));
        setFixedSize(fontMetrics().horizontalAdvance(glyph) + 2, 20);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(underMouse() || hasFocus() ? Color::Text() : Color::TextSecondary());
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

class VolumeTrack final : public QAbstractSlider {
public:
    VolumeTrack(int length, int knob, QWidget* parent) : QAbstractSlider(parent), knob(knob) {
        setOrientation(Qt::Horizontal);
        setRange(0, 200);
        setPageStep(5);
        setFixedSize(length, knob);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(InGameMenu::tr("Volume"));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal fraction = qreal(value() - minimum()) / std::max(1, maximum() - minimum());
        const qreal length = width();
        const qreal top = (knob - 6) / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xff, 0xff, 0xff, 0x2e));
        painter.drawRoundedRect(QRectF{0, top, length, 6}, 3, 3);
        painter.setBrush(Color::Accent());
        painter.drawRoundedRect(QRectF{0, top, length * fraction, 6}, 3, 3);
        const qreal x = std::clamp(length * fraction - knob / 2.0, 0.0, length - knob);
        painter.setBrush(QColor(0, 0, 0, 0x4d));
        painter.drawEllipse(QRectF{x - 0.5, 0.5, knob + 1.0, knob + 1.0});
        painter.setPen(hasFocus() ? QPen(Color::Accent(), 2) : Qt::NoPen);
        painter.setBrush(Color::Text());
        painter.drawEllipse(QRectF(x, 0, knob, knob).adjusted(1, 1, -1, -1));
    }

    void mousePressEvent(QMouseEvent* event) override {
        setSliderDown(true);
        Seek(event->position().x());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        Seek(event->position().x());
    }
    void mouseReleaseEvent(QMouseEvent*) override {
        setSliderDown(false);
    }

private:
    void Seek(qreal x) {
        setSliderPosition(minimum() +
                          qRound(std::clamp(x / width(), 0.0, 1.0) * (maximum() - minimum())));
    }

    int knob;
};

} // namespace

class ValueButton final : public HoverButton {
public:
    explicit ValueButton(QWidget* parent, bool compact = false)
        : HoverButton(parent), inset(compact ? 18 : 14) {
        setFont(Font(compact ? 12 : 13, 600));
        setFixedHeight(compact ? 28 : 34);
    }

    void SetValue(const QString& value) {
        if (value != text()) {
            setText(value);
            updateGeometry();
            update();
        }
    }

    QSize sizeHint() const override {
        return {inset + fontMetrics().horizontalAdvance(text()) + 8 + 16 + inset - 4, height()};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
        painter.setBrush(Fill());
        const qreal radius = std::min(12.0, height() / 2.0);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter.setFont(font());
        painter.setPen(isEnabled() ? Color::Text() : Color::TextFaint());
        const int caret = width() - inset + 4 - 16;
        painter.drawText(QRect{inset, 0, caret - 6 - inset, height()}, Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, caret - 6 - inset));
        painter.drawPixmap(
            caret, (height() - 16) / 2,
            IconPixmap(u"caret-down", 16, Color::TextSecondary(), devicePixelRatioF()));
    }

private:
    int inset;
};

class VolumeControl final : public QWidget {
public:
    VolumeControl(QAction* decrease, QAction* increase, QWidget* parent)
        : QWidget(parent), track(new VolumeTrack(120, 16, this)) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        auto* minus = new GlyphButton(QStringLiteral("−"), InGameMenu::tr("Volume down"), this);
        connect(minus, &QAbstractButton::clicked, decrease, &QAction::trigger);
        layout->addWidget(minus);
        layout->addWidget(track);
        auto* plus = new GlyphButton(QStringLiteral("+"), InGameMenu::tr("Volume up"), this);
        connect(plus, &QAbstractButton::clicked, increase, &QAction::trigger);
        layout->addWidget(plus);
        value = Label({}, Font(13, 600), Color::Text(), this);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setFixedWidth(44);
        layout->addWidget(value);
    }

    void Set(int volume, bool muted) {
        const QSignalBlocker blocker(track);
        track->setValue(volume);
        value->setText(muted ? InGameMenu::tr("Muted") : InGameMenu::tr("%1%").arg(volume));
    }

    VolumeTrack* track;

private:
    QLabel* value;
};

namespace {

constexpr int RowInset = 8;

int PageHeight(const QWidget& pages, int width) {
    const int height = pages.heightForWidth(width);
    return height >= 0 ? height : pages.sizeHint().height();
}

void PaintRowHover(QPainter& painter, const QAbstractButton& button) {
    if (!button.isEnabled() || (!button.underMouse() && !button.hasFocus())) {
        return;
    }
    painter.setPen(button.hasFocus() ? QPen(Color::Accent()) : Qt::NoPen);
    painter.setBrush(button.underMouse() ? QBrush(Color::FillFaint()) : QBrush());
    painter.drawRoundedRect(QRectF(button.rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
}

/// A row of More controls for one action of the quick controls menus: a switch for a setting
/// that toggles, its hotkey, and a caret when it opens a dialog.
class MoreRow final : public HoverButton {
public:
    MoreRow(QAction* action_, QWidget* parent) : HoverButton(parent), action(action_) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(action, &QAction::changed, this, &MoreRow::Sync);
        Sync();
    }

    void Sync() {
        setVisible(action->isVisible());
        setEnabled(action->isEnabled());
        setAccessibleName(Name());
        setFixedHeight((action->isCheckable() ? 26 : !Key().isEmpty() ? 24 : 17) + 9);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        PaintRowHover(painter, *this);
        int right = width() - RowInset;
        if (action->isCheckable()) {
            const QRectF track{right - 44.0, (height() - 26) / 2.0, 44, 26};
            painter.setPen(Qt::NoPen);
            painter.setBrush(action->isChecked() ? Color::Accent() : Color::ControlHover());
            painter.drawRoundedRect(track, 13, 13);
            painter.setBrush(Color::Text());
            painter.drawEllipse(QRectF{action->isChecked() ? track.right() - 23 : track.left() + 3,
                                       track.top() + 3, 20, 20});
            right -= 44 + 12;
        }
        const QString name = Name();
        if (const QString key = Key(); !key.isEmpty()) {
            right -= KeyWidth(key);
            PaintKey(painter, QRect{right, (height() - 24) / 2, KeyWidth(key), 24}, key,
                     Color::Control(), Color::Stroke());
            right -= 12;
        } else if (name.endsWith(QChar(0x2026))) {
            right -= 16;
            painter.drawPixmap(
                right, (height() - 16) / 2,
                IconPixmap(u"caret-right", 16, Color::TextSecondary(), devicePixelRatioF()));
            right -= 12;
        }
        const QFont font = Font(13);
        painter.setFont(font);
        painter.setPen(isEnabled() ? Color::Text() : Color::TextMuted());
        const int width = std::max(0, right - RowInset);
        painter.drawText(QRect{RowInset, 0, width, height()}, Qt::AlignVCenter,
                         QFontMetrics(font).elidedText(name, Qt::ElideRight, width));
    }

private:
    QString Name() const {
        return action->text().section(QLatin1Char('\t'), 0, 0).remove(QLatin1Char('&'));
    }

    QString Key() const {
        const QString key = action->property("spotlightKey").toString();
        return key.isEmpty() ? KeyText(action->shortcut()) : key;
    }

    QAction* action;
};

/// Opens More controls from the first page, or leads back to it.
class PageButton final : public HoverButton {
public:
    PageButton(const QString& name, bool back_, QWidget* parent)
        : HoverButton(parent), back(back_) {
        setText(name);
        setAccessibleName(back ? InGameMenu::tr("Back") : name);
        setFixedHeight(back ? 31 : 42);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        PaintRowHover(painter, *this);
        const qreal ratio = devicePixelRatioF();
        const int icon_top = (height() - 16) / 2;
        painter.setPen(Color::Text());
        if (back) {
            painter.drawPixmap(RowInset, icon_top,
                               IconPixmap(u"caret-left", 16, Color::Text(), ratio));
            painter.setFont(DisplayFont(18));
            painter.drawText(QRect{RowInset + 24, 0, width() - RowInset - 24, height()},
                             Qt::AlignVCenter, text());
            return;
        }
        painter.setFont(Font(14, 600));
        painter.drawText(QRect{RowInset, 0, width() - 2 * RowInset - 16, height()},
                         Qt::AlignVCenter, text());
        painter.drawPixmap(width() - RowInset - 16, icon_top,
                           IconPixmap(u"caret-right", 16, Color::TextSecondary(), ratio));
    }

private:
    bool back;
};

} // namespace

class GameArt final : public QWidget {
public:
    explicit GameArt(QWidget* parent) : QWidget(parent) {
        setFixedSize(52, 52);
    }

    void SetImage(const QPixmap& source) {
        const qreal ratio = devicePixelRatioF();
        image = source.isNull() ? QPixmap{}
                                : source.scaled(QSize{52, 52} * ratio, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
        image.setDevicePixelRatio(ratio);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        PaintWholeIcon(painter, rect(), image, 6);
    }

private:
    QPixmap image;
};

VolumeBlock::VolumeBlock(QWidget* parent) : QWidget(parent), track(new VolumeTrack(252, 18, this)) {
    setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    track->move(12, 30);
    connect(track, &QAbstractSlider::valueChanged, this, qOverload<>(&QWidget::update));
}

QAbstractSlider* VolumeBlock::Track() const {
    return track;
}

QSize VolumeBlock::sizeHint() const {
    return {12 + 252 + 12 + 44 + 12, 56};
}

void VolumeBlock::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QFont caption = Font(11, 600);
    caption.setLetterSpacing(QFont::PercentageSpacing, 106);
    painter.setFont(caption);
    painter.setPen(Color::TextMuted());
    painter.drawText(QRect{12, 8, width() - 24, 15}, Qt::AlignVCenter,
                     InGameMenu::tr("Volume").toUpper());
    painter.setFont(Font(13, 600));
    painter.setPen(Color::Text());
    painter.drawText(QRect{12 + 252 + 12, 30, 44, 18}, Qt::AlignRight | Qt::AlignVCenter,
                     InGameMenu::tr("%1%").arg(track->value()));
}

InGameMenu::InGameMenu(const Actions& actions, QWidget* parent)
    : QWidget(parent, PopupFlags),
      popup_actions{actions.pause,   actions.restart,    actions.stop,
                    actions.capture, actions.fullscreen, actions.controls.settings},
      controls(actions.controls) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoMouseReplay);
    setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    FadeInOnShow(this, Motion::Hover);
    setFocusPolicy(Qt::StrongFocus);

    auto* hint = new CloseHint(this);
    close_hint = hint;
    connect(hint, &QAbstractButton::clicked, this, &QWidget::hide);

    panel = new PanelFrame(26, this);
    auto* frame = new QVBoxLayout(panel);
    frame->setContentsMargins(1, 1, 1, 1);
    scroll = new QScrollArea(panel);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    scroll->viewport()->setAutoFillBackground(false);
    frame->addWidget(scroll);
    pages = new QWidget(scroll);
    scroll->setWidget(pages);
    auto* page_layout = new QVBoxLayout(pages);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->setSpacing(0);
    content = new QWidget(pages);
    page_layout->addWidget(content);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(19, 17, 19, 17);
    layout->setSpacing(12);

    auto* title_row = new QHBoxLayout;
    title_row->setSpacing(12);
    art = new GameArt(content);
    title_row->addWidget(art);
    auto* names = new QVBoxLayout;
    names->setSpacing(2);
    title = Label({}, DisplayFont(19), Color::Text(), content);
    title->setWordWrap(true);
    names->addWidget(title);
    play_time = Label({}, Font(12), Color::TextSecondary(), content);
    names->addWidget(play_time);
    title_row->addLayout(names, 1);
    layout->addLayout(title_row);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(0);
    const auto tile = [&](QAction* action, QString icon, const QString& name) {
        auto* button = new ActionTile(action, std::move(icon), name, content);
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
        sync();
        if (tiles->count() > 0) {
            tiles->addStretch();
        }
        tiles->addWidget(button);
        return button;
    };
    pause_tile = tile(actions.pause, QStringLiteral("pause"), tr("Pause"));
    tile(actions.restart, QStringLiteral("arrow-counter-clockwise"), tr("Restart"));
    tile(actions.stop, QStringLiteral("x"), tr("Stop"));
    tile(actions.capture, QStringLiteral("camera"), tr("Capture"));
    tile(actions.fullscreen, QStringLiteral("monitor"), tr("Fullscreen"));
    layout->addLayout(tiles);
    layout->addWidget(new Hairline(content));

    auto* features = new QVBoxLayout;
    features->setSpacing(8);
    auto* features_label = Label(tr("On now"), Font(12, 600), Color::TextSecondary(), content);
    features->addWidget(features_label);
    auto* chips_holder = new QWidget(content);
    auto* chips = new FlowLayout(chips_holder, 6);
    const auto chip = [&](QAction* action, QString icon, const QString& name) {
        if (!action) {
            return;
        }
        auto* button = new FeatureChip(action, std::move(icon), name, chips_holder);
        const auto sync = [button, action] {
            button->setVisible(action->isEnabled());
            button->setToolTip(action->toolTip());
            button->updateGeometry();
            button->update();
        };
        connect(action, &QAction::changed, button, sync);
        connect(button, &QAbstractButton::clicked, action, &QAction::trigger);
        chips->addWidget(button);
        sync();
    };
    chip(actions.frame_gen, QStringLiteral("film-strip"), tr("Frame gen"));
    chip(actions.effects, QStringLiteral("sparkle"), tr("FX"));
    chip(actions.turbo, QStringLiteral("lightning"), tr("Turbo"));
    features->addWidget(chips_holder);
    layout->addLayout(features);
    layout->addWidget(new Hairline(content));

    auto* rows = new QVBoxLayout;
    rows->setSpacing(4);
    const auto row = [&](const QString& name, QWidget* control, QLabel** note = nullptr) {
        auto* holder = new QWidget(content);
        holder->setMinimumHeight(42);
        auto* item = new QHBoxLayout(holder);
        item->setContentsMargins(0, 0, 0, 0);
        item->setSpacing(10);
        auto* names_column = new QVBoxLayout;
        names_column->setSpacing(1);
        names_column->addStretch();
        names_column->addWidget(Label(name, Font(14, 600), Color::Text(), holder));
        if (note) {
            *note = Label({}, Font(12, 400), Color::TextSecondary(), holder);
            names_column->addWidget(*note);
        }
        names_column->addStretch();
        item->addLayout(names_column, 1);
        control->setAccessibleName(name);
        item->addWidget(control);
        rows->addWidget(holder);
    };
    const auto dropdown = [&](const QString& name, QMenu* menu) {
        auto* button = new ValueButton(content);
        row(name, button);
        connect(button, &QAbstractButton::clicked, this,
                [button, menu] { PopupMenu(menu, button); });
        return button;
    };

    preset = new SegmentedControl({tr("Auto"), tr("Low"), tr("Mid"), tr("High")},
                                  SegmentedControl::Kind::Accent, 38, content);
    preset->setEnabled(false);
    preset->setToolTip(tr("Presets change resolution, which applies from the next launch. "
                          "Change them in Settings while no game runs."));
    row(tr("Preset"), preset, &preset_note);
    resolution = dropdown(tr("Resolution"), actions.controls.filter);
    frame_gen = dropdown(tr("Frame generation"), actions.controls.frame_gen);
    console_mode = new SegmentedControl({tr("Docked"), tr("Handheld")},
                                        SegmentedControl::Kind::Accent, 38, content);
    row(tr("Console mode"), console_mode);
    connect(console_mode, &SegmentedControl::CurrentChanged, this,
            [this](int index) { emit DockedChanged(index == 0); });
    volume = new VolumeControl(actions.controls.decrease_volume, actions.controls.increase_volume,
                               content);
    row(tr("Volume"), volume);
    connect(volume->track, &QAbstractSlider::valueChanged, this, &InGameMenu::VolumeChanged);
    layout->addLayout(rows);

    auto* more = new PageButton(tr("More controls"), false, content);
    more_entry = more;
    connect(more, &QAbstractButton::clicked, this, [this] { ShowMoreControls(true); });
    layout->addWidget(more);

    auto* settings = new SettingsRow(actions.controls.settings, content);
    const auto sync_settings = [settings, action = actions.controls.settings] {
        settings->setEnabled(action->isEnabled());
        settings->update();
    };
    connect(actions.controls.settings, &QAction::changed, settings, sync_settings);
    sync_settings();
    connect(settings, &QAbstractButton::clicked, this, [this, action = actions.controls.settings] {
        hide();
        action->trigger();
    });
    layout->addWidget(settings);
}

void InGameMenu::Open(QWidget* anchor, const QKeySequence& shortcut, const QPixmap& icon,
                      const QString& name, const QString& played, bool paused) {
    if (parentWidget() != anchor->window()) {
        setParent(anchor->window(), PopupFlags);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    menu_shortcut = shortcut;
    static_cast<CloseHint*>(close_hint)->SetKey(KeyText(shortcut));
    art->SetImage(icon);
    title->setText(name);
    play_time->setText(played);
    play_time->setVisible(!played.isEmpty());
    static_cast<ActionTile*>(pause_tile)
        ->SetIcon(paused ? QStringLiteral("play") : QStringLiteral("pause"));
    QString pause_text = popup_actions[0]->text();
    pause_tile->setAccessibleName(pause_text.remove(QLatin1Char('&')));
    static_cast<ActionTile*>(pause_tile)->setText(pause_text);

    constexpr int margin = 20;
    close_hint->move(margin, margin);
    const int panel_width = std::min(420, anchor->width() - 2 * margin);
    const int left = anchor->width() - panel_width - margin;
    const int top =
        left < close_hint->width() + 2 * margin ? close_hint->height() + 2 * margin : margin;
    panel_limit = std::max(anchor->height() - top - margin, 0);
    panel->setGeometry(left, top, panel_width,
                       std::min(PageHeight(*pages, panel_width - 2) + 2, panel_limit));
    setGeometry(QRect{anchor->mapToGlobal(QPoint{}), anchor->size()});
    show();
    raise();
    activateWindow();
    setFocus(Qt::PopupFocusReason);
}

void InGameMenu::SetSummary(const InGameSummary& summary_) {
    summary = summary_;
    if (more_page) {
        more_accuracy->SetValue(summary.accuracy);
        more_anti_aliasing->SetValue(summary.anti_aliasing);
    }
    resolution->SetValue(summary.resolution.isEmpty()
                             ? summary.filter
                             : tr("%1 · %2").arg(summary.resolution, summary.filter));
    frame_gen->SetValue(summary.frame_gen);
    const QString scope = summary.preset_global ? tr("Global") : tr("This game");
    preset_note->setText(summary.preset < 0 ? tr("%1 · Custom").arg(scope) : scope);
    const QSignalBlocker blocker(preset);
    preset->SetCurrentIndex(summary.preset);
}

void InGameMenu::SetConsoleMode(bool docked) {
    const QSignalBlocker blocker(console_mode);
    console_mode->SetCurrentIndex(docked ? 0 : 1);
}

void InGameMenu::SetVolume(int value, bool muted) {
    volume->Set(value, muted);
}

void InGameMenu::paintEvent(QPaintEvent*) {
    QPainter(this).fillRect(rect(), QColor(0x06, 0x07, 0x0a, 0x59));
}

void InGameMenu::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (more_page && more_page->isVisible()) {
        more_page->hide();
        content->show();
    }
    emit Dismissed();
}

void InGameMenu::ShowMoreControls(bool show) {
    const bool keyboard = (show ? more_entry : more_back)->hasFocus();
    if (show && !more_page) {
        BuildMoreControls();
    }
    if (show) {
        for (QMenu* menu : {controls.effects, controls.audio, controls.speed, controls.session}) {
            if (menu) {
                emit menu->aboutToShow();
            }
        }
        for (QWidget* row : more_rows) {
            static_cast<MoreRow*>(row)->Sync();
        }
    }
    content->setVisible(!show);
    more_page->setVisible(show);
    panel->resize(panel->width(),
                  std::min(PageHeight(*pages, panel->width() - 2) + 2, panel_limit));
    scroll->verticalScrollBar()->setValue(0);
    if (keyboard) {
        (show ? more_back : more_entry)->setFocus(Qt::TabFocusReason);
    } else {
        setFocus(Qt::OtherFocusReason);
    }
}

void InGameMenu::BuildMoreControls() {
    more_page = new QWidget(pages);
    pages->layout()->addWidget(more_page);
    auto* layout = new QVBoxLayout(more_page);
    layout->setContentsMargins(16 - RowInset, 16, 16 - RowInset, 16);
    layout->setSpacing(0);
    auto* back = new PageButton(tr("More controls"), true, more_page);
    more_back = back;
    connect(back, &QAbstractButton::clicked, this, [this] { ShowMoreControls(false); });
    layout->addWidget(back);

    const auto dropdown = [&](const QString& name, QMenu* menu) {
        auto* holder = new QWidget(more_page);
        holder->setFixedHeight(28 + 9);
        auto* item = new QHBoxLayout(holder);
        item->setContentsMargins(RowInset, 0, RowInset, 0);
        item->setSpacing(12);
        item->addWidget(Label(name, Font(13), Color::Text(), holder), 1);
        auto* button = new ValueButton(holder, true);
        button->setAccessibleName(name);
        item->addWidget(button);
        connect(button, &QAbstractButton::clicked, this,
                [button, menu] { PopupMenu(menu, button); });
        layout->addWidget(holder);
        return button;
    };
    more_accuracy = dropdown(tr("GPU accuracy"), controls.accuracy);
    more_anti_aliasing = dropdown(tr("Anti-aliasing"), controls.anti_aliasing);
    more_accuracy->SetValue(summary.accuracy);
    more_anti_aliasing->SetValue(summary.anti_aliasing);

    const auto add_row = [&](QAction* action, bool keep_open) {
        auto* row = new MoreRow(action, more_page);
        more_rows.push_back(row);
        connect(row, &QAbstractButton::clicked, this, [this, action, keep_open] {
            if (!keep_open || action->text().endsWith(QChar(0x2026))) {
                hide();
            }
            action->trigger();
        });
        layout->addWidget(row);
    };
    const auto section = [&](const QString& name, QMenu* menu, bool keep_open) {
        if (!menu) {
            return;
        }
        auto* label = Label(name, Font(12, 600), Color::TextSecondary(), more_page);
        label->setFixedHeight(16 + 9);
        label->setContentsMargins(RowInset, 0, RowInset, 0);
        layout->addWidget(label);
        for (QAction* action : menu->actions()) {
            if (!action->isSeparator() && !action->menu() &&
                !qobject_cast<QWidgetAction*>(action)) {
                add_row(action, keep_open || action->isCheckable());
            }
        }
    };
    section(tr("Post-processing"), controls.effects, true);
    section(tr("Audio"), controls.audio, true);
    section(tr("Emulation speed"), controls.speed, true);
    add_row(controls.rescan, false);
    add_row(controls.performance_overlay, true);
    section(tr("Session status"), controls.session, false);
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
    if (!panel->geometry().contains(event->position().toPoint())) {
        hide();
    }
    event->accept();
}

void InGameMenu::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (auto* button = qobject_cast<QAbstractButton*>(focusWidget())) {
            event->accept();
            button->click();
            return;
        }
    }
    if (event->key() == Qt::Key_Backspace && more_page && more_page->isVisible()) {
        event->accept();
        ShowMoreControls(false);
        return;
    }
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
