// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <QApplication>
#include <QFile>
#include <QFutureWatcher>
#include <QLinearGradient>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "lichii/configuration/iidb_key_dialog.h"
#include "lichii/game/artwork_image.h"
#include "lichii/game/iidb_client.h"
#include "lichii/spotlight/hero_art.h"
#include "lichii/spotlight/theme.h"
#include "lichii/util/icon_search.h"
#include "qt_common/config/uisettings.h"

namespace Spotlight {
namespace {

constexpr int MaxWidth = 1920;
constexpr qsizetype MaxCacheKiB = 64 * 1024;
const QString SteamGridDbSource = QStringLiteral("steamgriddb");
const QString IiDbSource = QStringLiteral("iidb");

QString ArtPath(u64 program_id, std::string_view extension) {
    return QString::fromStdString(Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir) / "artwork" / "hero" /
        fmt::format("{:016X}.{}", program_id, extension)));
}

bool WriteFile(const QString& path, const auto& write) {
    if (!Common::FS::CreateDirs(std::filesystem::path(path.toStdU16String()).parent_path())) {
        return false;
    }
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && write(file) && file.commit();
}

QImage Store(u64 program_id, QImage image) {
    if (image.isNull()) {
        return {};
    }
    if (image.width() > MaxWidth) {
        image = image.scaledToWidth(MaxWidth, Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_RGB32);
    if (!WriteFile(ArtPath(program_id, "jpg"),
                   [&image](QSaveFile& file) { return image.save(&file, "JPG", 90); })) {
        LOG_WARNING(Frontend, "Cannot save the hero art of {:016X}", program_id);
    }
    return image;
}

struct DiskResult {
    QImage image;
    QStringList searched;
    bool failed = false;
    bool ask_iidb = false;
};

DiskResult LoadOrLookUp(u64 program_id, const QString& title, const std::string& steamgriddb_key,
                        bool iidb_key, const std::atomic_bool& cancel) {
    DiskResult out;
    if (QImage image(ArtPath(program_id, "jpg")); !image.isNull()) {
        out.image = std::move(image);
        return out;
    }
    if (QFile marker(ArtPath(program_id, "none")); marker.open(QIODevice::ReadOnly)) {
        out.searched = QString::fromUtf8(marker.read(256)).split(u',', Qt::SkipEmptyParts);
    }
    if (!steamgriddb_key.empty() && !out.searched.contains(SteamGridDbSource)) {
        // Switch 2 titles are often remakes that share the name of an older game.
        auto result = LookUpSteamGridDbArtwork(title.toStdString(), steamgriddb_key, cancel,
                                               (program_id >> 56) == 0x04, SteamGridDbArt::Hero);
        if (result.status == ArtworkLookupStatus::Found) {
            out.image = Store(program_id, std::move(result.image));
            return out;
        }
        if (result.status == ArtworkLookupStatus::NotFound) {
            out.searched.append(SteamGridDbSource);
        } else {
            out.failed = true;
        }
    }
    out.ask_iidb = iidb_key && !out.searched.contains(IiDbSource);
    return out;
}

double Aspect(const IiDB::Asset& asset) {
    static const QRegularExpression size(QStringLiteral("(\\d+)\\D+(\\d+)"));
    const auto match = size.match(asset.resolution);
    const double height = match.captured(2).toDouble();
    return match.hasMatch() && height > 0 ? match.captured(1).toDouble() / height : 0.0;
}

} // namespace

HeroArt& HeroArt::Instance() {
    static auto* const instance = new HeroArt(qApp);
    return *instance;
}

HeroArt::HeroArt(QObject* parent) : QObject(parent) {
    images.setMaxCost(MaxCacheKiB);
    pool.setMaxThreadCount(1);
}

HeroArt::~HeroArt() {
    *cancel = true;
    queue.clear();
    if (iidb) {
        iidb->Cancel();
        iidb->CancelImage();
    }
    pool.waitForDone();
}

QImage HeroArt::Image(u64 program_id) const {
    const QImage* image = images.object(program_id);
    return image ? *image : QImage{};
}

void HeroArt::Request(u64 program_id, const QString& title) {
    if (program_id == 0 || title.trimmed().isEmpty() || images.contains(program_id) ||
        pending.contains(program_id) || without_art.contains(program_id)) {
        return;
    }
    pending.insert(program_id);
    // The game asked for last is the one on screen.
    queue.prepend({program_id, title.trimmed()});
    StartNext();
}

void HeroArt::StartNext() {
    if (running || queue.isEmpty() || *cancel) {
        return;
    }
    running = true;
    const Lookup lookup = queue.takeFirst();
    auto* const watcher = new QFutureWatcher<DiskResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, lookup] {
        watcher->deleteLater();
        const DiskResult result = watcher->result();
        if (result.image.isNull() && result.ask_iidb && !*cancel) {
            SearchIiDB(lookup, result.searched, result.failed);
            return;
        }
        Finish(lookup, result.image, result.searched, result.failed);
    });
    watcher->setFuture(QtConcurrent::run(
        &pool, [lookup, key = UISettings::values.steamgriddb_api_key.GetValue(),
                iidb_key = !IiDB::SessionKey().isEmpty(), cancel_flag = cancel] {
            return LoadOrLookUp(lookup.program_id, lookup.title, key, iidb_key, *cancel_flag);
        }));
}

void HeroArt::SearchIiDB(const Lookup& lookup, QStringList searched, bool failed) {
    if (!iidb) {
        iidb = std::make_unique<IiDB::Client>(IiDB::SessionRateLimits());
    }
    const QByteArray key = IiDB::SessionKey();
    const auto stop = [this, lookup, searched, failed](IiDB::Error error) {
        const bool none = error == IiDB::Error::None || error == IiDB::Error::NotFound;
        Finish(lookup, {}, none ? searched + QStringList{IiDbSource} : searched, failed || !none);
    };
    iidb->Search(key, lookup.title, [this, lookup, searched, failed, key, stop](
                                        std::vector<IiDB::Candidate> candidates,
                                        IiDB::Error error) mutable {
        if (error != IiDB::Error::None || candidates.empty()) {
            stop(error);
            return;
        }
        const auto on_switch = std::ranges::find_if(candidates, [](const IiDB::Candidate& game) {
            return game.platform.contains(QStringLiteral("Switch"), Qt::CaseInsensitive);
        });
        const IiDB::ParentId parent =
            (on_switch != candidates.end() ? *on_switch : candidates.front()).id;
        iidb->Gallery(
            key, parent, IiDB::Category::IiSUBoxArt, {},
            [this, lookup, searched, failed, key, parent, stop](IiDB::GalleryPage first,
                                                                IiDB::Error first_error) mutable {
                if (first_error != IiDB::Error::None && first_error != IiDB::Error::NotFound) {
                    stop(first_error);
                    return;
                }
                iidb->Gallery(
                    key, parent, IiDB::Category::BoxArt, {},
                    [this, lookup, searched, failed, stop, assets = std::move(first.assets)](
                        IiDB::GalleryPage second, IiDB::Error second_error) mutable {
                        if (second_error != IiDB::Error::None &&
                            second_error != IiDB::Error::NotFound) {
                            stop(second_error);
                            return;
                        }
                        assets.insert(assets.end(), second.assets.begin(), second.assets.end());
                        if (assets.empty()) {
                            stop(IiDB::Error::NotFound);
                            return;
                        }
                        const auto widest = std::ranges::max_element(assets, {}, Aspect);
                        searched.append(IiDbSource);
                        iidb->Download(widest->original, false,
                                       [this, lookup, searched, failed](QByteArray bytes,
                                                                        IiDB::Error download) {
                                           if (download != IiDB::Error::None) {
                                               Finish(lookup, {}, searched, true);
                                               return;
                                           }
                                           auto* const watcher = new QFutureWatcher<QImage>(this);
                                           connect(watcher, &QFutureWatcherBase::finished, this,
                                                   [this, watcher, lookup, searched, failed] {
                                                       watcher->deleteLater();
                                                       Finish(lookup, watcher->result(), searched,
                                                              failed);
                                                   });
                                           watcher->setFuture(QtConcurrent::run(
                                               &pool, [id = lookup.program_id, bytes] {
                                                   return Store(id, ArtworkImage::Decode(
                                                                        bytes, {MaxWidth, MaxWidth}));
                                               }));
                                       });
                    });
            });
    });
}

void HeroArt::Finish(const Lookup& lookup, const QImage& image, const QStringList& searched,
                     bool failed) {
    running = false;
    pending.remove(lookup.program_id);
    if (!image.isNull()) {
        images.insert(lookup.program_id, new QImage(image),
                      std::max<qsizetype>(1, image.sizeInBytes() / 1024));
        emit Ready(lookup.program_id);
    } else {
        without_art.insert(lookup.program_id);
        if (!failed && !searched.isEmpty() && !*cancel &&
            !WriteFile(ArtPath(lookup.program_id, "none"), [&searched](QSaveFile& file) {
                return file.write(searched.join(u',').toUtf8()) >= 0;
            })) {
            LOG_WARNING(Frontend, "Cannot save the hero art marker of {:016X}", lookup.program_id);
        }
    }
    StartNext();
}

void HeroArt::Paint(QPainter& painter, const QRectF& bounds, const QImage& image) {
    if (image.isNull() || bounds.isEmpty()) {
        return;
    }
    painter.save();
    painter.setClipRect(bounds);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSizeF size = image.size();
    if (size.width() / size.height() >= bounds.width() / bounds.height() * 0.75) {
        const QSizeF area = size.scaled(bounds.size(), Qt::KeepAspectRatioByExpanding);
        painter.drawImage(QRectF(bounds.center() - QPointF(area.width() / 2, area.height() / 2), area),
                          image);
    } else {
        const QSizeF area = size.scaled(bounds.size(), Qt::KeepAspectRatio);
        const QRectF target(bounds.right() - area.width(), bounds.top(), area.width(),
                            area.height());
        painter.drawImage(target, image);
        const QRectF edge(target.topLeft(), QSizeF(target.width() * 0.4, target.height()));
        QLinearGradient fade(edge.topLeft(), edge.topRight());
        auto clear = Color::Window();
        clear.setAlpha(0);
        fade.setColorAt(0, Color::Window());
        fade.setColorAt(1, clear);
        painter.fillRect(edge, fade);
    }
    painter.restore();
}

QColor HeroArt::Tint(const QPixmap& icon) {
    if (icon.isNull()) {
        return {};
    }
    const QImage small = icon.toImage().scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_ARGB32);
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 weight = 0;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QRgb pixel = small.pixel(x, y);
            const int alpha = qAlpha(pixel);
            red += qRed(pixel) * alpha;
            green += qGreen(pixel) * alpha;
            blue += qBlue(pixel) * alpha;
            weight += alpha;
        }
    }
    if (weight == 0) {
        return {};
    }
    return QColor(int(red / weight), int(green / weight), int(blue / weight), 0x2e);
}

} // namespace Spotlight
