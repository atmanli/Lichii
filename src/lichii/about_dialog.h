// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "lichii/spotlight/dialog.h"

class AboutDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent);
};
