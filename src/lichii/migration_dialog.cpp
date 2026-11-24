// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "migration_dialog.h"

#include <QAbstractButton>
#include <QLabel>
#include <QPushButton>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace {

// A check box or radio indicator and the gap after it, so a description lines up with the text.
constexpr int OptionIndent = 30;

} // namespace

MigrationDialog::MigrationDialog(QWidget* parent) : Spotlight::Dialog(parent) {
    SetIcon(u"arrow-square-out", Spotlight::Color::Success());
    SetCardWidth(600);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);

    m_boxes = new QVBoxLayout;
    m_boxes->setSpacing(12);
    m_buttons = new QHBoxLayout;
    m_buttons->setContentsMargins(0, 6, 0, 0);
    m_buttons->setSpacing(10);
    m_buttons->addStretch(1);

    layout->addLayout(m_boxes);
    layout->addLayout(m_buttons);
}

MigrationDialog::~MigrationDialog() = default;

void MigrationDialog::setText(const QString& text) {
    SetSubtitle(text);
}

void MigrationDialog::addField(const QString& label, QWidget* field) {
    auto* column = new QVBoxLayout;
    column->setSpacing(8);
    auto* caption = new QLabel(label);
    caption->setFont(Spotlight::Font(13, 600));
    Spotlight::SetTextColor(caption, Spotlight::Color::TextSecondary());
    caption->setBuddy(field);
    column->addWidget(caption);
    column->addWidget(field);
    m_boxes->addLayout(column);
}

void MigrationDialog::addOption(QAbstractButton* option, const QString& description) {
    auto* column = new QVBoxLayout;
    column->setSpacing(0);
    option->setFont(Spotlight::Font(14, 600));
    column->addWidget(option);
    auto* detail = new QLabel(description);
    detail->setFont(Spotlight::Font(13, 400));
    detail->setWordWrap(true);
    detail->setContentsMargins(OptionIndent, 0, 0, 0);
    Spotlight::SetTextColor(detail, Spotlight::Color::TextSecondary());
    column->addWidget(detail);
    m_boxes->addLayout(column);
}

void MigrationDialog::addBox(QWidget* box) {
    m_boxes->addWidget(box);
}

QAbstractButton* MigrationDialog::addButton(const QString& text, const bool reject) {
    QPushButton* button = new QPushButton(text, this);
    if (reject) {
        button->setProperty("kind", QStringLiteral("ghost"));
        button->setAutoDefault(false);
        m_buttons->insertWidget(0, button);
    } else {
        button->setIcon(Spotlight::Icon(u"download-simple", Spotlight::Color::OnAccent(), 15));
        button->setIconSize({15, 15});
        button->setDefault(true);
        m_buttons->addWidget(button);
    }

    connect(button, &QAbstractButton::clicked, this, [this, button, reject]() {
        m_clickedButton = button;

        if (reject) {
            this->reject();
        } else {
            this->accept();
        }
    });
    return button;
}

QAbstractButton* MigrationDialog::clickedButton() const {
    return m_clickedButton;
}
