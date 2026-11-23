// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <functional>

#include <QAbstractButton>
#include <QApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrentRun>

#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/common_funcs.h"
#include "core/hle/service/acc/nextendo_id_token.h"
#include "lichii/game/artwork_image.h"
#include "lichii/game/game_list.h"
#include "lichii/spotlight/nextendo_live.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/qt_common.h"

namespace Spotlight {
namespace {

constexpr auto PollInterval = std::chrono::seconds{60};
// A window coming back or taking focus asks again only when the last request is this old.
constexpr qint64 StaleMs = 55'000;

// Nextendo surfaces in Paper (03 · Spotlight · All surfaces, "Nextendo ·" artboards).
QColor Live() {
    return QColor(0x7e, 0xe2, 0xa8);
}
QColor Ink() {
    return QColor(0xff, 0xff, 0xff, 0xb8);
}
QColor Separator() {
    return QColor(0xff, 0xff, 0xff, 0x8c);
}

bool AnyWindowShown() {
    return std::ranges::any_of(QGuiApplication::topLevelWindows(), [](const QWindow* window) {
        return window->isVisible() && window->type() != Qt::ToolTip &&
               window->type() != Qt::Popup && !(window->windowStates() & Qt::WindowMinimized);
    });
}

// Counts never show while airplane mode is on.
bool Connected() {
    return Common::Nextendo::OnlinePlayActive() && !Settings::values.airplane_mode.GetValue();
}

QString Count(u32 value) {
    return QLocale().toString(value);
}

QString Lobbies(u32 lobbies) {
    return lobbies == 1 ? NextendoLive::tr("1 lobby")
                        : NextendoLive::tr("%1 lobbies").arg(Count(lobbies));
}

QString Friends(qsizetype friends) {
    return friends == 1 ? NextendoLive::tr("1 friend")
                        : NextendoLive::tr("%1 friends").arg(Count(u32(friends)));
}

void PaintDot(QPainter& painter, QPointF center, qreal diameter, bool playing) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(playing ? Live() : QColor(0xff, 0xff, 0xff, 0x66));
    painter.drawEllipse(center, diameter / 2, diameter / 2);
}

// Friends as the design's 20 px pastel discs, one colour per name.
int PaintFriends(QPainter& painter, QPoint origin, const QStringList& names) {
    static const std::array<QColor, 5> colors{QColor(0xb9, 0xe8, 0xcf), QColor(0xff, 0xe2, 0xa8),
                                              QColor(0xc9, 0xd8, 0xff), QColor(0xff, 0xc9, 0xd9),
                                              QColor(0xe3, 0xcc, 0xff)};
    const int shown = int(std::min<qsizetype>(names.size(), 3));
    for (int i = 0; i < shown; ++i) {
        painter.setPen(QPen(QColor(0x15, 0x16, 0x1e), 2));
        painter.setBrush(colors[qHash(names[i]) % colors.size()]);
        painter.drawEllipse(QRectF(origin.x() + i * 23 + 1, origin.y() + 1, 18, 18));
    }
    return shown == 0 ? 0 : shown * 23 - 3;
}

const QFont& BadgeFont() {
    static const QFont font = Font(11, 700);
    return font;
}

// The launcher's status pill (Nextendo · Status pill).
class NetworkChip final : public QAbstractButton {
public:
    explicit NetworkChip(QWidget* parent) : QAbstractButton(parent) {
        setFixedHeight(34);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setCheckable(true);
    }

    void Set(QStringList parts_) {
        parts = std::move(parts_);
        updateGeometry();
        update();
    }

    QSize sizeHint() const override {
        const QFontMetrics bold(Font(13, 600));
        const QFontMetrics regular(Font(13, 500));
        int width = 12 + 15 + 8 + bold.horizontalAdvance(Lead());
        for (const QString& part : parts) {
            width += 8 + regular.horizontalAdvance(QStringLiteral("·")) + 8 +
                     regular.horizontalAdvance(part);
        }
        return {width + 8 + 14 + 10, 34};
    }

protected:
    void nextCheckState() override {}

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(HasKeyboardFocus(this) ? QPen(Color::Accent())
                                              : QPen(QColor(0xff, 0xff, 0xff, 0x26), 1));
        painter.setBrush(isChecked() || isDown() ? QColor(0xff, 0xff, 0xff, 0x24)
                         : underMouse()          ? Color::ControlHover()
                                                 : QColor(0xff, 0xff, 0xff, 0x14));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 17, 17);
        const qreal ratio = devicePixelRatioF();
        painter.drawPixmap(12, 10, IconPixmap(u"globe", 15, Live(), ratio));
        int x = 35;
        const auto draw = [&](const QString& text, const QFont& font, const QColor& color) {
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(QRect{x, 0, width(), height()}, Qt::AlignVCenter, text);
            x += painter.fontMetrics().horizontalAdvance(text) + 8;
        };
        draw(Lead(), Font(13, 600), Color::Text());
        for (const QString& part : parts) {
            draw(QStringLiteral("·"), Font(13, 400), Separator());
            draw(part, Font(13, 500), Ink());
        }
        painter.drawPixmap(width() - 24, 10,
                           IconPixmap(isChecked() ? u"caret-down" : u"caret-up", 14,
                                      QColor(0xff, 0xff, 0xff, 0x9e), ratio));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }

private:
    static QString Lead() {
        return QStringLiteral("Nextendo");
    }

    QStringList parts;
};

struct PopoverRow {
    QString name;
    u32 players{};
    u32 lobbies{};
    QStringList friends;
    // The library entry that plays it, 0 when the library has none.
    u64 program_id{};
    QPixmap icon;
};

// The rows of the status popover: one 52 px row per game, divided by a hairline.
class PopoverRows final : public QWidget {
public:
    static constexpr int RowHeight = 53;

    PopoverRows(std::vector<PopoverRow> all_, bool details_, std::function<void(u64)> pick_,
                QWidget* parent)
        : QWidget(parent), all(std::move(all_)), details(details_), pick(std::move(pick_)) {
        setMouseTracking(true);
        SetLibraryOnly(false);
    }

    void SetLibraryOnly(bool library_only) {
        rows.clear();
        for (const auto& row : all) {
            if (!library_only || row.program_id != 0) {
                rows.push_back(&row);
            }
        }
        hovered = -1;
        setFixedHeight(std::max(1, int(rows.size())) * RowHeight - 1);
        update();
    }

    int Rows() const {
        return int(rows.size());
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal ratio = devicePixelRatioF();
        if (rows.empty()) {
            painter.setFont(Font(13));
            painter.setPen(QColor(0xff, 0xff, 0xff, 0x9e));
            painter.drawText(rect(), Qt::AlignCenter,
                             NextendoLive::tr("None of your games has Nextendo servers"));
            return;
        }
        const QString tag = NextendoLive::tr("In library");
        const int tag_width = QFontMetrics(Font(11, 600)).horizontalAdvance(tag) + 18;
        for (int i = 0; i < int(rows.size()); ++i) {
            const QRect row{0, i * RowHeight, width(), RowHeight - 1};
            if (!row.adjusted(0, 0, 0, 1).intersects(event->rect())) {
                continue;
            }
            const PopoverRow& entry = *rows[i];
            if (i == hovered && entry.program_id != 0) {
                painter.fillRect(row, Color::RowHover());
            }
            if (i + 1 < int(rows.size())) {
                painter.fillRect(QRect{18, row.bottom() + 1, width() - 36, 1},
                                 QColor(0xff, 0xff, 0xff, 0x12));
            }

            const QRect icon{12, row.center().y() - 17, 36, 36};
            if (!entry.icon.isNull()) {
                ArtworkImage::PaintIcon(painter, icon, entry.icon, 10);
            } else {
                painter.setPen(Qt::NoPen);
                painter.setBrush(Color::Fill());
                painter.drawRoundedRect(icon, 10, 10);
                painter.drawPixmap(
                    icon.center() - QPoint(8, 8),
                    IconPixmap(u"game-controller", 18, QColor(0xff, 0xff, 0xff, 0x9e), ratio));
            }

            int right = row.right() - 12;
            painter.setFont(Font(13, 700));
            painter.setPen(entry.players > 0 ? Color::Text() : QColor(0xff, 0xff, 0xff, 0x9e));
            const QString players = Count(entry.players);
            const int count_width = std::max(26, painter.fontMetrics().horizontalAdvance(players));
            painter.drawText(QRect{right - count_width, row.y(), count_width, row.height()},
                             Qt::AlignVCenter | Qt::AlignRight, players);
            right -= count_width + 6 + 7;
            PaintDot(painter, {right + 3.5, row.center().y() + 0.5}, 7, entry.players > 0);
            right -= 10;
            if (entry.program_id != 0) {
                const QRect pill{right - tag_width, row.center().y() - 11, tag_width, 22};
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0x7e, 0xe2, 0xa8, 0x2e));
                painter.drawRoundedRect(pill, 11, 11);
                painter.setFont(Font(11, 600));
                painter.setPen(Live());
                painter.drawText(pill, Qt::AlignCenter, tag);
                right = pill.left() - 10;
            }
            if (const int shown = std::min<int>(int(entry.friends.size()), 3); shown > 0) {
                right -= shown * 23 - 3;
                PaintFriends(painter, {right, row.center().y() - 10}, entry.friends);
                right -= 10;
            }

            const int left = icon.right() + 13;
            const int text_width = right - left;
            painter.setFont(Font(14, 600));
            painter.setPen(Color::Text());
            painter.drawText(
                QRect{left, row.y() + 7, text_width, 19}, Qt::AlignVCenter | Qt::AlignLeft,
                painter.fontMetrics().elidedText(entry.name, Qt::ElideRight, text_width));
            QStringList sub;
            if (details) {
                sub << (entry.lobbies > 0 ? Lobbies(entry.lobbies)
                                          : NextendoLive::tr("No lobbies open"));
            } else if (entry.program_id != 0) {
                sub << NextendoLive::tr("Lobbies not shown");
            }
            if (entry.program_id == 0) {
                sub << (sub.isEmpty() ? NextendoLive::tr("Not in your library")
                                      : NextendoLive::tr("not in your library"));
            }
            painter.setFont(Font(12, 400));
            painter.setPen(QColor(0xff, 0xff, 0xff, 0x9e));
            painter.drawText(QRect{left, row.y() + 28, text_width, 16},
                             Qt::AlignVCenter | Qt::AlignLeft,
                             painter.fontMetrics().elidedText(sub.join(QStringLiteral(" · ")),
                                                              Qt::ElideRight, text_width));
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        const int row = RowAt(event->position().toPoint());
        if (row != hovered) {
            hovered = row;
            setCursor(row >= 0 && rows[row]->program_id != 0 ? Qt::PointingHandCursor
                                                             : Qt::ArrowCursor);
            const QString names =
                row >= 0 ? rows[row]->friends.join(QStringLiteral(", ")) : QString{};
            setToolTip(names.isEmpty() ? QString{}
                                       : NextendoLive::tr("Friends playing: %1").arg(names));
            update();
        }
    }

    void leaveEvent(QEvent*) override {
        hovered = -1;
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        const int row = RowAt(event->position().toPoint());
        if (event->button() == Qt::LeftButton && row >= 0 && rows[row]->program_id != 0) {
            pick(rows[row]->program_id);
        }
    }

private:
    int RowAt(const QPoint& point) const {
        const int row = point.y() / RowHeight;
        return rect().contains(point) && row < int(rows.size()) ? row : -1;
    }

    std::vector<PopoverRow> all;
    std::vector<const PopoverRow*> rows;
    bool details;
    std::function<void(u64)> pick;
    int hovered = -1;
};

class StatusPageButton final : public QAbstractButton {
public:
    explicit StatusPageButton(QWidget* parent) : QAbstractButton(parent) {
        setText(NextendoLive::tr("Open status page"));
        setFixedHeight(28);
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override {
        return {8 + 12 + 8 + QFontMetrics(Font(12, 600)).horizontalAdvance(text()) + 8, 28};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || HasKeyboardFocus(this)) {
            painter.setPen(HasKeyboardFocus(this) ? QPen(Color::Accent()) : Qt::NoPen);
            painter.setBrush(Color::Fill());
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
        }
        painter.drawPixmap(8, 8,
                           IconPixmap(u"arrow-square-out", 12, Color::Text(), devicePixelRatioF()));
        painter.setFont(Font(12, 600));
        painter.setPen(Color::Text());
        painter.drawText(rect().adjusted(28, 0, 0, 0), Qt::AlignVCenter, text());
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }
};

// Nextendo · Status popover (games with servers).
class GamesPopover final : public QWidget {
public:
    GamesPopover(std::vector<PopoverRow> rows, std::function<void(u64)> pick, QWidget* parent)
        : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        // A click on the status pill only closes the popover, rather than opening it again.
        setAttribute(Qt::WA_NoMouseReplay);
        const auto& live = NextendoLive::Instance();
        const auto totals = live.GetTotals();
        setFixedWidth(440 + 2 * Shadow);
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(Shadow + 10, Shadow + 10, Shadow + 10, Shadow + 10);
        column->setSpacing(8);

        auto* header = new QHBoxLayout;
        header->setContentsMargins(4, 2, 0, 0);
        header->setSpacing(10);
        auto* heading = new QVBoxLayout;
        heading->setSpacing(2);
        auto* title = new QLabel(NextendoLive::tr("Games online now"), this);
        title->setFont(Font(15, 600));
        SetTextColor(title, Color::Text());
        heading->addWidget(title);
        auto* summary =
            new QLabel(NextendoLive::tr("%1 players · %2 games with servers · sorted by players")
                           .arg(Count(totals.players), Count(totals.games)),
                       this);
        summary->setFont(Font(12));
        summary->setWordWrap(true);
        SetTextColor(summary, QColor(0xff, 0xff, 0xff, 0x9e));
        heading->addWidget(summary);
        header->addLayout(heading, 1);
        auto* filter =
            new SegmentedControl({NextendoLive::tr("All"), NextendoLive::tr("My library")},
                                 SegmentedControl::Kind::Light, 38, this);
        filter->setFont(Font(13));
        header->addWidget(filter, 0, Qt::AlignVCenter);
        column->addLayout(header);

        list = new PopoverRows(
            std::move(rows), live.HasDetails(),
            [this, pick = std::move(pick)](u64 program_id) {
                close();
                pick(program_id);
            },
            this);
        scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget "
                                             "{ background: transparent; border: none; }"));
        scroll->setWidget(list);
        column->addWidget(scroll);
        connect(filter, &SegmentedControl::CurrentChanged, this, [this](int index) {
            list->SetLibraryOnly(index == 1);
            FitList();
        });

        auto* footer = new QHBoxLayout;
        footer->setContentsMargins(4, 0, 0, 0);
        const qint64 seconds = live.SecondsSinceUpdate();
        auto* updated =
            new QLabel((seconds < 5 ? NextendoLive::tr("Updated just now")
                                    : NextendoLive::tr("Updated %1 s ago").arg(seconds)) +
                           QStringLiteral(" · nextendo.network"),
                       this);
        updated->setFont(Font(11, 400));
        SetTextColor(updated, Separator());
        footer->addWidget(updated, 1);
        auto* status = new StatusPageButton(this);
        connect(status, &QAbstractButton::clicked, this, [this] {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://nextendo.network/status")));
            close();
        });
        footer->addWidget(status);
        column->addLayout(footer);
        FitList();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF panel = QRectF(rect()).adjusted(Shadow, Shadow, -Shadow, -Shadow);
        for (int i = Shadow; i > 0; i -= 2) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, std::max(1, 0x8c * (Shadow - i) / Shadow / 8)));
            painter.drawRoundedRect(panel.adjusted(-i, -i + 10, i, i + 10), 18 + i, 18 + i);
        }
        painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 0x1f), 1));
        painter.setBrush(QColor(0x18, 0x1a, 0x24, 0xfa));
        painter.drawRoundedRect(panel.adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
        const QRect area = scroll->geometry();
        painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 0x14), 1));
        painter.setBrush(QColor(0xff, 0xff, 0xff, 0x0d));
        painter.drawRoundedRect(QRectF(area).adjusted(-0.5, -0.5, 0.5, 0.5), 14, 14);
    }

private:
    static constexpr int Shadow = 16;
    static constexpr int VisibleRows = 7;

    void FitList() {
        const int bottom = geometry().bottom();
        const int rows = std::clamp(list->Rows(), 1, VisibleRows);
        scroll->setFixedHeight(rows * PopoverRows::RowHeight - 1);
        adjustSize();
        if (isVisible()) {
            move(x(), bottom - height() + 1);
        }
    }

    PopoverRows* list;
    QScrollArea* scroll;
};

class Scrim final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter(this).fillRect(rect(), QColor(0x07, 0x08, 0x0b, 0x59));
    }
};

} // namespace

NextendoLive& NextendoLive::Instance() {
    static auto* instance = new NextendoLive;
    return *instance;
}

NextendoLive::NextendoLive() : QObject(qApp) {
    timer.setInterval(PollInterval);
    connect(&timer, &QTimer::timeout, this, &NextendoLive::Poll);
    connect(&watcher, &QFutureWatcher<Answer>::finished, this, [this] { Apply(watcher.result()); });
    // Back from minimized, or the settings dialog closed: catch up without waiting a minute.
    connect(qApp, &QGuiApplication::focusWindowChanged, this, &NextendoLive::Poll);
    connect(qApp, &QGuiApplication::applicationStateChanged, this, &NextendoLive::Poll);
    timer.start();
    QTimer::singleShot(0, this, &NextendoLive::Poll);
}

void NextendoLive::Poll() {
    if (!Connected()) {
        since_request.invalidate();
        Clear();
        return;
    }
    if (watcher.isRunning() || !AnyWindowShown() ||
        (since_request.isValid() && since_request.elapsed() < StaleMs)) {
        return;
    }
    since_request.start();
    watcher.setFuture(
        QtConcurrent::run([client_id = Settings::values.nextendo_client_id.GetValue(),
                           profile = Service::Account::Nextendo::CurrentProfile(*QtCommon::system),
                           details = !QtCommon::system->IsPoweredOn()] {
            // Counts show while the player is connected to Nextendo, as the design has it.
            if (!Common::Nextendo::LoadAccount(profile)) {
                return Answer{};
            }
            Answer answer{.signed_in = true,
                          .counts = Common::Nextendo::FetchOnlineCounts(client_id)};
            if (answer.counts && details) {
                answer.live = Common::Nextendo::FetchLiveStatus(client_id, profile);
            }
            return answer;
        }));
}

void NextendoLive::Apply(Answer answer) {
    if (!Connected() || !answer.signed_in) {
        Clear();
        return;
    }
    if (!answer.counts) {
        return;
    }
    since_answer.start();
    games.clear();
    by_title.clear();
    games.reserve(answer.counts->size());
    for (auto& game : *answer.counts) {
        games.push_back({.name = QString::fromStdString(game.name),
                         .titles = std::move(game.titles),
                         .players = game.players});
    }
    std::ranges::stable_sort(games, [](const Game& a, const Game& b) {
        return a.players != b.players ? a.players > b.players
                                      : QString::localeAwareCompare(a.name, b.name) < 0;
    });
    for (std::size_t i = 0; i < games.size(); ++i) {
        for (const u64 title : games[i].titles) {
            by_title.emplace(title, i);
        }
    }
    details = answer.live.has_value();
    for (const auto& live : answer.live.value_or(std::vector<Common::Nextendo::LiveGame>{})) {
        const auto it = std::ranges::find_if(
            live.titles, [this](u64 title) { return by_title.contains(title); });
        if (it == live.titles.end()) {
            continue;
        }
        Game& game = games[by_title.at(*it)];
        game.lobbies = live.lobbies;
        game.friends.clear();
        for (const auto& name : live.friends) {
            game.friends << QString::fromStdString(name);
        }
    }
    emit Changed();
}

void NextendoLive::Clear() {
    since_answer.invalidate();
    if (games.empty()) {
        return;
    }
    games.clear();
    by_title.clear();
    details = false;
    emit Changed();
}

bool NextendoLive::Available() const {
    return !games.empty() && Common::Nextendo::OnlinePlayActive();
}

const NextendoLive::Game* NextendoLive::Find(u64 program_id) const {
    if (program_id == 0 || !Available()) {
        return nullptr;
    }
    const u64 base = FileSys::GetBaseTitleID(program_id);
    for (const u64 title : {program_id, base, FileSys::GetSwitch1ApplicationID(base)}) {
        if (const auto it = by_title.find(title); it != by_title.end()) {
            return &games[it->second];
        }
    }
    return nullptr;
}

bool NextendoLive::HasServers(u64 program_id) const {
    return Find(program_id) != nullptr;
}

std::optional<u32> NextendoLive::Players(u64 program_id) const {
    const Game* game = Find(program_id);
    return game ? std::optional{game->players} : std::nullopt;
}

u32 NextendoLive::Lobbies(u64 program_id) const {
    const Game* game = Find(program_id);
    return game ? game->lobbies : 0;
}

QStringList NextendoLive::FriendsIn(u64 program_id) const {
    const Game* game = Find(program_id);
    return game ? game->friends : QStringList{};
}

NextendoLive::Totals NextendoLive::GetTotals() const {
    Totals totals{.games = u32(games.size())};
    for (const Game& game : games) {
        totals.players += game.players;
        totals.games_with_players += game.players > 0 ? 1 : 0;
    }
    return totals;
}

const std::vector<NextendoLive::Game>& NextendoLive::Games() const {
    return games;
}

bool NextendoLive::HasDetails() const {
    return details;
}

qint64 NextendoLive::SecondsSinceUpdate() const {
    return since_answer.isValid() ? since_answer.elapsed() / 1000 : 0;
}

QSize OnlineBadgeSize(u64 program_id) {
    const auto players = NextendoLive::Instance().Players(program_id);
    if (!players) {
        return {};
    }
    return {8 + 7 + 5 + QFontMetrics(BadgeFont()).horizontalAdvance(Count(*players)) + 8, 22};
}

void PaintOnlineBadge(QPainter& painter, const QRect& rect, u64 program_id) {
    const auto players = NextendoLive::Instance().Players(program_id);
    if (!players || rect.isEmpty()) {
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (*players == 0) {
        painter.setOpacity(painter.opacity() * 0.7);
    }
    painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 0x2e), 1));
    painter.setBrush(QColor(0x0b, 0x0c, 0x10, 0xd9));
    painter.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), 11, 11);
    PaintDot(painter, {rect.x() + 11.5, rect.y() + rect.height() / 2.0}, 7, *players > 0);
    painter.setFont(BadgeFont());
    painter.setPen(*players > 0 ? Color::Text() : QColor(0xff, 0xff, 0xff, 0x9e));
    painter.drawText(rect.adjusted(20, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     Count(*players));
    painter.restore();
}

QString OnlineBadgeToolTip(u64 program_id) {
    const auto* game = NextendoLive::Instance().Find(program_id);
    if (!game) {
        return {};
    }
    QStringList lines{
        game->players == 0 ? NextendoLive::tr("Nobody is playing on Nextendo now")
        : game->players == 1
            ? NextendoLive::tr("1 player online on Nextendo")
            : NextendoLive::tr("%1 players online on Nextendo").arg(Count(game->players))};
    if (game->lobbies > 0) {
        lines << Lobbies(game->lobbies);
    }
    if (!game->friends.isEmpty()) {
        lines << NextendoLive::tr("Friends playing: %1")
                     .arg(game->friends.join(QStringLiteral(", ")));
    }
    return lines.join(QLatin1Char('\n'));
}

NextendoGameLine::NextendoGameLine(QWidget* parent) : QWidget(parent) {
    setFixedHeight(20);
    connect(&NextendoLive::Instance(), &NextendoLive::Changed, this, &NextendoGameLine::Refresh);
    Refresh();
}

void NextendoGameLine::SetProgramId(u64 program_id_) {
    program_id = program_id_;
    Refresh();
}

void NextendoGameLine::Refresh() {
    const auto& live = NextendoLive::Instance();
    const auto* game = live.Find(program_id);
    setVisible(game != nullptr);
    if (!game) {
        return;
    }
    parts = {game->players == 0   ? tr("Nobody playing online")
             : game->players == 1 ? tr("1 playing online")
                                  : tr("%1 playing online").arg(Count(game->players))};
    if (live.HasDetails()) {
        parts << (game->lobbies > 0 ? Lobbies(game->lobbies) : tr("No lobbies open"));
    }
    friend_names = game->friends;
    friends = friend_names.isEmpty() ? QString{} : Friends(friend_names.size());
    playing = game->players > 0;
    setToolTip(OnlineBadgeToolTip(program_id));
    updateGeometry();
    update();
}

QSize NextendoGameLine::sizeHint() const {
    const QFontMetrics metrics(Font(14, 500));
    const int separator = 8 + metrics.horizontalAdvance(QStringLiteral("·")) + 8;
    int width = 8 + 8;
    for (const QString& part : parts) {
        width += metrics.horizontalAdvance(part) + separator;
    }
    if (!friends.isEmpty()) {
        width += std::min<int>(int(friend_names.size()), 3) * 23 - 3 + 8 +
                 metrics.horizontalAdvance(friends);
    } else {
        width -= separator;
    }
    return {width, 20};
}

void NextendoGameLine::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    PaintDot(painter, {4.0, height() / 2.0}, 8, playing);
    int x = 16;
    const auto draw = [&](const QString& text, const QFont& font, const QColor& color) {
        painter.setFont(font);
        painter.setPen(color);
        painter.drawText(QRect{x, 0, width(), height()}, Qt::AlignVCenter, text);
        x += painter.fontMetrics().horizontalAdvance(text) + 8;
    };
    for (int i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            draw(QStringLiteral("·"), Font(14, 400), Separator());
        }
        draw(parts[i], Font(14, 500), Ink());
    }
    if (!friends.isEmpty()) {
        draw(QStringLiteral("·"), Font(14, 400), Separator());
        x += PaintFriends(painter, {x, 0}, friend_names) + 8;
        draw(friends, Font(14, 500), Ink());
    }
}

NextendoGameChip::NextendoGameChip(QWidget* parent) : QWidget(parent) {
    setFont(Font(12, 600));
    setFixedHeight(26);
    parent->installEventFilter(this);
    connect(&NextendoLive::Instance(), &NextendoLive::Changed, this, &NextendoGameChip::Refresh);
    Refresh();
}

bool NextendoGameChip::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show) {
        Refresh();
    }
    return QWidget::eventFilter(watched, event);
}

void NextendoGameChip::Refresh() {
    const u64 program_id =
        QtCommon::system->IsPoweredOn() ? QtCommon::system->GetApplicationProcessProgramID() : 0;
    const auto* game = NextendoLive::Instance().Find(program_id);
    setVisible(game != nullptr);
    if (!game) {
        return;
    }
    text = game->players == 0 ? tr("Nobody else online in this game")
                              : tr("%1 online in this game").arg(Count(game->players));
    playing = game->players > 0;
    setToolTip(OnlineBadgeToolTip(program_id));
    updateGeometry();
    update();
}

QSize NextendoGameChip::sizeHint() const {
    return {10 + 7 + 6 + fontMetrics().horizontalAdvance(text) + 10, 26};
}

void NextendoGameChip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Color::Fill());
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 13, 13);
    PaintDot(painter, {13.5, height() / 2.0}, 7, playing);
    painter.setFont(font());
    painter.setPen(playing ? QColor(0xff, 0xff, 0xff, 0xb8) : Color::TextSecondary());
    painter.drawText(rect().adjusted(23, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
}

NextendoBar::NextendoBar(GameList* games_, QWidget* parent)
    : QWidget(parent), games(games_), chip(new NetworkChip(this)), scrim(new Scrim(parent)) {
    setStyleSheet(QStringLiteral("background: transparent;"));
    scrim->setStyleSheet(QStringLiteral("background: transparent;"));
    scrim->hide();
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(40, 10, 40, 20);
    row->addWidget(chip);
    row->addStretch();
    connect(chip, &QAbstractButton::clicked, this, &NextendoBar::ShowGames);
    connect(&NextendoLive::Instance(), &NextendoLive::Changed, this, &NextendoBar::Refresh);
    Refresh();
}

void NextendoBar::Refresh() {
    const auto& live = NextendoLive::Instance();
    setVisible(live.Available());
    const auto totals = live.GetTotals();
    static_cast<NetworkChip*>(chip)->Set(
        {tr("%1 online").arg(Count(totals.players)),
         totals.games == 1 ? tr("1 game with servers")
                           : tr("%1 games with servers").arg(Count(totals.games))});
    chip->setToolTip(tr("Games on Nextendo, %1 of them with players right now")
                         .arg(Count(totals.games_with_players)));
}

void NextendoBar::ShowGames() {
    const auto& live = NextendoLive::Instance();
    std::unordered_map<const NextendoLive::Game*, const QStandardItem*> library;
    const std::function<void(const QStandardItem*)> walk = [&](const QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            const QStandardItem* item = parent->child(row);
            if (item->data(GameListItem::TypeRole).value<GameListItemType>() ==
                GameListItemType::Game) {
                const u64 program_id = item->data(GameListItemPath::ProgramIdRole).toULongLong();
                if (const auto* game = live.Find(program_id)) {
                    library.emplace(game, item);
                }
            }
            walk(item);
        }
    };
    walk(games->GetModel()->invisibleRootItem());

    std::vector<PopoverRow> rows;
    rows.reserve(live.Games().size());
    for (const auto& game : live.Games()) {
        PopoverRow row{.name = game.name,
                       .players = game.players,
                       .lobbies = game.lobbies,
                       .friends = game.friends};
        if (const auto it = library.find(&game); it != library.end()) {
            row.program_id = it->second->data(GameListItemPath::ProgramIdRole).toULongLong();
            row.icon = it->second->data(Qt::DecorationRole).value<QPixmap>();
        }
        rows.push_back(std::move(row));
    }
    auto* popover = new GamesPopover(
        std::move(rows), [games = games](u64 program_id) { games->SelectProgram(program_id); },
        this);
    connect(popover, &QObject::destroyed, this, [this] {
        chip->setChecked(false);
        scrim->hide();
    });
    scrim->setGeometry(scrim->parentWidget()->rect());
    scrim->show();
    scrim->raise();
    raise();
    chip->setChecked(true);
    const QPoint corner = chip->mapToGlobal(QPoint{0, 0});
    popover->move(corner.x() - 16, corner.y() - 8 - popover->height() + 16);
    popover->show();
}

} // namespace Spotlight
