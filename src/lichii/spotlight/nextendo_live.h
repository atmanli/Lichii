// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include "common/common_types.h"
#include "common/nextendo.h"

class GameList;
class QAbstractButton;

namespace Spotlight {

/// Who plays what on Nextendo. While online play is on, the profile is signed in and a window
/// shows, it polls nextendo.network's public player counts every minute and, with the friends scope
/// and no game running, the lobbies and friends of each game. Requests run on a worker thread; the
/// getters read the last answer.
class NextendoLive final : public QObject {
    Q_OBJECT

public:
    struct Game {
        QString name;
        std::vector<u64> titles;
        u32 players{};
        u32 lobbies{};
        QStringList friends;
    };

    struct Totals {
        u32 players{};
        u32 games{};
        u32 games_with_players{};
    };

    static NextendoLive& Instance();

    /// Online play is on and nextendo.network answered.
    bool Available() const;
    /// The game whose servers a program uses: its own entry, else its application's or its Switch 1
    /// application's (updates, Switch 2 Editions). Null when it has no servers.
    const Game* Find(u64 program_id) const;
    bool HasServers(u64 program_id) const;
    std::optional<u32> Players(u64 program_id) const;
    u32 Lobbies(u64 program_id) const;
    QStringList FriendsIn(u64 program_id) const;
    Totals GetTotals() const;
    /// Every game with servers, most players first.
    const std::vector<Game>& Games() const;
    /// Whether lobbies and friends are known, which takes a friends sign-in.
    bool HasDetails() const;
    /// Seconds since nextendo.network last answered.
    qint64 SecondsSinceUpdate() const;

signals:
    void Changed();

private:
    struct Answer {
        bool signed_in{};
        std::optional<std::vector<Common::Nextendo::OnlineGame>> counts;
        std::optional<std::vector<Common::Nextendo::LiveGame>> live;
    };

    NextendoLive();
    void Poll();
    void Apply(Answer answer);
    void Clear();

    QTimer timer;
    QFutureWatcher<Answer> watcher;
    QElapsedTimer since_request;
    QElapsedTimer since_answer;
    std::vector<Game> games;
    std::unordered_map<u64, std::size_t> by_title;
    bool details = false;
};

/// The tile pill: a green dot and the players of a game with Nextendo servers, a dim 0 when nobody
/// plays. An empty size for a game without servers, or while online play is off.
QSize OnlineBadgeSize(u64 program_id);
void PaintOnlineBadge(QPainter& painter, const QRect& rect, u64 program_id);
QString OnlineBadgeToolTip(u64 program_id);

/// "● 12 playing online · 3 lobbies · 2 friends" for the launcher hero. Hidden without servers.
class NextendoGameLine final : public QWidget {
    Q_OBJECT
public:
    explicit NextendoGameLine(QWidget* parent = nullptr);
    void SetProgramId(u64 program_id);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void Refresh();

    u64 program_id = 0;
    QStringList parts;
    QString friends;
    int friend_count = 0;
    QStringList friend_names;
    bool playing = false;
};

/// "● 12 online in this game" in the in-game bar, for the running program. It follows its parent
/// bar being shown, so it needs no call when a game starts.
class NextendoGameChip final : public QWidget {
    Q_OBJECT
public:
    explicit NextendoGameChip(QWidget* parent);
    QSize sizeHint() const override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent*) override;

private:
    void Refresh();

    QString text;
    bool playing = false;
};

/// The launcher's bottom bar: "Nextendo · 142 online · 48 games with servers". The chip opens the
/// games with servers; one in the library selects it.
class NextendoBar final : public QWidget {
    Q_OBJECT
public:
    NextendoBar(GameList* games, QWidget* parent);

private:
    void Refresh();
    void ShowGames();

    GameList* games;
    QAbstractButton* chip;
    QWidget* scrim;
};

} // namespace Spotlight
