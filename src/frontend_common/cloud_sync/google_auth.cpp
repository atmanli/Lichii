// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/httplib.h"
#include "common/logging.h"
#include "common/net/ca_store.h"
#include "frontend_common/cloud_sync/build_client.h"
#include "frontend_common/cloud_sync/google_auth.h"

namespace FrontendCommon::CloudSync {

namespace {

constexpr std::string_view DeviceCodeGrant = "urn:ietf:params:oauth:grant-type:device_code";

std::mutex base_mutex;
std::string oauth_base{OAuthBase};
// Held while the token is read, refreshed and written, so one refresh token isn't spent twice.
// Timed, so that a caller with a stop token can stop waiting behind a slow refresh.
std::timed_mutex token_mutex;

struct Token {
    std::string access_token;
    std::string refresh_token;
    // Unix time.
    s64 expires_at{};
};

// Bumped by each sign-in and sign-out, so a sign-in that a newer one or a sign-out overtook
// doesn't save its token.
u64 session_generation = 0;
// A refreshed token the file couldn't take. Kept so a new refresh token isn't lost.
std::optional<Token> unsaved_token;
// Set when a sign-out couldn't remove token.json: the file is then ignored, and its removal
// retried, until a sign-in replaces it.
bool token_file_stale = false;
// The last sign-in state the token showed, for SignedInSnapshot: -1 before any, else 0 or 1.
std::atomic<int> signed_in_snapshot{-1};

std::filesystem::path CloudSyncDir() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "cloud_sync";
}

std::filesystem::path TokenFile() {
    return CloudSyncDir() / "token.json";
}

std::filesystem::path ClientSecretFile() {
    return CloudSyncDir() / "client_secret.json";
}

s64 Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json ParseObject(std::string_view text) {
    auto json = nlohmann::json::parse(text, nullptr, false);
    return json.is_object() ? json : nlohmann::json::object();
}

std::string StringField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

s64 IntField(const nlohmann::json& object, const char* key, s64 fallback = 0) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<s64>() : fallback;
}

std::string ServerError(const nlohmann::json& reply) {
    const std::string description = StringField(reply, "error_description");
    return description.empty() ? StringField(reply, "error") : description;
}

// The file holds a refresh token or the client secret, so only its owner may read it. It is
// written beside the old one and renamed over it, so a crash mid-write keeps the previous one.
bool WritePrivateFile(const std::filesystem::path& path, std::string_view text) {
    auto temp = path;
    temp += ".tmp";
    void(Common::FS::CreateParentDirs(path));
    void(Common::FS::RemoveFile(temp));
#ifdef _WIN32
    // The file gets the config folder's ACL: the user's profile, or wherever the executable is in
    // portable mode. No owner-only ACL is set.
    Common::FS::IOFile file{temp, Common::FS::FileAccessMode::Write,
                            Common::FS::FileType::BinaryFile};
    const bool written = file.WriteString(text) == text.size();
    const bool closed = file.Close();
#else
    // Created owner-only rather than narrowed afterwards, so it is never readable by others.
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t count = ::write(fd, text.data() + done, text.size() - done);
        if (count <= 0) {
            break;
        }
        done += static_cast<std::size_t>(count);
    }
    const bool written = done == text.size();
    const bool closed = ::close(fd) == 0;
#endif
    std::error_code ec;
    if (written && closed) {
        std::filesystem::rename(temp, path, ec);
    }
    if (!written || !closed || ec) {
        void(Common::FS::RemoveFile(temp));
        return false;
    }
    return true;
}

std::optional<Token> ReadTokenFile() {
    const auto json = ParseObject(
        Common::FS::ReadStringFromFile(TokenFile(), Common::FS::FileType::TextFile));
    Token token{
        .access_token = StringField(json, "access_token"),
        .refresh_token = StringField(json, "refresh_token"),
        .expires_at = IntField(json, "expires_at"),
    };
    if (token.refresh_token.empty()) {
        return std::nullopt;
    }
    return token;
}

std::optional<Token> LoadTokenLocked() {
    std::optional<Token> token;
    if (unsaved_token) {
        token = unsaved_token;
    } else if (token_file_stale) {
        token_file_stale = !Common::FS::RemoveFile(TokenFile());
    } else {
        token = ReadTokenFile();
    }
    signed_in_snapshot = token.has_value();
    return token;
}

bool SaveTokenLocked(const Token& token) {
    const std::string text = nlohmann::json{
        {"access_token", token.access_token},
        {"refresh_token", token.refresh_token},
        {"expires_at", token.expires_at},
    }.dump();
    if (!WritePrivateFile(TokenFile(), text)) {
        LOG_ERROR(Frontend, "Could not save the Google Drive sign-in");
        return false;
    }
    unsaved_token.reset();
    token_file_stale = false;
    signed_in_snapshot = 1;
    return true;
}

// Signed out from now on, even when token.json can't be removed. False in that case.
bool ClearTokenLocked() {
    signed_in_snapshot = 0;
    unsaved_token.reset();
    token_file_stale = !Common::FS::RemoveFile(TokenFile());
    if (token_file_stale) {
        LOG_ERROR(Frontend, "Could not remove the Google Drive sign-in; it is ignored until the "
                            "next sign-in");
    }
    return !token_file_stale;
}

httplib::Result PostForm(std::string_view path, const httplib::Params& form,
                         std::stop_token stop = {}) {
    std::string base;
    {
        std::scoped_lock lock{base_mutex};
        base = oauth_base;
    }
    httplib::Client client{base};
    ConfigureClient(client);
    // Aborts the request in flight. A connection still being made isn't interrupted.
    const std::stop_callback abort{stop, [&client] { client.stop(); }};
    return client.Post(std::string{path}, httplib::Headers{{"User-Agent", "Lichii"}}, form);
}

bool CurrentSession(u64 generation) {
    std::scoped_lock lock{token_mutex};
    return generation == session_generation;
}

// Waits until the next poll. False when stop was requested.
bool WaitForPoll(std::chrono::seconds interval, std::stop_token stop) {
    std::mutex mutex;
    std::condition_variable_any wake;
    std::unique_lock lock{mutex};
    void(wake.wait_for(lock, stop, interval, [] { return false; }));
    return !stop.stop_requested();
}

std::optional<ClientCredentials> LoadImportedClient() {
    return ParseClientSecret(
        Common::FS::ReadStringFromFile(ClientSecretFile(), Common::FS::FileType::TextFile));
}

} // namespace

void SetOAuthBaseForTests(std::string base) {
    std::scoped_lock lock{base_mutex};
    oauth_base = std::move(base);
}

void ConfigureClient(httplib::Client& client) {
    constexpr time_t TimeoutSeconds = 30;
    client.set_connection_timeout(TimeoutSeconds);
    client.set_read_timeout(TimeoutSeconds);
    client.set_write_timeout(TimeoutSeconds);
    client.set_follow_location(true);
    Common::Net::ConfigureCaStore(client);
}

std::optional<ClientCredentials> ParseClientSecret(std::string_view json) {
    const auto root = ParseObject(json);
    const auto installed = root.find("installed");
    if (installed == root.end() || !installed->is_object()) {
        return std::nullopt;
    }
    ClientCredentials client{
        .client_id = StringField(*installed, "client_id"),
        .client_secret = StringField(*installed, "client_secret"),
    };
    if (client.client_id.empty() || client.client_secret.empty()) {
        return std::nullopt;
    }
    return client;
}

bool ImportClientSecret(std::string_view json) {
    if (!ParseClientSecret(json)) {
        return false;
    }
    if (!WritePrivateFile(ClientSecretFile(), json)) {
        LOG_ERROR(Frontend, "Could not save the Google OAuth client");
        return false;
    }
    return true;
}

std::optional<ClientCredentials> LoadClientCredentials() {
    if (auto imported = LoadImportedClient()) {
        return imported;
    }
    if (BuildClientId.empty() || BuildClientSecret.empty()) {
        return std::nullopt;
    }
    return ClientCredentials{std::string{BuildClientId}, std::string{BuildClientSecret}};
}

bool ClientImported() {
    return LoadImportedClient().has_value();
}

bool SignedIn() {
    std::scoped_lock lock{token_mutex};
    return LoadTokenLocked().has_value();
}

bool SignedInSnapshot() {
    if (const int known = signed_in_snapshot.load(); known >= 0) {
        return known == 1;
    }
    // Nothing has looked at the token yet: the file says, read without the lock.
    const bool signed_in = ReadTokenFile().has_value();
    int unknown = -1;
    signed_in_snapshot.compare_exchange_strong(unknown, signed_in);
    return signed_in_snapshot.load() == 1;
}

SignInResult SignIn(const std::function<void(const DeviceCode&)>& show_code, std::stop_token stop) {
    u64 generation = 0;
    {
        std::scoped_lock lock{token_mutex};
        generation = ++session_generation;
    }
    const auto client = LoadClientCredentials();
    if (!client) {
        return {SignInStatus::Failed, "This build has no Google OAuth client. Import a "
                                      "client_secret.json to sign in."};
    }
    const auto code_result =
        PostForm(DeviceCodePath,
                 {{"client_id", client->client_id}, {"scope", std::string{DriveScope}}}, stop);
    if (stop.stop_requested()) {
        return {SignInStatus::Cancelled, {}};
    }
    if (!code_result) {
        const std::string error = httplib::to_string(code_result.error());
        LOG_ERROR(Frontend, "Google device code request failed: {}", error);
        return {SignInStatus::Failed, fmt::format("Could not reach Google ({}).", error)};
    }
    const auto code = ParseObject(code_result->body);
    const std::string device_code = StringField(code, "device_code");
    if (code_result->status != 200 || device_code.empty()) {
        return {SignInStatus::Failed,
                fmt::format("Google refused the sign-in (HTTP {}): {}", code_result->status,
                            ServerError(code))};
    }
    const s64 expires_in = IntField(code, "expires_in");
    show_code({.user_code = StringField(code, "user_code"),
               .verification_url = StringField(code, "verification_url"),
               .expires_in = expires_in});

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{expires_in};
    auto interval = std::chrono::seconds{IntField(code, "interval", 5)};
    const httplib::Params poll_form{{"client_id", client->client_id},
                                    {"client_secret", client->client_secret},
                                    {"device_code", device_code},
                                    {"grant_type", std::string{DeviceCodeGrant}}};
    while (true) {
        if (!WaitForPoll(interval, stop) || !CurrentSession(generation)) {
            return {SignInStatus::Cancelled, {}};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return {SignInStatus::Expired, {}};
        }
        const auto poll = PostForm(TokenPath, poll_form, stop);
        if (stop.stop_requested()) {
            return {SignInStatus::Cancelled, {}};
        }
        if (!poll) {
            // Try again at the next interval: the code is still good until the deadline.
            continue;
        }
        const auto reply = ParseObject(poll->body);
        if (poll->status == 200) {
            Token token{
                .access_token = StringField(reply, "access_token"),
                .refresh_token = StringField(reply, "refresh_token"),
                .expires_at = Now() + IntField(reply, "expires_in"),
            };
            if (token.refresh_token.empty()) {
                return {SignInStatus::Failed, "Google sent no refresh token."};
            }
            std::scoped_lock lock{token_mutex};
            // Cancelled, signed out or signed in again while the request was out.
            if (stop.stop_requested() || generation != session_generation) {
                return {SignInStatus::Cancelled, {}};
            }
            if (!SaveTokenLocked(token)) {
                return {SignInStatus::Failed, "Could not save the sign-in."};
            }
            return {SignInStatus::SignedIn, {}};
        }
        const std::string error = StringField(reply, "error");
        if (error == "authorization_pending") {
            continue;
        }
        if (error == "slow_down") {
            interval += std::chrono::seconds{5};
            continue;
        }
        if (error == "access_denied") {
            return {SignInStatus::Denied, {}};
        }
        if (error == "expired_token") {
            return {SignInStatus::Expired, {}};
        }
        return {SignInStatus::Failed, fmt::format("Google refused the sign-in (HTTP {}): {}",
                                                  poll->status, ServerError(reply))};
    }
}

std::optional<std::string> AccessToken(bool force_refresh, std::stop_token stop) {
    std::unique_lock lock{token_mutex, std::defer_lock};
    while (!lock.try_lock_for(std::chrono::milliseconds{50})) {
        if (stop.stop_requested()) {
            return std::nullopt;
        }
    }
    auto token = LoadTokenLocked();
    if (!token) {
        return std::nullopt;
    }
    if (!force_refresh && !token->access_token.empty() &&
        token->expires_at - RefreshAheadSeconds > Now()) {
        return token->access_token;
    }
    const auto client = LoadClientCredentials();
    if (!client) {
        return std::nullopt;
    }
    const auto result = PostForm(TokenPath,
                                 {{"client_id", client->client_id},
                                  {"client_secret", client->client_secret},
                                  {"refresh_token", token->refresh_token},
                                  {"grant_type", "refresh_token"}},
                                 stop);
    if (!result) {
        LOG_WARNING(Frontend, "Could not refresh the Google access token: {}",
                    httplib::to_string(result.error()));
        return std::nullopt;
    }
    const auto reply = ParseObject(result->body);
    if (result->status != 200) {
        if (StringField(reply, "error") == "invalid_grant") {
            LOG_WARNING(Frontend, "Google refused the refresh token; signing out of Drive sync");
            ClearTokenLocked();
        } else {
            LOG_WARNING(Frontend, "Google refused the access token refresh (HTTP {}): {}",
                        result->status, ServerError(reply));
        }
        return std::nullopt;
    }
    token->access_token = StringField(reply, "access_token");
    if (token->access_token.empty()) {
        LOG_WARNING(Frontend, "Google sent no access token");
        return std::nullopt;
    }
    token->expires_at = Now() + IntField(reply, "expires_in");
    // Google keeps the refresh token unless it sends a new one.
    if (auto refresh_token = StringField(reply, "refresh_token"); !refresh_token.empty()) {
        token->refresh_token = std::move(refresh_token);
    }
    if (!SaveTokenLocked(*token)) {
        unsaved_token = token;
    }
    return token->access_token;
}

bool SignOut() {
    std::optional<Token> token;
    bool removed = false;
    {
        std::scoped_lock lock{token_mutex};
        token = LoadTokenLocked();
        ++session_generation;
        removed = ClearTokenLocked();
    }
    if (token) {
        const auto result = PostForm(RevokePath, {{"token", token->refresh_token}});
        if (!result || result->status != 200) {
            LOG_WARNING(Frontend, "Could not revoke the Google Drive sign-in");
        }
    }
    return removed;
}

} // namespace FrontendCommon::CloudSync
