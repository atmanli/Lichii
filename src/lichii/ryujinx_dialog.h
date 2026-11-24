// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <QPixmap>

#include "lichii/spotlight/dialog.h"

class QAbstractButton;

class RyujinxDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit RyujinxDialog(std::filesystem::path lichii_path, std::filesystem::path ryujinx_path,
                           const QString& ryujinx_save_id, const QString& title,
                           const QPixmap& icon, QWidget* parent = nullptr);
    ~RyujinxDialog();

private slots:
    void fromLichii();
    void fromRyujinx();

private:
    std::filesystem::path m_lichii;
    std::filesystem::path m_ryu;
    QAbstractButton* m_keep_lichii;
};
