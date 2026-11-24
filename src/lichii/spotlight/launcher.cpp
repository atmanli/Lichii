// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmapCache>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWindow>

#include "lichii/game/game_grid.h"
#include "lichii/game/game_list.h"
#include "lichii/game/search_field.h"
#include "lichii/spotlight/hero.h"
#include "lichii/spotlight/hero_art.h"
#include "lichii/spotlight/launcher.h"
#include "lichii/spotlight/library_header.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/nextendo_live.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"

namespace Spotlight {

Launcher::Launcher(QMainWindow* window, GameList* games_, GameListPlaceholder* placeholder,
                   RunningTitleBar* running_title_, const PlayTime::PlayTimeManager& playtime_,
                   LauncherActions actions, QWidget* parent)
    : QWidget(parent), owner(window), games(games_), running_title(running_title_),
      playtime(playtime_), hero(new Hero(this)),
      library(new LibraryHeader(actions.grid, actions.list, this)),
      library_stack(new QStackedLayout), pin_menu(new QAction(tr("Show menu bar\tAlt"), this)) {
    setObjectName(QStringLiteral("spotlightLauncher"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    InstallMenuStyle(*qApp);
    QPixmapCache::setCacheLimit(std::max(QPixmapCache::cacheLimit(), 64 * 1024));
    auto* add = new Menu(this);
    add->addAction(Icon(u"folder-open", Color::MenuInk(), 16), tr("Add game folder…"), games,
                   &GameList::AddDirectory);
    const auto mirror = [add](QAction* source, QStringView icon, const QString& label) {
        auto* entry = add->addAction(Icon(icon, Color::MenuInk(), 16), label);
        const auto sync = [entry, source, label] {
            const QString keys = source->shortcut().toString(QKeySequence::NativeText);
            entry->setText(keys.isEmpty() ? label : label + QLatin1Char('\t') + keys);
            entry->setEnabled(source->isEnabled());
            entry->setVisible(source->isVisible());
        };
        sync();
        connect(source, &QAction::changed, entry, sync);
        connect(entry, &QAction::triggered, source, &QAction::trigger);
    };
    mirror(actions.load_file, u"file", tr("Load file…"));
    mirror(actions.load_folder, u"folder", tr("Load folder…"));
    mirror(actions.install, u"download-simple", tr("Install files to NAND…"));
    add->addSeparator();
    add->addAction(Icon(u"arrow-clockwise", Color::MenuInk(), 16), tr("Rescan library"), this,
                   &Launcher::RescanRequested);

    auto* more = new Menu(this);
    more->AddSharedMenu(actions.tools, Icon(u"wrench", Color::MenuInk(), 16));
    more->AddSharedMenu(actions.multiplayer, Icon(u"users", Color::MenuInk(), 16));
    more->AddSharedMenu(actions.help, Icon(u"question", Color::MenuInk(), 16));
    if constexpr (ClientFrame) {
        more->addSeparator();
        pin_menu->setCheckable(true);
        more->addAction(pin_menu);
        connect(pin_menu, &QAction::toggled, this, [this] {
            temporary_menu = false;
            UpdateChrome();
        });
        connect(owner->menuBar(), &QMenuBar::triggered, this, [this] {
            temporary_menu = false;
            UpdateChrome();
        });
        edges = new FrameEdges(owner);
        qApp->installEventFilter(this);
    }

    auto* search = games->SearchField();
    connect(actions.search, &QAction::changed, search, [action = actions.search, search] {
        search->SetShortcutLabel(action->shortcut()
                                     .toString(QKeySequence::NativeText)
                                     .replace(QLatin1Char('+'), QLatin1Char(' ')));
    });
    auto* top = new TopBar(search, add, actions.settings, more, this);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(top);
    column->addWidget(hero);
    library_stack->addWidget(placeholder);
    library_stack->addWidget(games);
    placeholder->setContentsMargins(0, GameGrid::HeaderOverlap, 0, 0);
    auto* library_body = new QVBoxLayout;
    library_body->setContentsMargins(0, library->minimumHeight() - GameGrid::HeaderOverlap, 0, 0);
    library_body->addLayout(library_stack);
    auto* library_area = new QGridLayout;
    library_area->addLayout(library_body, 0, 0);
    library_area->addWidget(library, 0, 0, Qt::AlignTop);
    column->addLayout(library_area, 1);
    column->addWidget(new NextendoBar(games, this));

    connect(top, &TopBar::PlatformChanged, games, &GameList::SetPlatform);
    connect(hero, &Hero::PlayRequested, games, &GameList::PlaySelected);
    connect(hero, &Hero::MenuRequested, games, &GameList::ShowSelectedGameMenu);
    connect(hero, &Hero::AddDirectoryRequested, games, &GameList::AddDirectory);
    connect(library, &LibraryHeader::SortChanged, games, &GameList::SetLibrarySort);
    connect(games, &GameList::LibraryChanged, this, &Launcher::UpdateSelection);
    connect(games, &GameList::LibrarySortChanged, library, &LibraryHeader::SetSortLabel);
    connect(games, &GameList::LibraryEnabledChanged, hero, &QWidget::setEnabled);
    connect(games, &GameList::LibraryEnabledChanged, library, &QWidget::setEnabled);
    connect(games, &GameList::LibraryEnabledChanged, search, &QWidget::setEnabled);

    fade.setStartValue(0.0);
    fade.setEndValue(1.0);
    fade.setDuration(150);
    connect(&fade, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    connect(&HeroArt::Instance(), &HeroArt::Ready, this, [this](u64 program_id) {
        if (program_id == art_id) {
            ShowArt(HeroArt::Instance().Image(program_id), tint);
        }
    });
    SetLibraryAvailable(false);
    UpdateChrome();
}

Launcher::~Launcher() {
    if constexpr (ClientFrame) {
        qApp->removeEventFilter(this);
    }
}

void Launcher::SetLibraryAvailable(bool available) {
    library_available = available;
    library_stack->setCurrentIndex(available ? 1 : 0);
    library->raise();
    if (!available) {
        UpdateSelection({}, 0);
    }
}

void Launcher::SetInlineEmulation(bool running) {
    inline_emulation = running;
    temporary_menu = false;
    if (running && isAncestorOf(owner->focusWidget())) {
        owner->focusWidget()->clearFocus();
    }
    setVisible(!running);
    UpdateChrome();
}

void Launcher::SetInlineFullscreen(bool fullscreen) {
    inline_fullscreen = fullscreen;
    temporary_menu = false;
    UpdateChrome();
}

void Launcher::UpdateChrome() {
    if constexpr (ClientFrame) {
        running_title->setVisible(inline_emulation && !inline_fullscreen);
        owner->menuBar()->setVisible(!inline_fullscreen &&
                                     (pin_menu->isChecked() || temporary_menu));
        edges->setGeometry(owner->rect());
        edges->setVisible(!inline_fullscreen && !owner->isFullScreen() && !owner->isMaximized());
        edges->raise();
    }
}

bool Launcher::eventFilter(QObject* watched, QEvent* event) {
    if (watched == owner) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::WindowStateChange:
            UpdateChrome();
            break;
        case QEvent::WindowDeactivate:
            alt_pressed = false;
            break;
        default:
            break;
        }
    }

    if (inline_fullscreen ||
        (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease &&
         event->type() != QEvent::ShortcutOverride)) {
        return QWidget::eventFilter(watched, event);
    }
    auto* widget = qobject_cast<QWidget*>(watched);
    auto* context = widget ? widget->window() : nullptr;
    while (qobject_cast<QMenu*>(context)) {
        auto* parent = context->parentWidget();
        context = parent ? parent->window() : nullptr;
    }
    if (context != owner) {
        return QWidget::eventFilter(watched, event);
    }
    auto* key = static_cast<QKeyEvent*>(event);
    const bool close_menu =
        key->key() == Qt::Key_Escape && temporary_menu && !pin_menu->isChecked();
    if (event->type() == QEvent::ShortcutOverride && close_menu) {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        if (!key->isAutoRepeat()) {
            alt_pressed = key->key() == Qt::Key_Alt &&
                          (key->modifiers() & ~Qt::AltModifier) == Qt::NoModifier;
        }
        if (close_menu) {
            const auto* search = games->SearchField();
            if (widget != search || search->text().isEmpty()) {
                if (auto* popup = QApplication::activePopupWidget()) {
                    popup->close();
                }
                temporary_menu = false;
                UpdateChrome();
                return true;
            }
        }
    } else if (event->type() == QEvent::KeyRelease && key->key() == Qt::Key_Alt && alt_pressed &&
               !key->isAutoRepeat()) {
        alt_pressed = false;
        if (!pin_menu->isChecked()) {
            temporary_menu = !temporary_menu;
            UpdateChrome();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Launcher::UpdateSelection(const QModelIndex& game, int count) {
    if (!game.isValid() && showing_game && games->IsPopulating()) {
        return;
    }
    showing_game = game.isValid();
    library->SetCount(count);
    QString title = game.data(GameListItemPath::TitleRole).toString();
    if (title.isEmpty()) {
        title = QFileInfo(game.data(GameListItemPath::FullPathRole).toString()).completeBaseName();
    }
    const u64 id = game.data(GameListItemPath::ProgramIdRole).toULongLong();
    if (game.isValid()) {
        hero->SetGame(title, Lichii::PlatformOf(game), playtime.GetPlayTime(id), id);
    } else {
        hero->SetEmpty(library_available, count == 0 && !games->IsPopulating());
    }

    const QPixmap icon = game.data(Qt::DecorationRole).value<QPixmap>();
    if (id != art_id || icon.cacheKey() != art_key) {
        art_id = id;
        art_key = icon.cacheKey();
        HeroArt::Instance().Request(id, title);
        ShowArt(HeroArt::Instance().Image(id),
                game.data(GameListItemPath::HasIconRole).toBool() ? HeroArt::Tint(icon) : QColor{});
    }
}

void Launcher::ShowArt(const QImage& image, const QColor& color) {
    art = image;
    tint = color;
    previous_backdrop = backdrop;
    backdrop = {};
    fade.stop();
    fade.start();
}

QPixmap Launcher::RenderBackdrop(const QImage& image, const QColor& color) const {
    const qreal ratio = devicePixelRatioF();
    QPixmap result(size() * ratio);
    result.setDevicePixelRatio(ratio);
    QPainter painter(&result);
    painter.fillRect(rect(), Color::Window());
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const int canvas_width = std::max(width(), 1280);
    const int canvas_height = std::max(height(), 800);
    const QRectF bounds(240, 0, canvas_width - 240, canvas_height * 0.73125);
    if (!image.isNull()) {
        painter.setOpacity(0.9);
        HeroArt::Paint(painter, bounds, image);
        painter.setOpacity(1.0);
    } else if (color.isValid()) {
        painter.fillRect(bounds, color);
    }
    const QRectF edge(bounds.topLeft(), QSizeF(320, bounds.height()));
    QLinearGradient edge_fade(edge.topLeft(), edge.topRight());
    auto clear = Color::Window();
    clear.setAlpha(0);
    edge_fade.setColorAt(0, Color::Window());
    edge_fade.setColorAt(1, clear);
    painter.fillRect(edge, edge_fade);

    const auto scrim = [](int opacity) {
        auto color = Color::Scrim();
        color.setAlpha(opacity);
        return color;
    };
    QLinearGradient horizontal(0, 0, canvas_width, 0);
    horizontal.setStops(
        {{0.0, scrim(0xf7)}, {0.28, scrim(0xd9)}, {0.60, scrim(0x40)}, {1.0, scrim(0x33)}});
    painter.fillRect(rect(), horizontal);
    QLinearGradient vertical(0, 0, 0, canvas_height);
    vertical.setStops({{0.0, scrim(0x99)},
                       {0.14, scrim(0)},
                       {0.34, scrim(0)},
                       {0.50, scrim(0xed)},
                       {0.70, Color::WindowBottom()},
                       {1.0, Color::WindowBottom()}});
    painter.fillRect(rect(), vertical);
    return result;
}

void Launcher::paintEvent(QPaintEvent*) {
    const auto stale = [this](const QPixmap& pixmap) {
        return pixmap.deviceIndependentSize().toSize() != size() ||
               pixmap.devicePixelRatio() != devicePixelRatioF();
    };
    if (stale(backdrop)) {
        backdrop = RenderBackdrop(art, tint);
    }
    QPainter painter(this);
    if (fade.state() == QAbstractAnimation::Running) {
        if (stale(previous_backdrop)) {
            previous_backdrop = RenderBackdrop({}, {});
        }
        painter.drawPixmap(0, 0, previous_backdrop);
        painter.setOpacity(fade.currentValue().toReal());
    } else {
        previous_backdrop = {};
    }
    painter.drawPixmap(0, 0, backdrop);
}

} // namespace Spotlight
