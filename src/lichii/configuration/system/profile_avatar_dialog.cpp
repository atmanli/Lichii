// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "profile_avatar_dialog.h"

#include <QBoxLayout>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include "lichii/spotlight/theme.h"

namespace {
constexpr int AvatarSize = 64;
constexpr int AvatarCell = 78;
constexpr int SwatchSize = 26;

QIcon SwatchIcon(const QColor& color, bool selected, qreal ratio) {
    QPixmap pixmap(QSize(SwatchSize, SwatchSize) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(selected ? QPen(Spotlight::Color::Accent(), 2) : Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(1.5, 1.5, SwatchSize - 3, SwatchSize - 3));
    painter.end();
    return QIcon(pixmap);
}
} // namespace

ProfileAvatarDialog::ProfileAvatarDialog(QWidget* parent)
    : Spotlight::Dialog{parent}, avatar_list{new QListWidget(this)} {
    setWindowTitle(tr("Choose an avatar"));
    SetSubtitle(tr("Pictures from your console firmware"));
    SetIcon(QStringLiteral("image"), Spotlight::Color::Accent());

    avatar_list->setViewMode(QListView::IconMode);
    avatar_list->setIconSize(QSize(AvatarSize, AvatarSize));
    avatar_list->setGridSize(QSize(AvatarCell, AvatarCell));
    avatar_list->setMovement(QListView::Static);
    avatar_list->setResizeMode(QListView::Adjust);
    avatar_list->setFrameShape(QFrame::NoFrame);
    avatar_list->setSelectionMode(QAbstractItemView::SingleSelection);
    avatar_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    avatar_list->setDragDropMode(QAbstractItemView::NoDragDrop);
    avatar_list->setDragEnabled(false);
    avatar_list->setDropIndicatorShown(false);
    avatar_list->setAcceptDrops(false);
    avatar_list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; outline: none; }"
        "QListWidget::item { border: 3px solid transparent; border-radius: 35px; padding: 0; }"
        "QListWidget::item:selected { background: transparent; border-color: #ff6b95; }"));

    auto* background_row = new QHBoxLayout;
    background_row->setSpacing(10);
    auto* background_label = new QLabel(tr("Background"), this);
    background_label->setFont(Spotlight::Font(13, 600));
    background_row->addWidget(background_label);
    background_row->addSpacing(6);
    for (const QColor& color : {QColor(Qt::white), Spotlight::Color::Accent(),
                                Spotlight::Color::Success(), Spotlight::Color::Info(),
                                Spotlight::Color::Warning()}) {
        auto* swatch = new QToolButton(this);
        swatch->setFixedSize(SwatchSize, SwatchSize);
        swatch->setIconSize(QSize(SwatchSize, SwatchSize));
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setAccessibleName(color.name());
        swatch->setStyleSheet(QStringLiteral("QToolButton { background: transparent; border: none; }"));
        connect(swatch, &QToolButton::clicked, this, [this, color] {
            SetBackgroundColor(color);
            RefreshAvatars();
        });
        background_row->addWidget(swatch);
        swatches.emplace_back(swatch, color);
    }
    auto* custom = new QPushButton(tr("Custom…"), this);
    custom->setFlat(true);
    custom->setCursor(Qt::PointingHandCursor);
    background_row->addWidget(custom);
    background_row->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->addButton(tr("Use avatar"), QDialogButtonBox::AcceptRole)->setDefault(true);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(20);
    main_layout->addWidget(avatar_list);
    main_layout->addLayout(background_row);
    main_layout->addWidget(buttons);

    SetBackgroundColor(Qt::white);

    connect(custom, &QPushButton::clicked, this, [this]() {
        const auto new_color = QColorDialog::getColor(avatar_bg_color, this);
        if (new_color.isValid()) {
            SetBackgroundColor(new_color);
            RefreshAvatars();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ProfileAvatarDialog::~ProfileAvatarDialog() = default;

void ProfileAvatarDialog::SetBackgroundColor(const QColor& color) {
    avatar_bg_color = color;
    for (const auto& [swatch, swatch_color] : swatches) {
        swatch->setIcon(SwatchIcon(swatch_color, swatch_color == color, devicePixelRatioF()));
    }
}

QPixmap ProfileAvatarDialog::CreateAvatar(const QPixmap& avatar) {
    QPixmap output(avatar.size());
    output.fill(avatar_bg_color);

    // Scale the image and fill it black to become our shadow
    QPixmap shadow_pixmap = avatar.transformed(QTransform::fromScale(1.04, 1.04));
    QPainter shadow_painter(&shadow_pixmap);
    shadow_painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    shadow_painter.fillRect(shadow_pixmap.rect(), Qt::black);
    shadow_painter.end();

    QPainter painter(&output);
    painter.setOpacity(0.10);
    painter.drawPixmap(0, 0, shadow_pixmap);
    painter.setOpacity(1.0);
    painter.drawPixmap(0, 0, avatar);
    painter.end();

    return output;
}

void ProfileAvatarDialog::RefreshAvatars() {
    if (avatar_list->count() != avatar_image_store.size()) {
        return;
    }
    for (int i = 0; i < avatar_image_store.size(); ++i) {
        const QPixmap avatar = CreateAvatar(avatar_image_store[i])
                                   .scaled(AvatarSize, AvatarSize, Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation);
        QPixmap round(avatar.size());
        round.fill(Qt::transparent);
        QPainter painter(&round);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath circle;
        circle.addEllipse(QRectF(round.rect()));
        painter.setClipPath(circle);
        painter.drawPixmap(0, 0, avatar);
        painter.end();
        avatar_list->item(i)->setIcon(QIcon(round));
    }
}

void ProfileAvatarDialog::LoadImages(const QVector<QPixmap>& avatar_images) {
    avatar_image_store = avatar_images;
    avatar_list->clear();

    for (int i = 0; i < avatar_image_store.size(); ++i) {
        avatar_list->addItem(new QListWidgetItem);
    }
    RefreshAvatars();

    const int scroll_bar = avatar_list->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    avatar_list->setFixedSize(AvatarCell * 6 + scroll_bar + 4, AvatarCell * 9 / 2);
}

bool ProfileAvatarDialog::AreImagesLoaded() const {
    return !avatar_image_store.isEmpty();
}

QPixmap ProfileAvatarDialog::GetSelectedAvatar() {
    return CreateAvatar(avatar_image_store[avatar_list->currentRow()]);
}
