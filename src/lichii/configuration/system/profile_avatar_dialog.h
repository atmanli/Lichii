// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>
#include <QDialog>
#include "lichii/spotlight/dialog.h"

class QListWidget;
class QToolButton;

class ProfileAvatarDialog : public Spotlight::Dialog {
public:
    explicit ProfileAvatarDialog(QWidget* parent);
    ~ProfileAvatarDialog();

    void LoadImages(const QVector<QPixmap>& avatar_images);
    bool AreImagesLoaded() const;
    QPixmap GetSelectedAvatar();

private:
    void SetBackgroundColor(const QColor& color);
    QPixmap CreateAvatar(const QPixmap& avatar);
    void RefreshAvatars();

    QVector<QPixmap> avatar_image_store;
    QListWidget* avatar_list;
    QColor avatar_bg_color;
    std::vector<std::pair<QToolButton*, QColor>> swatches;
};
