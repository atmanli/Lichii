// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "lichii/spotlight/dialog.h"

class QKeySequenceEdit;

class SequenceDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit SequenceDialog(QWidget* parent, const QString& action);

    QKeySequence GetSequence() const;

private:
    QKeySequenceEdit* key_sequence;
    void keyPressEvent(QKeyEvent* event) override;
};
