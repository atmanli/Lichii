// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/configuration/iidb_artwork_dialog.h"

#include <array>
#include <utility>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QPainter>
#include <QPainterPath>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include "lichii/configuration/iidb_key_dialog.h"
#include "lichii/game/artwork_image.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace IiDB {
namespace {

using namespace Qt::StringLiterals;

constexpr std::array Categories{Category::IiSUBoxArt, Category::BoxArt, Category::Icons};
constexpr int MaxPages = 5;
constexpr QSize ThumbnailSize{136, 104};
constexpr QSize TileSize{152, 158};
constexpr QSize PreviewSize{142, 142};

// A gallery tile: the thumbnail fitted on a dark plate, the artwork's name and its details.
class TileDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return TileSize;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF tile = QRectF(option.rect).adjusted(0.5, 0.5, -4.5, -4.5);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->setPen(QPen(selected ? Spotlight::Color::Accent()
                                      : hovered ? Spotlight::Color::Stroke()
                                                : Spotlight::Color::StrokeSubtle(),
                             1));
        painter->setBrush(selected ? Spotlight::Color::AccentSoft() : Spotlight::Color::FillFaint());
        painter->drawRoundedRect(tile, 15, 15);

        const QRectF plate(tile.left() + 7, tile.top() + 7, tile.width() - 14,
                           ThumbnailSize.height());
        painter->setPen(Qt::NoPen);
        painter->setBrush(Spotlight::Color::Window());
        painter->drawRoundedRect(plate, 12, 12);
        const auto icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            const QPixmap pixmap = icon.pixmap(ThumbnailSize, painter->device()->devicePixelRatioF());
            QSizeF size = pixmap.deviceIndependentSize();
            size.scale(plate.size(), Qt::KeepAspectRatio);
            QPainterPath clip;
            clip.addRoundedRect(plate, 12, 12);
            painter->setClipPath(clip);
            painter->drawPixmap(QRectF(plate.center() - QPointF(size.width(), size.height()) / 2,
                                       size),
                                pixmap, QRectF(QPointF{}, pixmap.size()));
            painter->setClipping(false);
        }

        const auto lines = index.data(Qt::DisplayRole).toString().split(u'\n');
        const QRectF text(plate.left(), plate.bottom() + 4, plate.width(), 16);
        painter->setFont(Spotlight::Font(12, 600));
        painter->setPen(Spotlight::Color::Text());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                          painter->fontMetrics().elidedText(lines.value(0), Qt::ElideRight,
                                                            int(text.width())));
        if (lines.size() > 1) {
            painter->setFont(Spotlight::Font(11, 400));
            painter->setPen(Spotlight::Color::TextMuted());
            painter->drawText(text.translated(0, 19).adjusted(0, 0, 0, -1),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(lines.value(1), Qt::ElideRight,
                                                                int(text.width())));
        }
        if (option.state & QStyle::State_HasFocus && Spotlight::HasKeyboardFocus(option.widget)) {
            painter->setPen(QPen(Spotlight::Color::Text(), 1));
            painter->setBrush(Qt::NoBrush);
            painter->drawRoundedRect(tile.adjusted(-1, -1, 1, 1), 16, 16);
        }
        painter->restore();
    }
};

QLabel* Caption(const QString& text, QWidget* parent, const QColor& color = Spotlight::Color::TextSecondary()) {
    auto* label = new QLabel(text, parent);
    label->setFont(Spotlight::Font(13, 600));
    Spotlight::SetTextColor(label, color);
    return label;
}

QLabel* Body(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(Spotlight::Font(13, 400));
    label->setWordWrap(true);
    Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
    return label;
}

// Asks for its whole content, so the dialog only scrolls when the screen is too short for it.
class FittedScrollArea final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

    QSize sizeHint() const override {
        return widget()->sizeHint();
    }
};

QString CardStyle(const QString& name, int radius) {
    return QStringLiteral("#%1 { background-color: rgba(255, 255, 255, 13); "
                          "border: 1px solid rgba(255, 255, 255, 20); border-radius: %2px; }")
        .arg(name)
        .arg(radius);
}

} // namespace

ArtworkDialog::ArtworkDialog(QWidget* parent, const QString& game_title, const QByteArray& key_,
                             Client& client_)
    : Spotlight::Dialog(parent), key{key_}, client{client_} {
    setWindowTitle(tr("Browse iiDB Artwork"));
    SetIcon(u"image", Spotlight::Color::Accent());
    auto* scroll = new FittedScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(u"QScrollArea, #iidbContent { background: transparent; }"_s);
    auto* content_widget = new QWidget(scroll);
    content_widget->setObjectName(u"iidbContent"_s);
    auto* layout = new QVBoxLayout(content_widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* title_group = new QVBoxLayout;
    title_group->setSpacing(8);
    auto* title_label = Caption(tr("Game title:"), this);
    title_group->addWidget(title_label);
    auto* search_row = new QHBoxLayout;
    search_row->setSpacing(10);
    title = new QLineEdit(this);
    title->setObjectName(u"iidbTitle"_s);
    title->setMaxLength(200);
    title->setText(game_title);
    title->setClearButtonEnabled(true);
    title->setFixedHeight(40);
    title->setFont(Spotlight::Font(14, 500));
    title->addAction(Spotlight::Icon(u"magnifying-glass"_s, Spotlight::Color::TextSecondary(), 16),
                     QLineEdit::LeadingPosition);
    title->setToolTip(
        tr("Only the title you search is sent to iiDB, never your game files or ID."));
    title_label->setBuddy(title);
    search_button = new QPushButton(tr("Search"), this);
    search_button->setObjectName(u"iidbSearch"_s);
    search_button->setProperty("kind", u"primary"_s);
    key_button = new QPushButton(Spotlight::Icon(u"key"_s, Spotlight::Color::Text(), 16),
                                 tr("API Key…"), this);
    search_row->addWidget(title, 1);
    search_row->addWidget(search_button);
    search_row->addWidget(key_button);
    title_group->addLayout(search_row);
    layout->addLayout(title_group);
    layout->addWidget(Body(title->toolTip(), this));

    auto* match_group = new QVBoxLayout;
    match_group->setSpacing(8);
    auto* match_label = Caption(tr("iiDB match:"), this);
    matches = new QComboBox(this);
    matches->setObjectName(u"iidbMatches"_s);
    matches->setPlaceholderText(tr("Choose a search result"));
    matches->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    matches->setFont(Spotlight::Font(13, 600));
    match_label->setBuddy(matches);
    match_group->addWidget(match_label);
    match_group->addWidget(matches);
    layout->addLayout(match_group);

    auto* category_row = new QHBoxLayout;
    category_row->setSpacing(12);
    auto* category_track = new QWidget(this);
    category_track->setObjectName(u"iidbCategoryTrack"_s);
    category_track->setAttribute(Qt::WA_StyledBackground);
    category_track->setStyleSheet(
        u"#iidbCategoryTrack { background-color: rgba(255, 255, 255, 15); border-radius: 19px; }"_s);
    auto* track_layout = new QHBoxLayout(category_track);
    track_layout->setContentsMargins(3, 3, 3, 3);
    categories = new QTabBar(category_track);
    categories->setObjectName(u"iidbCategories"_s);
    categories->setExpanding(false);
    categories->setDrawBase(false);
    categories->setDocumentMode(true);
    categories->setFont(Spotlight::Font(13, 500));
    categories->setStyleSheet(
        u"QTabBar { background: transparent; }"
        "QTabBar::tab { background: transparent; color: #ffffff; border: 0; border-radius: 16px; "
        "height: 32px; padding: 0 14px; margin: 0 1px 0 1px; }"
        "QTabBar::tab:hover { background-color: rgba(255, 255, 255, 20); }"
        "QTabBar::tab:selected { background-color: #ffffff; color: #14151b; font-weight: 600; }"_s);
    for (const auto& name : category_names) {
        categories->addTab(name);
    }
    track_layout->addWidget(categories);
    category_row->addWidget(category_track);
    category_row->addStretch(1);
    category_row->addWidget(Caption(tr("Artwork gallery"), this));
    layout->addLayout(category_row);

    auto* content = new QHBoxLayout;
    content->setSpacing(16);
    auto* browsing = new QVBoxLayout;
    browsing->setSpacing(10);
    gallery_stack = new QStackedWidget(this);
    auto* empty = new QWidget(gallery_stack);
    empty->setObjectName(u"iidbEmptyGallery"_s);
    empty->setAttribute(Qt::WA_StyledBackground);
    empty->setStyleSheet(CardStyle(empty->objectName(), 15));
    auto* empty_layout = new QVBoxLayout(empty);
    empty_layout->setContentsMargins(16, 16, 16, 16);
    empty_layout->setSpacing(10);
    empty_layout->addStretch(1);
    empty_layout->addWidget(Caption(tr("Artwork gallery"), empty, Spotlight::Color::Text()));
    empty_layout->addWidget(Body(tr("No artwork to show yet."), empty));
    empty_layout->addStretch(1);
    gallery_stack->addWidget(empty);
    gallery = new QListWidget(gallery_stack);
    gallery->setObjectName(u"iidbGallery"_s);
    gallery->setAccessibleName(tr("Artwork gallery"));
    gallery->setViewMode(QListView::IconMode);
    gallery->setMovement(QListView::Static);
    gallery->setResizeMode(QListView::Adjust);
    gallery->setIconSize(ThumbnailSize);
    gallery->setUniformItemSizes(true);
    gallery->setMouseTracking(true);
    gallery->setItemDelegate(new TileDelegate(gallery));
    gallery->setFrameShape(QFrame::NoFrame);
    gallery->setStyleSheet(u"QListWidget { background: transparent; border: 0; }"_s);
    gallery->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    gallery->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gallery->setMinimumHeight(TileSize.height() + 4);
    gallery_stack->addWidget(gallery);
    browsing->addWidget(gallery_stack, 1);
    thumbnail_status = new Spotlight::Notice(this);
    thumbnail_status->setObjectName(u"iidbThumbnailStatus"_s);
    thumbnail_status->SetTone(Spotlight::Notice::Tone::Warning);
    thumbnail_status->setText(
        tr("Some thumbnails could not load because iiDB asked Lichii to wait."));
    auto thumbnail_notice_policy = thumbnail_status->sizePolicy();
    thumbnail_notice_policy.setRetainSizeWhenHidden(true);
    thumbnail_status->setSizePolicy(thumbnail_notice_policy);
    browsing->addWidget(thumbnail_status);
    auto* browsing_buttons = new QHBoxLayout;
    browsing_buttons->setSpacing(8);
    more = new QPushButton(tr("Load More"), this);
    more->setObjectName(u"iidbMore"_s);
    preview_button = new QPushButton(Spotlight::Icon(u"eye"_s, Spotlight::Color::Text(), 16),
                                     tr("Preview Selected"), this);
    preview_button->setObjectName(u"iidbPreview"_s);
    browsing_buttons->addWidget(more);
    browsing_buttons->addStretch();
    browsing_buttons->addWidget(preview_button);
    browsing->addLayout(browsing_buttons);
    content->addLayout(browsing, 1);

    auto* preview = new QWidget(this);
    preview->setObjectName(u"iidbPreviewCard"_s);
    preview->setAttribute(Qt::WA_StyledBackground);
    preview->setStyleSheet(CardStyle(preview->objectName(), 20));
    preview->setFixedWidth(329);
    auto* preview_layout = new QVBoxLayout(preview);
    preview_layout->setContentsMargins(14, 14, 14, 14);
    preview_layout->setSpacing(8);
    auto* panels = new QHBoxLayout;
    panels->setSpacing(10);
    source_preview = new QLabel(preview);
    icon_preview = new QLabel(preview);
    source_preview->setObjectName(u"iidbSourcePreview"_s);
    icon_preview->setObjectName(u"iidbIconPreview"_s);
    const std::array captions{tr("Original artwork"), tr("Icon in Lichii")};
    const std::array images{source_preview, icon_preview};
    for (size_t i = 0; i < images.size(); ++i) {
        auto* column = new QVBoxLayout;
        column->setSpacing(8);
        auto* caption = new QLabel(captions[i], preview);
        caption->setFont(Spotlight::Font(12, 600));
        Spotlight::SetTextColor(caption, Spotlight::Color::TextSecondary());
        column->addWidget(caption);
        auto* image = images[i];
        image->setAlignment(Qt::AlignCenter);
        image->setAccessibleName(captions[i]);
        image->setFixedSize(PreviewSize);
        image->setFont(Spotlight::Font(12, 500));
        image->setStyleSheet(QStringLiteral("#%1 { background-color: #0b0c10; border-radius: 12px; "
                                            "color: rgba(255, 255, 255, 140); }")
                                 .arg(image->objectName()));
        column->addWidget(image);
        panels->addLayout(column);
    }
    preview_layout->addLayout(panels);
    original_size = new QLabel(preview);
    original_size->setFont(Spotlight::Font(12, 600));
    original_size->hide();
    preview_layout->addWidget(original_size);
    preview_layout->addWidget(
        Body(tr("Lichii crops and scales the original to a 256 × 256 icon."), preview));
    content->addWidget(preview, 0, Qt::AlignTop);
    layout->addLayout(content, 1);

    status = new Spotlight::Notice(this);
    status->setObjectName(u"iidbStatus"_s);
    layout->addWidget(status);
    preview_status = new Spotlight::Notice(this);
    preview_status->setObjectName(u"iidbPreviewStatus"_s);
    preview_status->hide();
    layout->addWidget(preview_status);
    layout->addWidget(Body(tr("Cancel or a failed download leaves your artwork intact. Use "
                              "Artwork stages the icon; closing Game Settings saves it."),
                           this));
    const QString catalog_credit =
        tr("Catalog data from iiDB, adapted from the Libretro Database and its upstream sources "
           "(No-Intro and Redump).");
    auto* credits = new QHBoxLayout;
    credits->setSpacing(12);
    const std::pair<QString, QString> sources[] = {
        {tr("iiDB"), u"https://iidb.iisu.network"_s},
        {tr("Libretro Database"), u"https://github.com/libretro/libretro-database"_s},
        {tr("CC BY-SA 4.0"), u"https://creativecommons.org/licenses/by-sa/4.0/"_s},
        {tr("Artwork and catalog licensing"), u"https://iisu.network/license"_s},
    };
    for (const auto& [text, url] : sources) {
        auto* link = new QLabel(
            u"<a href=\"%1\" style=\"color: #8fb5ff; text-decoration: none\">%2</a>"_s.arg(
                url, text.toHtmlEscaped()),
            this);
        link->setFont(Spotlight::Font(13, 600));
        link->setOpenExternalLinks(true);
        link->setTextInteractionFlags(Qt::TextBrowserInteraction);
        link->setToolTip(url == sources[3].second ? catalog_credit : url);
        auto* icon = new QLabel(this);
        icon->setPixmap(Spotlight::IconPixmap(u"arrow-square-out", 12, Spotlight::Color::Info(),
                                              devicePixelRatioF()));
        auto* pair = new QHBoxLayout;
        pair->setSpacing(6);
        pair->addWidget(link);
        pair->addWidget(icon);
        credits->addLayout(pair);
    }
    credits->addStretch(1);
    layout->addLayout(credits);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    use = buttons->addButton(tr("Use Artwork"), QDialogButtonBox::AcceptRole);
    use->setObjectName(u"iidbUse"_s);
    use->setProperty("kind", u"primary"_s);
    for (auto* button : {search_button, key_button, more, preview_button, use,
                         buttons->button(QDialogButtonBox::Cancel)}) {
        button->setAutoDefault(false);
    }
    for (auto* button : {search_button, key_button, more, preview_button}) {
        button->setMinimumHeight(34);
        button->setFont(Spotlight::Font(12, 600));
    }
    auto* button_row = new QHBoxLayout;
    button_row->setContentsMargins(0, 6, 0, 0);
    button_row->addWidget(buttons);
    scroll->setWidget(content_widget);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll, 1);
    outer->addLayout(button_row);

    connect(title, &QLineEdit::textChanged, this, &ArtworkDialog::ResetSearch);
    connect(title, &QLineEdit::returnPressed, this, [this] {
        if (search_button->isEnabled()) {
            Search();
        }
    });
    connect(search_button, &QPushButton::clicked, this, &ArtworkDialog::Search);
    connect(key_button, &QPushButton::clicked, this, [this] {
        const auto previous = key;
        ConfigureKey(this);
        if (previous != key) {
            ResetSearch();
        }
    });
    connect(matches, &QComboBox::activated, this, [this](int index) {
        selected_parent = candidates.at(static_cast<size_t>(index)).id;
        ResetGallery();
        LoadPage();
    });
    connect(categories, &QTabBar::currentChanged, this, [this] {
        if (selected_parent) {
            ResetGallery();
            LoadPage();
        }
    });
    connect(more, &QPushButton::clicked, this, &ArtworkDialog::LoadPage);
    connect(gallery, &QListWidget::currentRowChanged, this, [this] {
        ClearPreview();
        LoadVisibleThumbnail();
    });
    connect(gallery, &QListWidget::itemDoubleClicked, this, [this] { Preview(); });
    connect(preview_button, &QPushButton::clicked, this, &ArtworkDialog::Preview);
    connect(gallery->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ArtworkDialog::LoadVisibleThumbnail);
    connect(gallery->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this] { QTimer::singleShot(0, this, &ArtworkDialog::LoadVisibleThumbnail); });
    gallery->viewport()->installEventFilter(this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!pending_icon.isNull()) {
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    ResetSearch();
    resize(880, 616);
}

void ArtworkDialog::SetStatus(const QString& text, Spotlight::Notice::Tone tone) {
    status->SetTone(tone);
    status->setText(text);
}

void ArtworkDialog::SetPreviewStatus(const QString& text, Spotlight::Notice::Tone tone) {
    preview_status->SetTone(tone);
    preview_status->setText(text);
    preview_status->setHidden(text.isEmpty());
}

ArtworkDialog::~ArtworkDialog() {
    client.Cancel();
}

QImage ArtworkDialog::TakeIcon() {
    return result() == QDialog::Accepted ? std::exchange(pending_icon, {}) : QImage{};
}

void ArtworkDialog::done(int result) {
    client.Cancel();
    QDialog::done(result);
}

bool ArtworkDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == gallery->viewport() && event->type() == QEvent::Resize) {
        QTimer::singleShot(0, this, &ArtworkDialog::LoadVisibleThumbnail);
    }
    return QDialog::eventFilter(watched, event);
}

void ArtworkDialog::UpdateRequestControls() {
    search_button->setEnabled(!search_loading && !key.isEmpty() &&
                              !title->text().trimmed().isEmpty());
    key_button->setEnabled(!search_loading && !page_loading);
}

void ArtworkDialog::ResetSearch() {
    selected_parent.reset();
    candidates.clear();
    matches->clear();
    matches->setEnabled(false);
    ResetGallery();
    search_loading = false;
    UpdateRequestControls();
    SetStatus(key.isEmpty() ? ErrorText(Error::MissingKey) : tr("Search for the game by title."),
              Spotlight::Notice::Tone::Neutral);
}

void ArtworkDialog::Search() {
    ResetSearch();
    search_loading = true;
    UpdateRequestControls();
    SetStatus(tr("Searching iiDB…"), Spotlight::Notice::Tone::Neutral);
    client.Search(key, title->text().trimmed(), [this](std::vector<Candidate> found, Error error) {
        search_loading = false;
        UpdateRequestControls();
        if (error != Error::None) {
            SetStatus(ErrorText(error), Spotlight::Notice::Tone::Warning);
            return;
        }
        candidates = std::move(found);
        const QSignalBlocker blocker(matches);
        for (const auto& candidate : candidates) {
            QStringList description{candidate.title};
            if (!candidate.platform.isEmpty()) {
                description.append(candidate.platform);
            }
            if (candidate.year > 0) {
                description.append(QString::number(candidate.year));
            }
            matches->addItem(description.join(u" · "_s));
        }
        matches->setCurrentIndex(-1);
        matches->setEnabled(!candidates.empty());
        SetStatus(candidates.empty() ? tr("No matching games with artwork. Try another title.")
                                     : tr("Choose the matching iiDB game to see its artwork."),
                  Spotlight::Notice::Tone::Neutral);
    });
}

void ArtworkDialog::ResetGallery() {
    client.Cancel();
    page_loading = false;
    thumbnail_loading = false;
    preview_loading = false;
    skipped_assets = false;
    assets.clear();
    asset_ids.clear();
    cursors.clear();
    {
        const QSignalBlocker blocker(gallery);
        const QSignalBlocker scroll_blocker(gallery->verticalScrollBar());
        gallery->clear();
    }
    gallery_stack->setCurrentIndex(0);
    next_cursor.clear();
    pages = 0;
    more->setText(tr("Load More"));
    more->setEnabled(false);
    thumbnail_status->hide();
    ClearPreview();
}

void ArtworkDialog::LoadPage() {
    if (!selected_parent || page_loading || pages >= MaxPages ||
        (pages > 0 && next_cursor.isEmpty())) {
        return;
    }
    page_loading = true;
    UpdateRequestControls();
    more->setEnabled(false);
    SetStatus(tr("Loading %1…").arg(category_names.at(categories->currentIndex())), Spotlight::Notice::Tone::Neutral);
    const auto category = Categories.at(static_cast<size_t>(categories->currentIndex()));
    client.Gallery(
        key, *selected_parent, category, next_cursor, [this](GalleryPage page, Error error) {
            page_loading = false;
            UpdateRequestControls();
            if (error != Error::None) {
                SetStatus(ErrorText(error), Spotlight::Notice::Tone::Warning);
                more->setText(tr("Retry Gallery"));
                more->setEnabled(true);
                return;
            }
            ++pages;
            next_cursor = std::move(page.next_cursor);
            const bool repeated_cursor = !next_cursor.isEmpty() && cursors.contains(next_cursor);
            if (!next_cursor.isEmpty()) {
                cursors.insert(next_cursor);
            }
            skipped_assets |= page.skipped_assets;
            const QSignalBlocker blocker(gallery);
            const QSignalBlocker scroll_blocker(gallery->verticalScrollBar());
            for (auto& asset : page.assets) {
                if (asset_ids.contains(asset.id)) {
                    continue;
                }
                asset_ids.insert(asset.id);
                QStringList details;
                if (!asset.region.isEmpty()) {
                    details.append(asset.region);
                }
                if (!asset.resolution.isEmpty()) {
                    details.append(asset.resolution);
                }
                QString label =
                    asset.name.isEmpty() ? tr("Artwork %1").arg(assets.size() + 1) : asset.name;
                if (!details.isEmpty()) {
                    label += u'\n' + details.join(u" · "_s);
                }
                auto* item = new QListWidgetItem(label, gallery);
                item->setSizeHint(TileSize);
                assets.push_back({std::move(asset)});
            }
            more->setText(tr("Load More"));
            more->setEnabled(!next_cursor.isEmpty() && !repeated_cursor && pages < MaxPages);
            if (repeated_cursor) {
                next_cursor.clear();
                SetStatus(tr("iiDB repeated a gallery page. No more pages were loaded."), Spotlight::Notice::Tone::Warning);
            } else if (pages == MaxPages && !next_cursor.isEmpty()) {
                SetStatus(tr("Showing up to %1 pages in this category.").arg(MaxPages), Spotlight::Notice::Tone::Neutral);
            } else if (assets.empty()) {
                SetStatus(skipped_assets ? tr("No usable images in this category.")
                                         : tr("No artwork in this category."),
                          skipped_assets ? Spotlight::Notice::Tone::Warning : Spotlight::Notice::Tone::Neutral);
            } else {
                SetStatus(tr("Select an image to preview the original."), Spotlight::Notice::Tone::Neutral);
            }
            gallery_stack->setCurrentIndex(assets.empty() ? 0 : 1);
            QTimer::singleShot(0, this, &ArtworkDialog::LoadVisibleThumbnail);
        });
}

void ArtworkDialog::LoadVisibleThumbnail() {
    if (!isVisible() || result() == QDialog::Accepted) {
        return;
    }
    UpdateThumbnailNotice();
    if (thumbnail_loading || preview_loading) {
        return;
    }
    const auto viewport = gallery->viewport()->rect();
    for (int row = 0; row < gallery->count(); ++row) {
        auto& entry = assets.at(static_cast<size_t>(row));
        if (entry.thumbnail != Thumbnail::Pending || entry.asset.thumbnail.isEmpty() ||
            !viewport.intersects(gallery->visualItemRect(gallery->item(row)))) {
            continue;
        }
        thumbnail_loading = true;
        client.Download(entry.asset.thumbnail, true, [this, row](QByteArray bytes, Error error) {
            thumbnail_loading = false;
            auto& thumbnail = assets.at(static_cast<size_t>(row)).thumbnail;
            if (error == Error::RateLimited) {
                thumbnail = Thumbnail::Limited;
                UpdateThumbnailNotice();
                return;
            }
            thumbnail = Thumbnail::Attempted;
            if (error == Error::None) {
                const auto image = ArtworkImage::Decode(bytes, ThumbnailSize);
                if (!image.isNull()) {
                    gallery->item(row)->setIcon(QPixmap::fromImage(image));
                }
            }
            LoadVisibleThumbnail();
        });
        return;
    }
}

void ArtworkDialog::UpdateThumbnailNotice() {
    const auto viewport = gallery->viewport()->rect();
    for (int row = 0; row < gallery->count(); ++row) {
        if (assets.at(static_cast<size_t>(row)).thumbnail == Thumbnail::Limited &&
            viewport.intersects(gallery->visualItemRect(gallery->item(row)))) {
            thumbnail_status->show();
            return;
        }
    }
    thumbnail_status->hide();
}

void ArtworkDialog::ClearPreview() {
    if (preview_loading) {
        client.CancelImage();
    }
    preview_loading = false;
    pending_icon = {};
    preview_button->setEnabled(gallery->currentRow() >= 0 && !assets.empty());
    use->setEnabled(false);
    source_preview->setText(tr("No preview"));
    icon_preview->setText(tr("No preview"));
    original_size->hide();
    SetPreviewStatus({}, Spotlight::Notice::Tone::Neutral);
}

void ArtworkDialog::Preview() {
    const int row = gallery->currentRow();
    if (row < 0 || preview_loading) {
        return;
    }
    ClearPreview();
    client.CancelImage();
    thumbnail_loading = false;
    preview_loading = true;
    preview_button->setEnabled(false);
    SetPreviewStatus(tr("Downloading original…"), Spotlight::Notice::Tone::Neutral);
    client.Download(
        assets.at(static_cast<size_t>(row)).asset.original, false,
        [this](QByteArray bytes, Error error) {
            preview_loading = false;
            preview_button->setEnabled(true);
            if (error != Error::None) {
                SetPreviewStatus(ErrorText(error), Spotlight::Notice::Tone::Warning);
            } else if (const auto original = ArtworkImage::Decode(bytes); !original.isNull()) {
                pending_icon = ArtworkImage::SquareIcon(original);
                const auto fit = PreviewSize;
                source_preview->setPixmap(QPixmap::fromImage(
                    original.scaled(fit, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                icon_preview->setPixmap(QPixmap::fromImage(
                    pending_icon.scaled(fit, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                original_size->setText(
                    tr("Original: %1 × %2").arg(original.width()).arg(original.height()));
                original_size->show();
                use->setEnabled(!pending_icon.isNull());
            } else {
                SetPreviewStatus(ErrorText(Error::InvalidImage), Spotlight::Notice::Tone::Error);
            }
            LoadVisibleThumbnail();
        });
}

} // namespace IiDB
