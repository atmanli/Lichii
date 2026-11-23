// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "lichii/spotlight/dialog.h"

class QSpinBox;

namespace Spotlight {
class Notice;
}

class SetPlayTimeDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit SetPlayTimeDialog(QWidget* parent, const QString& title, u64 current_play_time);
    ~SetPlayTimeDialog() override;

    u64 GetTotalSeconds() const;

private:
    void OnValueChanged();

    QSpinBox* hours;
    QSpinBox* minutes;
    QSpinBox* seconds;
    Spotlight::Notice* error;
};
