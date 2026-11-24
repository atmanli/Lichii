// SPDX-License-Identifier: GPL-2.0-or-later

#include <QLayout>
#include "core/core.h"
#include "ui_configure_input_profile_dialog.h"
#include "lichii/configuration/configure_input_player.h"
#include "lichii/configuration/configure_input_profile_dialog.h"
#include "lichii/spotlight/theme.h"

ConfigureInputProfileDialog::ConfigureInputProfileDialog(
    QWidget* parent, InputCommon::InputSubsystem* input_subsystem, InputProfiles* profiles,
    Core::System& system)
    : Spotlight::Dialog(parent), ui(std::make_unique<Ui::ConfigureInputProfileDialog>()),
      profile_widget(new ConfigureInputPlayer(this, 9, nullptr, input_subsystem, profiles,
                                              system.HIDCore(), system.IsPoweredOn(), false)) {
    ui->setupUi(this);
    SetIcon(QStringLiteral("game-controller"), Spotlight::Color::Purple());
    layout()->setContentsMargins(0, 0, 0, 0);
    ui->horizontalLayout->setSpacing(12);
    setStyleSheet(QStringLiteral("QPushButton#clear_all_button, QPushButton#restore_defaults_button {"
                                 " background: #14ffffff; color: #ffffff; border: none;"
                                 " border-radius: 20px; min-height: 40px; padding: 0 18px;"
                                 " font: 600 14px \"Lexend\"; }"
                                 "QPushButton#clear_all_button:hover,"
                                 " QPushButton#restore_defaults_button:hover { background: #1fffffff; }"));

    ui->controllerLayout->addWidget(profile_widget);

    connect(ui->clear_all_button, &QPushButton::clicked, this,
            [this] { profile_widget->ClearAll(); });
    connect(ui->restore_defaults_button, &QPushButton::clicked, this,
            [this] { profile_widget->RestoreDefaults(); });

    RetranslateUI();
}

ConfigureInputProfileDialog::~ConfigureInputProfileDialog() = default;

void ConfigureInputProfileDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    Spotlight::Dialog::changeEvent(event);
}

void ConfigureInputProfileDialog::RetranslateUI() {
    ui->retranslateUi(this);
}
