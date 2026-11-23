// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2016 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <vector>

#include <QList>
#include <QSet>
#include <QWidget>

#include "core/file_sys/vfs/vfs_types.h"

namespace Core {
class System;
}

class QGraphicsScene;
class QStandardItem;
class QStandardItemModel;
class QTreeView;
class QVBoxLayout;

namespace Ui {
class ConfigurePerGameAddons;
}

class ConfigurePerGameAddons : public QWidget {
    Q_OBJECT

public:
    enum PatchData {
        NUMERIC_VERSION = Qt::UserRole,
        PATCH_LOCATION,
        PATCH_TYPE = PATCH_LOCATION + 2
    };

    explicit ConfigurePerGameAddons(Core::System& system_, QWidget* parent = nullptr);
    ~ConfigurePerGameAddons() override;

    /// Save all button configurations to settings file
    void ApplyConfiguration();

    void LoadFromFile(FileSys::VirtualFile file_);
    void SetFile(FileSys::VirtualFile file_);

    void SetTitleId(u64 id);
    QStandardItemModel* Model() const {
        return item_model;
    }
    void ReloadAfterModStore(const QSet<QString>& store_toggled);

signals:
    void BrowseModsRequested();

public slots:
    void InstallMods(const QStringList& mods);
    void InstallModPath(const QString& path, const QString& fallbackName = {});

    void InstallModFolder();
    void InstallModZip();

    void AddonDeleteRequested(QList<QModelIndex> selected);

protected:
    void showContextMenu(const QPoint& pos);

private:
    static constexpr int LOADED_CHECK_STATE = PATCH_LOCATION + 1;

    static QString AddonKey(const QStandardItem& item);
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void LoadConfiguration();
    void OnItemChanged(QStandardItem* item);

    std::unique_ptr<Ui::ConfigurePerGameAddons> ui;
    FileSys::VirtualFile file;
    u64 title_id{};

    QVBoxLayout* layout;
    QTreeView* tree_view;
    QStandardItemModel* item_model;

    std::vector<QList<QStandardItem*>> list_items;
    std::vector<QStandardItem*> update_items;

    Core::System& system;
};
