// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "frontend_common/play_time_manager.h"
#include "lichii/set_play_time_dialog.h"
#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace {

QSpinBox* AddField(QHBoxLayout* row, const QString& label_text, int maximum,
                   const std::string& value) {
    auto* column = new QVBoxLayout;
    column->setSpacing(8);
    auto* label = new QLabel(label_text);
    label->setFont(Spotlight::Font(13, 600));
    Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
    auto* field = new QSpinBox;
    field->setRange(0, maximum);
    field->setButtonSymbols(QAbstractSpinBox::NoButtons);
    field->ensurePolished();
    const int digits = field->fontMetrics().horizontalAdvance(QString::number(maximum));
    field->setFixedWidth(std::max(label->sizeHint().width(), digits + 30));
    field->setValue(QString::fromStdString(value).toInt());
    label->setBuddy(field);
    column->addWidget(label);
    column->addWidget(field);
    row->addLayout(column);
    return field;
}

} // namespace

SetPlayTimeDialog::SetPlayTimeDialog(QWidget* parent, const QString& title, u64 current_play_time)
    : Spotlight::Dialog(parent) {
    setWindowTitle(tr("Play time"));
    SetIcon(u"clock", Spotlight::Color::Info());
    SetSubtitle(title);
    setModal(true);
    SetCardWidth(440);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* fields = new QHBoxLayout;
    fields->setSpacing(10);
    hours = AddField(fields, tr("Hours"), 9999,
                     PlayTime::PlayTimeManager::GetPlayTimeHours(current_play_time));
    minutes = AddField(fields, tr("Minutes"), 59,
                       PlayTime::PlayTimeManager::GetPlayTimeMinutes(current_play_time));
    seconds = AddField(fields, tr("Seconds"), 59,
                       PlayTime::PlayTimeManager::GetPlayTimeSeconds(current_play_time));
    fields->addStretch();
    layout->addLayout(fields);

    auto* reset = new QPushButton(
        Spotlight::Icon(u"arrow-counter-clockwise", Spotlight::Color::Destructive(), 15),
        tr("Reset to zero…"));
    reset->setProperty("kind", QStringLiteral("destructive"));
    reset->setProperty("compact", true);
    reset->setIconSize({15, 15});
    reset->setAutoDefault(false);
    connect(reset, &QPushButton::clicked, this, [this] {
        hours->setValue(0);
        minutes->setValue(0);
        seconds->setValue(0);
    });
    layout->addWidget(reset);

    error = new Spotlight::Notice;
    error->SetTone(Spotlight::Notice::Tone::Error);
    error->setText(tr("Total play time reached maximum."));
    error->hide();
    layout->addWidget(error);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->setContentsMargins(0, 6, 0, 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (QSpinBox* field : {hours, minutes, seconds}) {
        connect(field, &QSpinBox::valueChanged, this, &SetPlayTimeDialog::OnValueChanged);
    }
}

SetPlayTimeDialog::~SetPlayTimeDialog() = default;

u64 SetPlayTimeDialog::GetTotalSeconds() const {
    return static_cast<u64>(hours->value()) * 3600 + static_cast<u64>(minutes->value()) * 60 +
           static_cast<u64>(seconds->value());
}

void SetPlayTimeDialog::OnValueChanged() {
    constexpr u64 max_reasonable_time = 9999ULL * 3600;
    error->setVisible(GetTotalSeconds() > max_reasonable_time);
}
