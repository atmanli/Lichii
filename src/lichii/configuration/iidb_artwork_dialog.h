// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <vector>
#include <QImage>
#include <QSet>
#include <QStringList>
#include "lichii/game/iidb_client.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/notice.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTabBar;

namespace IiDB {

class ArtworkDialog final : public Spotlight::Dialog {
    Q_OBJECT

public:
    ArtworkDialog(QWidget* parent, const QString& game_title, const QByteArray& key,
                  Client& client);
    ~ArtworkDialog() override;

    QImage TakeIcon();

private:
    void done(int result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void ResetSearch();
    void Search();
    void ResetGallery();
    void LoadPage();
    void LoadVisibleThumbnail();
    void UpdateThumbnailNotice();
    void ClearPreview();
    void Preview();
    void UpdateRequestControls();
    void SetStatus(const QString& text, Spotlight::Notice::Tone tone);
    void SetPreviewStatus(const QString& text, Spotlight::Notice::Tone tone);

    enum class Thumbnail { Pending, Attempted, Limited };

    struct GalleryItem {
        Asset asset;
        Thumbnail thumbnail = Thumbnail::Pending;
    };

    const QByteArray& key;
    Client& client;
    std::vector<Candidate> candidates;
    std::optional<ParentId> selected_parent;
    std::vector<GalleryItem> assets;
    QSet<qint64> asset_ids;
    QSet<QString> cursors;
    QString next_cursor;
    int pages = 0;
    bool skipped_assets = false;
    bool search_loading = false;
    bool page_loading = false;
    bool thumbnail_loading = false;
    bool preview_loading = false;
    QImage pending_icon;

    QLineEdit* title;
    QPushButton* search_button;
    QPushButton* key_button;
    QComboBox* matches;
    QTabBar* categories;
    const QStringList category_names{tr("iiSU Box Art"), tr("Box Art"), tr("Icons")};
    QStackedWidget* gallery_stack;
    QListWidget* gallery;
    QPushButton* more;
    QPushButton* preview_button;
    Spotlight::Notice* status;
    Spotlight::Notice* thumbnail_status;
    QLabel* source_preview;
    QLabel* icon_preview;
    QLabel* original_size;
    Spotlight::Notice* preview_status;
    QPushButton* use;
};

} // namespace IiDB
