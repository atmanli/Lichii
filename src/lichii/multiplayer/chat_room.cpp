// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <future>
#include <QColor>
#include <QDesktopServices>
#include <QFutureWatcher>
#include <QImage>
#include <QList>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMetaType>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTime>
#include <QUrl>
#include <QtConcurrentRun>
#include "common/logging.h"
#include "network/announce_multiplayer_session.h"
#include "ui_chat_room.h"
#include "qt_common/game_list/game_list_p.h"
#include "lichii/multiplayer/chat_room.h"
#include "lichii/multiplayer/message.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/theme.h"
#ifdef ENABLE_WEB_SERVICE
#include "web_service/web_backend.h"
#endif

namespace {

constexpr std::array<const char*, 8> PlayerColors = {"#7EE2A8", "#8FB5FF", "#FFD37A", "#C9B3FF",
                                                     "#FF8FAE", "#6FCACF", "#F7CD8A", "#E6A3E0"};

} // namespace

class ChatMessage {
public:
    explicit ChatMessage(const Network::ChatEntry& chat, QTime ts = {}) {
        /// Convert the time to their default locale defined format
        QLocale locale;
        timestamp = locale.toString(ts.isValid() ? ts : QTime::currentTime(), QLocale::ShortFormat);
        nickname = QString::fromStdString(chat.nickname);
        username = QString::fromStdString(chat.username);
        message = QString::fromStdString(chat.message);

        // Check for user pings
        QString cur_nickname, cur_username;
        if (auto room = Network::GetRoomMember().lock()) {
            cur_nickname = QString::fromStdString(room->GetNickname());
            cur_username = QString::fromStdString(room->GetUsername());
        }

        // Handle pings at the beginning and end of message
        QString fixed_message = QStringLiteral(" %1 ").arg(message);
        if (fixed_message.contains(QStringLiteral(" @%1 ").arg(cur_nickname)) ||
            (!cur_username.isEmpty() &&
             fixed_message.contains(QStringLiteral(" @%1 ").arg(cur_username)))) {

            contains_ping = true;
        } else {
            contains_ping = false;
        }
    }

    bool ContainsPing() const {
        return contains_ping;
    }

    QString GetPlayerChatMessage(u16 player) const {
        QString name;
        if (username.isEmpty() || username == nickname) {
            name = nickname;
        } else {
            name = QStringLiteral("%1 (%2)").arg(nickname, username);
        }
        QString text = message.toHtmlEscaped();
        if (ContainsPing()) {
            text = QStringLiteral("<span style='background-color: #4d2a3a;'>%1</span>").arg(text);
        }
        return QStringLiteral("<table width='100%' cellspacing='0' cellpadding='0' "
                              "style='margin-top: 4px;'><tr><td><span style='color: %1; "
                              "font-weight: 600;'>%2</span></td><td align='right'><span "
                              "style='font-family: monospace; font-size: 12px; color: "
                              "rgba(255, 255, 255, 158);'>%3</span></td></tr></table>"
                              "<div style='color: #ffffff;'>%4</div>")
            .arg(QLatin1StringView(PlayerColors[player % PlayerColors.size()]),
                 name.toHtmlEscaped(), timestamp, text);
    }

private:
    QString timestamp;
    QString nickname;
    QString username;
    QString message;
    bool contains_ping;
};

class StatusMessage {
public:
    explicit StatusMessage(const QString& msg, bool is_success)
        : message{msg}, success{is_success} {}

    QString GetSystemChatMessage() const {
        return QStringLiteral("<div style='margin-top: 4px; color: rgba(255, 255, 255, 158);'><img "
                              "src='chat:%1' width='14' height='14' style='vertical-align: "
                              "middle;'>&nbsp;&nbsp;%2</div>")
            .arg(success ? QStringLiteral("success") : QStringLiteral("info"),
                 message.toHtmlEscaped());
    }

private:
    QString message;
    bool success;
};

class PlayerListItem : public QStandardItem {
public:
    static const int NicknameRole = Qt::UserRole + 1;
    static const int UsernameRole = Qt::UserRole + 2;
    static const int AvatarUrlRole = Qt::UserRole + 3;
    static const int GameNameRole = Qt::UserRole + 4;
    static const int GameVersionRole = Qt::UserRole + 5;

    PlayerListItem() = default;
    explicit PlayerListItem(const std::string& nickname, const std::string& username,
                            const std::string& avatar_url,
                            const AnnounceMultiplayerRoom::GameInfo& game_info) {
        setEditable(false);
        setData(QString::fromStdString(nickname), NicknameRole);
        setData(QString::fromStdString(username), UsernameRole);
        setData(QString::fromStdString(avatar_url), AvatarUrlRole);
        if (game_info.name.empty()) {
            setData(QObject::tr("Not playing a game"), GameNameRole);
        } else {
            setData(QString::fromStdString(game_info.name), GameNameRole);
        }
        setData(QString::fromStdString(game_info.version), GameVersionRole);
    }

    QVariant data(int role) const override {
        if (role != Qt::DisplayRole) {
            return QStandardItem::data(role);
        }
        QString name;
        const QString nickname = data(NicknameRole).toString();
        const QString username = data(UsernameRole).toString();
        if (username.isEmpty() || username == nickname) {
            name = nickname;
        } else {
            name = QStringLiteral("%1 (%2)").arg(nickname, username);
        }
        const QString version = data(GameVersionRole).toString();
        QString version_string;
        if (!version.isEmpty()) {
            version_string = QStringLiteral("(%1)").arg(version);
        }
        return QStringLiteral("%1\n      %2 %3")
            .arg(name, data(GameNameRole).toString(), version_string);
    }
};

namespace {

constexpr int MemberRowHeight = 57;

class MemberDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        return {option.rect.width(), MemberRowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect row = option.rect;
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(Spotlight::Color::FillFaint());
            painter->drawRect(row);
        }
        if (index.row() + 1 < index.model()->rowCount()) {
            painter->fillRect(QRect(row.left() + 14, row.bottom(), row.width() - 28, 1),
                              Spotlight::Color::Divider());
        }
        const QRectF avatar(row.left() + 14, row.top() + (row.height() - 32) / 2.0, 32, 32);
        const QPixmap picture = index.data(Qt::DecorationRole).value<QPixmap>();
        const QString nickname = index.data(PlayerListItem::NicknameRole).toString();
        if (!picture.isNull()) {
            Spotlight::PaintWholeIcon(*painter, avatar, picture, 10);
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(Spotlight::Color::Fill());
            painter->drawRoundedRect(avatar, 10, 10);
            painter->setPen(
                QColor(QLatin1StringView(PlayerColors[index.row() % PlayerColors.size()])));
            painter->setFont(Spotlight::Font(12, 600));
            painter->drawText(avatar, Qt::AlignCenter, nickname.left(1).toUpper());
        }
        const QString username = index.data(PlayerListItem::UsernameRole).toString();
        const QString name = username.isEmpty() || username == nickname
                                 ? nickname
                                 : QStringLiteral("%1 (%2)").arg(nickname, username);
        QString game = index.data(PlayerListItem::GameNameRole).toString();
        const QString version = index.data(PlayerListItem::GameVersionRole).toString();
        if (!version.isEmpty()) {
            game = QStringLiteral("%1 (%2)").arg(game, version);
        }
        const int text_left = row.left() + 14 + 32 + 12;
        const int text_width = row.right() - 14 - text_left;
        const QFont name_font = Spotlight::Font(14, 600);
        const QFont game_font = Spotlight::Font(12, 400);
        painter->setFont(name_font);
        painter->setPen(Spotlight::Color::Text());
        painter->drawText(QRect(text_left, row.top() + 10, text_width, 19),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(name_font).elidedText(name, Qt::ElideRight, text_width));
        painter->setFont(game_font);
        painter->setPen(Spotlight::Color::TextSecondary());
        painter->drawText(QRect(text_left, row.top() + 30, text_width, 16),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(game_font).elidedText(game, Qt::ElideRight, text_width));
        painter->restore();
    }
};

} // namespace

ChatRoom::ChatRoom(QWidget* parent) : QWidget(parent), ui(std::make_unique<Ui::ChatRoom>()) {
    ui->setupUi(this);

    // set the item_model for player_view

    player_list = new QStandardItemModel(ui->player_view);
    ui->player_view->setModel(player_list);
    ui->player_view->setContextMenuPolicy(Qt::CustomContextMenu);
    // set a header to make it look better though there is only one column
    player_list->insertColumns(0, 1);
    player_list->setHeaderData(0, Qt::Horizontal, tr("Members"));
    ui->player_view->setHeaderHidden(true);
    ui->player_view->setRootIsDecorated(false);
    ui->player_view->setItemDelegate(new MemberDelegate(ui->player_view));
    ui->player_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->player_view->setFixedWidth(234);
    ui->player_view->setStyleSheet(
        QStringLiteral("QTreeView { background-color: rgba(255, 255, 255, 13); border: 1px solid "
                       "rgba(255, 255, 255, 20); border-radius: 16px; }"));
    ui->player_view->ensurePolished();

    ui->chat_label->setFont(Spotlight::Font(13, 600));
    ui->members_label->setFont(Spotlight::Font(11, 600));
    ui->members_label->setStyleSheet(
        QStringLiteral("color: rgba(255, 255, 255, 158); letter-spacing: 1px;"));

    ui->chat_history->document()->setMaximumBlockCount(max_chat_lines);
    ui->chat_history->setFont(Spotlight::Font(14, 400));
    ui->chat_history->setStyleSheet(
        QStringLiteral("QTextEdit { background-color: rgba(255, 255, 255, 13); border: 1px solid "
                       "rgba(255, 255, 255, 20); border-radius: 16px; padding: 8px 10px; }"));
    AddIconResources();

    ui->send_message->setIcon(Spotlight::Icon(u"paper-plane-tilt", Spotlight::Color::Text(), 16));
    ui->send_message->setMinimumHeight(40);
    ui->send_message->setAutoDefault(false);
    ui->chat_message->setMinimumHeight(40);

    // register the network structs to use in slots and signals
    qRegisterMetaType<Network::ChatEntry>();
    qRegisterMetaType<Network::StatusMessageEntry>();
    qRegisterMetaType<Network::RoomInformation>();
    qRegisterMetaType<Network::RoomMember::State>();

    // Connect all the widgets to the appropriate events
    connect(ui->player_view, &QTreeView::customContextMenuRequested, this,
            &ChatRoom::PopupContextMenu);
    connect(ui->chat_message, &QLineEdit::returnPressed, this, &ChatRoom::OnSendChat);
    connect(ui->chat_message, &QLineEdit::textChanged, this, &ChatRoom::OnChatTextChanged);
    connect(ui->send_message, &QPushButton::clicked, this, &ChatRoom::OnSendChat);
}

ChatRoom::~ChatRoom() = default;

void ChatRoom::Initialize() {
    // setup the callbacks for network updates
    if (auto member = Network::GetRoomMember().lock()) {
        member->BindOnChatMessageReceived(
            [this](const Network::ChatEntry& chat) { emit ChatReceived(chat); });
        member->BindOnStatusMessageReceived(
            [this](const Network::StatusMessageEntry& status_message) {
                emit StatusMessageReceived(status_message);
            });
        connect(this, &ChatRoom::ChatReceived, this, &ChatRoom::OnChatReceive);
        connect(this, &ChatRoom::StatusMessageReceived, this, &ChatRoom::OnStatusMessageReceive);
    }
}

void ChatRoom::SetModPerms(bool is_mod) {
    has_mod_perms = is_mod;
}

void ChatRoom::RetranslateUi() {
    ui->retranslateUi(this);
}

void ChatRoom::Clear() {
    ui->chat_history->clear();
    AddIconResources();
    block_list.clear();
}

void ChatRoom::AddIconResources() {
    auto* document = ui->chat_history->document();
    const qreal ratio = devicePixelRatioF();
    document->addResource(
        QTextDocument::ImageResource, QUrl(QStringLiteral("chat:info")),
        Spotlight::IconPixmap(u"info", 14, Spotlight::Color::TextSecondary(), ratio));
    document->addResource(
        QTextDocument::ImageResource, QUrl(QStringLiteral("chat:success")),
        Spotlight::IconPixmap(u"check-circle", 14, Spotlight::Color::Success(), ratio));
}

void ChatRoom::AppendStatusMessage(const QString& msg, bool success) {
    ui->chat_history->append(StatusMessage(msg, success).GetSystemChatMessage());
}

void ChatRoom::AppendChatMessage(const QString& msg) {
    ui->chat_history->append(msg);
}

void ChatRoom::SendModerationRequest(Network::RoomMessageTypes type, const std::string& nickname) {
    if (auto room = Network::GetRoomMember().lock()) {
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&nickname](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == nickname;
                               });
        if (it == members.end()) {
            NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::NO_SUCH_USER);
            return;
        }
        room->SendModerationRequest(type, nickname);
    }
}

bool ChatRoom::ValidateMessage(const std::string& msg) {
    return !msg.empty();
}

void ChatRoom::OnRoomUpdate(const Network::RoomInformation& info) {
    // TODO(B3N30): change title
    if (auto room_member = Network::GetRoomMember().lock()) {
        SetPlayerList(room_member->GetMemberInformation());
    }
}

void ChatRoom::Disable() {
    ui->send_message->setDisabled(true);
    ui->chat_message->setDisabled(true);
}

void ChatRoom::Enable() {
    ui->send_message->setEnabled(true);
    ui->chat_message->setEnabled(true);
}

void ChatRoom::OnChatReceive(const Network::ChatEntry& chat) {
    if (!ValidateMessage(chat.message)) {
        return;
    }
    if (auto room = Network::GetRoomMember().lock()) {
        // get the id of the player
        auto members = room->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&chat](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == chat.nickname &&
                                          member.username == chat.username;
                               });
        if (it == members.end()) {
            LOG_INFO(Network, "Chat message received from unknown player. Ignoring it.");
            return;
        }
        if (block_list.count(chat.nickname)) {
            LOG_INFO(Network, "Chat message received from blocked player {}. Ignoring it.",
                     chat.nickname);
            return;
        }
        auto player = std::distance(members.begin(), it);
        ChatMessage m(chat);
        if (m.ContainsPing()) {
            emit UserPinged();
        }
        AppendChatMessage(m.GetPlayerChatMessage(player));
    }
}

void ChatRoom::OnStatusMessageReceive(const Network::StatusMessageEntry& status_message) {
    QString name;
    if (status_message.username.empty() || status_message.username == status_message.nickname) {
        name = QString::fromStdString(status_message.nickname);
    } else {
        name = QStringLiteral("%1 (%2)").arg(QString::fromStdString(status_message.nickname),
                                             QString::fromStdString(status_message.username));
    }
    QString message;
    switch (status_message.type) {
    case Network::IdMemberJoin:
        message = tr("%1 has joined").arg(name);
        break;
    case Network::IdMemberLeave:
        message = tr("%1 has left").arg(name);
        break;
    case Network::IdMemberKicked:
        message = tr("%1 has been kicked").arg(name);
        break;
    case Network::IdMemberBanned:
        message = tr("%1 has been banned").arg(name);
        break;
    case Network::IdAddressUnbanned:
        message = tr("%1 has been unbanned").arg(name);
        break;
    }
    if (!message.isEmpty())
        AppendStatusMessage(message);
}

void ChatRoom::OnSendChat() {
    if (auto room_member = Network::GetRoomMember().lock()) {
        if (!room_member->IsConnected()) {
            return;
        }
        auto message = ui->chat_message->text().toStdString();
        if (!ValidateMessage(message)) {
            return;
        }
        auto nick = room_member->GetNickname();
        auto username = room_member->GetUsername();
        Network::ChatEntry chat{nick, username, message};

        auto members = room_member->GetMemberInformation();
        auto it = std::find_if(members.begin(), members.end(),
                               [&chat](const Network::RoomMember::MemberInformation& member) {
                                   return member.nickname == chat.nickname &&
                                          member.username == chat.username;
                               });
        if (it == members.end()) {
            LOG_INFO(Network, "Cannot find self in the player list when sending a message.");
        }
        auto player = std::distance(members.begin(), it);
        ChatMessage m(chat);
        room_member->SendChatMessage(message);
        AppendChatMessage(m.GetPlayerChatMessage(player));
        ui->chat_message->clear();
    }
}

void ChatRoom::UpdateIconDisplay() {
    for (int row = 0; row < player_list->invisibleRootItem()->rowCount(); ++row) {
        QStandardItem* item = player_list->invisibleRootItem()->child(row);
        const std::string avatar_url =
            item->data(PlayerListItem::AvatarUrlRole).toString().toStdString();
        if (icon_cache.count(avatar_url)) {
            item->setData(icon_cache.at(avatar_url), Qt::DecorationRole);
        } else {
            item->setData(QVariant(), Qt::DecorationRole);
        }
    }
}

void ChatRoom::SetPlayerList(const Network::RoomMember::MemberList& member_list) {
    // TODO(B3N30): Remember which row is selected
    player_list->removeRows(0, player_list->rowCount());
    for (const auto& member : member_list) {
        if (member.nickname.empty())
            continue;
        QStandardItem* name_item = new PlayerListItem(member.nickname, member.username,
                                                      member.avatar_url, member.game_info);

#ifdef ENABLE_WEB_SERVICE
        if (!icon_cache.count(member.avatar_url) && !member.avatar_url.empty()) {
            // Start a request to get the member's avatar
            const QUrl url(QString::fromStdString(member.avatar_url));
            QFuture<std::string> future = QtConcurrent::run([url] {
                WebService::Client client(
                    QStringLiteral("%1://%2").arg(url.scheme(), url.host()).toStdString(), "", "");
                auto result = client.GetImage(url.path().toStdString(), true);
                if (result.returned_data.empty()) {
                    LOG_ERROR(WebService, "Failed to get avatar");
                }
                return result.returned_data;
            });
            auto* future_watcher = new QFutureWatcher<std::string>(this);
            connect(future_watcher, &QFutureWatcher<std::string>::finished, this,
                    [this, future_watcher, avatar_url = member.avatar_url] {
                        const std::string result = future_watcher->result();
                        if (result.empty())
                            return;
                        QPixmap pixmap;
                        if (!pixmap.loadFromData(reinterpret_cast<const u8*>(result.data()),
                                                 static_cast<uint>(result.size())))
                            return;
                        icon_cache[avatar_url] =
                            pixmap.scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        // Update all the displayed icons with the new icon_cache
                        UpdateIconDisplay();
                    });
            future_watcher->setFuture(future);
        }
#endif

        player_list->invisibleRootItem()->appendRow(name_item);
    }
    UpdateIconDisplay();
    ui->player_view->setMaximumHeight(std::max(1, player_list->rowCount()) * MemberRowHeight +
                                      2 * ui->player_view->frameWidth());
    // TODO(B3N30): Restore row selection
}

void ChatRoom::OnChatTextChanged() {
    if (ui->chat_message->text().length() > static_cast<int>(Network::MaxMessageSize))
        ui->chat_message->setText(
            ui->chat_message->text().left(static_cast<int>(Network::MaxMessageSize)));
}

void ChatRoom::PopupContextMenu(const QPoint& menu_location) {
    QModelIndex item = ui->player_view->indexAt(menu_location);
    if (!item.isValid())
        return;

    std::string nickname =
        player_list->item(item.row())->data(PlayerListItem::NicknameRole).toString().toStdString();

    Spotlight::Menu context_menu;
    const auto icon = [](QStringView name, const QColor& color = Spotlight::Color::MenuInk()) {
        return Spotlight::Icon(name, color, 16);
    };

    QString username = player_list->item(item.row())->data(PlayerListItem::UsernameRole).toString();
    if (!username.isEmpty()) {
        QAction* view_profile_action =
            context_menu.addAction(icon(u"user-circle"), tr("View Profile"));
        connect(view_profile_action, &QAction::triggered, [username] {
            QDesktopServices::openUrl(
                QUrl(QStringLiteral("https://community.citra-emu.org/u/%1").arg(username)));
        });
    }

    std::string cur_nickname;
    if (auto room = Network::GetRoomMember().lock()) {
        cur_nickname = room->GetNickname();
    }

    if (nickname != cur_nickname) { // You can't block yourself
        QAction* block_action = context_menu.addAction(tr("Block Player"));

        block_action->setCheckable(true);
        block_action->setChecked(block_list.count(nickname) > 0);

        connect(block_action, &QAction::triggered, [this, nickname] {
            if (block_list.count(nickname)) {
                block_list.erase(nickname);
            } else {
                QMessageBox::StandardButton result = QMessageBox::warning(
                    this, tr("Block Player"),
                    tr("When you block a player, you will no longer receive chat messages from "
                       "them.<br><br>Are you sure you would like to block %1?")
                        .arg(QString::fromStdString(nickname)),
                    QMessageBox::Yes | QMessageBox::No);
                if (result == QMessageBox::Yes)
                    block_list.emplace(nickname);
            }
        });
    }

    if (has_mod_perms && nickname != cur_nickname) { // You can't kick or ban yourself
        context_menu.addSeparator();

        QAction* kick_action = context_menu.addAction(icon(u"sign-out"), tr("Kick"));
        QAction* ban_action =
            context_menu.addAction(icon(u"prohibit", Spotlight::Color::AccentLight()), tr("Ban"));

        connect(kick_action, &QAction::triggered, [this, nickname] {
            QMessageBox::StandardButton result =
                QMessageBox::warning(this, tr("Kick Player"),
                                     tr("Are you sure you would like to <b>kick</b> %1?")
                                         .arg(QString::fromStdString(nickname)),
                                     QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes)
                SendModerationRequest(Network::IdModKick, nickname);
        });
        connect(ban_action, &QAction::triggered, [this, nickname] {
            QMessageBox::StandardButton result = QMessageBox::warning(
                this, tr("Ban Player"),
                tr("Are you sure you would like to <b>kick and ban</b> %1?\n\nThis would "
                   "ban both their forum username and their IP address.")
                    .arg(QString::fromStdString(nickname)),
                QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes)
                SendModerationRequest(Network::IdModBan, nickname);
        });
    }

    context_menu.exec(ui->player_view->viewport()->mapToGlobal(menu_location));
}
