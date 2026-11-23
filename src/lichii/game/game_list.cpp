// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2015 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <filesystem>
#include <functional>
#include <utility>

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QListView>
#include <QMenu>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QVariantAnimation>
#include <QtConcurrent/QtConcurrentRun>
#include <qlayoutitem.h>

#include "common/common_types.h"
#include "common/fs/fs_util.h"
#include "common/logging.h"
#include "core/core.h"
#include "core/file_sys/nup_writer.h"
#ifdef LICHII_NX_TOOLS
#include "lichii/nx_tools.h"
#endif
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "lichii/game/carousel.h"
#include "lichii/game/game_grid.h"
#include "lichii/game/game_list.h"
#include "lichii/game/game_tree.h"
#include "lichii/game/search_field.h"
#include "lichii/main_window.h"
#include "lichii/shader_warm_dialog.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "lichii/util/controller_navigation.h"
#include "lichii/util/icon_search.h"
#include "lichii/util/util.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/game.h"

namespace {
// A few at a time keeps SteamGridDB happy and the scan of a large library short.
constexpr int MaxParallelLookups = 2;
} // namespace

class GameListViewModel final : public QSortFilterProxyModel {
public:
    explicit GameListViewModel(GameListModel* source, QObject* parent)
        : QSortFilterProxyModel(parent), library(source) {
        setSourceModel(source);
        setSortRole(GameListItem::SortRole);
        setSortCaseSensitivity(Qt::CaseInsensitive);
        sort(0);
    }

    void SetFilter(const QString& text, Lichii::LibraryPlatform platform) {
        query = text;
        platform_filter = platform;
        invalidateRowsFilter();
    }

    bool HasFilter() const {
        return !query.isEmpty() || platform_filter != Lichii::LibraryPlatform::All;
    }

    void SortLibrary(int column, Qt::SortOrder order, bool recent) {
        recent_first = recent;
        if (sortColumn() == column && sortOrder() == order) {
            invalidate();
        } else {
            sort(column, order);
        }
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QModelIndex item = library->index(row, 0, parent);
        switch (item.data(GameListItem::TypeRole).value<GameListItemType>()) {
        case GameListItemType::Game: {
            return (platform_filter == Lichii::LibraryPlatform::All ||
                    platform_filter == Lichii::PlatformOf(item)) &&
                   Lichii::FilterMatches(query, library->itemFromIndex(item));
        }
        case GameListItemType::Favorites:
            return !HasFilter() && library->rowCount(item) > 0;
        default:
            if (!HasFilter()) {
                return true;
            }
            for (int i = 0; i < library->rowCount(item); ++i) {
                if (filterAcceptsRow(i, item)) {
                    return true;
                }
            }
            return false;
        }
    }

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        const QModelIndex lhs = left.siblingAtColumn(0);
        const QModelIndex rhs = right.siblingAtColumn(0);
        if (lhs.data(GameListItem::TypeRole).value<GameListItemType>() != GameListItemType::Game) {
            return sortOrder() == Qt::AscendingOrder ? left.row() < right.row()
                                                     : left.row() > right.row();
        }
        if (recent_first) {
            const auto rank = [](const QModelIndex& item) {
                const auto& recent = UISettings::values.recent_files;
                const qsizetype row =
                    recent.indexOf(item.data(GameListItemPath::FullPathRole).toString());
                return row < 0 ? recent.size() : row;
            };
            if (const auto a = rank(lhs), b = rank(rhs); a != b) {
                return a < b;
            }
            return QString::localeAwareCompare(lhs.data(GameListItemPath::TitleRole).toString(),
                                               rhs.data(GameListItemPath::TitleRole).toString()) <
                   0;
        }
        return QSortFilterProxyModel::lessThan(left, right);
    }

private:
    GameListModel* library;
    QString query;
    Lichii::LibraryPlatform platform_filter = Lichii::LibraryPlatform::All;
    bool recent_first = true;
};

GameList::GameList(FileSys::VirtualFilesystem vfs_, FileSys::ManualContentProvider* provider_,
                   PlayTime::PlayTimeManager& play_time_manager_, Core::System& system_,
                   MainWindow* parent)
    : QWidget{parent}, vfs{std::move(vfs_)}, provider{provider_},
      play_time_manager{play_time_manager_}, system{system_} {

    setObjectName(QStringLiteral("spotlightGameList"));
    this->main_window = parent;
    artwork_pool.setMaxThreadCount(MaxParallelLookups);
    layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setLayout(layout);

    item_model = new GameListModel(vfs, provider, play_time_manager, system, this);
    item_model->SetFlat(true);
    library_model = new GameListViewModel(item_model, this);

    SetupViews();

    search_field = new GameListSearchField(this);
    connect(search_field, &QLineEdit::textChanged, this, &GameList::SetFilterText);
    connect(search_field, &GameListSearchField::Submitted, this, &GameList::PlaySingleResult);

    controller_navigation = new ControllerNavigation(system.HIDCore(), this);

    SetupScrollAnimation();

    connect(tree_view, &QTreeView::doubleClicked, this, &GameList::ValidateEntry);
    connect(tree_view, &QTreeView::customContextMenuRequested, this, &GameList::PopupContextMenu);

    connect(grid_view, &QListView::doubleClicked, this, &GameList::ValidateEntry);
    connect(grid_view, &GameGrid::PlayRequested, this, &GameList::ValidateEntry);
    connect(grid_view, &QListView::customContextMenuRequested, this, &GameList::PopupContextMenu);

    connect(carousel_view, &QListView::doubleClicked, this, &GameList::ValidateEntry);
    connect(carousel_view, &GameGrid::PlayRequested, this, &GameList::ValidateEntry);
    connect(carousel_view, &QListView::customContextMenuRequested, this, &GameList::PopupContextMenu);

    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent, this,
            [this](Qt::Key key) {
                if (system.IsPoweredOn()) {
                    return;
                }
                if (!this->isActiveWindow()) {
                    return;
                }
                QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                QCoreApplication::postEvent(m_currentView, event);
            });

    connect(item_model, &GameListModel::PopulatingCompleted, this,
            &GameList::OnPopulatingCompleted);

    connect(item_model, &GameListModel::ShowList, this, &GameList::ShowList);
    connect(item_model, &GameListModel::SaveConfig, this, &GameList::SaveConfig);
    connect(item_model, &GameListModel::PopulatingStarted, this, &GameList::OnPopulate);

    connect(library_model, &QAbstractItemModel::dataChanged, this, [this] {
        if (!m_populating) {
            emit LibraryChanged(SelectedGame(), result_count);
        }
    });

    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    ResetViewMode();
}

void GameList::SetNupConversionRunning(bool running) {
    nup_conversion_running = running;
}

void GameList::SetNxToolRunning(bool running) {
    nx_tool_running = running;
}

GameList::~GameList() {
    // Running lookups see the flag and stop; their results are dropped with this widget. Lookups
    // that have not started are dropped; the pool then waits for the running ones.
    artwork_cancel->store(true);
    artwork_pool.clear();
    UnloadController();
}

void GameList::SetupViews() {
    tree_view = new GameTree(this);
    grid_view = new GameGrid(this);
    carousel_view = new GameCarousel(this);

    tree_view->SetModel(library_model);
    grid_view->setModel(library_model);
    carousel_view->setModel(library_model);

    connect(tree_view->header(), &QHeaderView::sortIndicatorChanged, this, &GameList::ApplySort);

    for (auto* view : std::array<QAbstractItemView*, 3>{tree_view, grid_view, carousel_view}) {
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
        view->viewport()->setAttribute(Qt::WA_Hover);
        view->setMouseTracking(true);
        connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, view] {
            if (view == m_currentView && !m_populating) {
                RememberSelection();
                emit LibraryChanged(SelectedGame(), result_count);
            }
        });
        auto* properties = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), view);
        properties->setContext(Qt::WidgetWithChildrenShortcut);
        connect(properties, &QShortcut::activated, this, [this] {
            const auto game = SelectedGame();
            if (game.isValid()) {
                emit OpenPerGameGeneralRequested(
                    game.data(GameListItemPath::ProgramIdRole).toULongLong(),
                    game.data(GameListItemPath::FullPathRole).toString().toStdString());
            }
        });
        auto* remove = new QShortcut(QKeySequence(Qt::Key_Delete), view);
        remove->setContext(Qt::WidgetWithChildrenShortcut);
        connect(remove, &QShortcut::activated, this, [this] {
            const u64 id = SelectedGame().data(GameListItemPath::ProgramIdRole).toULongLong();
            if (id != 0) {
                emit RemoveInstalledEntryRequested(id, QtCommon::Game::InstalledEntryType::Game);
            }
        });
        auto* favorite = new QShortcut(QKeySequence(Qt::Key_F), view);
        favorite->setContext(Qt::WidgetWithChildrenShortcut);
        connect(favorite, &QShortcut::activated, this, [this] {
            const u64 id = SelectedGame().data(GameListItemPath::ProgramIdRole).toULongLong();
            if (id != 0) {
                ToggleFavorite(id);
            }
        });
    }
}

void GameList::SetFilterFocus() {
    if (search_field->isVisible()) {
        search_field->setFocus(Qt::ShortcutFocusReason);
    }
}

GameListSearchField* GameList::SearchField() const {
    return search_field;
}

void GameList::SetFilterText(const QString& text) {
    filter_text = text.toLower();
    ApplyFilter();
}

void GameList::SetPlatform(Lichii::LibraryPlatform new_platform) {
    platform = new_platform;
    ApplyFilter();
    Spotlight::FadeIn(m_currentView);
}

void GameList::SetLibrarySort(Lichii::LibrarySort sort) {
    using enum Lichii::LibrarySort;
    int column = sort == Recent ? -1 : GameListModel::COLUMN_NAME;
    if (sort == PlayTime) {
        column = GameListModel::COLUMN_PLAY_TIME;
    } else if (sort == Size) {
        column = GameListModel::COLUMN_SIZE;
    }
    const auto order =
        (sort == PlayTime || sort == Size) ? Qt::DescendingOrder : Qt::AscendingOrder;
    {
        const QSignalBlocker blocked(tree_view->header());
        tree_view->header()->setSortIndicator(column, order);
    }
    ApplySort();
}

void GameList::ApplySort() {
    const auto* header = tree_view->header();
    const int column = header->sortIndicatorSection();
    library_model->SortLibrary(std::max(0, column), header->sortIndicatorOrder(), column < 0);
    emit LibrarySortChanged(
        column < 0 ? QString{} : library_model->headerData(column, Qt::Horizontal).toString());
    UpdateLibrary();
}

void GameList::RefreshLibraryOrder() {
    library_model->invalidate();
    UpdateLibrary();
}

QModelIndex GameList::SelectedGame() const {
    if (!m_currentView) {
        return {};
    }
    const auto current = m_currentView->currentIndex().siblingAtColumn(0);
    return current.data(GameListItem::TypeRole).value<GameListItemType>() == GameListItemType::Game
               ? current
               : QModelIndex{};
}

void GameList::PlaySelected() {
    ValidateEntry(SelectedGame());
}

void GameList::SelectProgram(u64 program_id) {
    const auto* model = m_currentView->model();
    const auto find = [&](const QModelIndex& parent, const auto& self) -> QModelIndex {
        for (int row = 0; row < model->rowCount(parent); ++row) {
            const QModelIndex index = model->index(row, 0, parent);
            if (index.data(GameListItem::TypeRole).value<GameListItemType>() ==
                    GameListItemType::Game &&
                index.data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                return index;
            }
            if (const QModelIndex child = self(index, self); child.isValid()) {
                return child;
            }
        }
        return {};
    };
    if (const QModelIndex index = find({}, find); index.isValid()) {
        m_currentView->setCurrentIndex(index);
        m_currentView->scrollTo(index);
        m_currentView->setFocus();
    }
}

void GameList::PlaySingleResult() {
    if (result_count == 1 && !m_populating) {
        ValidateEntry(first_result);
    }
}

void GameList::ShowSelectedGameMenu(const QPoint& position) {
    const auto game = SelectedGame();
    if (!game.isValid()) {
        return;
    }
    Spotlight::Menu menu(this);
    menu.setMinimumWidth(270);
    AddGamePopup(menu, game.data(GameListItemPath::ProgramIdRole).toULongLong(),
                 game.data(GameListItemPath::FullPathRole).toString().toStdString());
    menu.exec(position);
}

bool GameList::IsPopulating() const {
    return m_populating;
}

void GameList::OnPopulate() {
    m_populating = true;
    first_result = QModelIndex{};
    result_count = 0;
    emit LibraryChanged({}, 0);
    const QWidget* focus = window()->focusWidget();
    refocus_view = focus == nullptr || focus == m_currentView;
    if (refocus_view) {
        window()->setFocus();
    }
    m_currentView->setEnabled(false);

    switch (game_list_mode) {
    case Settings::GameListMode::TreeView:
        tree_view->UpdateColumnVisibility();
        break;
    case Settings::GameListMode::GridView:
        grid_view->UpdateIconSize();
        break;
    case Settings::GameListMode::CarouselView:
        carousel_view->UpdateIconSize();
        break;
    }
}

void GameList::PopulateAsync(QVector<UISettings::GameDir>& game_dirs) {
    item_model->PopulateAsync(game_dirs);
}

void GameList::SetScanEnabled(bool enabled, bool stop_worker) {
    item_model->SetScanEnabled(enabled, stop_worker);
}

void GameList::SaveInterfaceLayout() {
    tree_view->SaveInterfaceLayout();
}

void GameList::LoadInterfaceLayout() {
    const QSignalBlocker blocked(tree_view->header());
    tree_view->LoadInterfaceLayout();
    ApplySort();
}

QStandardItemModel* GameList::GetModel() const {
    return item_model;
}

void GameList::UnloadController() {
    controller_navigation->UnloadController();
}

void GameList::ResetViewMode() {
    const auto mode = UISettings::values.game_list_mode.GetValue();
    game_list_mode = mode;
    const bool view_focused = m_currentView && m_currentView->hasFocus();
    const QModelIndex selected = SelectedGame();

    if (m_currentView)
        layout->removeWidget(m_currentView);

    bool newTreeMode = false;

    switch (mode) {
    case Settings::GameListMode::TreeView:
        m_currentView = tree_view;
        newTreeMode = true;

        break;
    case Settings::GameListMode::GridView:
        m_currentView = grid_view;
        newTreeMode = false;

        break;
    case Settings::GameListMode::CarouselView:
        m_currentView = carousel_view;
        newTreeMode = false;

        break;
    default:
        UNREACHABLE();
    }

    tree_view->setVisible(false);
    grid_view->setVisible(false);
    carousel_view->setVisible(false);

    tree_view->setEnabled(false);
    grid_view->setEnabled(false);
    carousel_view->setEnabled(false);

    m_currentView->setVisible(true);
    Spotlight::FadeIn(m_currentView);
    const int side_margin = newTreeMode ? 36 : 0;
    layout->setContentsMargins(side_margin, newTreeMode ? GameGrid::HeaderOverlap : 0, side_margin,
                               0);
    // MainWindow switches the view right after starting the first populate; keep it disabled
    // until that finishes, as OnPopulate does for every other populate.
    m_currentView->setEnabled(!m_populating);
    layout->insertWidget(0, m_currentView);
    if (selected.isValid()) {
        m_currentView->setCurrentIndex(selected);
    }
    if (view_focused) {
        m_currentView->setFocus();
    }

    auto view = m_currentView->viewport();

    view->grabGesture(Qt::SwipeGesture);
    view->grabGesture(Qt::PanGesture);

    QScroller::grabGesture(view, QScroller::TouchGesture);

    auto scroller = QScroller::scroller(view);
    QScrollerProperties props;
    props.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    props.setScrollMetric(QScrollerProperties::VerticalOvershootPolicy,
                          QScrollerProperties::OvershootAlwaysOff);
    scroller->setScrollerProperties(props);

    m_isTreeMode = newTreeMode;
    if (!m_populating) {
        ApplyFilter();
    }
}

void GameList::ApplyFilter() {
    library_model->SetFilter(filter_text, platform);
    UpdateLibrary();
}

void GameList::RememberSelection() {
    if (const auto game = SelectedGame(); game.isValid()) {
        selected_path = game.data(GameListItemPath::FullPathRole).toString();
    }
}

void GameList::UpdateLibrary() {
    if (m_populating || !m_currentView) {
        return;
    }

    result_count = 0;
    first_result = QModelIndex{};
    QModelIndex first_visible;
    QModelIndex previous;
    const auto visit = [&](const auto& self, const QModelIndex& parent, bool expanded) -> void {
        for (int row = 0; row < library_model->rowCount(parent); ++row) {
            const auto item = library_model->index(row, 0, parent);
            const auto type = item.data(GameListItem::TypeRole).value<GameListItemType>();
            if (type == GameListItemType::Game) {
                if (!first_result.isValid()) {
                    first_result = item;
                }
                if (expanded && !first_visible.isValid()) {
                    first_visible = item;
                }
                if (expanded && !previous.isValid() && !selected_path.isEmpty() &&
                    item.data(GameListItemPath::FullPathRole).toString() == selected_path) {
                    previous = item;
                }
                ++result_count;
            } else if (type != GameListItemType::Favorites) {
                self(self, item, expanded && tree_view->isExpanded(item));
            }
        }
    };
    visit(visit, {}, true);
    if (!SelectedGame().isValid() && first_visible.isValid()) {
        m_currentView->setCurrentIndex(previous.isValid() ? previous : first_visible);
    }
    RememberSelection();
    emit LibraryChanged(SelectedGame(), result_count);
}

void GameList::OnPopulatingCompleted(const QStringList& watch_list) {
    // Watcher updates
    auto* watcher = item_model->GetWatcher();
    auto current_watch_list = watcher->directories();

    constexpr qsizetype LIMIT_WATCH_DIRECTORIES = 5000;
    constexpr int SLICE_SIZE = 25;

    QStringList to_remove, to_add;

    const auto slice = [&](const QStringList& list, std::function<void(const QStringList&)> callback) {
        const int len = (std::min)(list.size(), LIMIT_WATCH_DIRECTORIES);
        for (int i = 0; i < len; i += SLICE_SIZE) {
            auto chunk = list.mid(i, SLICE_SIZE);
            if (!chunk.isEmpty()) {
                callback(chunk);
            }
        }
    };

    // remove any paths not in the new watch list
    for (const auto& path : std::as_const(current_watch_list)) {
        if (!watch_list.contains(path)) {
            to_remove.emplaceBack(path);
        }
    }

    slice(to_remove, [watcher](const QStringList& chunk) { watcher->removePaths(chunk); });

    // add any paths not in the old watch list
    for (const auto& path : std::as_const(watch_list)) {
        if (!current_watch_list.contains(path)) {
            to_add.emplaceBack(path);
        }
    }

    slice(to_add, [watcher](const QStringList& chunk) { watcher->addPaths(chunk); });

    m_populating = false;
    m_currentView->setEnabled(true);
    ApplyFilter();
    if (refocus_view && window()->focusWidget() == window()) {
        m_currentView->setFocus();
    }
    refocus_view = false;

    // Artwork a lookup saved while this scan ran can be missing from rows added after the save.
    for (const u64 program_id : std::as_const(artwork_saved)) {
        ShowCustomArtwork(program_id);
    }
    artwork_saved.clear();

    emit PopulatingCompleted();
    QueueArtworkLookups();
}

QString GameList::GameTitle(u64 program_id) const {
    QString title;
    const std::function<void(const QStandardItem*)> visit = [&](const QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount() && title.isEmpty(); ++row) {
            const QStandardItem* item = parent->child(row);
            if (item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                    GameListItemType::Game &&
                item->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                title = item->data(GameListItemPath::TitleRole).toString();
            }
            visit(item);
        }
    };
    visit(item_model->invisibleRootItem());
    return title;
}

void GameList::QueueArtworkLookups() {
    if (UISettings::values.steamgriddb_api_key.GetValue().empty()) {
        return;
    }
    const std::function<void(const QStandardItem*)> visit = [&](const QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            const QStandardItem* item = parent->child(row);
            if (item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::Game) {
                // Games with an icon in the file or custom artwork already have a picture.
                if (!item->data(GameListItemPath::HasIconRole).toBool()) {
                    QueueArtworkLookup(item->data(GameListItemPath::ProgramIdRole).toULongLong(),
                                       item->data(GameListItemPath::TitleRole).toString(), false);
                }
            } else {
                visit(item);
            }
        }
    };
    visit(item_model->invisibleRootItem());
}

void GameList::QueueArtworkLookup(u64 program_id, const QString& title, bool forced) {
    const std::string& key = UISettings::values.steamgriddb_api_key.GetValue();
    if (program_id == 0 || title.trimmed().isEmpty() || key.empty()) {
        return;
    }
    // A new key gives the games looked up with the old one another try.
    if (key != artwork_key) {
        artwork_key = key;
        artwork_attempted.clear();
    }
    if (!forced && artwork_attempted.contains(program_id)) {
        return;
    }
    artwork_attempted.insert(program_id);
    for (auto& queued : artwork_queue) {
        if (queued.program_id == program_id) {
            // A forced lookup takes over the waiting one, with the name the game has now.
            if (forced) {
                queued.title = title.trimmed();
                queued.forced = true;
            }
            return;
        }
    }
    const u64 serial = ++artwork_serial;
    artwork_newest.insert(program_id, serial);
    artwork_queue.append({program_id, title.trimmed(), forced, serial});
    StartArtworkLookups();
}

void GameList::StartArtworkLookups() {
    while (artwork_running < MaxParallelLookups && !artwork_queue.isEmpty()) {
        ArtworkLookup lookup = artwork_queue.takeFirst();
        lookup.key = UISettings::values.steamgriddb_api_key.GetValue();
        // A waiting lookup is always the newest one of its game.
        if (lookup.forced) {
            artwork_forced.insert(lookup.program_id);
        } else {
            artwork_forced.remove(lookup.program_id);
        }
        ++artwork_running;
        auto* const watcher = new QFutureWatcher<ArtworkLookupResult>(this);
        connect(watcher, &QFutureWatcher<ArtworkLookupResult>::finished, this, [this, watcher,
                                                                                 lookup] {
            watcher->deleteLater();
            --artwork_running;
            const ArtworkLookupResult result = watcher->result();
            // A newer lookup of the same game wins, whichever finishes first.
            const bool newest = artwork_newest.value(lookup.program_id) == lookup.serial;
            if (newest) {
                artwork_newest.remove(lookup.program_id);
                artwork_forced.remove(lookup.program_id);
            }
            // An automatic lookup only fills a game without artwork: artwork the user set while
            // it ran stays.
            std::error_code ec;
            const bool artwork_added =
                !lookup.forced &&
                std::filesystem::exists(QtCommon::Game::CustomArtworkPath(lookup.program_id), ec);
            // A lookup whose key was cleared or replaced while it ran saves nothing.
            const bool key_current =
                lookup.key == UISettings::values.steamgriddb_api_key.GetValue();
            if (result.status == ArtworkLookupStatus::Found && newest && !artwork_added &&
                key_current) {
                if (QtCommon::Game::SaveCustomArtwork(lookup.program_id, result.image)) {
                    LOG_INFO(Frontend, "SteamGridDB artwork saved for {:016X}", lookup.program_id);
                    artwork_saved.insert(lookup.program_id);
                    ShowCustomArtwork(lookup.program_id);
                }
            } else if (newest && result.status != ArtworkLookupStatus::Found &&
                       result.status != ArtworkLookupStatus::Cancelled &&
                       !artwork_failure_logged) {
                // A found picture that was dropped is no failure, and neither is the result of a
                // lookup replaced by a newer one, so the one log stays free.
                // Logged once per session. The key itself is never logged.
                artwork_failure_logged = true;
                const char* reason = "it could not be reached";
                if (result.status == ArtworkLookupStatus::BadApiKey) {
                    reason = "the API key was rejected";
                } else if (result.status == ArtworkLookupStatus::NotFound) {
                    reason = "no match was found";
                }
                LOG_INFO(Frontend, "SteamGridDB artwork lookup for {:016X} failed: {} {}",
                         lookup.program_id, reason, result.detail);
            }
            if (result.status == ArtworkLookupStatus::BadApiKey &&
                lookup.key == UISettings::values.steamgriddb_api_key.GetValue()) {
                // Every other lookup would fail the same way. A late failure with a key that was
                // replaced since leaves the lookups of the new key alone.
                artwork_queue.clear();
            }
            StartArtworkLookups();
        });
        // Switch 2 program IDs start with 04. Their titles are often remakes that share the name
        // of an older game, so the newest release of that name is the right one.
        const bool prefer_newest = (lookup.program_id >> 56) == 0x04;
        watcher->setFuture(QtConcurrent::run(
            &artwork_pool, [title = lookup.title.toStdString(), key = lookup.key,
                            cancel = artwork_cancel, prefer_newest] {
                return LookUpSteamGridDbArtwork(title, key, *cancel, prefer_newest);
            }));
    }
}

void GameList::ShowCustomArtwork(u64 program_id) {
    // Updates the shown pictures in place. A rescan here would stop and wait for a scan that is
    // still running on the UI thread, and rebuild the whole library for one picture.
    const std::vector<u8> artwork = QtCommon::Game::ReadCustomArtwork(program_id);
    if (artwork.empty()) {
        return;
    }
    const std::function<void(QStandardItem*)> visit = [&](QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            QStandardItem* item = parent->child(row);
            if (item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::Game) {
                if (item->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                    GameListItemPath::SetPicture(item, artwork);
                }
            } else {
                visit(item);
            }
        }
    };
    visit(item_model->invisibleRootItem());
}

void GameList::SetGameTitle(u64 program_id, const QString& title) {
    if (program_id == 0 || title.isEmpty()) {
        return;
    }
    // Favorites are plain copies of a library entry that store their shown text, so they take it
    // from an updated library entry afterwards.
    QList<QStandardItem*> copies;
    QVariant display;
    QVariant sort;
    const std::function<void(QStandardItem*)> visit = [&](QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            QStandardItem* item = parent->child(row);
            if (item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::Game) {
                if (item->data(GameListItemPath::ProgramIdRole).toULongLong() == program_id) {
                    item->setData(title, GameListItemPath::TitleRole);
                    // A copy is a plain item: only library entries report the game type.
                    if (item->type() != static_cast<int>(GameListItemType::Game)) {
                        copies.append(item);
                    } else if (!display.isValid()) {
                        display = item->data(Qt::DisplayRole);
                        sort = item->data(GameListItem::SortRole);
                    }
                }
            } else {
                visit(item);
            }
        }
    };
    visit(item_model->invisibleRootItem());
    if (display.isValid()) {
        for (QStandardItem* copy : copies) {
            copy->setData(display, Qt::DisplayRole);
            copy->setData(sort, GameListItem::SortRole);
        }
    }
    ApplyFilter();
    // A waiting artwork lookup searches the new name. A running one searched the old name, so
    // its result is dropped and the new name is looked up instead.
    const QString search_title = title.trimmed();
    for (auto& queued : artwork_queue) {
        if (queued.program_id == program_id) {
            if (!search_title.isEmpty()) {
                queued.title = search_title;
            }
            return;
        }
    }
    if (!artwork_newest.contains(program_id)) {
        return;
    }
    const bool forced = artwork_forced.contains(program_id);
    artwork_newest.remove(program_id);
    artwork_forced.remove(program_id);
    // An automatic lookup only fills a game that still has no artwork.
    std::error_code ec;
    if (search_title.isEmpty() || UISettings::values.steamgriddb_api_key.GetValue().empty() ||
        (!forced && std::filesystem::exists(QtCommon::Game::CustomArtworkPath(program_id), ec))) {
        return;
    }
    const u64 serial = ++artwork_serial;
    artwork_newest.insert(program_id, serial);
    artwork_queue.append({program_id, search_title, forced, serial});
    StartArtworkLookups();
}

void GameList::OnCustomArtworkChanged(u64 program_id) {
    // A running lookup's result is dropped and a waiting one is not started.
    artwork_newest.remove(program_id);
    artwork_forced.remove(program_id);
    artwork_queue.removeIf(
        [program_id](const ArtworkLookup& queued) { return queued.program_id == program_id; });
}

void GameList::RefreshGameDirectory() {
    item_model->RefreshGameDirectory();
}

void GameList::RefreshExternalContent() {
    item_model->RefreshExternalContent();
}

void GameList::UpdateIconSizes() {
    switch (game_list_mode) {
    case Settings::GameListMode::GridView:
        grid_view->UpdateIconSize();
        break;
    case Settings::GameListMode::CarouselView:
        carousel_view->UpdateIconSize();
        break;
    case Settings::GameListMode::TreeView:
        break;
    }
}

void GameList::ValidateEntry(const QModelIndex& item) {
    const auto selected = item.sibling(item.row(), 0);

    switch (selected.data(GameListItem::TypeRole).value<GameListItemType>()) {
    case GameListItemType::Game: {
        const QString file_path = selected.data(GameListItemPath::FullPathRole).toString();
        if (file_path.isEmpty())
            return;
        const QFileInfo file_info(file_path);
        if (!file_info.exists())
            return;

        if (file_info.isDir()) {
            const QDir dir{file_path};
            const QStringList matching_main = dir.entryList({QStringLiteral("main")}, QDir::Files);
            if (matching_main.size() == 1) {
                emit GameChosen(dir.path() + QDir::separator() + matching_main[0]);
            }
            return;
        }

        const auto title_id = selected.data(GameListItemPath::ProgramIdRole).toULongLong();

        search_field->clear();
        emit GameChosen(file_path, title_id);
        break;
    }
    case GameListItemType::AddDir:
        emit AddDirectory();
        break;
    default:
        break;
    }
}

void GameList::ToggleFavorite(u64 program_id) {
    item_model->ToggleFavorite(program_id);
    ApplyFilter();
    m_currentView->viewport()->update();
}

void GameList::PopupContextMenu(const QPoint& menu_location) {
    const QModelIndex item = m_currentView->indexAt(menu_location);
    const QPoint position = m_currentView->viewport()->mapToGlobal(menu_location);
    if (item.isValid()) {
        m_currentView->setCurrentIndex(item.siblingAtColumn(0));
        ShowSelectedGameMenu(position);
        return;
    }
    Spotlight::Menu blank_menu;
    QAction* add_directory = blank_menu.addAction(tr("&Add New Game Directory"));
    connect(add_directory, &QAction::triggered, this, &GameList::AddDirectory);
    blank_menu.exec(position);
}

void GameList::SetReportableProgramId(u64 program_id) {
    reportable_program_id = program_id;
}

void GameList::AddGamePopup(Spotlight::Menu& context_menu, u64 program_id, const std::string& path) {
    const auto group = [](Spotlight::Menu& parent, const QString& label, QStringView icon,
                          const QColor& color = Spotlight::Color::MenuInk()) {
        return parent.AddSubmenu(Spotlight::Icon(icon, color, 16), label);
    };
    const auto add = [](QMenu& parent, const QString& label, QStringView icon) {
        return parent.addAction(Spotlight::Icon(icon, Spotlight::Color::MenuInk(), 16), label);
    };
    QAction* start_game = add(context_menu, tr("Play\tEnter"), u"play-fill");
    QMenu* custom_setup =
        group(context_menu, tr("Play with a custom setup"), u"sliders-horizontal");
    QAction* start_game_global = custom_setup->addAction(tr("Play with global settings"));
    context_menu.addSeparator();
    QAction* properties = add(context_menu, tr("Game settings\tCtrl+E"), u"gear-six");
    QAction* mods = add(context_menu, tr("Mods and cheats"), u"puzzle-piece");
    mods->setVisible(program_id != 0);
    connect(mods, &QAction::triggered, this,
            [this, program_id, path] { emit OpenGameModsRequested(program_id, path); });
    QMenu* open_folder = group(context_menu, tr("Open folder"), u"folder-open");
    open_folder->menuAction()->setVisible(program_id != 0);
    QAction* open_save_location = open_folder->addAction(tr("Save data"));
    QAction* open_mod_location = open_folder->addAction(tr("Mods"));
    QAction* open_transferable_shader_cache =
        open_folder->addAction(tr("Transferable pipeline cache"));
    QMenu* updates = group(context_menu, tr("Updates and DLC"), u"cloud-arrow-down");
    updates->menuAction()->setVisible(program_id != 0);
    QAction* remove_update = updates->addAction(tr("Remove installed update"));
    QAction* remove_dlc = updates->addAction(tr("Remove all installed DLC"));
    // NUP and NX archives: conversion, and the title and icon stored in the file.
    const bool is_loose_dump =
        FileSys::Nup::DumpRootFromMain(std::filesystem::path{Common::FS::ToU8String(path)})
            .has_value();
    const QString suffix = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    const bool is_nup_archive = !is_loose_dump && suffix == QStringLiteral("nup");
    const bool can_convert_nup =
        is_loose_dump || suffix == QStringLiteral("nsp") || suffix == QStringLiteral("xci") ||
        suffix == QStringLiteral("nca");
    auto* archive = group(context_menu, tr("Archive"), u"archive");
    QAction* convert_nup = nullptr;
    if (can_convert_nup) {
        convert_nup = archive->addAction(tr("Convert to NUP…"));
        convert_nup->setEnabled(!nup_conversion_running);
    }
    QAction* convert_nx = nullptr;
    QAction* add_to_nx = nullptr;
    QAction* remove_from_nx = nullptr;
    QAction* edit_nx_metadata = nullptr;
#ifdef LICHII_NX_TOOLS
    if (NxTools::CanConvert(path)) {
        convert_nx = archive->addAction(tr("Convert to NX…"));
        convert_nx->setEnabled(!nx_tool_running);
    }
#endif
    QAction* extract_nup = nullptr;
    QAction* edit_nup = nullptr;
    if (is_nup_archive) {
        extract_nup = archive->addAction(tr("Dump NUP…"));
        extract_nup->setEnabled(!nup_conversion_running);
        edit_nup = archive->addAction(tr("Edit title and icon…"));
        edit_nup->setEnabled(!nup_conversion_running);
    }
#ifdef LICHII_NX_TOOLS
    if (NxTools::IsNxArchive(path)) {
        edit_nx_metadata = archive->addAction(tr("Edit title and icon…"));
        QMenu* nx_menu = archive->AddSubmenu({}, tr("Manage NX archive"));
        add_to_nx = nx_menu->addAction(tr("Add update or DLC…"));
        remove_from_nx = nx_menu->addAction(tr("Remove update or DLC…"));
        edit_nx_metadata->setEnabled(!nx_tool_running);
        nx_menu->setEnabled(!nx_tool_running);
    }
#endif
    archive->menuAction()->setVisible(!archive->isEmpty());
#if !defined(__APPLE__)
    QMenu* shortcut_menu = group(context_menu, tr("Add to desktop"), u"push-pin");
    QAction* create_desktop_shortcut = shortcut_menu->addAction(tr("Desktop"));
    QAction* create_applications_menu_shortcut = shortcut_menu->addAction(tr("Applications menu"));
#endif
    context_menu.addSeparator();
    QAction* favorite = context_menu.addAction(tr("Add to favourites\tF"));
    QAction* copy_tid = add(context_menu, tr("Copy title ID"), u"copy");
    auto* dump_verify = group(context_menu, tr("Dump and verify"), u"cube");
    auto* more = group(context_menu, tr("More"), u"dots-three");
    QMenu* shaders = group(*more, tr("Shaders"), u"cpu");
    const bool has_shader_warm = ShaderWarmFilesExist(program_id);
    QAction* prepare_shaders = shaders->addAction(has_shader_warm ? tr("Rebuild Shader Precache")
                                                                  : tr("Build Shader Precache"));
    QAction* precompile_shaders = shaders->addAction(tr("Precompile All Shaders"));
    precompile_shaders->setCheckable(true);
    precompile_shaders->setChecked(ShaderWarmRequested(program_id));
    QAction* delete_shader_warm = nullptr;
    if (has_shader_warm) {
        delete_shader_warm = shaders->addAction(tr("Delete Shader Precache"));
    }
    shaders->addSeparator();
    QAction* remove_gl_shader_cache = shaders->addAction(tr("Remove OpenGL Pipeline Cache"));
    QAction* remove_vk_shader_cache = shaders->addAction(tr("Remove Vulkan Pipeline Cache"));
    QAction* remove_shader_cache = shaders->addAction(tr("Remove All Pipeline Caches"));
    QAction* ryujinx = add(*more, tr("Link to Ryujinx"), u"link");
    QMenu* play_time_menu = group(*more, tr("Play time"), u"clock");
    QAction* set_play_time = play_time_menu->addAction(tr("Set play time"));
    QAction* remove_play_time_data = play_time_menu->addAction(tr("Remove play time"));
    QAction* dump_romfs = dump_verify->addAction(tr("Dump RomFS"));
    QAction* dump_romfs_sdmc = dump_verify->addAction(tr("Dump RomFS to SDMC"));
    QAction* verify_integrity = dump_verify->addAction(tr("Verify integrity"));
    QMenu* artwork = group(*more, tr("Artwork"), u"image");
    artwork->menuAction()->setVisible(program_id != 0);
    QAction* lookup_artwork = artwork->addAction(tr("Look up artwork on SteamGridDB"));
    lookup_artwork->setEnabled(!UISettings::values.steamgriddb_api_key.GetValue().empty());
    if (program_id != 0 && program_id == reportable_program_id) {
        auto* report = add(*more, tr("Submit compatibility report…"), u"bug");
        connect(report, &QAction::triggered, this,
                [this, program_id] { emit ReportCompatibilityRequested(program_id); });
    }

    QAction* clear_favorites = add(*more, tr("Clear favourites"), u"star");
    clear_favorites->setVisible(!UISettings::values.favorited_ids.empty());
    connect(clear_favorites, &QAction::triggered, this, [this] {
        UISettings::values.favorited_ids.clear();
        m_currentView->viewport()->update();
        emit SaveConfig();
    });

    context_menu.addSeparator();
    QAction* remove_all_content = context_menu.addAction(
        Spotlight::Icon(u"trash", Spotlight::Color::AccentLight(), 16),
        tr("Remove from library\tDel"));
    remove_all_content->setProperty("spotlightDestructive", true);
    QMenu* remove_menu =
        group(context_menu, tr("Remove data"), u"x", Spotlight::Color::AccentLight());
    remove_menu->menuAction()->setProperty("spotlightDestructive", true);
    QAction* remove_custom_config = remove_menu->addAction(tr("Remove Custom Configuration"));
    QAction* remove_cache_storage = remove_menu->addAction(tr("Remove Cache Storage"));

    favorite->setVisible(program_id != 0);
    favorite->setCheckable(true);
    favorite->setChecked(UISettings::values.favorited_ids.contains(program_id));
    open_save_location->setVisible(program_id != 0);
    open_mod_location->setVisible(program_id != 0);
    open_transferable_shader_cache->setVisible(program_id != 0);
    prepare_shaders->setVisible(program_id != 0);
    remove_update->setVisible(program_id != 0);
    remove_dlc->setVisible(program_id != 0);
    remove_gl_shader_cache->setVisible(program_id != 0);
    remove_vk_shader_cache->setVisible(program_id != 0);
    remove_shader_cache->setVisible(program_id != 0);
    remove_all_content->setVisible(program_id != 0);

    connect(favorite, &QAction::triggered, this,
            [this, program_id]() { ToggleFavorite(program_id); });
    connect(open_save_location, &QAction::triggered, this, [this, program_id, path]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::SaveData, path);
    });
    connect(start_game, &QAction::triggered, this,
            [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Normal); });
    connect(start_game_global, &QAction::triggered, this,
            [this, path]() { emit BootGame(QString::fromStdString(path), StartGameType::Global); });
    connect(open_mod_location, &QAction::triggered, this, [this, program_id, path]() {
        emit OpenFolderRequested(program_id, GameListOpenTarget::ModData, path);
    });
    connect(open_transferable_shader_cache, &QAction::triggered, this,
            [this, program_id]() { emit OpenTransferableShaderCacheRequested(program_id); });
    connect(prepare_shaders, &QAction::triggered, this,
            [this, program_id, path]() { emit PrepareShaderWarmRequested(program_id, path); });
    connect(precompile_shaders, &QAction::toggled, this, [this, program_id, path](bool checked) {
        emit PrecompileShadersRequested(program_id, path, checked);
    });
    if (delete_shader_warm) {
        connect(delete_shader_warm, &QAction::triggered, this,
                [this, program_id]() { emit DeleteShaderWarmRequested(program_id); });
    }
    connect(remove_all_content, &QAction::triggered, this, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, QtCommon::Game::InstalledEntryType::Game);
    });
    connect(remove_update, &QAction::triggered, this, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id, QtCommon::Game::InstalledEntryType::Update);
    });
    connect(remove_dlc, &QAction::triggered, this, [this, program_id]() {
        emit RemoveInstalledEntryRequested(program_id,
                                           QtCommon::Game::InstalledEntryType::AddOnContent);
    });
    connect(remove_gl_shader_cache, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::GlShaderCache,
                                 path);
    });
    connect(remove_vk_shader_cache, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::VkShaderCache,
                                 path);
    });
    connect(remove_shader_cache, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::AllShaderCache,
                                 path);
    });
    connect(remove_custom_config, &QAction::triggered, this, [this, program_id, path]() {
        emit RemoveFileRequested(program_id,
                                 QtCommon::Game::GameListRemoveTarget::CustomConfiguration, path);
    });
    connect(set_play_time, &QAction::triggered, this,
            [this, program_id]() { emit SetPlayTimeRequested(program_id); });
    connect(remove_play_time_data, &QAction::triggered, this,
            [this, program_id]() { emit RemovePlayTimeRequested(program_id); });
    connect(remove_cache_storage, &QAction::triggered, this, [this, program_id, path] {
        emit RemoveFileRequested(program_id, QtCommon::Game::GameListRemoveTarget::CacheStorage,
                                 path);
    });
    connect(dump_romfs, &QAction::triggered, this, [this, program_id, path]() {
        emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::Normal);
    });
    connect(dump_romfs_sdmc, &QAction::triggered, this, [this, program_id, path]() {
        emit DumpRomFSRequested(program_id, path, DumpRomFSTarget::SDMC);
    });
    connect(verify_integrity, &QAction::triggered, this,
            [this, path]() { emit VerifyIntegrityRequested(path); });
    connect(copy_tid, &QAction::triggered, this,
            [this, program_id]() { emit CopyTIDRequested(program_id); });
    if (convert_nup) {
        connect(convert_nup, &QAction::triggered, this,
                [this, path]() { emit ConvertToNupRequested(path); });
    }
    if (convert_nx) {
        connect(convert_nx, &QAction::triggered, this,
                [this, path]() { emit ConvertToNxRequested(path); });
    }
    if (add_to_nx) {
        connect(add_to_nx, &QAction::triggered, this,
                [this, path]() { emit AddToNxRequested(path); });
        connect(remove_from_nx, &QAction::triggered, this,
                [this, path]() { emit RemoveFromNxRequested(path); });
        connect(edit_nx_metadata, &QAction::triggered, this,
                [this, path]() { emit EditNxMetadataRequested(path); });
    }
    if (extract_nup) {
        connect(extract_nup, &QAction::triggered, this,
                [this, path]() { emit ExtractNupRequested(path); });
    }
    if (edit_nup) {
        connect(edit_nup, &QAction::triggered, this,
                [this, path]() { emit EditNupMetadataRequested(path); });
    }
#if !defined(__APPLE__)
    connect(create_desktop_shortcut, &QAction::triggered, this, [this, program_id, path]() {
        emit CreateShortcut(program_id, path, QtCommon::Game::ShortcutTarget::Desktop);
    });
    connect(create_applications_menu_shortcut, &QAction::triggered, this,
            [this, program_id, path]() {
                emit CreateShortcut(program_id, path, QtCommon::Game::ShortcutTarget::Applications);
            });
#endif
    connect(properties, &QAction::triggered, this,
            [this, program_id, path]() { emit OpenPerGameGeneralRequested(program_id, path); });
    connect(lookup_artwork, &QAction::triggered, this, [this, program_id] {
        QueueArtworkLookup(program_id, GameTitle(program_id), true);
    });

    connect(ryujinx, &QAction::triggered, this,
            [this, program_id]() { emit LinkToRyujinxRequested(program_id); });
};

void GameList::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    } else if (event->type() == QEvent::EnabledChange) {
        const QWidget* focus = window()->focusWidget();
        if (isEnabled() && (!focus || !focus->isVisible())) {
            m_currentView->setFocus();
        }
        emit LibraryEnabledChanged(isEnabled());
    }

    QWidget::changeEvent(event);
}

void GameList::RetranslateUI() {
    item_model->RetranslateUI();
}

void GameList::SetupScrollAnimation() {
    auto setup = [this](QVariantAnimation* anim, Qt::Orientation orientation) {
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setDuration(200);
        connect(anim, &QVariantAnimation::valueChanged, this,
                [this, orientation](const QVariant& value) {
                    auto* bar = orientation == Qt::Vertical ? m_currentView->verticalScrollBar()
                                                            : m_currentView->horizontalScrollBar();
                    bar->setValue(value.toInt());
                });
    };

    vertical_scroll = new QVariantAnimation(this);
    horizontal_scroll = new QVariantAnimation(this);

    setup(vertical_scroll, Qt::Vertical);
    setup(horizontal_scroll, Qt::Horizontal);
}

bool GameList::eventFilter(QObject* obj, QEvent* event) {
    if (!m_currentView) {
        return QWidget::eventFilter(obj, event);
    }
    if ((obj == m_currentView || obj == m_currentView->viewport()) &&
        event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !(key->modifiers() & ~Qt::KeypadModifier)) {
            if (!key->isAutoRepeat() && !m_populating) {
                ValidateEntry(m_currentView->currentIndex());
            }
            return true;
        }
    }
    if (obj == m_currentView->viewport() && event->type() == QEvent::Wheel) {
        QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);

        const bool horizontal = game_list_mode == Settings::GameListMode::CarouselView ||
                                (wheelEvent->modifiers() & Qt::ShiftModifier);

        const QPoint pixels = wheelEvent->pixelDelta();
        const QPoint delta = pixels.isNull() ? wheelEvent->angleDelta() : pixels;
        int deltaX = delta.x();
        int deltaY = delta.y();

        if (horizontal && deltaY != 0 && deltaX == 0) {
            deltaX = deltaY;
            deltaY = 0;
        }
        if (!pixels.isNull()) {
            vertical_scroll->stop();
            horizontal_scroll->stop();
            auto* y = m_currentView->verticalScrollBar();
            auto* x = m_currentView->horizontalScrollBar();
            y->setValue(y->value() - deltaY);
            x->setValue(x->value() - deltaX);
            return true;
        }

        if (deltaY != 0) {
            if (vertical_scroll->state() == QAbstractAnimation::Stopped)
                vertical_scroll_target = m_currentView->verticalScrollBar()->value();

            vertical_scroll_target -= deltaY;
            vertical_scroll_target =
                qBound(0, vertical_scroll_target, m_currentView->verticalScrollBar()->maximum());

            vertical_scroll->stop();
            vertical_scroll->setStartValue(m_currentView->verticalScrollBar()->value());
            vertical_scroll->setEndValue(vertical_scroll_target);
            vertical_scroll->start();
        }

        if (deltaX != 0) {
            if (horizontal_scroll->state() == QAbstractAnimation::Stopped)
                horizontal_scroll_target = m_currentView->horizontalScrollBar()->value();

            horizontal_scroll_target -= deltaX;
            horizontal_scroll_target = qBound(0, horizontal_scroll_target,
                                              m_currentView->horizontalScrollBar()->maximum());

            horizontal_scroll->stop();
            horizontal_scroll->setStartValue(m_currentView->horizontalScrollBar()->value());
            horizontal_scroll->setEndValue(horizontal_scroll_target);
            horizontal_scroll->start();
        }

        return true;
    }

    if (obj == m_currentView->viewport() && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);

        QModelIndex index = m_currentView->indexAt(mouseEvent->pos());
        if (!index.isValid()) {
            m_currentView->selectionModel()->clearSelection();
            m_currentView->setCurrentIndex(QModelIndex());
        }
    }

    if (obj == carousel_view->viewport() && event->type() == QEvent::Resize) {
        carousel_view->UpdateIconSize();
    }

    return QWidget::eventFilter(obj, event);
}

GameListPlaceholder::GameListPlaceholder(MainWindow* parent) : QWidget{parent} {
    setObjectName(QStringLiteral("spotlightGameListPlaceholder"));
    layout = new QVBoxLayout;
    image = new QLabel;
    text = new QLabel;
    layout->setAlignment(Qt::AlignCenter);
    image->setPixmap(Spotlight::IconPixmap(u"folder-open", 40, Spotlight::Color::TextMuted(),
                                           devicePixelRatioF()));

    RetranslateUI();
    text->setFont(Spotlight::Font(13));
    text->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 158);"));
    text->setAlignment(Qt::AlignHCenter);
    image->setAlignment(Qt::AlignHCenter);

    layout->addWidget(image);
    layout->addWidget(text);
    setLayout(layout);
}

GameListPlaceholder::~GameListPlaceholder() = default;

void GameListPlaceholder::mouseDoubleClickEvent(QMouseEvent* event) {
    emit GameListPlaceholder::AddDirectory();
}

void GameListPlaceholder::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void GameListPlaceholder::RetranslateUI() {
    text->setText(tr("No games in your library yet"));
}
