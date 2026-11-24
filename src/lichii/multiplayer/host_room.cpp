// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <future>
#include <QColor>
#include <QList>
#include <QLocale>
#include <QMessageBox>
#include <QMetaType>
#include <QTime>
#include <QtConcurrentRun>
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/internal_network/network_interface.h"
#include "lichii/game/artwork_image.h"
#include "lichii/game/common.h"
#include "lichii/main_window.h"
#include "lichii/multiplayer/host_room.h"
#include "lichii/multiplayer/message.h"
#include "lichii/multiplayer/state.h"
#include "lichii/multiplayer/validation.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "network/announce_multiplayer_session.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "ui_host_room.h"
#ifdef ENABLE_WEB_SERVICE
#include "web_service/verify_user_jwt.h"
#endif

HostRoomWindow::HostRoomWindow(QWidget* parent, QStandardItemModel* list,
                               std::shared_ptr<Core::AnnounceMultiplayerSession> session,
                               Core::System& system_)
    : Spotlight::Dialog(parent,
                        Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::HostRoom>()), announce_multiplayer_session(session), system{system_} {
    ui->setupUi(this);
    SetIcon(u"plus", Spotlight::Color::Success());
    setFixedWidth(712);
    for (auto* label : {ui->label, ui->label_2, ui->label_3, ui->label_4, ui->label_5, ui->label_6,
                        ui->label_7}) {
        label->setFont(Spotlight::Font(13, 600));
        Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
    }
    for (auto* hint : {ui->room_name_hint, ui->max_player_hint, ui->username_hint}) {
        hint->setFont(Spotlight::Font(12, 400));
        Spotlight::SetTextColor(hint, Spotlight::Color::TextMuted());
    }
    ui->game_grid->setColumnStretch(0, 1);
    ui->user_grid->setColumnStretch(0, 1);
    host_type = new Spotlight::SegmentedControl({tr("Public"), tr("Unlisted")},
                                                Spotlight::SegmentedControl::Kind::Accent, 38, this);
    host_type->SetCurrentIndex(0);
    ui->options_row->insertWidget(0, host_type);
    ui->host->setProperty("kind", QStringLiteral("primary"));
    ui->host->setMinimumHeight(40);

    // set up validation for all of the fields
    ui->room_name->setValidator(validation.GetRoomName());
    ui->username->setValidator(validation.GetNickname());
    ui->port->setValidator(validation.GetPort());
    ui->port->setPlaceholderText(QString::number(Network::DefaultRoomPort));

    // Create a proxy to the game list to display the list of preferred games
    game_list = new QStandardItemModel;
    UpdateGameList(list);

    proxy = new ComboBoxProxyModel;
    proxy->setSourceModel(game_list);
    proxy->sort(0, Qt::AscendingOrder);
    ui->game_list->setModel(proxy);

    // Connect all the widgets to the appropriate events
    connect(ui->host, &QPushButton::clicked, this, &HostRoomWindow::Host);

    // Restore the settings:
    ui->username->setText(
        QString::fromStdString(UISettings::values.multiplayer_room_nickname.GetValue()));
    if (ui->username->text().isEmpty() && !Settings::values.eden_username.GetValue().empty()) {
        // Use Eden Web Service user name as nickname by default
        ui->username->setText(QString::fromStdString(Settings::values.eden_username.GetValue()));
    }
    ui->room_name->setText(
        QString::fromStdString(UISettings::values.multiplayer_room_name.GetValue()));
    ui->port->setText(QString::number(UISettings::values.multiplayer_room_port.GetValue()));
    ui->max_player->setValue(UISettings::values.multiplayer_max_player.GetValue());
    int index = UISettings::values.multiplayer_host_type.GetValue();
    if (index >= 0 && index < 2) {
        host_type->SetCurrentIndex(index);
    }
    index = ui->game_list->findData(UISettings::values.multiplayer_game_id.GetValue(),
                                    GameListItemPath::ProgramIdRole);
    if (index != -1) {
        ui->game_list->setCurrentIndex(index);
    }
    ui->room_description->setText(
        QString::fromStdString(UISettings::values.multiplayer_room_description.GetValue()));
}

HostRoomWindow::~HostRoomWindow() = default;

void HostRoomWindow::UpdateGameList(QStandardItemModel* list) {
    Lichii::CopyLibraryGames(*game_list, *list);
    for (int row = 0; row < game_list->rowCount(); ++row) {
        auto* game = game_list->item(row);
        game->setIcon(ArtworkImage::Icon(game->data(Qt::DecorationRole).value<QPixmap>(), 16, 4));
    }
}

void HostRoomWindow::RetranslateUi() {
    ui->retranslateUi(this);
    host_type->SetOptions({tr("Public"), tr("Unlisted")});
}

std::unique_ptr<Network::VerifyUser::Backend> HostRoomWindow::CreateVerifyBackend(
    bool use_validation) const {
    std::unique_ptr<Network::VerifyUser::Backend> verify_backend;
    if (use_validation) {
#ifdef ENABLE_WEB_SERVICE
        verify_backend =
            std::make_unique<WebService::VerifyUserJWT>(Settings::values.web_api_url.GetValue());
#else
        verify_backend = std::make_unique<Network::VerifyUser::NullBackend>();
#endif
    } else {
        verify_backend = std::make_unique<Network::VerifyUser::NullBackend>();
    }
    return verify_backend;
}

void HostRoomWindow::Host() {
    if (!Network::GetSelectedNetworkInterface()) {
        NetworkMessage::ErrorManager::ShowError(
            NetworkMessage::ErrorManager::NO_INTERFACE_SELECTED);
        return;
    }
    if (!ui->username->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::USERNAME_NOT_VALID);
        return;
    }
    if (!ui->room_name->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::ROOMNAME_NOT_VALID);
        return;
    }
    if (!ui->port->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::PORT_NOT_VALID);
        return;
    }
    if (ui->game_list->currentIndex() == -1) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::GAME_NOT_SELECTED);
        return;
    }
    if (system.IsPoweredOn()) {
        if (!NetworkMessage::WarnGameRunning()) {
            return;
        }
    }
    if (auto member = Network::GetRoomMember().lock()) {
        if (member->GetState() == Network::RoomMember::State::Joining) {
            return;
        } else if (member->IsConnected()) {
            auto parent = static_cast<MultiplayerState*>(parentWidget());
            if (!parent->OnCloseRoom()) {
                close();
                return;
            }
        }
        ui->host->setDisabled(true);

        const AnnounceMultiplayerRoom::GameInfo game{
            .name = ui->game_list->currentData(Qt::DisplayRole).toString().toStdString(),
            .id = ui->game_list->currentData(GameListItemPath::ProgramIdRole).toULongLong(),
        };
        const auto port =
            ui->port->isModified() ? ui->port->text().toInt() : Network::DefaultRoomPort;
        const auto password = ui->password->text().toStdString();
        const bool is_public = host_type->CurrentIndex() == 0;
        Network::Room::BanList ban_list{};
        if (ui->load_ban_list->isChecked()) {
            ban_list = UISettings::values.multiplayer_ban_list;
        }
        if (auto room = Network::GetRoom().lock()) {
            const bool created =
                room->Create(ui->room_name->text().toStdString(),
                             ui->room_description->toPlainText().toStdString(), "", port, password,
                             ui->max_player->value(), Settings::values.eden_username.GetValue(),
                             game, CreateVerifyBackend(is_public), ban_list);
            if (!created) {
                NetworkMessage::ErrorManager::ShowError(
                    NetworkMessage::ErrorManager::COULD_NOT_CREATE_ROOM);
                LOG_ERROR(Network, "Could not create room!");
                ui->host->setEnabled(true);
                return;
            }
        }
        // Start the announce session if they chose Public
        if (is_public) {
            if (auto session = announce_multiplayer_session.lock()) {
                // Register the room first to ensure verify_uid is present when we connect
                WebService::WebResult result = session->Register();
                if (result.result_code != WebService::WebResult::Code::Success) {
                    QMessageBox::warning(
                        this, tr("Error"),
                        tr("Failed to announce the room to the public lobby. In order to host a "
                           "room publicly, you must have a valid Eden account configured in "
                           "Emulation -> Configure -> Web. If you do not want to publish a room in "
                           "the public lobby, then select Unlisted instead.\nDebug Message: ") +
                            QString::fromStdString(result.result_string),
                        QMessageBox::Ok);
                    ui->host->setEnabled(true);
                    if (auto room = Network::GetRoom().lock()) {
                        room->Destroy();
                    }
                    return;
                }
                session->Start();
            } else {
                LOG_ERROR(Network, "Starting announce session failed");
            }
        }
        std::string token;
#ifdef ENABLE_WEB_SERVICE
        if (is_public) {
            WebService::Client client(Settings::values.web_api_url.GetValue(),
                                      Settings::values.eden_username.GetValue(),
                                      Settings::values.eden_token.GetValue());
            if (auto room = Network::GetRoom().lock()) {
                token = client.GetExternalJWT(room->GetVerifyUID()).returned_data;
            }
            if (token.empty()) {
                LOG_ERROR(WebService, "Could not get external JWT, verification may fail");
            } else {
                LOG_INFO(WebService, "Successfully requested external JWT: size={}", token.size());
            }
        }
#endif
        // TODO: Check what to do with this
        member->Join(ui->username->text().toStdString(), "127.0.0.1", port, 0,
                     Network::NoPreferredIP, password, token);

        // Store settings
        UISettings::values.multiplayer_room_nickname = ui->username->text().toStdString();
        UISettings::values.multiplayer_room_name = ui->room_name->text().toStdString();
        UISettings::values.multiplayer_game_id =
            ui->game_list->currentData(GameListItemPath::ProgramIdRole).toLongLong();
        UISettings::values.multiplayer_max_player = ui->max_player->value();

        UISettings::values.multiplayer_host_type = host_type->CurrentIndex();
        if (ui->port->isModified() && !ui->port->text().isEmpty()) {
            UISettings::values.multiplayer_room_port = ui->port->text().toInt();
        } else {
            UISettings::values.multiplayer_room_port = Network::DefaultRoomPort;
        }
        UISettings::values.multiplayer_room_description =
            ui->room_description->toPlainText().toStdString();
        ui->host->setEnabled(true);
        emit SaveConfig();
        close();
    }
}

QVariant ComboBoxProxyModel::data(const QModelIndex& idx, int role) const {
    if (role != Qt::DisplayRole) {
        return QSortFilterProxyModel::data(idx, role);
    }
    std::string filename;
    Common::SplitPath(
        QSortFilterProxyModel::data(idx, GameListItemPath::FullPathRole).toString().toStdString(),
        nullptr, &filename, nullptr);
    QString title = QSortFilterProxyModel::data(idx, GameListItemPath::TitleRole).toString();
    return title.isEmpty() ? QString::fromStdString(filename) : title;
}

bool ComboBoxProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    auto leftData = left.data(GameListItemPath::TitleRole).toString();
    auto rightData = right.data(GameListItemPath::TitleRole).toString();
    return leftData.compare(rightData) < 0;
}
