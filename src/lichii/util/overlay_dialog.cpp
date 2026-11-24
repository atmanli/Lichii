// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <QKeyEvent>
#include <QScreen>
#include <QWindow>

#include "core/core.h"
#include "hid_core/frontend/input_interpreter.h"
#include "hid_core/hid_types.h"
#include "ui_overlay_dialog.h"
#include "lichii/util/overlay_dialog.h"
#include "lichii/spotlight/theme.h"

namespace {

constexpr int MessageCardWidth = 530;
constexpr int TextCheckCardWidth = 420;
constexpr int CardPadding = 24;

} // Anonymous namespace

OverlayDialog::OverlayDialog(QWidget* parent, Core::System& system, const QString& title_text,
                             const QString& body_text, const QString& left_button_text,
                             const QString& right_button_text, Qt::Alignment alignment,
                             bool use_rich_text_)
    : QDialog(parent), ui{std::make_unique<Ui::OverlayDialog>()}, use_rich_text{use_rich_text_} {
    ui->setupUi(this);
    for (auto* content : {ui->verticalLayout_2, ui->verticalLayout_3}) {
        content->setContentsMargins(CardPadding, CardPadding, CardPadding, CardPadding);
        content->setSpacing(12);
    }
    for (auto* buttons : {ui->horizontalLayout, ui->horizontalLayout_2}) {
        buttons->setContentsMargins(0, 8, 0, 0);
        buttons->setSpacing(10);
        buttons->insertStretch(0);
    }
    for (auto* button :
         {ui->button_cancel, ui->button_ok_label, ui->button_cancel_rich, ui->button_ok_rich}) {
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::CustomizeWindowHint);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_TranslucentBackground);

    if (use_rich_text) {
        InitializeRichTextDialog(title_text, body_text, left_button_text, right_button_text,
                                 alignment);
    } else {
        InitializeRegularTextDialog(title_text, body_text, left_button_text, right_button_text,
                                    alignment);
    }

    MoveAndResizeWindow();

    // TODO (Morph): Remove this when InputInterpreter no longer relies on the HID backend
    if (system.IsPoweredOn() && !ui->buttonsDialog->isHidden()) {
        input_interpreter = std::make_unique<InputInterpreter>(system);

        StartInputThread();
    }
}

OverlayDialog::~OverlayDialog() {
    StopInputThread();
}

void OverlayDialog::InitializeRegularTextDialog(const QString& title_text, const QString& body_text,
                                                const QString& left_button_text,
                                                const QString& right_button_text,
                                                Qt::Alignment alignment) {
    ui->stackedDialog->setCurrentIndex(0);

    ui->label_title->setText(title_text);
    ui->label_dialog->setText(body_text);
    ui->button_cancel->setText(left_button_text);
    ui->button_ok_label->setText(right_button_text);

    ui->label_dialog->setAlignment(alignment);

    if (title_text.isEmpty()) {
        ui->label_title->hide();
    }

    if (left_button_text.isEmpty()) {
        ui->button_cancel->hide();
        ui->button_cancel->setEnabled(false);
    }

    if (right_button_text.isEmpty()) {
        ui->button_ok_label->hide();
        ui->button_ok_label->setEnabled(false);
    }

    if (ui->button_cancel->isHidden() && ui->button_ok_label->isHidden()) {
        ui->buttonsDialog->hide();
        return;
    }

    connect(
        ui->button_cancel, &QPushButton::clicked, this,
        [this](bool) {
            StopInputThread();
            QDialog::reject();
        },
        Qt::QueuedConnection);
    connect(
        ui->button_ok_label, &QPushButton::clicked, this,
        [this](bool) {
            StopInputThread();
            QDialog::accept();
        },
        Qt::QueuedConnection);
}

void OverlayDialog::InitializeRichTextDialog(const QString& title_text, const QString& body_text,
                                             const QString& left_button_text,
                                             const QString& right_button_text,
                                             Qt::Alignment alignment) {
    ui->stackedDialog->setCurrentIndex(1);

    ui->label_title_rich->setText(title_text);
    ui->text_browser_dialog->setText(body_text);
    ui->button_cancel_rich->setText(left_button_text);
    ui->button_ok_rich->setText(right_button_text);

    // TODO (Morph/Rei): Replace this with something that works better
    ui->text_browser_dialog->setAlignment(alignment);

    if (title_text.isEmpty()) {
        ui->label_title_rich->hide();
    }

    if (left_button_text.isEmpty()) {
        ui->button_cancel_rich->hide();
        ui->button_cancel_rich->setEnabled(false);
    }

    if (right_button_text.isEmpty()) {
        ui->button_ok_rich->hide();
        ui->button_ok_rich->setEnabled(false);
    }

    if (ui->button_cancel_rich->isHidden() && ui->button_ok_rich->isHidden()) {
        ui->buttonsRichDialog->hide();
        return;
    }

    connect(
        ui->button_cancel_rich, &QPushButton::clicked, this,
        [this](bool) {
            StopInputThread();
            QDialog::reject();
        },
        Qt::QueuedConnection);
    connect(
        ui->button_ok_rich, &QPushButton::clicked, this,
        [this](bool) {
            StopInputThread();
            QDialog::accept();
        },
        Qt::QueuedConnection);
}

void OverlayDialog::MoveAndResizeWindow() {
    const auto pos = parentWidget()->mapToGlobal(parentWidget()->rect().topLeft());
    const int width = parentWidget()->width();
    const int height = parentWidget()->height();

    const QFont title_font = Spotlight::DisplayFont(22);
    const QFont body_font = Spotlight::Font(14, 400);
    const QFont button_font = Spotlight::Font(14, 600);
    if (use_rich_text) {
        ui->label_title_rich->setFont(title_font);
        ui->text_browser_dialog->setFont(body_font);
        ui->button_cancel_rich->setFont(button_font);
        ui->button_ok_rich->setFont(button_font);
    } else {
        ui->label_title->setFont(title_font);
        ui->label_dialog->setFont(body_font);
        ui->button_cancel->setFont(button_font);
        ui->button_ok_label->setFont(button_font);
        const int card_width = ui->label_title->isHidden() ? TextCheckCardWidth : MessageCardWidth;
        ui->contentDialog->setFixedWidth(std::min(card_width, width - 2 * CardPadding));
    }

    QDialog::move(pos);
    QDialog::resize(width, height);
}

template <Core::HID::NpadButton... T>
void OverlayDialog::HandleButtonPressedOnce() {
    const auto f = [this](Core::HID::NpadButton button) {
        if (input_interpreter->IsButtonPressedOnce(button)) {
            TranslateButtonPress(button);
        }
    };

    (f(T), ...);
}

void OverlayDialog::TranslateButtonPress(Core::HID::NpadButton button) {
    QPushButton* left_button = use_rich_text ? ui->button_cancel_rich : ui->button_cancel;
    QPushButton* right_button = use_rich_text ? ui->button_ok_rich : ui->button_ok_label;

    // TODO (Morph): Handle QTextBrowser text scrolling
    // TODO (Morph): focusPrevious/NextChild() doesn't work well with the rich text dialog, fix it

    switch (button) {
    case Core::HID::NpadButton::A:
    case Core::HID::NpadButton::B:
        if (left_button->hasFocus()) {
            left_button->click();
        } else if (right_button->hasFocus()) {
            right_button->click();
        }
        break;
    case Core::HID::NpadButton::Left:
    case Core::HID::NpadButton::StickLLeft:
        focusPreviousChild();
        break;
    case Core::HID::NpadButton::Right:
    case Core::HID::NpadButton::StickLRight:
        focusNextChild();
        break;
    default:
        break;
    }
}

void OverlayDialog::StartInputThread() {
    input_thread = std::jthread([&](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            input_interpreter->PollInput();
            HandleButtonPressedOnce<Core::HID::NpadButton::A, Core::HID::NpadButton::B,
                                    Core::HID::NpadButton::Left, Core::HID::NpadButton::Right,
                                    Core::HID::NpadButton::StickLLeft,
                                    Core::HID::NpadButton::StickLRight>();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void OverlayDialog::StopInputThread() {
    input_thread.request_stop();
}

void OverlayDialog::keyPressEvent(QKeyEvent* e) {
    if (!ui->buttonsDialog->isHidden() || e->key() != Qt::Key_Escape) {
        QDialog::keyPressEvent(e);
    }
}
