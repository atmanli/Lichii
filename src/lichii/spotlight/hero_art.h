// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <memory>
#include <QCache>
#include <QColor>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

#include "common/common_types.h"

class QPainter;
class QPixmap;
class QRectF;

namespace IiDB {
class Client;
}

namespace Spotlight {

// Wide art for the launcher hero and the loading screen: a SteamGridDB hero, else the widest iiDB
// box art, cached under <user>/artwork/hero. Each game is looked up at most once, in the
// background and only with an API key. A game no source has art for gets a marker naming the
// sources searched, so it is asked again only when a key for another source is added.
class HeroArt final : public QObject {
    Q_OBJECT

public:
    static HeroArt& Instance();

    // Null while the art loads and for a game without art.
    QImage Image(u64 program_id) const;
    void Request(u64 program_id, const QString& title);

    // Draws art sharp into bounds: wide art covers them, narrower art fills their height on the
    // right and fades out to the left.
    static void Paint(QPainter& painter, const QRectF& bounds, const QImage& image);
    // A faint average colour of the icon, for a game without art.
    static QColor Tint(const QPixmap& icon);

signals:
    void Ready(u64 program_id);

private:
    struct Lookup {
        u64 program_id;
        QString title;
    };

    explicit HeroArt(QObject* parent);
    ~HeroArt() override;

    void StartNext();
    void SearchIiDB(const Lookup& lookup, QStringList searched, bool failed);
    void Finish(const Lookup& lookup, const QImage& image, const QStringList& searched,
                bool failed);

    QCache<u64, QImage> images;
    QSet<u64> pending;
    QSet<u64> without_art;
    QList<Lookup> queue;
    bool running = false;
    std::shared_ptr<std::atomic_bool> cancel{std::make_shared<std::atomic_bool>(false)};
    QThreadPool pool;
    std::unique_ptr<IiDB::Client> iidb;
};

} // namespace Spotlight
