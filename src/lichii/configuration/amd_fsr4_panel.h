// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Shown under the FSR 4 setting. On Windows: whether AMD's FidelityFX runtime is downloaded, and a
// button that fetches it from AMD's FidelityFX SDK release on GitHub. On Linux: whether the fsr4-vk
// port is in the package.

#pragma once

#include <QWidget>

namespace GraphicsPanel {
class Notice;
}
namespace Spotlight {
class PillButton;
}

class AmdFsr4Panel : public QWidget {
public:
    explicit AmdFsr4Panel(QWidget* parent);

    void Refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void Download();

    GraphicsPanel::Notice* status;
    Spotlight::PillButton* download_button;
};
