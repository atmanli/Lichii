// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include "lichii/spotlight/theme.h"
#include "lichii/util/limitable_input_dialog.h"

LimitableInputDialog::LimitableInputDialog(QWidget* parent) : Spotlight::Dialog{parent} {
    SetIcon(QStringLiteral("text-aa"), Spotlight::Color::Accent());
    CreateUI();
    ConnectEvents();
}

LimitableInputDialog::~LimitableInputDialog() = default;

void LimitableInputDialog::CreateUI() {
    text_label = new QLabel(this);
    text_entry = new QLineEdit(this);
    text_label_invalid = new QLabel(this);
    buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    text_label->setFont(Spotlight::Font(13, 600));
    text_entry->setMinimumWidth(328);
    text_label_invalid->setFont(Spotlight::Font(11, 400));
    text_label_invalid->setStyleSheet(QStringLiteral("color: #8cffffff;"));
    text_label_invalid->setWordWrap(true);

    auto* const layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->addWidget(text_label);
    layout->addWidget(text_entry);
    layout->addWidget(text_label_invalid);
    layout->addSpacing(10);
    layout->addWidget(buttons);
}

void LimitableInputDialog::ConnectEvents() {
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString LimitableInputDialog::GetText(QWidget* parent, const QString& title, const QString& text,
                                      int min_character_limit, int max_character_limit,
                                      InputLimiter limit_type) {
    Q_ASSERT(min_character_limit <= max_character_limit);

    LimitableInputDialog dialog{parent};
    dialog.setWindowTitle(title);
    dialog.text_label->setText(text);
    dialog.text_entry->setMaxLength(max_character_limit);
    dialog.text_label_invalid->show();

    switch (limit_type) {
    case InputLimiter::Filesystem:
        dialog.invalid_characters = QStringLiteral("<>:;\"/\\|,.!?*");
        break;
    default:
        dialog.invalid_characters.clear();
        dialog.text_label_invalid->hide();
        break;
    }
    dialog.text_label_invalid->setText(
        tr("The text can't contain any of the following characters:\n%1")
            .arg(dialog.invalid_characters));

    auto* const ok_button = dialog.buttons->button(QDialogButtonBox::Ok);
    ok_button->setEnabled(false);
    connect(dialog.text_entry, &QLineEdit::textEdited, [&] {
        if (!dialog.invalid_characters.isEmpty()) {
            dialog.RemoveInvalidCharacters();
        }
        ok_button->setEnabled(dialog.text_entry->text().length() >= min_character_limit);
    });

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }

    return dialog.text_entry->text();
}

void LimitableInputDialog::RemoveInvalidCharacters() {
    auto cpos = text_entry->cursorPosition();
    for (int i = 0; i < text_entry->text().length(); i++) {
        if (invalid_characters.contains(text_entry->text().at(i))) {
            text_entry->setText(text_entry->text().remove(i, 1));
            i--;
            cpos--;
        }
    }
    text_entry->setCursorPosition(cpos);
}
