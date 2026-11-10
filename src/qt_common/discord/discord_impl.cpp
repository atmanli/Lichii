// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2018 Citra Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <discord_rpc.h>
#include <fmt/format.h>

#include "common/scm_rev.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "discord_impl.h"

namespace DiscordRPC {
namespace {

constexpr char DEFAULT_DISCORD_TEXT[] = "Lichii is an emulator for the Nintendo Switch";
constexpr char DEFAULT_DISCORD_IMAGE[] =
    "https://raw.githubusercontent.com/zortos293/Lichii/nx2/dist/"
    "lichii.png";

QString GetCoverSlug(const std::string& title) {
    static const QRegularExpression invalid_characters(QStringLiteral("[^a-z0-9-]"));
    static const QRegularExpression repeated_dashes(QStringLiteral("-{2,}"));
    static const QRegularExpression outer_dashes(QStringLiteral("^-|-$"));

    return QString::fromStdString(title)
        .normalized(QString::NormalizationForm_D)
        .toLower()
        .replace(QLatin1Char(' '), QLatin1Char('-'))
        .remove(invalid_characters)
        .replace(repeated_dashes, QStringLiteral("-"))
        .remove(outer_dashes);
}

} // namespace

DiscordImpl::DiscordImpl(Core::System& system_) : system{system_} {
    DiscordEventHandlers handlers{};
    Discord_Initialize("1397286652128264252", &handlers, 1, nullptr);
}

DiscordImpl::~DiscordImpl() {
    Pause();
    Discord_Shutdown();
}

void DiscordImpl::Pause() {
    CancelCoverRequest();
    game_start = 0;
    game_title.clear();
    cover_url.clear();
    Discord_ClearPresence();
}

void DiscordImpl::CancelCoverRequest() {
    if (auto* reply = std::exchange(cover_reply, nullptr)) {
        QObject::disconnect(reply, nullptr, &network, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void DiscordImpl::RequestCover() {
    const auto slug = GetCoverSlug(game_title);
    if (slug.isEmpty()) {
        return;
    }

    const QUrl url(QStringLiteral(
                       "https://raw.githubusercontent.com/eden-emulator/boxart/refs/heads/master/"
                       "img/%1.png")
                       .arg(slug));
    QNetworkRequest request(url);
    request.setTransferTimeout(3000);
    cover_reply = network.head(request);
    auto* const reply = cover_reply;

    QObject::connect(reply, &QNetworkReply::finished, &network, [this, reply, url] {
        if (cover_reply != reply) {
            return;
        }
        cover_reply = nullptr;
        if (reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
            cover_url = url.toString().toStdString();
            UpdateGameStatus();
        }
        reply->deleteLater();
    });
}

void DiscordImpl::UpdateGameStatus() {
    static const auto build = fmt::format("{} {}", Common::g_build_name, Common::g_build_version);
    DiscordRichPresence presence{};

    presence.largeImageKey = cover_url.empty() ? DEFAULT_DISCORD_IMAGE : cover_url.c_str();
    presence.largeImageText = game_title.c_str();
    presence.smallImageKey = DEFAULT_DISCORD_IMAGE;
    presence.smallImageText = DEFAULT_DISCORD_TEXT;
    presence.state = game_title.c_str();
    presence.status_display_type = DiscordStatusDisplayType_State;
    presence.details = build.c_str();
    presence.startTimestamp = game_start;
    Discord_UpdatePresence(&presence);
}

void DiscordImpl::Update(s64 session_start) {
    if (session_start == 0 || !system.IsPoweredOn()) {
        Pause();
        return;
    }

    std::string title;
    if (system.GetAppLoader().ReadTitle(title) != Loader::ResultStatus::Success || title.empty()) {
        title = fmt::format("Nintendo Switch game ({:016X})",
                            system.GetApplicationProcessProgramID());
    }
    if (game_start == session_start && game_title == title) {
        return;
    }

    CancelCoverRequest();
    game_start = session_start;
    game_title = std::move(title);
    cover_url.clear();

    UpdateGameStatus();
    RequestCover();
}

} // namespace DiscordRPC
