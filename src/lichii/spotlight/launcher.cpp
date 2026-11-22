// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QRegion>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QWindow>

#include "lichii/game/game_list.h"
#include "lichii/game/search_field.h"
#include "lichii/spotlight/hero.h"
#include "lichii/spotlight/launcher.h"
#include "lichii/spotlight/library_header.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"

namespace Spotlight {

namespace {

class FrameEdges final : public QWidget {
public:
    explicit FrameEdges(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
    }

protected:
    void resizeEvent(QResizeEvent*) override {
        setMask(QRegion(rect()) - QRegion(rect().adjusted(Size, Size, -Size, -Size)));
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const auto edges = EdgesAt(event->pos());
        const bool horizontal = edges & (Qt::LeftEdge | Qt::RightEdge);
        const bool vertical = edges & (Qt::TopEdge | Qt::BottomEdge);
        if (horizontal && vertical) {
            const bool descending =
                edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge);
            setCursor(descending ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
        } else {
            setCursor(horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && window()->windowHandle() &&
            window()->windowHandle()->startSystemResize(EdgesAt(event->pos()))) {
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    Qt::Edges EdgesAt(QPoint point) const {
        Qt::Edges result;
        if (point.x() < Size) {
            result |= Qt::LeftEdge;
        } else if (point.x() >= width() - Size) {
            result |= Qt::RightEdge;
        }
        if (point.y() < Size) {
            result |= Qt::TopEdge;
        } else if (point.y() >= height() - Size) {
            result |= Qt::BottomEdge;
        }
        return result;
    }

    static constexpr int Size = 6;
};

} // namespace

Launcher::Launcher(QMainWindow* window, GameList* games_, GameListPlaceholder* placeholder,
                   RunningTitleBar* running_title_, const PlayTime::PlayTimeManager& playtime_,
                   LauncherActions actions, QWidget* parent)
    : QWidget(parent), owner(window), games(games_), running_title(running_title_),
      playtime(playtime_), hero(new Hero(this)),
      library(new LibraryHeader(actions.grid, actions.list, this)),
      library_stack(new QStackedLayout), pin_menu(new QAction(tr("Show menu bar\tAlt"), this)) {
    setObjectName(QStringLiteral("spotlightLauncher"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto* add = new QMenu(this);
    add->addAction(Icon(u"folder-open", Color::TextSecondary(), 16), tr("Add game folder…"), games,
                   &GameList::AddDirectory);
    add->addAction(actions.load_file);
    add->addAction(actions.load_folder);
    add->addAction(actions.install);
    add->addSeparator();
    add->addAction(Icon(u"arrow-clockwise", Color::TextSecondary(), 16), tr("Rescan library"), this,
                   &Launcher::RescanRequested);

    auto* more = new QMenu(this);
    more->addMenu(actions.tools);
    more->addMenu(actions.multiplayer);
    more->addMenu(actions.help);
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
    column->addWidget(library);
    library_stack->setContentsMargins(0, 0, 0, 0);
    library_stack->addWidget(placeholder);
    library_stack->addWidget(games);
    column->addLayout(library_stack, 1);

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
    if (!available) {
        UpdateSelection({}, 0);
    }
}

void Launcher::SetInlineEmulation(bool running) {
    inline_emulation = running;
    temporary_menu = false;
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
    library->SetCount(count);
    if (game.isValid()) {
        QString title = game.data(GameListItemPath::TitleRole).toString();
        if (title.isEmpty()) {
            title =
                QFileInfo(game.data(GameListItemPath::FullPathRole).toString()).completeBaseName();
        }
        const u64 id = game.data(GameListItemPath::ProgramIdRole).toULongLong();
        hero->SetGame(title, Lichii::PlatformOf(game), playtime.GetPlayTime(id));
    } else {
        hero->SetEmpty(library_available, count == 0 && !games->IsPopulating());
    }

    const QPixmap image = game.data(Qt::DecorationRole).value<QPixmap>();
    if (image.cacheKey() != art_key) {
        art_key = image.cacheKey();
        previous_art = art;
        art = image.isNull() ? QImage{}
                             : image.toImage()
                                   .scaled(8, 8, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                   .scaled(144, 144, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        fade.stop();
        fade.start();
    }
}

void Launcher::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Color::Window());
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const qreal alpha = fade.currentValue().toReal();
    const int canvas_width = std::max(width(), 1280);
    const int canvas_height = std::max(height(), 800);
    const QRectF bounds(240, 0, canvas_width - 240, canvas_height * 0.73125);
    const auto draw = [&](const QImage& image, qreal opacity) {
        if (image.isNull() || bounds.isEmpty() || opacity <= 0) {
            return;
        }
        const QSizeF area =
            QSizeF(image.size()).scaled(bounds.size(), Qt::KeepAspectRatioByExpanding);
        const QPointF origin = bounds.center() - QPointF(area.width() / 2, area.height() / 2);
        painter.save();
        painter.setClipRect(bounds);
        painter.setOpacity(opacity * 0.9);
        painter.drawImage(QRectF(origin, area), image);
        painter.restore();
    };
    draw(previous_art, 1.0 - alpha);
    draw(art, alpha);

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
}

} // namespace Spotlight
