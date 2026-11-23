// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <tuple>
#include <utility>
#include <QAbstractSlider>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QVBoxLayout>
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "lichii/configuration/configuration_shared.h"
#include "lichii/configuration/configure_applets.h"
#include "lichii/configuration/configure_audio.h"
#include "lichii/configuration/configure_cpu.h"
#include "lichii/configuration/configure_debug_tab.h"
#include "lichii/configuration/configure_dialog.h"
#include "lichii/configuration/configure_filesystem.h"
#include "lichii/configuration/configure_general.h"
#include "lichii/configuration/configure_graphics.h"
#include "lichii/configuration/configure_graphics_advanced.h"
#include "lichii/configuration/configure_graphics_extensions.h"
#include "lichii/configuration/configure_hotkeys.h"
#include "lichii/configuration/configure_input.h"
#include "lichii/configuration/configure_input_player.h"
#include "lichii/configuration/configure_microphone.h"
#include "lichii/configuration/configure_network.h"
#include "lichii/configuration/configure_profile_manager.h"
#include "lichii/configuration/configure_system.h"
#include "lichii/configuration/configure_ui.h"
#include "lichii/configuration/configure_web.h"
#include "lichii/configuration/shared_widget.h"
#include "lichii/hotkeys.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/settings_get_started.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/vk.h"
#include "ui_configure.h"

namespace {
using Spotlight::PresetCard;

class ColourSwatch final : public QAbstractButton {
public:
    ColourSwatch(QPushButton* source_, const QString& name) : source{source_} {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(name);
        setToolTip(name);
        connect(this, &QAbstractButton::clicked, source, &QAbstractButton::click);
        source->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::StyleChange) {
            update();
        }
        return QAbstractButton::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent*) override {
        static const QRegularExpression pattern(
            QStringLiteral("background-color:\\s*(#[0-9a-fA-F]{6})"));
        const auto match = pattern.match(source->styleSheet());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool active = hasFocus() || underMouse();
        painter.setPen(
            QPen(active ? Spotlight::Color::Accent() : QColor(255, 255, 255, 70), 1.5));
        painter.setBrush(match.hasMatch() ? QColor(match.captured(1)) : QColor(Qt::transparent));
        painter.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
    }

    void enterEvent(QEnterEvent* event) override {
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        update();
        QAbstractButton::leaveEvent(event);
    }

private:
    QPushButton* source;
};
using Spotlight::SettingsPage;
namespace Color = Spotlight::Color;
namespace Tints = Spotlight::SettingsColor;

class BindingButton final : public QAbstractButton {
public:
    BindingButton(QString icon_, int width) : icon{std::move(icon_)} {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        setFont(Spotlight::Font(11, 600));
        setFixedSize(width, 28);
    }

    void SetBinding(QStringList keys_, bool set_) {
        keys = std::move(keys_);
        set = set_;
        update();
    }

    std::function<void(const QPoint&)> menu;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (menu) {
            menu(event->globalPos());
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setFont(font());
        int x = 0;
        if (!icon.isEmpty()) {
            painter.drawPixmap(
                0, (height() - 14) / 2,
                Spotlight::IconPixmap(icon, 14, Color::TextSecondary(), devicePixelRatioF()));
            x = 19;
        }
        const int start = x;
        const QColor fill =
            underMouse() ? QColor(0xff, 0xff, 0xff, 0x2e) : QColor(0xff, 0xff, 0xff, 0x1f);
        for (const QString& key : keys) {
            const int cap = std::min(fontMetrics().horizontalAdvance(key) + 16, width() - x);
            const QRectF rect(x + 0.5, 2.5, cap - 1, 23);
            painter.setPen(QPen(Color::Stroke(), 1));
            painter.setBrush(fill);
            painter.drawRoundedRect(rect, 7, 7);
            painter.setPen(set ? Color::Text() : Color::TextSecondary());
            painter.drawText(rect, Qt::AlignCenter,
                             fontMetrics().elidedText(key, Qt::ElideRight, cap - 12));
            x += cap + 4;
        }
        if (hasFocus()) {
            painter.setPen(QPen(Color::Accent(), 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(start - 2, 0.75, x - 4 - start + 4, height() - 1.5), 9,
                                    9);
        }
    }

private:
    QString icon;
    QStringList keys;
    bool set = false;
};

class LevelMeter final : public QWidget {
public:
    explicit LevelMeter(QProgressBar* source_) : source{source_} {
        setFixedSize(120, 8);
        connect(source, &QProgressBar::valueChanged, this, [this] { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(QRectF(rect()), 4, 4);
        const int span = source->maximum() - source->minimum();
        const qreal ratio = span > 0 ? qreal(source->value() - source->minimum()) / span : 0.0;
        if (ratio > 0) {
            painter.setBrush(Color::Accent());
            painter.drawRoundedRect(QRectF(0, 0, width() * ratio, height()), 4, 4);
        }
    }

private:
    QProgressBar* source;
};

class VisibilityFollower final : public QObject {
public:
    VisibilityFollower(QWidget* source_, QWidget* target_)
        : QObject(target_), source{source_}, target{target_} {
        target->setVisible(!source->isHidden());
        source->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) {
            target->setVisible(!source->isHidden());
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* source;
    QWidget* target;
};

class ToggleSegments final : public QWidget {
public:
    explicit ToggleSegments(QList<std::pair<QString, QCheckBox*>> options_)
        : options{std::move(options_)} {
        setFixedHeight(38);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);
        for (const auto& option : options) {
            connect(option.second, &QCheckBox::toggled, this, [this] { update(); });
        }
        setFixedWidth(SegmentRect(static_cast<int>(options.size()) - 1).right() + 4);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
        for (int i = 0; i < options.size(); ++i) {
            const QRect segment = SegmentRect(i);
            const bool on = options[i].second->isChecked();
            const bool usable = options[i].second->isEnabled();
            painter.setPen(Qt::NoPen);
            if (on || (i == hovered && usable)) {
                painter.setBrush(on ? Color::Accent() : Color::Fill());
                painter.drawRoundedRect(QRectF(segment), segment.height() / 2.0,
                                        segment.height() / 2.0);
            }
            if (hasFocus() && i == focused) {
                painter.setPen(QPen(Color::Text(), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(QRectF(segment).adjusted(-1.5, -1.5, 1.5, 1.5),
                                        segment.height() / 2.0 + 1.5, segment.height() / 2.0 + 1.5);
            }
            painter.setPen(!usable ? Color::TextFaint() : on ? Color::OnAccent() : Color::Text());
            painter.setFont(Spotlight::Font(13, on ? 600 : 500));
            painter.drawText(segment, Qt::AlignCenter, options[i].first);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        const int index = SegmentAt(event->position().toPoint());
        if (index >= 0) {
            focused = index;
            Toggle(index);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const int index = SegmentAt(event->position().toPoint());
        if (index != hovered) {
            hovered = index;
            update();
        }
    }

    void leaveEvent(QEvent*) override {
        hovered = -1;
        update();
    }

    void keyPressEvent(QKeyEvent* event) override {
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
            focused = std::clamp(focused + (event->key() == Qt::Key_Left ? -1 : 1), 0,
                                 static_cast<int>(options.size()) - 1);
            update();
            return;
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            Toggle(focused);
            return;
        default:
            QWidget::keyPressEvent(event);
        }
    }

private:
    QRect SegmentRect(int index) const {
        const QFontMetrics metrics(Spotlight::Font(13, 600));
        int x = 3;
        for (int i = 0; i < index; ++i) {
            x += metrics.horizontalAdvance(options[i].first) + 28 + 2;
        }
        return QRect(x, 3, metrics.horizontalAdvance(options[index].first) + 28, 32);
    }

    int SegmentAt(const QPoint& point) const {
        for (int i = 0; i < options.size(); ++i) {
            if (SegmentRect(i).contains(point)) {
                return i;
            }
        }
        return -1;
    }

    void Toggle(int index) {
        if (auto* box = options[index].second; box->isEnabled()) {
            box->click();
        }
    }

    QList<std::pair<QString, QCheckBox*>> options;
    int hovered = -1;
    int focused = 0;
};

class ProfileCard final : public QAbstractButton {
public:
    ProfileCard(const QString& name, const QPixmap& avatar_, bool current_, QColor tint_)
        : avatar{avatar_}, tint{tint_}, current{current_} {
        setText(name);
        setFixedSize(150, 156);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_Hover);
        setAccessibleName(current ? tr("%1, playing as").arg(name) : tr("Switch to %1").arg(name));
    }

    std::function<void(const QPoint&)> menu;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (menu) {
            menu(event->globalPos());
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool add = avatar.isNull() && !tint.isValid();
        const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setBrush(add                          ? QColor(Qt::transparent)
                         : underMouse() || hasFocus() ? Color::Fill()
                                                      : Color::FillFaint());
        painter.setPen(current ? QPen(Color::Accent(), 2)
                       : hasFocus()
                           ? QPen(Color::Text(), 1)
                           : QPen(add ? Color::Stroke() : QColor(0xff, 0xff, 0xff, 0x14), 1));
        painter.drawRoundedRect(frame, 18, 18);
        const QRectF circle(43, 16, 64, 64);
        if (add) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Color::Stroke(), 2, Qt::DashLine));
            painter.drawEllipse(circle.adjusted(1, 1, -1, -1));
            painter.drawPixmap(
                QRectF(circle.center() - QPointF(10, 10), QSizeF(20, 20)),
                Spotlight::IconPixmap(u"plus", 20, Color::TextSecondary(), devicePixelRatioF()),
                QRectF());
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(tint);
            painter.drawEllipse(circle);
            painter.save();
            QPainterPath clip;
            clip.addEllipse(circle);
            painter.setClipPath(clip);
            painter.drawPixmap(circle.toRect(), avatar);
            painter.restore();
            if (current) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(Color::Accent(), 3));
                painter.drawEllipse(circle.adjusted(1.5, 1.5, -1.5, -1.5));
            }
        }
        painter.setFont(Spotlight::Font(14, 600));
        painter.setPen(add ? Color::TextSecondary() : Color::Text());
        const QRect name_rect(8, 90, width() - 16, 18);
        painter.drawText(
            name_rect, Qt::AlignCenter,
            QFontMetrics(painter.font()).elidedText(text(), Qt::ElideRight, name_rect.width()));
        if (add) {
            return;
        }
        if (current) {
            const QString label = tr("Playing as");
            painter.setFont(Spotlight::Font(11, 600));
            const int badge_width = QFontMetrics(painter.font()).horizontalAdvance(label) + 18;
            const QRectF badge((width() - badge_width) / 2.0, 118, badge_width, 22);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Color::Accent());
            painter.drawRoundedRect(badge, 11, 11);
            painter.setPen(Color::OnAccent());
            painter.drawText(badge, Qt::AlignCenter, label);
        } else {
            painter.setFont(Spotlight::Font(12, 600));
            painter.setPen(Color::TextSecondary());
            painter.drawText(QRect(8, 118, width() - 16, 22), Qt::AlignCenter, tr("Switch to"));
        }
    }

private:
    QPixmap avatar;
    QColor tint;
    bool current;
};

class FlowRow final : public QWidget {
public:
    FlowRow() {
        setProperty("spotlightTransparent", true);
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    void Clear() {
        for (auto* child : findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
            child->hide();
            child->deleteLater();
        }
        items.clear();
    }

    void Add(QWidget* item) {
        item->setParent(this);
        item->show();
        items.append(item);
        Arrange(width());
        updateGeometry();
    }

    bool hasHeightForWidth() const override {
        return true;
    }

    int heightForWidth(int available) const override {
        return Arrange(available, false);
    }

    QSize sizeHint() const override {
        return {400, heightForWidth(width() > 0 ? width() : 640)};
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        Arrange(width());
    }

private:
    int Arrange(int available, bool place = true) const {
        constexpr int padding = 16;
        constexpr int gap = 12;
        int x = padding;
        int y = padding;
        int line = 0;
        for (auto* item : items) {
            const QSize size = item->size();
            if (x > padding && x + size.width() > available - padding) {
                x = padding;
                y += line + gap;
                line = 0;
            }
            if (place) {
                item->move(x, y);
            }
            x += size.width() + gap;
            line = std::max(line, size.height());
        }
        return y + line + padding;
    }

    QList<QWidget*> items;
};

Spotlight::StatusBanner::Tone BannerTone(ConfigureNetwork::NextendoTone tone) {
    switch (tone) {
    case ConfigureNetwork::NextendoTone::Success:
        return Spotlight::StatusBanner::Tone::Success;
    case ConfigureNetwork::NextendoTone::Warning:
        return Spotlight::StatusBanner::Tone::Warning;
    case ConfigureNetwork::NextendoTone::Error:
        return Spotlight::StatusBanner::Tone::Error;
    case ConfigureNetwork::NextendoTone::Info:
        break;
    }
    return Spotlight::StatusBanner::Tone::Info;
}

QPixmap AvatarDot(const QColor& tint, qreal ratio) {
    QPixmap pixmap(QSize(40, 40) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(tint);
    painter.drawEllipse(QRectF(0, 0, 40, 40));
    return pixmap;
}

class FriendsDialog final : public Spotlight::Dialog {
public:
    FriendsDialog(ConfigureNetwork* network_, QWidget* parent)
        : Spotlight::Dialog(parent), network{network_} {
        setWindowTitle(tr("Friends"));
        SetIcon(u"users", Tints::Green());
        setFixedWidth(560);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);
        auto* add_row = new QHBoxLayout;
        add_row->setSpacing(8);
        code->setPlaceholderText(tr("Friend code"));
        code->setAccessibleName(code->placeholderText());
        code->addAction(Spotlight::Icon(u"user", Color::TextSecondary(), 16),
                        QLineEdit::LeadingPosition);
        code->setMinimumHeight(40);
        add_row->addWidget(code, 1);
        auto* add = new Spotlight::PillButton(tr("Add friend"), {},
                                              Spotlight::PillButton::Kind::Accent, 40);
        add_row->addWidget(add);
        layout->addLayout(add_row);
        layout->addWidget(message);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* holder = new QWidget;
        holder->setProperty("spotlightTransparent", true);
        lists = new QVBoxLayout(holder);
        lists->setContentsMargins(0, 0, 0, 0);
        lists->setSpacing(8);
        scroll->setWidget(holder);
        scroll->viewport()->setAutoFillBackground(false);
        holder->setAutoFillBackground(false);
        layout->addWidget(scroll);
        auto* footer = new QHBoxLayout;
        footer->addWidget(signed_in, 1);
        auto* done =
            new Spotlight::PillButton(tr("Done"), {}, Spotlight::PillButton::Kind::Accent, 40);
        footer->addWidget(done);
        layout->addLayout(footer);
        signed_in->setFont(Spotlight::Font(13, 500));
        Spotlight::SetTextColor(signed_in, Color::TextSecondary());

        const auto send = [this] {
            network->AddNextendoFriend(code->text());
            code->clear();
        };
        connect(add, &QAbstractButton::clicked, this, send);
        connect(code, &QLineEdit::returnPressed, this, send);
        connect(done, &QAbstractButton::clicked, this, &QDialog::accept);
        connect(network, &ConfigureNetwork::NextendoChanged, this, [this] { Rebuild(); });
        Rebuild();
        network->RefreshNextendoFriends();
    }

private:
    void Rebuild() {
        const auto profile = network->NextendoProfile();
        const auto account = Common::Nextendo::LoadAccount(profile);
        SetSubtitle(
            account && !account->friend_code.empty()
                ? tr("Your friend code: %1").arg(QString::fromStdString(account->friend_code))
                : QString{});
        signed_in->setText(
            account ? tr("Signed in as %1").arg(QString::fromStdString(account->username))
                    : tr("Not signed in"));
        const QString result = network->NextendoFriendsMessage();
        message->Set(result, result == tr("Friend request sent.")
                                 ? Spotlight::StatusBanner::Tone::Success
                                 : Spotlight::StatusBanner::Tone::Error);
        while (auto* item = lists->takeAt(0)) {
            if (auto* widget = item->widget()) {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }
        static const std::array tints{QColor(0xc9, 0xd8, 0xff), QColor(0xb9, 0xe8, 0xcf),
                                      QColor(0xff, 0xe0, 0xa8), QColor(0xff, 0xb4, 0xc8)};
        const auto tint = [](const Common::Nextendo::Friend& entry) {
            return entry.status == 0 ? QColor(0x6b, 0x6f, 0x7b)
                                     : tints[static_cast<std::size_t>(entry.pid % tints.size())];
        };
        const auto group = [this](const QString& title) {
            auto* heading = SettingsPage::Text(title, 13, 600, true);
            heading->setContentsMargins(0, 6, 0, 0);
            lists->addWidget(heading);
            auto* card = new QWidget;
            card->setObjectName(QStringLiteral("friendsCard"));
            card->setAttribute(Qt::WA_StyledBackground);
            card->setStyleSheet(QStringLiteral(
                "QWidget#friendsCard { background: rgba(255,255,255,13); border: 1px solid "
                "rgba(255,255,255,20); border-radius: 16px; }"));
            auto* rows = new QVBoxLayout(card);
            rows->setContentsMargins(1, 1, 1, 1);
            rows->setSpacing(0);
            lists->addWidget(card);
            return rows;
        };
        const qreal ratio = devicePixelRatioF();
        const auto list = Common::Nextendo::CachedFriends(profile);
        if (!list.requests.empty()) {
            auto* rows = group(tr("Requests"));
            for (const auto& entry : list.requests) {
                auto* control = new QWidget;
                auto* buttons = new QHBoxLayout(control);
                buttons->setContentsMargins(0, 0, 0, 0);
                buttons->setSpacing(6);
                auto* decline = new Spotlight::PillButton(tr("Decline"), {},
                                                          Spotlight::PillButton::Kind::Control, 32);
                auto* accept = new Spotlight::PillButton(tr("Accept"), {},
                                                         Spotlight::PillButton::Kind::Accent, 32);
                buttons->addWidget(decline);
                buttons->addWidget(accept);
                for (const auto& [button, action] :
                     {std::pair{decline, Common::Nextendo::FriendAction::Decline},
                      std::pair{accept, Common::Nextendo::FriendAction::Accept}}) {
                    connect(button, &QAbstractButton::clicked, this,
                            [this, pid = entry.pid, action] {
                                network->AnswerNextendoFriend(pid, action, this);
                            });
                }
                SettingsPage::AddRow(rows, QString::fromStdString(entry.name),
                                     tr("Wants to be friends"), {}, {}, control, {},
                                     AvatarDot(tint(entry), ratio));
            }
        }
        if (!list.sent.empty()) {
            auto* rows = group(tr("Sent requests"));
            for (const auto& entry : list.sent) {
                SettingsPage::AddRow(rows, QString::fromStdString(entry.name), tr("Request sent"),
                                     {}, {}, nullptr, {}, AvatarDot(tint(entry), ratio));
            }
        }
        const auto online = std::count_if(list.friends.cbegin(), list.friends.cend(),
                                          [](const auto& entry) { return entry.status != 0; });
        auto* rows =
            group(list.friends.empty() ? tr("Friends") : tr("Friends · %1 online").arg(online));
        if (list.friends.empty()) {
            SettingsPage::AddRow(rows, tr("No friends yet"),
                                 tr("Share your friend code, or add someone's above."), {}, {},
                                 nullptr, {}, AvatarDot(QColor(0x6b, 0x6f, 0x7b), ratio));
        }
        for (const auto& entry : list.friends) {
            const QString detail =
                entry.status == 2
                    ? (entry.app_detail.empty()
                           ? tr("Playing")
                           : tr("Playing %1").arg(QString::fromStdString(entry.app_detail)))
                : entry.status == 1 ? tr("Online")
                                    : tr("Offline");
            auto* control = new QWidget;
            auto* line = new QHBoxLayout(control);
            line->setContentsMargins(0, 0, 0, 0);
            line->setSpacing(10);
            if (entry.status != 0) {
                auto* badge = SettingsPage::Text(QStringLiteral("● ") + tr("Online"), 13, 600);
                Spotlight::SetTextColor(badge, Color::Success());
                line->addWidget(badge);
            }
            auto* more = new Spotlight::CircleButton(QStringLiteral("dots-three"), 30);
            more->setAccessibleName(tr("More for %1").arg(QString::fromStdString(entry.name)));
            connect(more, &QAbstractButton::clicked, this, [this, more, pid = entry.pid] {
                auto* menu = new QMenu(more);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->addAction(tr("Remove friend"), this, [this, pid] {
                    network->AnswerNextendoFriend(pid, Common::Nextendo::FriendAction::Remove,
                                                  this);
                });
                menu->popup(more->mapToGlobal(QPoint(0, more->height())));
            });
            line->addWidget(more);
            SettingsPage::AddRow(rows, QString::fromStdString(entry.name), detail, {}, {}, control,
                                 {}, AvatarDot(tint(entry), ratio));
        }
        int wanted = 0;
        for (int i = 0; i < lists->count(); ++i) {
            if (const auto* widget = lists->itemAt(i)->widget()) {
                wanted += widget->sizeHint().height() + lists->spacing();
            }
        }
        lists->addStretch();
        scroll->setFixedHeight(std::min(380, wanted));
    }

    ConfigureNetwork* network;
    QLineEdit* code = new QLineEdit;
    Spotlight::StatusBanner* message = new Spotlight::StatusBanner;
    QScrollArea* scroll = new QScrollArea;
    QVBoxLayout* lists = nullptr;
    QLabel* signed_in = new QLabel;
};

bool IsBuiltInFolder(const UISettings::GameDir& folder) {
    return folder.path == "SDMC" || folder.path == "UserNAND" || folder.path == "SysNAND";
}

} // namespace

ConfigureDialog::ConfigureDialog(QWidget* parent, HotkeyRegistry& registry_,
                                 InputCommon::InputSubsystem* input_subsystem_,
                                 std::vector<VkDeviceInfo::Record>& vk_device_records,
                                 Core::System& system_, bool enable_web_config)
    : QDialog(parent), ui{std::make_unique<Ui::ConfigureDialog>()}, registry(registry_),
      system{system_}, devices{vk_device_records}, input_subsystem{input_subsystem_},
      builder{std::make_unique<ConfigurationShared::Builder>(this, !system_.IsPoweredOn())},
      applets_tab{std::make_unique<ConfigureApplets>(system_, nullptr, *builder, this)},
      audio_tab{std::make_unique<ConfigureAudio>(system_, nullptr, *builder, this)},
      cpu_tab{std::make_unique<ConfigureCpu>(system_, nullptr, *builder, this)},
      debug_tab_tab{std::make_unique<ConfigureDebugTab>(system_, this)},
      filesystem_tab{std::make_unique<ConfigureFilesystem>(this)},
      general_tab{std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, this)},
      graphics_advanced_tab{
          std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, this)},
      graphics_extensions_tab{
          std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, this)},
      ui_tab{std::make_unique<ConfigureUi>(system_, this)},
      graphics_tab{std::make_unique<ConfigureGraphics>(
          system_, vk_device_records, [&]() { graphics_advanced_tab->ExposeComputeOption(); },
          [this](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
              ui_tab->UpdateScreenshotInfo(ratio, setup);
          },
          nullptr, *builder, this)},
      hotkeys_tab{std::make_unique<ConfigureHotkeys>(system_.HIDCore(), this)},
      input_tab{std::make_unique<ConfigureInput>(system_, this)},
      network_tab{std::make_unique<ConfigureNetwork>(system_, this)},
      profile_tab{std::make_unique<ConfigureProfileManager>(system_, this)},
      system_tab{std::make_unique<ConfigureSystem>(system_, nullptr, *builder, this)},
      web_tab{std::make_unique<ConfigureWeb>(this)} {
    Settings::SetConfiguringGlobal(true);

    ui->setupUi(this);
    Spotlight::FadeInOnShow(this);

    web_tab->SetWebServiceConfigEnabled(enable_web_config);
    hotkeys_tab->Populate(registry);
    game_dirs = UISettings::values.game_dirs;
    general_tab->SetResetCallback([this] { done(QDialog::Rejected); });
    connect(ui_tab.get(), &ConfigureUi::LanguageChanged, this, &ConfigureDialog::OnLanguageChanged);
    connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged, this,
            &ConfigureDialog::ExternalContentDirsChanged);

    ui->footer->setVisible(system.IsPoweredOn());
    BuildSettings();
    ConfigurationShared::FitToScreen(this, QSize(1280, 800));
}

ConfigureDialog::~ConfigureDialog() = default;

void ConfigureDialog::ApplyConfiguration() {
    general_tab->ApplyConfiguration();
    ui_tab->ApplyConfiguration();
    system_tab->ApplyConfiguration();
    profile_tab->ApplyConfiguration();
    filesystem_tab->ApplyConfiguration();
    if (input_initialized) {
        input_tab->ApplyConfiguration();
        auto& players = Settings::values.players.GetValue();
        for (int i = 0; i < vibration_strength.size(); ++i) {
            players[i].vibration_strength = vibration_strength[i];
        }
    }
    hotkeys_tab->ApplyConfiguration(registry);
    cpu_tab->ApplyConfiguration();
    graphics_tab->ApplyConfiguration();
    graphics_advanced_tab->ApplyConfiguration();
    graphics_extensions_tab->ApplyConfiguration();
    audio_tab->ApplyConfiguration();
    debug_tab_tab->ApplyConfiguration();
    web_tab->ApplyConfiguration();
    network_tab->ApplyConfiguration();
    applets_tab->ApplyConfiguration();
    if (start_fullscreen != nullptr && start_fullscreen->isEnabled() &&
        UISettings::values.fullscreen.GetValue() != start_fullscreen->isChecked()) {
        UISettings::values.fullscreen.SetValue(start_fullscreen->isChecked());
        emit FullscreenOnStartChanged(start_fullscreen->isChecked());
    }
    if (game_dirs_changed) {
        UISettings::values.game_dirs = game_dirs;
        UISettings::values.is_game_list_reload_pending = true;
        game_dirs_changed = false;
    }
    system.ApplySettings();
    Settings::LogSettings();
}

void ConfigureDialog::RestoreInputConfiguration() {
    if (input_initialized) {
        input_tab->RestoreConfiguration();
    }
}

void ConfigureDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    QDialog::changeEvent(event);
}

void ConfigureDialog::reject() {
    accept();
}

void ConfigureDialog::OnLanguageChanged(const QString& locale) {
    emit LanguageChanged(locale);
    UISettings::values.is_game_list_reload_pending = true;
    ApplyConfiguration();
    reopen_requested = true;
    accept();
}

void ConfigureDialog::BuildSettings() {
    setWindowFlag(Qt::FramelessWindowHint, Spotlight::ClientFrame);
    settings_page = new SettingsPage(this, false);
    for (const char* name :
         {"label_external_desc", "external_content_list", "add_external_dir_button",
          "remove_external_dir_button"}) {
        general_tab->findChild<QWidget*>(QLatin1String(name))->setProperty("spotlightBound", true);
    }
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->setSpacing(0);
    ui->verticalLayout->addWidget(settings_page);
    settings_page->SetFooter(ui->footer);
    connect(settings_page, &SettingsPage::BackRequested, this, &QDialog::accept);

    const int general =
        settings_page->AddCategory(tr("General"), tr("How Lichii looks and behaves"),
                                   QStringLiteral("globe"), Color::Accent());
    const int library = settings_page->AddCategory(
        tr("Library"), tr("Where Lichii looks for games, updates and DLC"),
        QStringLiteral("folder"), Tints::Green());
    const int graphics = settings_page->AddCategory(
        tr("Graphics"), tr("Preset first; everything else follows it unless you change it"),
        QStringLiteral("monitor"), Tints::Gold());
    const int audio =
        settings_page->AddCategory(tr("Audio"), tr("What you hear and where it plays"),
                                   QStringLiteral("speaker-high"), Tints::Blue());
    const int controls = settings_page->AddCategory(
        tr("Controls"), tr("One controller per player; hotkeys work with keyboard and pad"),
        QStringLiteral("game-controller"), Tints::Lavender());
    const int system_page =
        settings_page->AddCategory(tr("System"), tr("The console your games think they run on"),
                                   QStringLiteral("cpu"), Color::Accent());
    const int multiplayer =
        settings_page->AddCategory(tr("Multiplayer"), tr("Local wireless, LAN and online play"),
                                   QStringLiteral("users"), Tints::Green());
    const int shortcuts = settings_page->AddCategory(
        tr("Shortcuts"), tr("Click a keyboard or controller binding to change it."),
        QStringLiteral("keyboard"), Tints::Gold());

    settings_page->SetBuilder(general, [this, general, library, controls] {
        BuildGeneral(general, library, controls);
    });
    settings_page->SetBuilder(library, [this, library] { BuildLibrary(library); });
    settings_page->SetBuilder(graphics, [this, graphics] { BuildGraphics(graphics); });
    settings_page->SetBuilder(audio, [this, audio] {
        auto& values = Settings::values;
        auto* output = settings_page->AddSection(audio, tr("Output"));
        if (auto* device = SettingsPage::Editor(audio_tab.get(), values.audio_output_device_id);
            device && device->combobox) {
            auto* detail = SettingsPage::AddRow(output, tr("Output device"), {},
                                                QStringLiteral("speaker-high"), Tints::Blue(),
                                                SettingsPage::BindSetting(device));
            const auto describe = [detail, combo = device->combobox] {
                detail->setText(combo->currentText());
                detail->setVisible(!combo->currentText().isEmpty());
            };
            describe();
            connect(device->combobox, &QComboBox::currentIndexChanged, detail, describe);
        }
        AddSettingRow(output, audio_tab.get(), values.volume, tr("Volume"),
                      tr("Every game; a game can set its own"), QStringLiteral("speaker-high"),
                      Tints::Blue());
        AddSettingRow(output, audio_tab.get(), values.audio_muted, tr("Mute"), {},
                      QStringLiteral("speaker-x"), Color::Accent());
        settings_page->AddBlock(
            audio,
            SettingsPage::Note(tr("Output engine, surround mode and muting in the background are "
                                  "under Advanced.")),
            tr("Output engine"));
    });
    settings_page->SetBuilder(controls,
                              [this, controls, shortcuts] { BuildControls(controls, shortcuts); });
    settings_page->SetBuilder(system_page, [this, system_page] { BuildSystem(system_page); });
    settings_page->SetBuilder(multiplayer, [this, multiplayer] {
        auto* connection = settings_page->AddSection(multiplayer, tr("Connection"));
        for (const auto& [name, title, detail, icon, tint] :
             std::initializer_list<std::tuple<const char*, QString, QString, QString, QColor>>{
                 {"lan_play", tr("LAN play"), tr("Find players on your network and in rooms"),
                  QStringLiteral("wifi-high"), Tints::Green()},
                 {"airplane_mode", tr("Airplane mode"), tr("Games see no network at all"),
                  QStringLiteral("airplane"), Tints::Blue()},
                 {"nextendo_enabled", tr("Play online through Nextendo"),
                  tr("A community service for online play"), QStringLiteral("globe"),
                  Color::Accent()},
             }) {
            if (auto* source = network_tab->findChild<QCheckBox*>(QLatin1String(name))) {
                SettingsPage::AddRow(connection, title, detail, icon, tint,
                                     SettingsPage::BindToggle(source));
            }
        }
        auto* profile = network_tab->findChild<QComboBox*>(QStringLiteral("nextendo_profile"));
        auto* sign_in = network_tab->findChild<QPushButton*>(QStringLiteral("nextendo_sign_in"));
        auto* sign_out = network_tab->findChild<QPushButton*>(QStringLiteral("nextendo_sign_out"));
        if (profile == nullptr || sign_in == nullptr || sign_out == nullptr) {
            return;
        }
        auto* account = settings_page->AddSection(multiplayer, tr("Nextendo account"));
        SettingsPage::AddRow(account, tr("Profile"), tr("The local profile that signs in"),
                             QStringLiteral("user"), Tints::Lavender(),
                             SettingsPage::BindCombo(profile));
        auto* state = new QWidget;
        state->setProperty("spotlightTransparent", true);
        auto* state_layout = new QVBoxLayout(state);
        state_layout->setContentsMargins(14, 4, 14, 14);
        state_layout->setSpacing(14);
        auto* banner = new Spotlight::StatusBanner;
        state_layout->addWidget(banner);
        auto* actions = new QHBoxLayout;
        actions->setSpacing(8);
        auto* friends = new Spotlight::PillButton(tr("Friends"), QStringLiteral("users"));
        auto* start = new Spotlight::PillButton(tr("Sign in to Nextendo"), {},
                                                Spotlight::PillButton::Kind::Accent);
        auto* leave = new Spotlight::PillButton(tr("Sign out"));
        actions->addWidget(friends);
        actions->addStretch();
        actions->addWidget(start);
        actions->addWidget(leave);
        state_layout->addLayout(actions);
        account->addWidget(state);
        connect(start, &QAbstractButton::clicked, sign_in, &QAbstractButton::click);
        connect(leave, &QAbstractButton::clicked, sign_out, &QAbstractButton::click);
        connect(friends, &QAbstractButton::clicked, this, [this] {
            FriendsDialog dialog(network_tab.get(), this);
            dialog.exec();
        });
        const auto refresh = [this, banner, friends, start, leave, sign_out] {
            auto* network = network_tab.get();
            const bool signing_in = network->NextendoSigningIn();
            const bool signed_in = sign_out->isEnabled() && !signing_in;
            banner->Set(network->NextendoStatus(), BannerTone(network->NextendoStatusTone()));
            start->setText(signing_in ? tr("Cancel sign-in") : tr("Sign in to Nextendo"));
            start->SetKind(signing_in ? Spotlight::PillButton::Kind::Control
                                      : Spotlight::PillButton::Kind::Accent);
            start->setVisible(!signed_in);
            leave->setVisible(signed_in);
            friends->setVisible(signed_in && network->NextendoFriendsAvailable());
        };
        refresh();
        connect(network_tab.get(), &ConfigureNetwork::NextendoChanged, state, refresh);
        settings_page->AddBlock(
            multiplayer,
            SettingsPage::Note(tr("Network adapter, custom servers, lobby token and the Mod Store "
                                  "address are under Advanced.")),
            tr("Network adapter"));
    });

    for (const auto& [category, widget, name] :
         std::initializer_list<std::tuple<int, QWidget*, QString>>{
             {general, general_tab.get(), tr("General")},
             {general, debug_tab_tab.get(), tr("Debug")},
             {library, ui_tab.get(), tr("Interface and game list")},
             {graphics, graphics_tab.get(), tr("Graphics")},
             {graphics, graphics_advanced_tab.get(), tr("Advanced graphics")},
             {graphics, graphics_extensions_tab.get(), tr("Extras")},
             {audio, audio_tab.get(), tr("Audio")},
             {system_page, system_tab.get(), tr("System")},
             {system_page, cpu_tab.get(), tr("CPU")},
             {system_page, filesystem_tab.get(), tr("Filesystem")},
             {system_page, applets_tab.get(), tr("Applets")},
             {multiplayer, network_tab.get(), tr("Network")},
             {multiplayer, web_tab.get(), tr("Web")},
         }) {
        settings_page->AddPage(category, widget, name);
    }
    settings_page->SetBuilder(shortcuts, [this, shortcuts] { BuildShortcuts(shortcuts); });
    using Spotlight::AdvancedSection;
    for (const auto& [category, text] : std::initializer_list<std::pair<int, QString>>{
             {general, tr("Advanced: diagnostics for bug reports, screenshots and resets.")},
             {library, tr("Advanced: artwork sources, the list view and the metadata cache.")},
             {graphics, tr("Advanced: for troubleshooting a game or tuning performance. Each "
                           "section says when to touch it.")},
             {audio, tr("Set the host audio backend and the sound mode games see.")},
             {system_page, tr("Advanced: CPU, speed, console identity, storage and applets.")},
             {multiplayer, tr("Advanced: network adapter, custom servers and the public lobby.")},
         }) {
        settings_page->SetAdvancedSubtitle(category, text);
    }
    if (auto* iidb = ui_tab->findChild<QWidget*>(QStringLiteral("iidb_api_key"))) {
        iidb->setProperty("spotlightBound", true);
    }
    settings_page->SetAdvancedSections(
        general,
        {
            AdvancedSection{
                tr("Behaviour"),
                tr("Desktop integration"),
                0,
                {QStringLiteral("hideInactiveMouse|%1").arg(tr("Hide the mouse when idle")),
                 QStringLiteral("enable_gamemode|%1|%2").arg(tr("Gamemode"), tr("Linux")),
                 QStringLiteral("gui_force_x11|%1|%2")
                     .arg(tr("Force X11"), tr("Linux, when Wayland misbehaves")),
                 QStringLiteral("button_reset_defaults|%1|%2")
                     .arg(tr("Reset all settings"), tr("Also removes every game’s own settings"))}},
            AdvancedSection{tr("Screenshots"),
                            tr("Where captures go"),
                            0,
                            {QStringLiteral("screenshot_path_edit"),
                             QStringLiteral("enable_screenshot_save_as"),
                             QStringLiteral("screenshot_height")}},
            AdvancedSection{
                tr("Diagnostics"),
                tr("Support may ask you to change these"),
                1,
                {QStringLiteral("open_log_button"),
                 QStringLiteral("log_filter_edit|%1").arg(tr("Log filter")),
                 QStringLiteral("extended_logging|%1").arg(tr("Extended logging")),
                 QStringLiteral("flush_line|%1|%2")
                     .arg(tr("Write the log on every line"), tr("Keeps the log after a crash")),
                 QStringLiteral("toggle_console|%1|%2")
                     .arg(tr("Show the log in a console"), tr("Windows")),
                 QStringLiteral("censor_username|%1").arg(tr("Hide your user name in logs")),
                 QStringLiteral("enable_graphics_debugging|%1|%2")
                     .arg(tr("Vulkan validation"), tr("For driver bug reports")),
                 QStringLiteral("enable_renderdoc_hotkey|%1").arg(tr("RenderDoc hotkey")),
                 QStringLiteral("perform_vulkan_check|%1").arg(tr("Check Vulkan at startup"))}},
        },
        {QStringLiteral("select_user_on_boot"),
         QStringLiteral("toggle_gdbstub"),
         QStringLiteral("gdbport_spinbox"),
         QStringLiteral("gpu_log_level"),
         QStringLiteral("homebrew_args_edit"),
         QStringLiteral("enable_shader_feedback"),
         QStringLiteral("enable_nsight_aftermath"),
         QStringLiteral("disable_loop_safety_checks"),
         QStringLiteral("disable_buffer_reorder"),
         QStringLiteral("dump_guest_shaders"),
         QStringLiteral("gpu_log_shader_dumps"),
         QStringLiteral("disable_macro_jit"),
         QStringLiteral("dump_macros"),
         QStringLiteral("disable_macro_hle"),
         QStringLiteral("quest_flag"),
         QStringLiteral("use_dev_keys"),
         QStringLiteral("use_debug_asserts"),
         QStringLiteral("disable_web_applet"),
         QStringLiteral("enable_all_controllers"),
         QStringLiteral("use_auto_stub"),
         QStringLiteral("dump_audio_commands"),
         QStringLiteral("fs_access_log"),
         QStringLiteral("reporting_services"),
         QStringLiteral("serial_battery_edit"),
         QStringLiteral("serial_board_edit"),
         QStringLiteral("debug_knobs_spinbox"),
         QStringLiteral("cpuopt_page_tables"),
         QStringLiteral("cpuopt_block_linking"),
         QStringLiteral("cpuopt_return_stack_buffer"),
         QStringLiteral("cpuopt_fast_dispatcher"),
         QStringLiteral("cpuopt_context_elimination"),
         QStringLiteral("cpuopt_const_prop"),
         QStringLiteral("cpuopt_misc_ir"),
         QStringLiteral("cpuopt_reduce_misalign_checks"),
         QStringLiteral("cpuopt_fastmem"),
         QStringLiteral("cpuopt_fastmem_exclusives"),
         QStringLiteral("cpuopt_recompile_exclusives"),
         QStringLiteral("cpuopt_ignore_memory_aborts")});
    settings_page->SetAdvancedBuilder(general, [this, general] {
        if (auto* path = ui_tab->findChild<QLineEdit*>(QStringLiteral("screenshot_path_edit"))) {
            auto* control = new QWidget;
            auto* layout = new QHBoxLayout(control);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            layout->addWidget(SettingsPage::BindLineEdit(path));
            if (auto* change =
                    ui_tab->findChild<QAbstractButton*>(QStringLiteral("screenshot_path_button"))) {
                layout->addWidget(SettingsPage::BindButton(change, tr("Change")));
            }
            settings_page->AddAdvancedRow(general, QStringLiteral("screenshot_path_edit"),
                                          tr("Folder"), {}, control);
        }
        if (auto* ask =
                ui_tab->findChild<QCheckBox*>(QStringLiteral("enable_screenshot_save_as"))) {
            settings_page->AddAdvancedRow(general, QStringLiteral("enable_screenshot_save_as"),
                                          tr("Ask where to save"), tr("Windows"),
                                          SettingsPage::BindToggle(ask));
        }
        if (auto* height = ui_tab->findChild<QComboBox*>(QStringLiteral("screenshot_height"))) {
            settings_page->AddAdvancedRow(general, QStringLiteral("screenshot_height"),
                                          tr("Height"), {}, SettingsPage::BindCombo(height));
        }
        if (auto* open =
                debug_tab_tab->findChild<QAbstractButton*>(QStringLiteral("open_log_button"))) {
            settings_page->AddAdvancedRow(general, QStringLiteral("open_log_button"),
                                          tr("Log folder"), {},
                                          SettingsPage::BindButton(open, tr("Open")));
        }
    });

    settings_page->SetAdvancedSections(
        library,
        {
            AdvancedSection{tr("Artwork"),
                            tr("Optional sources for missing icons and art"),
                            0,
                            {QStringLiteral("|%1|%2").arg(tr("SteamGridDB API key"),
                                                          tr("Empty disables lookup")),
                             QStringLiteral("iidb_api_key|%1").arg(tr("iiDB artwork API key"))}},
            AdvancedSection{
                tr("Maintenance"),
                tr("If the library shows stale names or icons"),
                0,
                {QStringLiteral("cache_game_list"), QStringLiteral("reset_game_list_cache")}},
            AdvancedSection{
                tr("List view"),
                tr("Columns and text of the list layout"),
                1,
                {QStringLiteral("folder_icon_size_combobox|%1").arg(tr("Folder icon size")),
                 QStringLiteral("row_1_text_combobox|%1").arg(tr("First line")),
                 QStringLiteral("row_2_text_combobox|%1").arg(tr("Second line")),
                 QStringLiteral("show_add_ons|%1").arg(tr("Add-ons column")),
                 QStringLiteral("show_size|%1").arg(tr("Size column")),
                 QStringLiteral("show_types|%1").arg(tr("Format column")),
                 QStringLiteral("show_play_time|%1").arg(tr("Play time column"))}},
        },
        {QStringLiteral("theme_combobox"), QStringLiteral("enable_screenshot_save_as"),
         QStringLiteral("screenshot_path_edit"), QStringLiteral("screenshot_path_button"),
         QStringLiteral("screenshot_height")});
    settings_page->SetAdvancedBuilder(library, [this, library] {
        if (auto* iidb = ui_tab->findChild<QAbstractButton*>(QStringLiteral("iidb_api_key"))) {
            settings_page->AddAdvancedRow(library, QStringLiteral("iidb_api_key"),
                                          tr("iiDB artwork API key"), {},
                                          SettingsPage::BindButton(iidb, tr("Add key")));
        }
        if (auto* cache =
                filesystem_tab->findChild<QCheckBox*>(QStringLiteral("cache_game_list"))) {
            settings_page->AddAdvancedRow(library, QStringLiteral("cache_game_list"),
                                          tr("Cache game list metadata"), {},
                                          SettingsPage::BindToggle(cache));
        }
        if (auto* reset = filesystem_tab->findChild<QAbstractButton*>(
                QStringLiteral("reset_game_list_cache"))) {
            settings_page->AddAdvancedRow(library, QStringLiteral("reset_game_list_cache"),
                                          tr("Reset metadata cache"), {},
                                          SettingsPage::BindButton(reset, tr("Reset")));
        }
    });

    settings_page->SetAdvancedSections(
        graphics,
        {
            AdvancedSection{tr("Accuracy"),
                            tr("Higher is closer to the console, lower is faster"),
                            0,
                            {QStringLiteral("gpu_accuracy|%1|%2")
                                 .arg(tr("GPU mode"), tr("Choose Fast or Accurate")),
                             QStringLiteral("dma_accuracy|%1").arg(tr("DMA accuracy")),
                             QStringLiteral("gpu_fence_behavior|%1").arg(tr("GPU fence behaviour")),
                             QStringLiteral("native_compute_write_mark|%1|%2")
                                 .arg(tr("Switch 2 compute write mark"),
                                      tr("Correctness knob for Switch 2 compute"))}},
            AdvancedSection{
                tr("Shaders"),
                tr("When games stutter while new effects appear"),
                0,
                {QStringLiteral("use_disk_shader_cache|%1")
                     .arg(tr("Keep a pipeline cache on disk")),
                 QStringLiteral("use_vulkan_driver_pipeline_cache|%1")
                     .arg(tr("Use the driver pipeline cache")),
                 QStringLiteral("precache_shaders|%1")
                     .arg(tr("Precache shaders before a game starts")),
                 QStringLiteral("build_all_cached_shaders|%1|%2")
                     .arg(tr("Build all cached shaders first"), tr("Longer start, fewer hitches")),
                 QStringLiteral("use_asynchronous_shaders|%1|%2")
                     .arg(tr("Don't wait for shaders still compiling"),
                          tr("Draws skip effects that are not ready")),
                 QStringLiteral(">native_async_draws|%1")
                     .arg(tr("Switch 2 draws with unbuilt shaders")),
                 QStringLiteral(">native_pipeline_wait_ms|%1").arg(tr("Wait per frame")),
                 QStringLiteral(">skip_unbuilt_mesh_draws|%1")
                     .arg(tr("Skip Switch 2 mesh draws with unbuilt shaders"))}},
            AdvancedSection{
                tr("Compatibility"),
                tr("Change only when a game shows broken graphics"),
                0,
                {QStringLiteral("nvdec_emulation|%1").arg(tr("Video decoding (NVDEC)")),
                 QStringLiteral("accelerate_astc|%1").arg(tr("ASTC decoding")),
                 QStringLiteral("skip_cpu_inner_invalidation|%1")
                     .arg(tr("Skip CPU inner invalidation")),
                 QStringLiteral("sync_memory_operations|%1").arg(tr("Sync memory operations")),
                 QStringLiteral("enable_compute_pipelines|%1|%2")
                     .arg(tr("Compute pipelines"), tr("Intel Vulkan driver only")),
                 QStringLiteral("use_reactive_flushing|%1").arg(tr("Reactive flushing")),
                 QStringLiteral("barrier_feedback_loops|%1").arg(tr("Barrier feedback loops")),
                 QStringLiteral("enable_buffer_history|%1").arg(tr("Buffer history")),
                 QStringLiteral("fix_bloom_effects|%1|%2")
                     .arg(tr("Fix bloom effects"), tr("For games with incorrect bloom")),
                 QStringLiteral("dyna_state|%1|%2")
                     .arg(tr("Extended dynamic state"), tr("Unavailable on macOS")),
                 QStringLiteral("vertex_input_dynamic_state|%1")
                     .arg(tr("Vertex input dynamic state")),
                 QStringLiteral("mesh_shaders|%1").arg(tr("Mesh shaders")),
                 QStringLiteral("sample_shading_fraction|%1").arg(tr("Sample shading"))}},
            AdvancedSection{tr("Memory"),
                            tr("For GPUs with little video memory"),
                            0,
                            {QStringLiteral("astc_recompression|%1").arg(tr("ASTC recompression")),
                             QStringLiteral("vram_usage_mode|%1").arg(tr("VRAM use"))}},
            AdvancedSection{tr("Device"),
                            tr("Only for PCs with more than one GPU"),
                            0,
                            {QStringLiteral("backend|%1").arg(tr("Graphics API")),
                             QStringLiteral("vulkan_device|%1").arg(tr("GPU"))}},
            AdvancedSection{
                tr("Frame rate"),
                tr("Pacing and caps beyond the Everyday frame rate"),
                1,
                {QStringLiteral("nx2_frame_rate_limit|%1|%2")
                     .arg(tr("Switch 2 refresh limit"),
                          tr("Sets the guest display refresh, not the FPS cap")),
                 QStringLiteral("frame_pacing_mode|%1").arg(tr("Frame pacing (Vulkan)")),
                 QStringLiteral("async_presentation|%1").arg(tr("Asynchronous presentation")),
                 QStringLiteral("use_video_framerate|%1")
                     .arg(tr("Match video playback frame rate"))}},
            AdvancedSection{
                tr("Frame generation"),
                tr("Fine-tuning for the FlyHigh and FSR methods"),
                1,
                {QStringLiteral("frame_gen_target_rate|%1|%2")
                     .arg(tr("Target frame rate"), tr("Off uses the multiplier")),
                 QStringLiteral("frame_gen_flow_scale_auto|%1|%2")
                     .arg(tr("Flow scale"), tr("With Auto off, set the scale manually")),
                 QStringLiteral(">frame_gen_flow_scale"),
                 QStringLiteral("frame_gen_queue_target|%1").arg(tr("Queue target")),
                 QStringLiteral("frame_gen_flyhigh_protect_hud|%1|%2")
                     .arg(tr("Keep the HUD still"), tr("FlyHigh only"))}},
            AdvancedSection{tr("Display and HDR"),
                            tr("How the picture reaches your screen"),
                            1,
                            {QStringLiteral("fullscreen_mode|%1").arg(tr("Fullscreen mode")),
                             QStringLiteral("aspect_ratio|%1").arg(tr("Aspect ratio")),
                             QStringLiteral("max_anisotropy|%1").arg(tr("Anisotropic filtering")),
                             QStringLiteral(">hdr_swapchain|%1").arg(tr("HDR format")),
                             QStringLiteral(">hdr_reference_white|%1").arg(tr("Reference white")),
                             QStringLiteral(">hdr_peak_luminance|%1").arg(tr("Peak brightness"))}},
            AdvancedSection{tr("Upscaling details"),
                            tr("Quality of the Game upscaling choice"),
                            1,
                            {QString{}}},
            AdvancedSection{
                tr("Post-processing"),
                tr("Filters drawn over the game"),
                1,
                {QStringLiteral("post_shader_enabled|%1").arg(tr("Post-processing effects"))}},
            AdvancedSection{
                tr("Performance"),
                tr("Trade power or stability for speed"),
                1,
                {QStringLiteral("force_max_clock|%1|%2")
                     .arg(tr("Force maximum GPU clocks"), tr("Uses more power")),
                 QStringLiteral("gpu_unswizzle_enabled|%1").arg(tr("GPU unswizzle")),
                 QStringLiteral(">gpu_unswizzle_texture_size|%1").arg(tr("Max texture size")),
                 QStringLiteral(">gpu_unswizzle_stream_size|%1").arg(tr("Stream size")),
                 QStringLiteral(">gpu_unswizzle_chunk_size|%1").arg(tr("Chunk size")),
                 QStringLiteral("pin_gpu_threads|%1").arg(tr("Pin GPU threads to their own cores")),
                 QStringLiteral("render_thread|%1|%2")
                     .arg(tr("Render thread"), tr("Experimental"))}},
        },
        {QStringLiteral("game_dlss_network"), QStringLiteral("amd_fsr4"),
         QStringLiteral("anti_aliasing"), QStringLiteral("ocarina_fsr"),
         QStringLiteral("oot_anti_aliasing"), QStringLiteral("mkw_30fps"),
         QStringLiteral("mkw_60fps"), QStringLiteral("mkw_60fps_fallback"),
         QStringLiteral("mkw_dynamic_fps"), QStringLiteral("hyrule_dynamic_fps"),
         QStringLiteral("hyrule_30fps"), QStringLiteral("frame_gen_dump_flow"),
         QStringLiteral("use_asynchronous_gpu_emulation"), QStringLiteral("bg_button")});

    settings_page->SetAdvancedSections(
        audio,
        {AdvancedSection{
            tr("Output"),
            tr("Changing the engine rebuilds the available output-device list."),
            0,
            {QStringLiteral("output_engine|%1|%2")
                 .arg(tr("Output engine"), tr("Automatic selects the available audio backend.")),
             QStringLiteral("sound_index|%1|%2")
                 .arg(tr("Sound output mode"), tr("Mono, Stereo or Surround.")),
             QStringLiteral("muteWhenInBackground|%1|%2")
                 .arg(tr("Mute audio when in background"),
                      tr("Silence Lichii while another window has focus."))}}});

    settings_page->SetAdvancedSections(
        system_page,
        {
            AdvancedSection{
                tr("CPU"),
                tr("Leave on Auto unless a game asks for it"),
                0,
                {QStringLiteral("use_multi_core|%1|%2")
                     .arg(tr("Multicore CPU emulation"), tr("Off is for diagnosing crashes")),
                 QStringLiteral("memory_layout_mode|%1|%2")
                     .arg(tr("Memory layout"), tr("Some mods need 6 or 8 GB")),
                 QStringLiteral("cpu_accuracy|%1").arg(tr("Accuracy")),
                 QStringLiteral("cpu_backend|%1").arg(tr("Backend")),
                 QStringLiteral("cpu_ticks|%1").arg(tr("Custom CPU ticks")),
                 QStringLiteral(">cpuopt_unsafe_host_mmu|%1")
                     .arg(tr("Host MMU emulation (fastmem)")),
                 QStringLiteral(">cpuopt_unsafe_unfuse_fma|%1").arg(tr("Unfuse FMA")),
                 QStringLiteral(">cpuopt_unsafe_reduce_fp_error|%1")
                     .arg(tr("Faster FRSQRTE and FRECPE")),
                 QStringLiteral(">cpuopt_unsafe_ignore_standard_fpcr|%1")
                     .arg(tr("Faster ASIMD (32-bit)")),
                 QStringLiteral(">cpuopt_unsafe_inaccurate_nan|%1")
                     .arg(tr("Inaccurate NaN handling")),
                 QStringLiteral(">cpuopt_unsafe_fastmem_check|%1")
                     .arg(tr("Skip address space checks")),
                 QStringLiteral(">cpuopt_unsafe_ignore_global_monitor|%1")
                     .arg(tr("Ignore global monitor"))}},
            AdvancedSection{
                tr("Speed"),
                tr("Speed limits and the Turbo and Slow hotkeys"),
                0,
                {QStringLiteral("speed_limit|%1").arg(tr("Limit speed")),
                 QStringLiteral("turbo_speed_limit|%1").arg(tr("Turbo speed")),
                 QStringLiteral("slow_speed_limit|%1").arg(tr("Slow speed")),
                 QStringLiteral("sync_core_speed|%1").arg(tr("Synchronise core speed"))}},
            AdvancedSection{tr("Performance tweaks"),
                            tr("Clock rates reported to the game"),
                            0,
                            {QStringLiteral("fast_cpu_time|%1").arg(tr("CPU clocks")),
                             QStringLiteral("fast_gpu_time|%1").arg(tr("GPU clocks"))}},
            AdvancedSection{tr("Console"),
                            tr("Identity games see; mostly for speedruns and TAS"),
                            1,
                            {QStringLiteral("rng_seed|%1").arg(tr("RNG seed")),
                             QStringLiteral("device_name|%1").arg(tr("Device name")),
                             QStringLiteral("custom_rtc|%1").arg(tr("Custom clock")),
                             QStringLiteral("time_zone_index|%1|%2")
                                 .arg(tr("Time zone"), tr("Follows this PC"))}},
            AdvancedSection{tr("Storage"),
                            tr("Moving these does not move the files"),
                            1,
                            {QStringLiteral("nand_directory_edit|%1").arg(tr("System (NAND)")),
                             QStringLiteral("sdmc_directory_edit|%1").arg(tr("SD card")),
                             QStringLiteral("save_directory_edit|%1").arg(tr("Save data")),
                             QStringLiteral("load_path_edit|%1").arg(tr("Mods (load)"))}},
            AdvancedSection{
                tr("Applets"),
                tr("Choose Custom frontend, Real applet or Disabled"),
                1,
                {QStringLiteral("controller_applet_mode|%1").arg(tr("Controller configuration")),
                 QStringLiteral("error_applet_mode|%1").arg(tr("Error")),
                 QStringLiteral("player_select_applet_mode|%1").arg(tr("Player select")),
                 QStringLiteral("swkbd_applet_mode|%1").arg(tr("Software keyboard")),
                 QStringLiteral("mii_edit_applet_mode|%1").arg(tr("Mii Edit")),
                 QStringLiteral("web_applet_mode|%1").arg(tr("Online web")),
                 QStringLiteral("offline_web_applet_mode|%1").arg(tr("Offline web")),
                 QStringLiteral("photo_viewer_applet_mode|%1").arg(tr("Photo viewer")),
                 QStringLiteral("cabinet_applet_mode|%1").arg(tr("Amiibo editor")),
                 QStringLiteral("enable_overlay|%1").arg(tr("Enable Overlay Applet"))}},
            AdvancedSection{
                tr("Profiles"), tr("Who plays"), 1, {QStringLiteral("select_user_on_boot")}},
        },
        {QStringLiteral("data_erase_applet_mode"), QStringLiteral("net_connect_applet_mode"),
         QStringLiteral("shop_applet_mode"), QStringLiteral("login_share_applet_mode"),
         QStringLiteral("wifi_web_auth_applet_mode"), QStringLiteral("my_page_applet_mode"),
         QStringLiteral("gamecard_inserted"), QStringLiteral("gamecard_current_game"),
         QStringLiteral("gamecard_path_edit"), QStringLiteral("gamecard_path_button"),
         QStringLiteral("dump_path_edit"), QStringLiteral("dump_path_button"),
         QStringLiteral("dump_nso"), QStringLiteral("dump_exefs"),
         QStringLiteral("cache_game_list"), QStringLiteral("reset_game_list_cache")});
    settings_page->SetAdvancedBuilder(system_page, [this, system_page] {
        if (auto* ask =
                SettingsPage::Editor(general_tab.get(), UISettings::values.select_user_on_boot)) {
            settings_page->AddAdvancedRow(system_page, QStringLiteral("select_user_on_boot"),
                                          tr("Ask for a profile when a game starts"), {},
                                          SettingsPage::BindSetting(ask));
        }
    });

    settings_page->SetAdvancedSections(
        multiplayer,
        {
            AdvancedSection{tr("Network"),
                            tr("Adapter changes are unavailable while a game runs"),
                            0,
                            {QStringLiteral("network_interface|%1").arg(tr("Network interface")),
                             QStringLiteral("nextendo_sync_profile|%1|%2")
                                 .arg(tr("Sync Nextendo name and picture to the profile"),
                                      tr("Requires an enabled, signed-in account"))}},
            AdvancedSection{tr("Public lobby"),
                            tr("Locked while hosting a public room"),
                            0,
                            {QStringLiteral("edit_username|%1|%2")
                                 .arg(tr("Lobby username"), tr("4–20 characters")),
                             QStringLiteral("edit_token")}},
            AdvancedSection{tr("Mod Store"),
                            tr("Where mods are listed from"),
                            0,
                            {QStringLiteral("edit_mod_store_api_url")}},
            AdvancedSection{tr("Custom server"),
                            tr("Host redirect is unavailable while a game runs"),
                            1,
                            {QStringLiteral("online_service_redirect"),
                             QStringLiteral(">redirect_hosts|%1").arg(tr("Host names")),
                             QStringLiteral(">redirect_server|%1").arg(tr("Server address")),
                             QStringLiteral(">redirect_ca|%1").arg(tr("CA certificate")),
                             QStringLiteral(">redirect_ca_browse"),
                             QStringLiteral(">redirect_loopback_only|%1")
                                 .arg(tr("Block all other network access"))}},
        },
        {QStringLiteral("nextendo_client_id"), QStringLiteral("nextendo_sign_in"),
         QStringLiteral("nextendo_sign_out"), QStringLiteral("nextendo_profile"),
         QStringLiteral("nextendo_friend_list"), QStringLiteral("nextendo_accept"),
         QStringLiteral("nextendo_decline"), QStringLiteral("nextendo_remove"),
         QStringLiteral("nextendo_add_code"), QStringLiteral("nextendo_add")});
    for (const char* name : {"edit_token", "button_generate", "edit_mod_store_api_url"}) {
        if (auto* widget = web_tab->findChild<QWidget*>(QLatin1String(name))) {
            widget->setProperty("spotlightBound", true);
        }
    }
    settings_page->SetAdvancedBuilder(multiplayer, [this, multiplayer] {
        auto* token = web_tab->findChild<QLineEdit*>(QStringLiteral("edit_token"));
        auto* generate = web_tab->findChild<QAbstractButton*>(QStringLiteral("button_generate"));
        if (token != nullptr && generate != nullptr) {
            auto* control = new QWidget;
            auto* layout = new QHBoxLayout(control);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            auto* field = SettingsPage::BindLineEdit(token);
            field->setFixedWidth(160);
            layout->addWidget(field);
            layout->addWidget(SettingsPage::BindButton(generate, tr("Generate")));
            settings_page->AddAdvancedRow(multiplayer, QStringLiteral("edit_token"), tr("Token"),
                                          tr("80 characters"), control);
        }
        if (auto* url = web_tab->findChild<QLineEdit*>(QStringLiteral("edit_mod_store_api_url"))) {
            auto* field = SettingsPage::BindLineEdit(url);
            field->setFixedWidth(180);
            settings_page->AddAdvancedRow(multiplayer, QStringLiteral("edit_mod_store_api_url"),
                                          tr("Mod Store API base URL"), {}, field);
        }
    });
    hotkeys_tab->hide();
    profile_tab->hide();
    input_tab->hide();
    settings_page->SetCategory(general);
}

void ConfigureDialog::BuildSystem(int system_page) {
    auto* console = settings_page->AddSection(system_page, tr("Console"));
    AddSettingRow(console, system_tab.get(), Settings::values.language_index, tr("Language"),
                  tr("The language games use"), QStringLiteral("translate"), Tints::Blue());
    AddSettingRow(console, system_tab.get(), Settings::values.region_index, tr("Region"), {},
                  QStringLiteral("globe"), Tints::Green());
    const int profiles = settings_page->AddSubPage(
        system_page, tr("Profiles"), tr("Local console users; games start as the current one"));
    settings_page->SetBuilder(profiles, [this, profiles] { BuildProfiles(profiles); });
    auto* manage = SettingsPage::Manage(tr("Manage"));
    connect(manage, &QAbstractButton::clicked, settings_page,
            [this, profiles] { settings_page->SetCategory(profiles); });
    auto* cards_section = settings_page->AddSection(system_page, tr("Profiles"), false, manage);
    auto* cards = new FlowRow;
    cards_section->addWidget(cards);
    const auto rebuild = [this, cards] {
        cards->Clear();
        auto* model = profile_tab->Model();
        const int current = Settings::values.current_user.GetValue();
        static const std::array tints{QColor(0xff, 0xb4, 0xc8), QColor(0xb9, 0xe8, 0xcf),
                                      QColor(0xc9, 0xd8, 0xff), QColor(0xff, 0xe0, 0xa8)};
        for (int row = 0; row < model->rowCount(); ++row) {
            const QStandardItem* user = model->item(row);
            const QString name = user->text().section(QChar::fromLatin1('\n'), 0, 0);
            auto* card = new ProfileCard(name, user->icon().pixmap(64, 64), row == current,
                                         tints[static_cast<std::size_t>(row) % tints.size()]);
            card->menu = [this, card, row](const QPoint& at) {
                auto* menu = new QMenu(card);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->addAction(tr("Edit name and picture"), this,
                                [this, row] { profile_tab->EditUserAt(row); });
                menu->addAction(tr("Delete"), this,
                                [this, row] { profile_tab->ConfirmDeleteUserAt(row); });
                menu->popup(at);
            };
            connect(card, &QAbstractButton::clicked, this, [this, card, row, current] {
                if (row == current) {
                    card->menu(card->mapToGlobal(QPoint(0, card->height())));
                } else {
                    profile_tab->SelectUserAt(row);
                }
            });
            cards->Add(card);
        }
        if (profile_tab->CanAddUser()) {
            auto* add = new ProfileCard(tr("New profile"), {}, false, {});
            add->setAccessibleName(tr("New profile"));
            connect(add, &QAbstractButton::clicked, this, [this] { profile_tab->AddUser(); });
            cards->Add(add);
        }
    };
    rebuild();
    auto* model = profile_tab->Model();
    connect(model, &QAbstractItemModel::rowsInserted, cards, rebuild);
    connect(model, &QAbstractItemModel::rowsRemoved, cards, rebuild);
    connect(model, &QAbstractItemModel::dataChanged, cards, rebuild);
    connect(profile_tab.get(), &ConfigureProfileManager::CurrentUserChanged, cards, rebuild);
    settings_page->AddBlock(
        system_page,
        SettingsPage::Note(tr("Manage profiles to edit names and avatars or delete a profile.")),
        tr("Manage profiles"));
}

void ConfigureDialog::BuildProfiles(int profiles) {
    profile_rows = settings_page->AddSection(profiles, tr("Users"));
    auto* actions = new QWidget;
    auto* actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(0, 4, 0, 0);
    actions_layout->setSpacing(14);
    add_profile = new Spotlight::PillButton(tr("Add a profile"), QStringLiteral("plus"),
                                            Spotlight::PillButton::Kind::Accent, 38);
    connect(add_profile, &QAbstractButton::clicked, this, [this] { profile_tab->AddUser(); });
    actions_layout->addWidget(add_profile);
    if (system.IsPoweredOn()) {
        actions_layout->addWidget(SettingsPage::Text(
            tr("Changes reach the running game after it restarts."), 13, 500, true));
    }
    actions_layout->addStretch();
    settings_page->AddBlock(profiles, actions, tr("Add a profile"));
    auto* model = profile_tab->Model();
    connect(model, &QAbstractItemModel::rowsInserted, this, &ConfigureDialog::RebuildProfiles);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &ConfigureDialog::RebuildProfiles);
    connect(model, &QAbstractItemModel::dataChanged, this, &ConfigureDialog::RebuildProfiles);
    connect(profile_tab.get(), &ConfigureProfileManager::CurrentUserChanged, this,
            &ConfigureDialog::RebuildProfiles);
    RebuildProfiles();
}

void ConfigureDialog::RebuildProfiles() {
    while (auto* item = profile_rows->takeAt(0)) {
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    auto* model = profile_tab->Model();
    const int current = Settings::values.current_user.GetValue();
    for (int row = 0; row < model->rowCount(); ++row) {
        const QStandardItem* user = model->item(row);
        const QStringList lines = user->text().split(QChar::fromLatin1('\n'));
        auto* control = new QWidget;
        auto* layout = new QHBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        if (row == current) {
            layout->addWidget(
                SettingsPage::Badge(tr("Current"), Color::Accent(), Color::OnAccent()), 0,
                Qt::AlignVCenter);
        } else {
            auto* use = new Spotlight::PillButton(tr("Make current"));
            connect(use, &QAbstractButton::clicked, this,
                    [this, row] { profile_tab->SelectUserAt(row); });
            layout->addWidget(use);
        }
        auto* more = new Spotlight::CircleButton(QStringLiteral("dots-three"), 32);
        more->setAccessibleName(tr("More for %1").arg(lines.value(0)));
        auto* menu = new QMenu(more);
        menu->addAction(tr("Edit name and picture"), this,
                        [this, row] { profile_tab->EditUserAt(row); });
        menu->addAction(tr("Delete"), this, [this, row] { profile_tab->ConfirmDeleteUserAt(row); });
        connect(more, &QAbstractButton::clicked, menu,
                [menu, more] { menu->popup(more->mapToGlobal(QPoint(0, more->height()))); });
        layout->addWidget(more);
        SettingsPage::AddRow(profile_rows, lines.value(0), lines.value(1), QStringLiteral("user"),
                             Tints::Lavender(), control, {}, user->icon().pixmap(64, 64));
    }
    add_profile->setEnabled(profile_tab->CanAddUser());
}

void ConfigureDialog::BuildShortcuts(int shortcuts) {
    auto* actions = new QWidget;
    actions->setProperty("spotlightTransparent", true);
    auto* actions_layout = new QHBoxLayout(actions);
    actions_layout->setContentsMargins(0, 0, 0, 0);
    actions_layout->setSpacing(10);
    actions_layout->addStretch();
    auto* clear_all = new Spotlight::PillButton(tr("Clear all"));
    connect(clear_all, &QAbstractButton::clicked, this, [this] { hotkeys_tab->ClearAll(); });
    actions_layout->addWidget(clear_all);
    auto* restore = new Spotlight::PillButton(tr("Restore defaults"),
                                              QStringLiteral("arrow-counter-clockwise"));
    connect(restore, &QAbstractButton::clicked, this, [this] { hotkeys_tab->RestoreDefaults(); });
    actions_layout->addWidget(restore);
    settings_page->AddBlock(shortcuts, actions, tr("Clear all\nRestore defaults"));

    const std::array<QString, 4> titles{tr("Playing"), tr("Display, speed and audio"),
                                        tr("Window and library"), tr("Multiplayer and TAS")};
    static const std::vector<std::pair<QString, int>> order{
        {QStringLiteral("Continue/Pause Emulation"), 0},
        {QStringLiteral("Stop Emulation"), 0},
        {QStringLiteral("Restart Emulation"), 0},
        {QStringLiteral("Fullscreen"), 0},
        {QStringLiteral("Exit Fullscreen"), 0},
        {QStringLiteral("HOME Button"), 0},
        {QStringLiteral("In-game Menu"), 0},
        {QStringLiteral("Capture Screenshot"), 0},
        {QStringLiteral("Load/Remove Amiibo"), 0},
        {QStringLiteral("Toggle Mouse Panning"), 0},
        {QStringLiteral("Change Adapting Filter"), 1},
        {QStringLiteral("Change Docked Mode"), 1},
        {QStringLiteral("Change GPU Mode"), 1},
        {QStringLiteral("Toggle Framerate Limit"), 1},
        {QStringLiteral("Toggle Turbo Speed"), 1},
        {QStringLiteral("Toggle Slow Speed"), 1},
        {QStringLiteral("Toggle Performance Overlay"), 1},
        {QStringLiteral("Audio Mute/Unmute"), 1},
        {QStringLiteral("Audio Volume Down"), 1},
        {QStringLiteral("Audio Volume Up"), 1},
        {QStringLiteral("Browse Public Game Lobby"), 3},
        {QStringLiteral("Create Room"), 3},
        {QStringLiteral("Direct Connect to Room"), 3},
        {QStringLiteral("Leave Room"), 3},
        {QStringLiteral("Show Current Room"), 3},
        {QStringLiteral("TAS Start/Stop"), 3},
        {QStringLiteral("TAS Reset"), 3},
        {QStringLiteral("TAS Record"), 3},
    };
    static const QHash<QString, QString> pad_names{
        {QStringLiteral("Dpad_Right"), QStringLiteral("D-pad →")},
        {QStringLiteral("Dpad_Left"), QStringLiteral("D-pad ←")},
        {QStringLiteral("Dpad_Up"), QStringLiteral("D-pad ↑")},
        {QStringLiteral("Dpad_Down"), QStringLiteral("D-pad ↓")},
    };
    std::array<std::vector<std::pair<std::size_t, QPersistentModelIndex>>, 4> members;
    auto* model = hotkeys_tab->Model();
    for (int group_row = 0; group_row < model->rowCount(); ++group_row) {
        const QStandardItem* group = model->item(group_row);
        for (int row = 0; row < group->rowCount(); ++row) {
            const QStandardItem* action = group->child(row);
            const auto found =
                std::find_if(order.cbegin(), order.cend(), [action](const auto& entry) {
                    return entry.first == action->data().toString();
                });
            const auto rank = static_cast<std::size_t>(found - order.cbegin());
            members[found == order.cend() ? 2 : static_cast<std::size_t>(found->second)]
                .emplace_back(rank, action->index());
        }
    }
    for (std::size_t group = 0; group < titles.size(); ++group) {
        if (members[group].empty()) {
            continue;
        }
        std::stable_sort(members[group].begin(), members[group].end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        auto* rows = settings_page->AddSection(shortcuts, titles[group], false, nullptr,
                                               static_cast<int>(group % 2));
        rows->parentWidget()->setProperty("denseRows", true);
        for (const auto& [rank, action] : members[group]) {
            auto* control = new QWidget;
            auto* layout = new QHBoxLayout(control);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            auto* keyboard = new BindingButton({}, 98);
            auto* pad = new BindingButton(QStringLiteral("game-controller"), 133);
            auto* reset =
                new Spotlight::CircleButton(QStringLiteral("arrow-counter-clockwise"), 26);
            reset->setToolTip(tr("Restore default"));
            reset->setAccessibleName(reset->toolTip());
            reset->setFocusPolicy(Qt::StrongFocus);
            layout->addWidget(keyboard);
            layout->addWidget(pad);
            layout->addWidget(reset);
            const auto refresh = [this, action, keyboard, pad, reset] {
                const QString key = Spotlight::KeyText(QKeySequence::fromString(
                    action.sibling(action.row(), 1).data().toString(), QKeySequence::NativeText));
                QStringList buttons = action.sibling(action.row(), 2)
                                          .data()
                                          .toString()
                                          .split(QLatin1Char('+'), Qt::SkipEmptyParts);
                for (QString& button : buttons) {
                    button = pad_names.value(button, button);
                }
                const QString pad_text = buttons.join(QStringLiteral(" + "));
                keyboard->SetBinding(key.isEmpty() ? QStringList{tr("Not set")}
                                                   : key.split(QLatin1Char(' ')),
                                     !key.isEmpty());
                keyboard->setToolTip(tr("Keyboard: %1").arg(key.isEmpty() ? tr("not set") : key));
                keyboard->setAccessibleName(keyboard->toolTip());
                pad->SetBinding({pad_text.isEmpty() ? tr("Not set") : pad_text},
                                !pad_text.isEmpty());
                pad->setToolTip(
                    tr("Controller: %1").arg(pad_text.isEmpty() ? tr("not set") : pad_text));
                pad->setAccessibleName(pad->toolTip());
                reset->setEnabled(!hotkeys_tab->IsDefault(action));
            };
            refresh();
            connect(model, &QAbstractItemModel::dataChanged, control, refresh);
            for (const auto& [button, column] : {std::pair{keyboard, 1}, std::pair{pad, 2}}) {
                connect(button, &QAbstractButton::clicked, this, [this, action, column] {
                    hotkeys_tab->Configure(action.sibling(action.row(), column));
                });
                button->menu = [this, button, action, column](const QPoint& at) {
                    auto* menu = new QMenu(button);
                    menu->setAttribute(Qt::WA_DeleteOnClose);
                    menu->addSection(tr("Binding actions"));
                    menu->addAction(tr("Edit"), this, [this, action, column] {
                        hotkeys_tab->Configure(action.sibling(action.row(), column));
                    });
                    menu->addAction(tr("Clear"), this, [this, action, column] {
                        hotkeys_tab->Model()->setData(action.sibling(action.row(), column),
                                                      QString{});
                    });
                    menu->addAction(tr("Restore default"), this,
                                    [this, action] { hotkeys_tab->RestoreAction(action); });
                    menu->popup(at);
                };
            }
            connect(reset, &QAbstractButton::clicked, this,
                    [this, action] { hotkeys_tab->RestoreAction(action); });
            SettingsPage::AddRow(rows, action.data().toString(), {}, {}, {}, control);
        }
    }
    settings_page->AddBlock(
        shortcuts,
        SettingsPage::Note(tr("Press a controller chord while its binding is waiting. A timeout or "
                              "conflict keeps the prior binding.")),
        tr("Controller chord"));
}

void ConfigureDialog::AddSettingRow(QVBoxLayout* group, QWidget* page,
                                    const Settings::BasicSetting& setting, const QString& title,
                                    const QString& detail, const QString& icon,
                                    const QColor& color) {
    if (auto* source = SettingsPage::Editor(page, setting)) {
        if (auto* control = SettingsPage::BindSetting(source)) {
            SettingsPage::AddRow(group, title, detail, icon, color, control);
        }
    }
}

ConfigureDialog::PresetSources ConfigureDialog::FindPresetSources() const {
    const auto& values = Settings::values;
    auto* gfx = graphics_tab.get();
    auto* resolution = SettingsPage::Editor(gfx, values.resolution_setup);
    auto* filter = SettingsPage::Editor(gfx, values.scaling_filter);
    auto* aa = SettingsPage::Editor(gfx, values.anti_aliasing);
    auto* frame_gen = SettingsPage::Editor(gfx, values.frame_gen);
    auto* device = SettingsPage::Editor(gfx, values.vulkan_device);
    if (!resolution || !filter || !aa || !frame_gen) {
        return {};
    }
    return {resolution->combobox, filter->combobox, aa->combobox, frame_gen->checkbox,
            device ? device->combobox : nullptr};
}

bool ConfigureDialog::PresetSources::CanApply() const {
    return resolution->isEnabled() && filter->isEnabled() && aa->isEnabled() &&
           frame_generation->isEnabled();
}

const VkDeviceInfo::Record* ConfigureDialog::SelectedGpu(const PresetSources& sources) const {
    const int index = sources.device ? sources.device->currentIndex() : -1;
    return index >= 0 && std::cmp_less(index, devices.size()) ? &devices[index] : nullptr;
}

int ConfigureDialog::CurrentPreset(const PresetSources& sources) const {
    const int selected = Spotlight::PerformancePresetIndex({
        static_cast<Settings::ResolutionSetup>(sources.resolution->currentData().toUInt()),
        static_cast<Settings::ScalingFilter>(sources.filter->currentData().toUInt()),
        static_cast<Settings::AntiAliasing>(sources.aa->currentData().toUInt()),
        sources.frame_generation->isChecked(),
    });
    if (selected == -1) {
        return -1;
    }
    return selected == Spotlight::SuggestedPresetTier(SelectedGpu(sources)) ? 0 : selected + 1;
}

void ConfigureDialog::ApplyPreset(const PresetSources& sources, int selection) {
    const auto& preset =
        Spotlight::PerformancePresets[selection == 0
                                          ? Spotlight::SuggestedPresetTier(SelectedGpu(sources))
                                          : selection - 1];
    SettingsPage::SetComboIndex(sources.resolution,
                                sources.resolution->findData(static_cast<u32>(preset.resolution)));
    SettingsPage::SetComboIndex(sources.filter,
                                sources.filter->findData(static_cast<u32>(preset.filter)));
    SettingsPage::SetComboIndex(sources.aa,
                                sources.aa->findData(static_cast<u32>(preset.anti_aliasing)));
    if (sources.frame_generation->isChecked() != preset.frame_generation) {
        sources.frame_generation->click();
    }
}

void ConfigureDialog::WatchPreset(const PresetSources& sources, QObject* context,
                                  const std::function<void()>& sync) {
    for (auto* combo : {sources.resolution, sources.filter, sources.aa}) {
        connect(combo, &QComboBox::currentIndexChanged, context, sync);
    }
    connect(sources.frame_generation, &QCheckBox::toggled, context, sync);
    if (sources.device != nullptr) {
        connect(sources.device, &QComboBox::currentIndexChanged, context, sync);
    }
    sync();
}

void ConfigureDialog::BuildGeneral(int general, int library, int controls) {
    const auto sources = FindPresetSources();
    QList<PresetCard*> presets;
    if (sources.resolution != nullptr) {
        auto* preset_holder = new QWidget(this);
        preset_holder->hide();
        const std::array cards{
            new PresetCard(tr("Auto"), tr("Picks for your GPU"), preset_holder),
            new PresetCard(Spotlight::PerformancePresetName(0), tr("Older or integrated GPUs"),
                           preset_holder),
            new PresetCard(Spotlight::PerformancePresetName(1), tr("Most gaming laptops"),
                           preset_holder),
            new PresetCard(Spotlight::PerformancePresetName(2), tr("Desktop GPUs"), preset_holder),
        };
        for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
            connect(cards[i], &QAbstractButton::clicked, this, [this, sources, cards, i] {
                ApplyPreset(sources, i);
                cards[i]->setChecked(CurrentPreset(sources) == i);
            });
        }
        presets = {cards.cbegin(), cards.cend()};
        WatchPreset(sources, preset_holder, [this, sources, cards] {
            const auto* gpu = SelectedGpu(sources);
            cards[0]->SetSuggestion(
                gpu ? QString::fromStdString(gpu->name) : tr("No GPU identified"),
                tr(" → %1").arg(
                    Spotlight::PerformancePresetName(Spotlight::SuggestedPresetTier(gpu))));
            const int selected = CurrentPreset(sources);
            const bool enabled = sources.CanApply();
            for (int i = 0; i < static_cast<int>(cards.size()); ++i) {
                cards[i]->setChecked(i == selected);
                cards[i]->setEnabled(enabled);
            }
        });
    }

    auto* view = ui_tab.get();
    auto* look_rows = settings_page->AddSection(general, tr("Look and language"));
    auto* theme = view->findChild<QComboBox*>(QStringLiteral("theme_combobox"));
    const int dark_index = theme->findData(QStringLiteral("lichii"));
    const int light_index = theme->findData(QStringLiteral("default"));
    const QStringList looks{tr("Dark"), tr("Light")};
    auto* look =
        new Spotlight::SegmentedControl(looks, Spotlight::SegmentedControl::Kind::Accent, 38);
    const auto sync_theme = [look, theme, dark_index, light_index, looks] {
        const QSignalBlocker blocker(look);
        const int selected = theme->currentIndex() == dark_index    ? 0
                             : theme->currentIndex() == light_index ? 1
                                                                    : 2;
        QStringList items = looks;
        if (selected == 2) {
            items.append(tr("Other"));
        }
        look->SetOptions(items);
        look->SetCurrentIndex(selected);
        look->SetOptionEnabled(2, false);
    };
    sync_theme();
    connect(theme, &QComboBox::currentIndexChanged, look, sync_theme);
    connect(look, &Spotlight::SegmentedControl::CurrentChanged, theme,
            [theme, dark_index, light_index](int index) {
                SettingsPage::SetComboIndex(theme, index == 0 ? dark_index : light_index);
            });
    SettingsPage::AddRow(look_rows, tr("Look"),
                         tr("Dark is the Spotlight look; Light follows the classic theme"),
                         QStringLiteral("palette"), Color::Accent(), look);
    auto* language = view->findChild<QComboBox*>(QStringLiteral("language_combobox"));
    auto* language_combo =
        SettingsPage::BindCombo(language, {}, [](const QComboBox& combo, int index) {
            const auto locale = combo.itemData(index).toString();
            if (locale.isEmpty()) {
                return tr("System");
            }
            const QLocale parsed(locale);
            return parsed.language() == QLocale::C ? combo.itemText(index)
                                                   : QLocale::languageToString(parsed.language());
        });
    language_combo->setMinimumWidth(170);
    SettingsPage::AddRow(look_rows, tr("Launcher language"),
                         tr("Menus and settings. Games use System › Language."),
                         QStringLiteral("translate"), Tints::Blue(), language_combo);

    auto* play_rows = settings_page->AddSection(general, tr("While you play"));
    AddSettingRow(play_rows, general_tab.get(), UISettings::values.pause_when_in_background,
                  tr("Pause when Lichii is in the background"),
                  tr("The game waits while you use another window"), QStringLiteral("pause"),
                  Tints::Gold());
    AddSettingRow(play_rows, general_tab.get(), UISettings::values.confirm_before_stopping,
                  tr("Ask before stopping a game"),
                  tr("Unsaved progress is lost when a game stops"), QStringLiteral("stop"),
                  Color::Accent());

    auto* privacy_rows = settings_page->AddSection(general, tr("Updates and privacy"));
#ifdef ENABLE_UPDATE_CHECKER
    AddSettingRow(privacy_rows, general_tab.get(), UISettings::values.check_for_updates,
                  tr("Check for updates when Lichii starts"), {}, QStringLiteral("arrow-circle-up"),
                  Tints::Green());
#endif
    if (auto* discord = web_tab->findChild<QCheckBox*>(QStringLiteral("toggle_discordrpc"))) {
        SettingsPage::AddRow(privacy_rows, tr("Show the current game in Discord"),
                             tr("Your friends see what you play"), QStringLiteral("chat-circle"),
                             Tints::Lavender(), SettingsPage::BindToggle(discord));
    }

    auto* footer = new QWidget;
    auto* footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(12);
    footer_layout->addWidget(
        SettingsPage::Note(
            tr("Find screenshots, diagnostics, Gamemode and Reset all settings under Advanced.")),
        1);
    auto* get_started = new Spotlight::PillButton(tr("Get Started"), QStringLiteral("sparkle"));
    get_started->setToolTip(tr("Run the setup again: keys, firmware, folders, controller, preset"));
    connect(get_started, &QAbstractButton::clicked, this,
            [this, controls, presets] { OpenGetStarted(controls, presets); });
    footer_layout->addWidget(get_started);
    settings_page->AddBlock(general, footer, tr("Get Started"));
    static_cast<void>(library);
}

void ConfigureDialog::BuildLibrary(int library) {
    auto* add_game = new Spotlight::PillButton(tr("Add folder"), QStringLiteral("plus"));
    connect(add_game, &QAbstractButton::clicked, this, &ConfigureDialog::AddGameFolder);
    folders_layout = settings_page->AddSection(library, tr("Game folders"), false, add_game);
    RebuildGameFolders();
    auto* external_list =
        general_tab->findChild<QListWidget*>(QStringLiteral("external_content_list"));
    auto* add_external =
        general_tab->findChild<QPushButton*>(QStringLiteral("add_external_dir_button"));
    auto* remove_external =
        general_tab->findChild<QPushButton*>(QStringLiteral("remove_external_dir_button"));
    auto* add_update = new Spotlight::PillButton(tr("Add folder"), QStringLiteral("plus"));
    connect(add_update, &QAbstractButton::clicked, add_external, &QAbstractButton::click);
    auto* external_rows =
        settings_page->AddSection(library, tr("Update and DLC folders"), false, add_update);
    const auto rebuild_external = [this, external_rows, external_list, remove_external] {
        while (auto* item = external_rows->takeAt(0)) {
            item->widget()->hide();
            item->widget()->deleteLater();
            delete item;
        }
        for (int i = 0; i < external_list->count(); ++i) {
            const QString path = external_list->item(i)->text();
            auto* more = new Spotlight::CircleButton(QStringLiteral("dots-three"), 32);
            more->setAccessibleName(tr("More for %1").arg(path));
            auto* menu = new QMenu(more);
            menu->addAction(tr("Open location"), this,
                            [this, path] { emit OpenGameFolderRequested(path); });
            menu->addSeparator();
            menu->addAction(tr("Remove folder"), external_list,
                            [external_list, remove_external, path] {
                                const auto found = external_list->findItems(path, Qt::MatchExactly);
                                if (!found.isEmpty()) {
                                    external_list->setCurrentItem(found.first());
                                    remove_external->click();
                                }
                            });
            connect(more, &QAbstractButton::clicked, menu,
                    [menu, more] { menu->popup(more->mapToGlobal(QPoint(0, more->height()))); });
            SettingsPage::AddRow(external_rows, QDir::toNativeSeparators(path),
                                 tr("Updates and DLC here apply without installing"),
                                 QStringLiteral("cloud-arrow-down"), Color::Accent(), more);
        }
        if (external_list->count() == 0) {
            SettingsPage::AddRow(external_rows, tr("No update and DLC folders"),
                                 tr("Lichii reads updates and DLC from them without installing"),
                                 QStringLiteral("cloud-arrow-down"), Color::Accent(), nullptr);
        }
    };
    rebuild_external();
    auto* external_model = external_list->model();
    for (const auto signal :
         {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(external_model, signal, external_rows->parentWidget(), rebuild_external);
    }
    connect(external_model, &QAbstractItemModel::modelReset, external_rows->parentWidget(),
            rebuild_external);
    settings_page->AddBlock(library,
                            SettingsPage::Note(tr("Removing a folder does not delete its files.")),
                            tr("Removing a folder"));
}

void ConfigureDialog::BuildGraphics(int graphics) {
    auto& values = Settings::values;
    auto* gfx = graphics_tab.get();
    auto* quick = settings_page->AddSection(graphics);
    const auto sources = FindPresetSources();
    if (sources.resolution != nullptr) {
        auto* presets_control = new Spotlight::SegmentedControl(
            {tr("Auto"), tr("Low"), tr("Mid"), tr("High")},
            Spotlight::SegmentedControl::Kind::Accent, 38);
        auto* preset_summary =
            SettingsPage::AddRow(quick, tr("Preset"), {}, QStringLiteral("sliders-horizontal"),
                                 Color::Accent(), presets_control);
        connect(presets_control, &Spotlight::SegmentedControl::CurrentChanged, this,
                [this, sources](int selection) { ApplyPreset(sources, selection); });
        WatchPreset(sources, presets_control, [this, sources, presets_control, preset_summary] {
            const int selected = CurrentPreset(sources);
            const QSignalBlocker blocker(presets_control);
            if (selected == -1) {
                presets_control->ClearSelection();
            } else {
                presets_control->SetCurrentIndex(selected);
            }
            presets_control->setEnabled(sources.CanApply());
            const int tier =
                selected == 0 ? Spotlight::SuggestedPresetTier(SelectedGpu(sources)) : selected - 1;
            const QString resolution = SettingsPage::ClosedSettingLabel(
                sources.resolution, Settings::values.resolution_setup.Id(),
                sources.resolution->currentIndex());
            const QString filter = SettingsPage::ClosedSettingLabel(
                sources.filter, Settings::values.scaling_filter.Id(),
                sources.filter->currentIndex());
            preset_summary->setText(
                selected == -1 ? tr("Custom graphics settings")
                               : tr("%1: %2 resolution, %3, %4. Game overrides stay.")
                                     .arg(Spotlight::PerformancePresetName(tier), resolution,
                                          filter, sources.aa->currentText()));
            preset_summary->show();
        });
    }
    AddSettingRow(quick, gfx, values.fps_limit, tr("FPS limit"),
                  tr("Auto follows the game. The cap does not change a game's native frame rate."),
                  QStringLiteral("lightning"), Tints::Green());
    AddSettingRow(quick, gfx, values.vsync_mode, tr("VSync mode"), {}, QStringLiteral("clock"),
                  Tints::Blue());

    auto* picture = settings_page->AddSection(graphics, tr("Picture"));
    if (auto* resolution = SettingsPage::Editor(gfx, values.resolution_setup)) {
        auto* detail =
            SettingsPage::AddRow(picture, tr("Resolution"), {}, QStringLiteral("monitor"),
                                 Tints::Gold(), SettingsPage::BindSetting(resolution));
        const auto describe = [detail, combo = resolution->combobox] {
            Settings::ResolutionScalingInfo info{};
            Settings::TranslateResolutionInfo(
                static_cast<Settings::ResolutionSetup>(combo->currentData().toUInt()), info);
            detail->setText(tr("%1× native · %2p handheld / %3p docked")
                                .arg(info.up_factor)
                                .arg(qRound(720 * info.up_factor))
                                .arg(qRound(1080 * info.up_factor)));
            detail->show();
        };
        describe();
        connect(resolution->combobox, &QComboBox::currentIndexChanged, detail, describe);
    }
    if (auto* filter = SettingsPage::Editor(gfx, values.scaling_filter)) {
        auto* control = SettingsPage::BindSetting(filter);
        if (auto* sharpness = SettingsPage::Editor(gfx, values.fsr_sharpening_slider)) {
            auto* line = new QWidget(control);
            line->setObjectName(QStringLiteral("detailLine"));
            line->setProperty("spotlightTransparent", true);
            auto* line_layout = new QHBoxLayout(line);
            line_layout->setContentsMargins(0, 4, 0, 0);
            line_layout->setSpacing(14);
            line_layout->addWidget(SettingsPage::Text(tr("RCAS sharpness"), 12, 500, true));
            auto* slider = SettingsPage::BindSetting(sharpness);
            slider->setFixedWidth(240);
            line_layout->addWidget(slider);
            line_layout->addStretch();
            new VisibilityFollower(sharpness, line);
        }
        SettingsPage::AddRow(picture, tr("Scaling filter"), tr("How the image fits the window"),
                             QStringLiteral("stack"), Tints::Blue(), control);
    }
    if (auto* aa = SettingsPage::Editor(gfx, values.anti_aliasing); aa && aa->combobox) {
        QStringList names;
        for (int i = 0; i < aa->combobox->count(); ++i) {
            names.append(aa->combobox->itemText(i));
        }
        auto* segments =
            new Spotlight::SegmentedControl(names, Spotlight::SegmentedControl::Kind::Accent, 38);
        const auto sync = [segments, combo = aa->combobox] {
            const QSignalBlocker blocker(segments);
            segments->SetCurrentIndex(combo->currentIndex());
            segments->setEnabled(combo->isEnabled());
        };
        sync();
        connect(aa->combobox, &QComboBox::currentIndexChanged, segments, sync);
        connect(segments, &Spotlight::SegmentedControl::CurrentChanged, aa->combobox,
                [combo = aa->combobox](int index) { SettingsPage::SetComboIndex(combo, index); });
        SettingsPage::AddRow(picture, tr("Anti-aliasing"), {}, QStringLiteral("paint-brush"),
                             Tints::Lavender(), segments);
    }
    QList<std::pair<QString, QCheckBox*>> upscalers;
    for (const auto& [setting, name] : {std::pair<const Settings::BasicSetting*, QString>{
                                            &values.game_dlss_network, tr("Game DLSS")},
                                        {&values.nvidia_dlss, tr("NVIDIA DLSS")},
                                        {&values.amd_fsr4, tr("FSR 4")}}) {
        if (auto* editor = SettingsPage::Editor(gfx, *setting);
            editor && editor->checkbox && editor->isVisibleTo(gfx)) {
            upscalers.append({name, editor->checkbox});
        }
    }
    if (!upscalers.isEmpty()) {
        SettingsPage::AddRow(picture, tr("Game upscaling"),
                             tr("Replaces the upscaler a game ships with. Only what this PC "
                                "supports."),
                             QStringLiteral("magic-wand"), Color::Accent(),
                             new ToggleSegments(upscalers));
    }

    auto* smooth = settings_page->AddSection(graphics, tr("Smoothness and display"));
    if (auto* frame = SettingsPage::Editor(gfx, values.frame_gen); frame && frame->checkbox) {
        auto* control = new QWidget;
        auto* layout = new QHBoxLayout(control);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        for (const auto& [setting, name] :
             {std::pair<const Settings::BasicSetting*, QString>{&values.frame_gen_type, tr("Type")},
              {&values.frame_gen_multiplier, tr("Multiplier")}}) {
            auto* editor = SettingsPage::Editor(gfx, *setting);
            if (editor == nullptr) {
                continue;
            }
            auto* column = new QWidget;
            column->setProperty("spotlightTransparent", true);
            auto* column_layout = new QVBoxLayout(column);
            column_layout->setContentsMargins(0, 0, 0, 0);
            column_layout->setSpacing(6);
            column_layout->addWidget(SettingsPage::Text(name, 12, 600, true));
            column_layout->addWidget(SettingsPage::BindSetting(editor));
            new VisibilityFollower(editor, column);
            layout->addWidget(column, 0, Qt::AlignBottom);
        }
        layout->addWidget(SettingsPage::BindSetting(frame), 0, Qt::AlignBottom);
        SettingsPage::AddRow(smooth, tr("Frame generation"), {}, QStringLiteral("film-strip"),
                             Tints::Lavender(), control);
    }
    AddSettingRow(smooth, gfx, values.hdr_mode, tr("HDR output"),
                  tr("Unavailable without an HDR-capable display and Vulkan."),
                  QStringLiteral("sun"), Tints::Gold());
    start_fullscreen = new Spotlight::ToggleSwitch;
    start_fullscreen->setChecked(UISettings::values.fullscreen.GetValue());
    start_fullscreen->setEnabled(!system.IsPoweredOn());
    SettingsPage::AddRow(smooth, tr("Fullscreen when a game starts"), {},
                         QStringLiteral("arrows-out"), Color::Accent(), start_fullscreen);
}

void ConfigureDialog::EnsureInput() {
    if (input_initialized) {
        return;
    }
    input_initialized = true;
    input_tab->Initialize(input_subsystem);
}

void ConfigureDialog::BuildControls(int category, int shortcuts) {
    EnsureInput();
    const auto pages = input_tab->GetSubTabs();
    auto* input_tabs = input_tab->PlayerTabs();
    const int mapping = settings_page->AddSubPage(
        category, tr("Map buttons"), tr("Click a binding, then press the button on your controller"));
    mapping_page = mapping;
    auto* player_pages = new QStackedWidget;
    QStringList player_names;
    QList<QCheckBox*> connected;
    const auto& players = Settings::values.players.GetValue();
    vibration_strength.clear();
    const auto configure_pair = [](QGroupBox* group, QPushButton* configure) {
        auto* manage = SettingsPage::Manage(tr("Configure"));
        manage->setEnabled(group->isChecked());
        connect(group, &QGroupBox::toggled, manage, &QWidget::setEnabled);
        connect(manage, &QAbstractButton::clicked, configure, &QPushButton::click);
        auto* pair = new QWidget;
        pair->setProperty("spotlightTransparent", true);
        auto* layout = new QHBoxLayout(pair);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        layout->addWidget(manage);
        layout->addWidget(SettingsPage::BindToggle(group));
        return pair;
    };
    auto* motion_group = input_tab->findChild<QGroupBox*>(QStringLiteral("motionGroup"));
    auto* motion_button = input_tab->findChild<QPushButton*>(QStringLiteral("motionButton"));
    for (int i = 0; i < 8; ++i) {
        player_names.append(tr("P%1").arg(i + 1));
        auto* state = input_tab->PlayerConnected(i);
        connected.append(state);
        vibration_strength.append(players[i].vibration_strength);
        auto* type = pages[i]->findChild<QComboBox*>(QStringLiteral("comboControllerType"));
        auto* profile = pages[i]->findChild<QComboBox*>(QStringLiteral("comboProfiles"));
        auto* card = SettingsPage::Card();
        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(1, 1, 1, 1);
        card_layout->setSpacing(0);
        auto* header = new QWidget;
        header->setProperty("spotlightTransparent", true);
        auto* header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(18, 18, 18, 18);
        header_layout->setSpacing(20);
        auto* picture = new QLabel;
        picture->setFixedSize(150, 110);
        picture->setAlignment(Qt::AlignCenter);
        picture->setPixmap(Spotlight::IconPixmap(QStringLiteral("game-controller"), 72,
                                                 Color::Text(), devicePixelRatioF()));
        picture->setStyleSheet(
            QStringLiteral("QLabel { background: rgba(255,255,255,20); border-radius: 24px; }"));
        header_layout->addWidget(picture, 0, Qt::AlignVCenter);
        auto* detail = new QVBoxLayout;
        detail->setSpacing(4);
        detail->addWidget(SettingsPage::Text(tr("Player %1").arg(i + 1), 13, 600, true));
        auto* controller = SettingsPage::Text(type->currentText(), 24, 600);
        controller->setFont(Spotlight::DisplayFont(24));
        connect(type, &QComboBox::currentTextChanged, controller, &QLabel::setText);
        detail->addWidget(controller);
        auto* status_row = new QHBoxLayout;
        status_row->setSpacing(7);
        auto* dot = new QLabel;
        dot->setFixedSize(8, 8);
        auto* connected_text = SettingsPage::Text({}, 13, 500, true);
        const auto sync_state = [connected_text, dot, state] {
            connected_text->setText(state->isChecked() ? tr("Connected") : tr("Disconnected"));
            const QString dot_color = state->isChecked() ? QStringLiteral("#3FD18B")
                                                         : QStringLiteral("rgba(255,255,255,70)");
            dot->setStyleSheet(
                QStringLiteral("QLabel { background: %1; border-radius: 4px; }").arg(dot_color));
        };
        sync_state();
        connect(state, &QAbstractButton::toggled, connected_text, sync_state);
        status_row->addWidget(dot, 0, Qt::AlignVCenter);
        status_row->addWidget(connected_text);
        status_row->addStretch();
        detail->addLayout(status_row);
        auto* actions = new QHBoxLayout;
        actions->setContentsMargins(0, 8, 0, 0);
        actions->setSpacing(8);
        auto* map_buttons = new Spotlight::PillButton(
            tr("Map buttons"), QStringLiteral("keyboard"), Spotlight::PillButton::Kind::Accent, 38);
        connect(map_buttons, &QAbstractButton::clicked, this, [this, i, mapping, input_tabs] {
            input_tabs->setCurrentIndex(i);
            settings_page->SetCategory(mapping);
        });
        actions->addWidget(map_buttons);
        auto* profiles = SettingsPage::BindCombo(
            profile, tr("Profile: none"), [](const QComboBox& combo, int index) {
                return tr("Profile: %1").arg(combo.itemText(index));
            });
        profiles->setAccessibleName(tr("Controller profile"));
        actions->addWidget(profiles);
        actions->addStretch();
        detail->addLayout(actions);
        header_layout->addLayout(detail, 1);
        card_layout->addWidget(header);

        auto* divider = new QWidget;
        divider->setFixedHeight(1);
        divider->setStyleSheet(QStringLiteral("QWidget { background: rgba(255,255,255,18); }"));
        auto* divider_row = new QHBoxLayout;
        divider_row->setContentsMargins(18, 0, 18, 0);
        divider_row->addWidget(divider);
        card_layout->addLayout(divider_row);
        auto* rows = new QVBoxLayout;
        rows->setSpacing(0);
        card_layout->addLayout(rows);
        SettingsPage::AddRow(rows, tr("Controller type"), tr("What the game sees"),
                             QStringLiteral("game-controller"), Tints::Green(),
                             SettingsPage::BindCombo(type));
        SettingsPage::AddRow(rows, tr("Motion"), tr("Gyro and touch from your controllers"),
                             QStringLiteral("arrows-out-cardinal"), Tints::Gold(),
                             configure_pair(motion_group, motion_button));
        auto* vibration = new QWidget;
        vibration->setProperty("spotlightTransparent", true);
        auto* vibration_layout = new QHBoxLayout(vibration);
        vibration_layout->setContentsMargins(0, 0, 0, 0);
        vibration_layout->setSpacing(6);
        auto* slider = new QSlider(Qt::Horizontal, vibration);
        slider->hide();
        slider->setRange(1, 150);
        slider->setValue(vibration_strength[i]);
        auto* strength = SettingsPage::Text(tr("%1%").arg(slider->value()), 13, 600);
        strength->setMinimumWidth(44);
        strength->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        connect(slider, &QSlider::valueChanged, strength, [this, i, strength](int value) {
            vibration_strength[i] = value;
            strength->setText(tr("%1%").arg(value));
        });
        vibration_layout->addWidget(SettingsPage::BindSlider(slider));
        vibration_layout->addWidget(strength);
        SettingsPage::AddRow(rows, tr("Vibration"), tr("Rumble strength for this player"),
                             QStringLiteral("vibrate"), Tints::Gold(), vibration);
        player_pages->addWidget(card);
    }

    auto* picker = new QWidget;
    auto* picker_layout = new QHBoxLayout(picker);
    picker_layout->setContentsMargins(0, 0, 0, 0);
    auto* player_picker = new Spotlight::SegmentedControl(
        player_names, Spotlight::SegmentedControl::Kind::Accent, 38);
    player_picker->setAccessibleName(tr("Player"));
    picker_layout->addWidget(player_picker, 0, Qt::AlignLeft);
    picker_layout->addStretch();
    auto* count = SettingsPage::Text({}, 13, 500, true);
    picker_layout->addWidget(count);
    const auto sync_count = [connected, count] {
        const int active = std::count_if(connected.cbegin(), connected.cend(),
                                         [](const auto* player) { return player->isChecked(); });
        count->setText(active == 1 ? tr("1 controller connected")
                                   : tr("%1 controllers connected").arg(active));
    };
    sync_count();
    for (auto* state : connected) {
        connect(state, &QAbstractButton::toggled, count, sync_count);
    }
    connect(player_picker, &Spotlight::SegmentedControl::CurrentChanged, player_pages,
            &QStackedWidget::setCurrentIndex);
    settings_page->AddBlock(category, picker, tr("Players"));
    settings_page->AddBlock(category, player_pages, tr("Controller and profile"));

    auto* hotkeys = settings_page->AddSection(category);
    auto* controllers = settings_page->AddSection(category, tr("All players"), true);
    auto* connected_toggles = new QStackedWidget;
    connected_toggles->setProperty("spotlightTransparent", true);
    for (auto* state : connected) {
        connected_toggles->addWidget(SettingsPage::BindToggle(state));
    }
    connect(player_picker, &Spotlight::SegmentedControl::CurrentChanged, connected_toggles,
            &QStackedWidget::setCurrentIndex);
    SettingsPage::AddRow(controllers, tr("Connected"),
                         tr("The player picked above; players connect in order"),
                         QStringLiteral("power"), Tints::Blue(), connected_toggles);
    SettingsPage::AddRow(
        controllers, tr("Vibration"), tr("Rumble on or off, and per-controller tests"),
        QStringLiteral("vibrate"), Tints::Lavender(),
        configure_pair(input_tab->findChild<QGroupBox*>(QStringLiteral("vibrationGroup")),
                       input_tab->findChild<QPushButton*>(QStringLiteral("vibrationButton"))));

    auto* docked = input_tab->findChild<QRadioButton*>(QStringLiteral("radioDocked"));
    auto* handheld = input_tab->findChild<QRadioButton*>(QStringLiteral("radioUndocked"));
    auto* mode = new Spotlight::SegmentedControl({tr("Docked"), tr("Handheld")},
                                                 Spotlight::SegmentedControl::Kind::Accent, 38);
    const auto sync_mode = [mode, docked] {
        const QSignalBlocker blocker(mode);
        mode->SetCurrentIndex(docked->isChecked() ? 0 : 1);
    };
    sync_mode();
    connect(docked, &QAbstractButton::toggled, mode, sync_mode);
    connect(mode, &Spotlight::SegmentedControl::CurrentChanged, this,
            [docked, handheld, sync_mode](int index) {
                auto* source = index == 0 ? docked : handheld;
                if (source->isEnabled()) {
                    source->click();
                }
                sync_mode();
            });
    SettingsPage::AddRow(controllers, tr("Console mode"), tr("Choose docked or handheld mode"),
                         QStringLiteral("monitor"), Tints::Blue(), mode);
    QStringList hotkey_names;
    for (const auto& [action, name] : {std::pair{"Continue/Pause Emulation", tr("Pause")},
                                       std::pair{"Stop Emulation", tr("Stop")},
                                       std::pair{"Fullscreen", tr("Fullscreen")},
                                       std::pair{"Toggle Performance Overlay", tr("Overlay")}}) {
        const auto sequence = registry.GetKeySequence("Main Window", action);
        if (!sequence.isEmpty()) {
            hotkey_names.append(
                QStringLiteral("%1 %2").arg(name, sequence.toString(QKeySequence::NativeText)));
        }
    }
    auto* edit_hotkeys = SettingsPage::Manage();
    connect(edit_hotkeys, &QAbstractButton::clicked, settings_page,
            [this, shortcuts] { settings_page->SetCategory(shortcuts); });
    SettingsPage::AddRow(hotkeys, tr("Hotkeys"),
                         hotkey_names.isEmpty() ? tr("Edit keyboard and controller shortcuts")
                                                : hotkey_names.join(QStringLiteral(", ")),
                         QStringLiteral("keyboard"), Tints::Gold(), edit_hotkeys);

    settings_page->SetBuilder(mapping, [this, mapping] {
        settings_page->AddBlock(mapping, input_tab->MappingPage(), tr("Map buttons"));
    });
    connect(input_tab.get(), &ConfigureInput::AdvancedRequested, this,
            [this, category, advanced_page = pages[8]] {
                settings_page->ShowAdvancedPage(category, advanced_page);
            });
    auto* colours = settings_page->AddSection(category, tr("Controller colours"), true);
    for (int i = 0; i < 8; ++i) {
        auto* swatches = new QWidget;
        auto* swatch_layout = new QHBoxLayout(swatches);
        swatch_layout->setContentsMargins(0, 0, 0, 0);
        swatch_layout->setSpacing(8);
        for (const auto& [part, name] : std::array{
                 std::pair{"left_body", tr("Left body")},
                 std::pair{"left_buttons", tr("Left buttons")},
                 std::pair{"right_body", tr("Right body")},
                 std::pair{"right_buttons", tr("Right buttons")},
             }) {
            auto* source = pages[8]->findChild<QPushButton*>(
                QStringLiteral("player%1_%2_button").arg(i + 1).arg(QLatin1String(part)));
            if (source != nullptr) {
                source->setProperty("spotlightBound", true);
                swatch_layout->addWidget(new ColourSwatch(source, name));
            }
        }
        SettingsPage::AddRow(colours, tr("Player %1").arg(i + 1),
                             tr("Left body, left buttons, right body, right buttons"),
                             QStringLiteral("palette"), Tints::Lavender(), swatches);
    }
    settings_page->AddPage(category, pages[8], tr("Input devices"));
    BuildMicrophone(category, static_cast<ConfigureMicrophone*>(pages[9]));
}

void ConfigureDialog::BuildMicrophone(int category, ConfigureMicrophone* microphone) {
    auto* mic_devices = microphone->findChild<QComboBox*>(QStringLiteral("microphone_device"));
    auto* refresh = microphone->findChild<QPushButton*>(QStringLiteral("microphone_refresh"));
    auto* volume = microphone->findChild<QSlider*>(QStringLiteral("microphone_volume"));
    auto* level = microphone->findChild<QProgressBar*>(QStringLiteral("microphone_level"));
    auto* test = microphone->findChild<QPushButton*>(QStringLiteral("microphone_test"));
    auto* status = microphone->findChild<QLabel*>(QStringLiteral("microphone_status"));
    auto* rows = settings_page->AddSection(category, tr("Microphone"), true);

    auto* device_control = new QWidget;
    auto* device_layout = new QHBoxLayout(device_control);
    device_layout->setContentsMargins(0, 0, 0, 0);
    device_layout->setSpacing(8);
    device_layout->addWidget(SettingsPage::BindCombo(mic_devices));
    auto* refresh_button = new Spotlight::CircleButton(QStringLiteral("arrow-clockwise"), 32);
    refresh_button->setToolTip(refresh->text());
    refresh_button->setAccessibleName(refresh_button->toolTip());
    refresh_button->setEnabled(refresh->isEnabled());
    connect(refresh_button, &QAbstractButton::clicked, refresh, &QAbstractButton::click);
    device_layout->addWidget(refresh_button);
    SettingsPage::AddRow(rows, tr("Input device"), tr("The microphone games hear"),
                         QStringLiteral("record"), Tints::Blue(), device_control);

    auto* volume_control = new QWidget;
    auto* volume_layout = new QHBoxLayout(volume_control);
    volume_layout->setContentsMargins(0, 0, 0, 0);
    volume_layout->setSpacing(10);
    volume_layout->addWidget(SettingsPage::BindSlider(volume));
    auto* volume_value = SettingsPage::Text(tr("%1%").arg(volume->value()), 13, 600);
    volume_value->setMinimumWidth(44);
    volume_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(volume, &QSlider::valueChanged, volume_value,
            [volume_value](int value) { volume_value->setText(tr("%1%").arg(value)); });
    volume_layout->addWidget(volume_value);
    SettingsPage::AddRow(rows, tr("Microphone volume"), tr("Gain applied to the input"),
                         QStringLiteral("speaker-high"), Tints::Blue(), volume_control);

    auto* level_control = new QWidget;
    auto* level_layout = new QHBoxLayout(level_control);
    level_layout->setContentsMargins(0, 0, 0, 0);
    level_layout->setSpacing(14);
    level_layout->addWidget(new LevelMeter(level), 0, Qt::AlignVCenter);
    auto* test_button = new Spotlight::PillButton(test->text());
    connect(test_button, &QAbstractButton::clicked, test, &QAbstractButton::click);
    level_layout->addWidget(test_button);
    const QString idle_detail = tr("Start a test and speak to check the input level");
    auto* detail = SettingsPage::AddRow(rows, tr("Input level"), idle_detail,
                                        QStringLiteral("sliders-horizontal"), Tints::Blue(),
                                        level_control);
    const auto sync = [test, status, test_button, detail, idle_detail] {
        test_button->setText(test->text());
        test_button->updateGeometry();
        detail->setText(status->text().isEmpty() ? idle_detail : status->text());
    };
    sync();
    connect(microphone, &ConfigureMicrophone::StateChanged, detail, sync);
}

void ConfigureDialog::RebuildGameFolders() {
    while (auto* item = folders_layout->takeAt(0)) {
        item->widget()->hide();
        item->widget()->deleteLater();
        delete item;
    }
    for (int i = 0; i < game_dirs.size(); ++i) {
        const auto& folder = game_dirs[i];
        const bool built_in = IsBuiltInFolder(folder);
        const QString path = QString::fromStdString(folder.path);
        auto* menu_button = new Spotlight::CircleButton(QStringLiteral("dots-three"), 32);
        auto* menu = new QMenu(menu_button);
        const auto changed = [this] {
            game_dirs_changed = true;
            RebuildGameFolders();
        };
        const auto icon = [](QStringView name) {
            return Spotlight::Icon(name, Spotlight::Color::MenuInk(), 16);
        };
        menu->addAction(icon(u"folder-open"), tr("Open location"), this,
                        [this, path] { emit OpenGameFolderRequested(path); });
        auto* up = menu->addAction(icon(u"arrow-up"), tr("Move up"), this, [this, i, changed] {
            game_dirs.swapItemsAt(i, i - 1);
            changed();
        });
        up->setEnabled(i > 0);
        auto* down =
            menu->addAction(icon(u"arrow-down"), tr("Move down"), this, [this, i, changed] {
                game_dirs.swapItemsAt(i, i + 1);
                changed();
            });
        down->setEnabled(i + 1 < game_dirs.size());
        if (!built_in) {
            auto* scan = menu->addAction(tr("Scan subfolders"));
            scan->setCheckable(true);
            scan->setChecked(folder.deep_scan);
            connect(scan, &QAction::toggled, this, [this, i, changed](bool enabled) {
                game_dirs[i].deep_scan = enabled;
                changed();
            });
            menu->addSeparator();
            auto* remove =
                menu->addAction(Spotlight::Icon(u"trash", Spotlight::Color::AccentLight(), 16),
                                tr("Remove folder"), this, [this, i, changed] {
                                    game_dirs.removeAt(i);
                                    changed();
                                });
            remove->setProperty("spotlightDestructive", true);
        }
        connect(menu_button, &QAbstractButton::clicked, menu, [menu, menu_button] {
            menu->popup(menu_button->mapToGlobal(QPoint(
                menu_button->width() - menu->sizeHint().width(), menu_button->height() + 5)));
        });
        if (built_in) {
            const QString title = folder.path == "SDMC"       ? tr("SD card")
                                  : folder.path == "UserNAND" ? tr("User NAND")
                                                              : tr("System NAND");
            SettingsPage::AddRow(folders_layout, title, tr("Built-in location"),
                                 QStringLiteral("hard-drives"), Tints::Blue(), menu_button);
            continue;
        }
        auto* control = new QWidget;
        control->setProperty("spotlightTransparent", true);
        auto* control_layout = new QHBoxLayout(control);
        control_layout->setContentsMargins(0, 0, 0, 0);
        control_layout->setSpacing(14);
        auto* subfolders = new QCheckBox(tr("Subfolders"));
        subfolders->setChecked(folder.deep_scan);
        connect(subfolders, &QCheckBox::toggled, this, [this, i, changed](bool enabled) {
            game_dirs[i].deep_scan = enabled;
            changed();
        });
        control_layout->addWidget(subfolders);
        control_layout->addWidget(menu_button);
        SettingsPage::AddRow(folders_layout, QDir::toNativeSeparators(path),
                             folder.deep_scan ? tr("Subfolders scanned") : QString(),
                             QStringLiteral("folder"), Tints::Green(), control);
    }
    if (folder_summary != nullptr) {
        folder_summary->setText(GameFolderSummary());
    }
}

QString ConfigureDialog::GameFolderSummary() const {
    const auto custom = std::count_if(game_dirs.cbegin(), game_dirs.cend(),
                                      [](const auto& folder) { return !IsBuiltInFolder(folder); });
    return custom == 1 ? tr("1 folder added") : tr("%1 folders added").arg(custom);
}

void ConfigureDialog::AddGameFolder() {
    const QString path = QFileDialog::getExistingDirectory(this, tr("Select game folder"));
    const UISettings::GameDir dir{path.toStdString(), false, true};
    if (!path.isEmpty() && !game_dirs.contains(dir)) {
        game_dirs.append(dir);
        game_dirs_changed = true;
        RebuildGameFolders();
    }
}

void ConfigureDialog::OpenGetStarted(int controls, const QList<PresetCard*>& presets) {
    EnsureInput();
    const auto* player = input_tab->Player(0);
    Spotlight::GetStarted wizard(this, system,
                                 {
                                     .game_folders =
                                         [this] {
                                             QStringList folders;
                                             for (const auto& folder : game_dirs) {
                                                 if (!IsBuiltInFolder(folder)) {
                                                     folders.append(
                                                         QString::fromStdString(folder.path));
                                                 }
                                             }
                                             return folders;
                                         },
                                     .add_game_folder = [this] { AddGameFolder(); },
                                     .controller = player->ControllerName(),
                                     .controller_device = player->DeviceName(),
                                     .controller_connected =
                                         input_tab->PlayerConnected(0)->isChecked(),
                                     .presets = presets,
                                 });
    connect(&wizard, &Spotlight::GetStarted::ContentInstalled, this,
            &ConfigureDialog::ContentInstalled);
    wizard.exec();
    if (wizard.MapButtonsRequested()) {
        settings_page->SetCategory(controls);
        input_tab->PlayerTabs()->setCurrentIndex(0);
        settings_page->SetCategory(mapping_page);
    }
}
