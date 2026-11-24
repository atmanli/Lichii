// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <sstream>

#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStringListModel>
#include <QStyle>
#include <QTimer>

#include "common/logging.h"
#include "common/settings.h"
#include "input_common/drivers/udp_client.h"
#include "input_common/helpers/udp_protocol.h"
#include "input_common/main.h"
#include "ui_configure_motion_touch.h"
#include "lichii/configuration/configure_motion_touch.h"
#include "lichii/configuration/configure_touch_from_button.h"
#include "lichii/spotlight/theme.h"

namespace {
constexpr int StatusIconSize = 36;

class ServerListModel : public QStringListModel {
public:
    using QStringListModel::QStringListModel;

    QVariant data(const QModelIndex& index, int role) const override {
        if (role == Qt::DecorationRole) {
            return Spotlight::Icon(QStringLiteral("broadcast"), Spotlight::Color::Info(), 16);
        }
        return QStringListModel::data(index, role);
    }
};
} // namespace

CalibrationConfigurationDialog::CalibrationConfigurationDialog(QWidget* parent,
                                                               const std::string& host, u16 port)
    : QDialog(parent) {
    Spotlight::AdoptDialogChrome(this, false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    status_icon = new QLabel;
    status_icon->setFixedSize(StatusIconSize, StatusIconSize);
    status_label = new QLabel(tr("Communicating with the server..."));
    status_label->setFont(Spotlight::Font(14, 400));
    status_label->setMinimumWidth(240);
    cancel_button = new QPushButton(tr("Cancel"));
    cancel_button->setAutoDefault(false);
    spinner = new QTimer(this);
    spinner->setInterval(16);
    connect(spinner, &QTimer::timeout, this, [this] {
        spinner_angle = (spinner_angle + 6) % 360;
        DrawSpinner();
    });
    spinner->start();
    DrawSpinner();
    connect(cancel_button, &QPushButton::clicked, this, [this] {
        if (!completed) {
            job->Stop();
        }
        accept();
    });
    auto* status_row = new QHBoxLayout;
    status_row->setSpacing(14);
    status_row->addWidget(status_icon, 0, Qt::AlignTop);
    status_row->addWidget(status_label, 1);
    layout->addLayout(status_row);
    layout->addWidget(cancel_button, 0, Qt::AlignRight);

    using namespace InputCommon::CemuhookUDP;
    job = std::make_unique<CalibrationConfigurationJob>(
        host, port,
        [this](CalibrationConfigurationJob::Status status) {
            QMetaObject::invokeMethod(this, [status, this] {
                QString text;
                switch (status) {
                case CalibrationConfigurationJob::Status::Ready:
                    text = tr("Touch the top left corner <br>of your touchpad.");
                    SetStatusIcon(QStringLiteral("arrow-up-left"), Spotlight::Color::Info());
                    break;
                case CalibrationConfigurationJob::Status::Stage1Completed:
                    text = tr("Now touch the bottom right corner <br>of your touchpad.");
                    SetStatusIcon(QStringLiteral("arrow-down-right"), Spotlight::Color::Info());
                    break;
                case CalibrationConfigurationJob::Status::Completed:
                    text = tr("Configuration completed!");
                    SetStatusIcon(QStringLiteral("check-circle"), Spotlight::Color::Success());
                    break;
                default:
                    break;
                }
                UpdateLabelText(text);
            });
            if (status == CalibrationConfigurationJob::Status::Completed) {
                QMetaObject::invokeMethod(this, [this] {
                    UpdateButtonText(tr("OK"));
                    cancel_button->setProperty("kind", QStringLiteral("primary"));
                    cancel_button->style()->unpolish(cancel_button);
                    cancel_button->style()->polish(cancel_button);
                });
            }
        },
        [this](u16 min_x_, u16 min_y_, u16 max_x_, u16 max_y_) {
            completed = true;
            min_x = min_x_;
            min_y = min_y_;
            max_x = max_x_;
            max_y = max_y_;
        });
}

CalibrationConfigurationDialog::~CalibrationConfigurationDialog() = default;

void CalibrationConfigurationDialog::UpdateLabelText(const QString& text) {
    status_label->setText(text);
}

void CalibrationConfigurationDialog::UpdateButtonText(const QString& text) {
    cancel_button->setText(text);
}

void CalibrationConfigurationDialog::SetStatusIcon(const QString& icon, const QColor& tint) {
    spinner->stop();
    const qreal ratio = devicePixelRatioF();
    QPixmap tile(QSize(StatusIconSize, StatusIconSize) * ratio);
    tile.setDevicePixelRatio(ratio);
    tile.fill(Qt::transparent);
    QPainter p(&tile);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(icon == QStringLiteral("check-circle") ? QColor(tint.red(), tint.green(), tint.blue(), 0x1f)
                                                      : Spotlight::Color::Fill());
    p.drawRoundedRect(QRectF(0, 0, StatusIconSize, StatusIconSize), 10, 10);
    p.drawPixmap(9, 9, Spotlight::IconPixmap(icon, 18, tint, ratio));
    p.end();
    status_icon->setPixmap(tile);
}

void CalibrationConfigurationDialog::DrawSpinner() {
    const qreal ratio = devicePixelRatioF();
    QPixmap ring(QSize(StatusIconSize, StatusIconSize) * ratio);
    ring.setDevicePixelRatio(ratio);
    ring.fill(Qt::transparent);
    QPainter p(&ring);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(2.5, 2.5, StatusIconSize - 5, StatusIconSize - 5);
    p.setPen(QPen(Spotlight::Color::Stroke(), 3));
    p.drawEllipse(bounds);
    p.setPen(QPen(Spotlight::Color::Accent(), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(bounds, (90 - spinner_angle) * 16, -100 * 16);
    p.end();
    status_icon->setPixmap(ring);
}

ConfigureMotionTouch::ConfigureMotionTouch(QWidget* parent,
                                           InputCommon::InputSubsystem* input_subsystem_)
    : Spotlight::Dialog(parent), input_subsystem{input_subsystem_},
      ui(std::make_unique<Ui::ConfigureMotionTouch>()) {
    ui->setupUi(this);
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    SetIcon(QStringLiteral("hand-tap"), Spotlight::Color::Info());
    for (auto* section : {ui->labelTouchSection, ui->labelUdpSection}) {
        QFont font = section->font();
        font.setCapitalization(QFont::AllUppercase);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
        section->setFont(font);
    }
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (QWidget* widget : {static_cast<QWidget*>(ui->touch_calibration),
                            static_cast<QWidget*>(ui->udp_server),
                            static_cast<QWidget*>(ui->udp_port)}) {
        QFont font = mono;
        font.setPixelSize(widget == ui->touch_calibration ? 13 : 14);
        widget->setFont(font);
    }
    ui->udp_help->setMinimumHeight(
        ui->udp_help->heightForWidth(ui->touch_group_box->minimumWidth() - 26));
    ui->udp_add->setIcon(Spotlight::Icon(QStringLiteral("plus"), Spotlight::Color::Text(), 15));
    ui->udp_remove->setIcon(Spotlight::Icon(QStringLiteral("minus"), Spotlight::Color::Text(), 15));
    SetConfiguration();
    UpdateUiDisplay();
    ConnectEvents();
}

ConfigureMotionTouch::~ConfigureMotionTouch() = default;

void ConfigureMotionTouch::SetConfiguration() {
    const Common::ParamPackage touch_param(Settings::values.touch_device.GetValue());

    touch_from_button_maps = Settings::values.touch_from_button_maps;
    for (const auto& touch_map : touch_from_button_maps)
        ui->touch_from_button_map->addItem(QString::fromStdString(touch_map.name));
    if (auto const index = Settings::values.touch_from_button_map_index.GetValue(); int(index) < ui->touch_from_button_map->count())
        ui->touch_from_button_map->setCurrentIndex(index);

    min_x = touch_param.Get("min_x", 100);
    min_y = touch_param.Get("min_y", 50);
    max_x = touch_param.Get("max_x", 1800);
    max_y = touch_param.Get("max_y", 850);

    ui->udp_server->setText(QString::fromStdString("127.0.0.1"));
    ui->udp_port->setText(QString::number(26760));

    udp_server_list_model = new ServerListModel(this);
    udp_server_list_model->setStringList({});
    ui->udp_server_list->setModel(udp_server_list_model);

    std::stringstream ss(Settings::values.udp_input_servers.GetValue());
    std::string token;

    while (std::getline(ss, token, ',')) {
        const int row = udp_server_list_model->rowCount();
        udp_server_list_model->insertRows(row, 1);
        const QModelIndex index = udp_server_list_model->index(row);
        udp_server_list_model->setData(index, QString::fromStdString(token));
    }
}

void ConfigureMotionTouch::UpdateUiDisplay() {
    const QString cemuhook_udp = QStringLiteral("cemuhookudp");

    ui->touch_calibration->setVisible(true);
    ui->touch_calibration_config->setVisible(true);
    ui->touch_calibration_label->setVisible(true);
    ui->touch_calibration->setText(
        QStringLiteral("(%1, %2) - (%3, %4)").arg(min_x).arg(min_y).arg(max_x).arg(max_y));

    ui->udp_config_group_box->setVisible(true);
}

void ConfigureMotionTouch::ConnectEvents() {
    connect(ui->udp_test, &QPushButton::clicked, this, &ConfigureMotionTouch::OnCemuhookUDPTest);
    connect(ui->udp_add, &QPushButton::clicked, this, &ConfigureMotionTouch::OnUDPAddServer);
    connect(ui->udp_remove, &QPushButton::clicked, this, &ConfigureMotionTouch::OnUDPDeleteServer);
    connect(ui->touch_calibration_config, &QPushButton::clicked, this,
            &ConfigureMotionTouch::OnConfigureTouchCalibration);
    connect(ui->touch_from_button_config_btn, &QPushButton::clicked, this,
            &ConfigureMotionTouch::OnConfigureTouchFromButton);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
            &ConfigureMotionTouch::ApplyConfiguration);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [this] {
        if (CanCloseDialog()) {
            reject();
        }
    });
}

void ConfigureMotionTouch::OnUDPAddServer() {
    // Validator for IP address
    const QRegularExpression re(QStringLiteral(
        R"re(^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)re"));
    bool ok;
    const QString port_text = ui->udp_port->text();
    const QString server_text = ui->udp_server->text();
    const QString server_string = tr("%1:%2").arg(server_text, port_text);
    const int port_number = port_text.toInt(&ok, 10);
    const int row = udp_server_list_model->rowCount();

    if (!ok) {
        QMessageBox::warning(this, tr("Lichii"), tr("Port number has invalid characters"));
        return;
    }
    if (port_number < 0 || port_number > 65353) {
        QMessageBox::warning(this, tr("Lichii"), tr("Port has to be in range 0 and 65353"));
        return;
    }
    if (!re.match(server_text).hasMatch()) {
        QMessageBox::warning(this, tr("Lichii"), tr("IP address is not valid"));
        return;
    }
    // Search for duplicates
    for (const auto& item : udp_server_list_model->stringList()) {
        if (item == server_string) {
            QMessageBox::warning(this, tr("Lichii"), tr("This UDP server already exists"));
            return;
        }
    }
    // Limit server count to 8
    if (row == 8) {
        QMessageBox::warning(this, tr("Lichii"), tr("Unable to add more than 8 servers"));
        return;
    }

    udp_server_list_model->insertRows(row, 1);
    QModelIndex index = udp_server_list_model->index(row);
    udp_server_list_model->setData(index, server_string);
    ui->udp_server_list->setCurrentIndex(index);
}

void ConfigureMotionTouch::OnUDPDeleteServer() {
    udp_server_list_model->removeRows(ui->udp_server_list->currentIndex().row(), 1);
}

void ConfigureMotionTouch::OnCemuhookUDPTest() {
    ui->udp_test->setEnabled(false);
    ui->udp_test->setText(tr("Testing"));
    udp_test_in_progress = true;
    InputCommon::CemuhookUDP::TestCommunication(
        ui->udp_server->text().toStdString(), static_cast<u16>(ui->udp_port->text().toInt()),
        [this] {
            LOG_INFO(Frontend, "UDP input test success");
            QMetaObject::invokeMethod(this, [this] { ShowUDPTestResult(true); });
        },
        [this] {
            LOG_ERROR(Frontend, "UDP input test failed");
            QMetaObject::invokeMethod(this, [this] { ShowUDPTestResult(false); });
        });
}

void ConfigureMotionTouch::OnConfigureTouchCalibration() {
    ui->touch_calibration_config->setEnabled(false);
    ui->touch_calibration_config->setText(tr("Configuring"));
    CalibrationConfigurationDialog dialog(this, ui->udp_server->text().toStdString(),
                                          static_cast<u16>(ui->udp_port->text().toUInt()));
    dialog.exec();
    if (dialog.completed) {
        min_x = dialog.min_x;
        min_y = dialog.min_y;
        max_x = dialog.max_x;
        max_y = dialog.max_y;
        LOG_INFO(Frontend,
                 "UDP touchpad calibration config success: min_x={}, min_y={}, max_x={}, max_y={}",
                 min_x, min_y, max_x, max_y);
        UpdateUiDisplay();
    } else {
        LOG_ERROR(Frontend, "UDP touchpad calibration config failed");
    }
    ui->touch_calibration_config->setEnabled(true);
    ui->touch_calibration_config->setText(tr("Configure"));
}

void ConfigureMotionTouch::closeEvent(QCloseEvent* event) {
    if (CanCloseDialog()) {
        event->accept();
    } else {
        event->ignore();
    }
}

void ConfigureMotionTouch::ShowUDPTestResult(bool result) {
    udp_test_in_progress = false;
    if (result) {
        QMessageBox box(QMessageBox::Information, tr("Test Successful"),
                        tr("Successfully received data from the server."), QMessageBox::Ok, this);
        Spotlight::MarkSuccess(&box);
        box.exec();
    } else {
        QMessageBox::critical(this, tr("Test Failed"),
                              tr("Could not receive valid data from the server.<br>Please verify "
                                 "that the server is set up correctly and "
                                 "the address and port are correct."));
    }
    ui->udp_test->setEnabled(true);
    ui->udp_test->setText(tr("Test"));
}

void ConfigureMotionTouch::OnConfigureTouchFromButton() {
    ConfigureTouchFromButton dialog{this, touch_from_button_maps, input_subsystem,
                                    ui->touch_from_button_map->currentIndex()};
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    touch_from_button_maps = dialog.GetMaps();

    while (ui->touch_from_button_map->count() > 0) {
        ui->touch_from_button_map->removeItem(0);
    }
    for (const auto& touch_map : touch_from_button_maps) {
        ui->touch_from_button_map->addItem(QString::fromStdString(touch_map.name));
    }
    ui->touch_from_button_map->setCurrentIndex(dialog.GetSelectedIndex());
}

bool ConfigureMotionTouch::CanCloseDialog() {
    if (udp_test_in_progress) {
        QMessageBox::warning(this, tr("Lichii"),
                             tr("UDP Test or calibration configuration is in progress.<br>Please "
                                "wait for them to finish."));
        return false;
    }
    return true;
}

void ConfigureMotionTouch::ApplyConfiguration() {
    if (!CanCloseDialog()) {
        return;
    }

    Common::ParamPackage touch_param{};
    touch_param.Set("min_x", min_x);
    touch_param.Set("min_y", min_y);
    touch_param.Set("max_x", max_x);
    touch_param.Set("max_y", max_y);

    Settings::values.touch_device = touch_param.Serialize();
    Settings::values.touch_from_button_map_index = ui->touch_from_button_map->currentIndex();
    Settings::values.touch_from_button_maps = touch_from_button_maps;
    Settings::values.udp_input_servers = GetUDPServerString();
    input_subsystem->ReloadInputDevices();

    accept();
}

std::string ConfigureMotionTouch::GetUDPServerString() const {
    QString input_servers;

    for (const auto& item : udp_server_list_model->stringList()) {
        input_servers += item;
        input_servers += QLatin1Char{','};
    }

    // Remove last comma
    input_servers.chop(1);
    return input_servers.toStdString();
}
