// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2017 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <QComboBox>
#include <QFuture>
#include <QIntValidator>
#include <QRegularExpressionValidator>
#include <QString>
#include <QtConcurrentRun>
#include "common/settings.h"
#include "core/core.h"
#include "core/internal_network/network_interface.h"
#include "network/network.h"
#include "qt_common/config/uisettings.h"
#include "ui_direct_connect.h"
#include "lichii/main_window.h"
#include "lichii/multiplayer/client_room.h"
#include "lichii/multiplayer/direct_connect.h"
#include "lichii/multiplayer/message.h"
#include "lichii/multiplayer/state.h"
#include "lichii/multiplayer/validation.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

enum class ConnectionType : u8 { TraversalServer, IP };

DirectConnectWindow::DirectConnectWindow(Core::System& system_, QWidget* parent)
    : Spotlight::Dialog(parent,
                        Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowSystemMenuHint),
      ui(std::make_unique<Ui::DirectConnect>()), system{system_} {

    ui->setupUi(this);
    SetIcon(u"link", Spotlight::Color::Info());
    setFixedWidth(531);
    for (auto* label : {ui->label, ui->label_2, ui->label_3, ui->label_5}) {
        label->setFont(Spotlight::Font(13, 600));
        Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
    }
    ui->connect->setProperty("kind", QStringLiteral("primary"));
    ui->connect->setMinimumHeight(40);
    ui->connect->setIcon(Spotlight::Icon(u"link", Spotlight::Color::OnAccent(), 16));

    // setup the watcher for background connections
    watcher = new QFutureWatcher<void>;
    connect(watcher, &QFutureWatcher<void>::finished, this, &DirectConnectWindow::OnConnection);

    ui->nickname->setValidator(validation.GetNickname());
    ui->nickname->setText(
        QString::fromStdString(UISettings::values.multiplayer_nickname.GetValue()));
    if (ui->nickname->text().isEmpty() && !Settings::values.eden_username.GetValue().empty()) {
        // Use Lichii Web Service user name as nickname by default
        ui->nickname->setText(QString::fromStdString(Settings::values.eden_username.GetValue()));
    }
    ui->ip->setValidator(validation.GetIP());
    ui->ip->setText(QString::fromStdString(UISettings::values.multiplayer_ip.GetValue()));
    ui->port->setValidator(validation.GetPort());
    ui->port->setText(QString::number(UISettings::values.multiplayer_port.GetValue()));

    // TODO(jroweboy): Show or hide the connection options based on the current value of the combo
    // box. Add this back in when the traversal server support is added.
    connect(ui->connect, &QPushButton::clicked, this, &DirectConnectWindow::Connect);
}

DirectConnectWindow::~DirectConnectWindow() = default;

void DirectConnectWindow::RetranslateUi() {
    ui->retranslateUi(this);
}

void DirectConnectWindow::Connect() {
    if (!Network::GetSelectedNetworkInterface()) {
        NetworkMessage::ErrorManager::ShowError(
            NetworkMessage::ErrorManager::NO_INTERFACE_SELECTED);
        return;
    }
    if (!ui->nickname->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::USERNAME_NOT_VALID);
        return;
    }
    if (system.IsPoweredOn()) {
        if (!NetworkMessage::WarnGameRunning()) {
            return;
        }
    }
    if (const auto member = Network::GetRoomMember().lock()) {
        // Prevent the user from trying to join a room while they are already joining.
        if (member->GetState() == Network::RoomMember::State::Joining) {
            return;
        } else if (member->IsConnected()) {
            // And ask if they want to leave the room if they are already in one.
            if (!NetworkMessage::WarnDisconnect()) {
                return;
            }
        }
    }
    if (!ui->ip->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::IP_ADDRESS_NOT_VALID);
        return;
    }
    if (!ui->port->hasAcceptableInput()) {
        NetworkMessage::ErrorManager::ShowError(NetworkMessage::ErrorManager::PORT_NOT_VALID);
        return;
    }

    // Store settings
    UISettings::values.multiplayer_nickname = ui->nickname->text().toStdString();
    UISettings::values.multiplayer_ip = ui->ip->text().toStdString();
    if (!ui->port->text().isEmpty()) {
        UISettings::values.multiplayer_port = ui->port->text().toInt();
    } else {
        UISettings::values.multiplayer_port = UISettings::values.multiplayer_port.GetDefault();
    }

    emit SaveConfig();

    // attempt to connect in a different thread
    QFuture<void> f = QtConcurrent::run([&] {
        if (auto room_member = Network::GetRoomMember().lock()) {
            auto port = UISettings::values.multiplayer_port.GetValue();
            room_member->Join(ui->nickname->text().toStdString(),
                              ui->ip->text().toStdString().c_str(), port, 0, Network::NoPreferredIP,
                              ui->password->text().toStdString().c_str());
        }
    });
    watcher->setFuture(f);
    // and disable widgets and display a connecting while we wait
    BeginConnecting();
}

void DirectConnectWindow::BeginConnecting() {
    ui->connect->setEnabled(false);
    ui->connect->setText(tr("Connecting"));
    ui->connect->setIcon(Spotlight::Icon(u"circle-notch", Spotlight::Color::TextMuted(), 16));
}

void DirectConnectWindow::EndConnecting() {
    ui->connect->setEnabled(true);
    ui->connect->setText(tr("Connect"));
    ui->connect->setIcon(Spotlight::Icon(u"link", Spotlight::Color::OnAccent(), 16));
}

void DirectConnectWindow::OnConnection() {
    EndConnecting();

    if (auto room_member = Network::GetRoomMember().lock()) {
        if (room_member->IsConnected()) {
            close();
        }
    }
}
