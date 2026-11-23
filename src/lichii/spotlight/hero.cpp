// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "lichii/spotlight/hero.h"
#include "lichii/spotlight/nextendo_live.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {

Hero::Hero(QWidget* parent)
    : QWidget(parent), platform(new QLabel(this)), playtime(new QLabel(this)),
      online(new NextendoGameLine(this)), title(new QLabel(this)),
      play(new PillButton(tr("Play"), QStringLiteral("play-fill"), PillButton::Kind::Accent, 44,
                          this)),
      menu(new CircleButton(QStringLiteral("dots-three"), 44, this)),
      add(new PillButton(tr("Add game folder"), QStringLiteral("folder-open"),
                         PillButton::Kind::Accent, 44, this)) {
    setObjectName(QStringLiteral("spotlightHero"));
    setFixedHeight(290);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(40, 60, 40, 0);
    column->setSpacing(12);

    auto* metadata = new QHBoxLayout;
    metadata->setSpacing(12);
    platform->setFont(Font(12, 600));
    platform->setAlignment(Qt::AlignCenter);
    platform->setFixedHeight(24);
    platform->setStyleSheet(QStringLiteral("QLabel { color: %1; background: %2; border-radius: "
                                           "12px; padding: 0 10px; }")
                                .arg(Color::OnAccent().name(), Color::Accent().name()));
    metadata->addWidget(platform);
    playtime->setFont(Font(14));
    playtime->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 184);"));
    metadata->addWidget(playtime);
    metadata->addStretch();
    column->addLayout(metadata);

    title->setFont(DisplayFont(42));
    title->setStyleSheet(QStringLiteral("color: white;"));
    title->setFixedHeight(50);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    title->setTextFormat(Qt::PlainText);
    column->addWidget(title);
    column->addWidget(online);
    column->addSpacing(8);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    play->setFixedWidth(play->fontMetrics().horizontalAdvance(play->text()) + 67);
    buttons->addWidget(play);
    buttons->addWidget(menu);
    buttons->addWidget(add);
    buttons->addStretch();
    column->addLayout(buttons);
    column->addStretch();

    menu->setAccessibleName(tr("Selected game actions"));
    menu->setToolTip(tr("Selected game actions"));
    connect(play, &QAbstractButton::clicked, this, &Hero::PlayRequested);
    connect(menu, &QAbstractButton::clicked, this,
            [this] { emit MenuRequested(menu->mapToGlobal(QPoint(0, menu->height() + 5))); });
    connect(add, &QAbstractButton::clicked, this, &Hero::AddDirectoryRequested);
    SetEmpty(false, false);
}

void Hero::SetGame(const QString& name, Lichii::LibraryPlatform console, u64 seconds,
                   u64 program_id) {
    online->SetProgramId(program_id);
    using enum Lichii::LibraryPlatform;
    platform->setText(console == Switch2 ? tr("Switch 2") : tr("Switch 1"));
    platform->setVisible(console != All);
    if (seconds == 0) {
        playtime->setText(tr("Not played yet"));
    } else if (seconds >= 3600) {
        playtime->setText(tr("%1h %2m played").arg(seconds / 3600).arg(seconds / 60 % 60));
    } else {
        playtime->setText(tr("%1m played").arg(std::max<u64>(1, seconds / 60)));
    }
    playtime->show();
    play->show();
    menu->show();
    add->hide();
    layout()->setContentsMargins(40, 60, 40, 0);
    SetTitle(name);
}

void Hero::SetEmpty(bool has_library, bool filtered) {
    online->SetProgramId(0);
    platform->hide();
    playtime->hide();
    play->hide();
    menu->hide();
    add->setVisible(!has_library);
    layout()->setContentsMargins(40, 80, 40, 0);
    SetTitle(!has_library ? tr("Add your games")
             : filtered   ? tr("No games found")
                          : tr("Select a game"));
}

void Hero::SetTitle(const QString& text) {
    title_text = text;
    UpdateTitle();
}

void Hero::UpdateTitle() {
    const QString elided =
        title->fontMetrics().elidedText(title_text, Qt::ElideRight, title->width());
    title->setText(elided);
    title->setToolTip(elided == title_text ? QString{} : title_text.toHtmlEscaped());
}

void Hero::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateTitle();
}

} // namespace Spotlight
