// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lichii/spotlight/dialog.h"

class DepsDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit DepsDialog(QWidget* parent);
    ~DepsDialog() override;
};
