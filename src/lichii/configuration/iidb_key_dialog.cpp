// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/configuration/iidb_key_dialog.h"

#include <utility>
#include <QCheckBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef _WIN32
#include <windows.h>

#include <wincred.h>
#endif

#include "lichii/game/iidb_client.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace IiDB {
namespace {

using namespace Qt::StringLiterals;

enum class Storage { Empty, SessionOnly, Saved, VaultUnavailable, SaveFailed, DeleteFailed };

struct Credentials {
    QByteArray key;
    Storage storage = Storage::Empty;
};

#ifdef _WIN32
wchar_t VaultEntry[] = L"Lichii/iiDB API Key";

bool DeleteVaultKey() {
    return CredDeleteW(VaultEntry, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
}

bool WriteVaultKey(QByteArray& key) {
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = VaultEntry;
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(key.data());
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&credential, 0) != FALSE;
}
#endif

Credentials ReadCredentials() {
    Credentials result;
#ifdef _WIN32
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(VaultEntry, CRED_TYPE_GENERIC, 0, &credential)) {
        if (GetLastError() != ERROR_NOT_FOUND) {
            result.storage = Storage::VaultUnavailable;
        }
        return result;
    }
    if (credential->CredentialBlobSize <= 512 && credential->CredentialBlob) {
        const QByteArray key(reinterpret_cast<const char*>(credential->CredentialBlob),
                             credential->CredentialBlobSize);
        if (IsValidKey(key)) {
            result = {key, Storage::Saved};
        }
    }
    CredFree(credential);
#endif
    return result;
}

Credentials& CurrentCredentials() {
    static auto credentials = ReadCredentials();
    return credentials;
}

void UseKey(QByteArray key, bool remember) {
    auto& credentials = CurrentCredentials();
    credentials = {std::move(key), Storage::SessionOnly};
#ifdef _WIN32
    if (remember) {
        credentials.storage = WriteVaultKey(credentials.key) ? Storage::Saved : Storage::SaveFailed;
    } else if (!DeleteVaultKey()) {
        credentials.storage = Storage::DeleteFailed;
    }
#else
    Q_UNUSED(remember);
#endif
}

void ForgetKey() {
    auto& credentials = CurrentCredentials();
    credentials = {};
#ifdef _WIN32
    if (!DeleteVaultKey()) {
        credentials.storage = Storage::DeleteFailed;
    }
#endif
}

class KeyDialog final : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit KeyDialog(QWidget* parent) : Spotlight::Dialog(parent), client{SessionRateLimits()} {
        setWindowTitle(tr("iiDB API Key"));
        SetIcon(u"key", Spotlight::Color::Accent());
        SetSubtitle(tr("Enter a personal iiDB API key to search for artwork from game Properties."));
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);

        auto* get_key = new QLabel(
            tr("<a href=\"https://iisu.network/wiki/iidb-developers/getting-started\" "
               "style=\"color: #8fb5ff; text-decoration: none\">Get a key</a>"),
            this);
        get_key->setFont(Spotlight::Font(13, 600));
        get_key->setOpenExternalLinks(true);
        get_key->setTextInteractionFlags(Qt::TextBrowserInteraction);
        auto* get_key_icon = new QLabel(this);
        get_key_icon->setPixmap(Spotlight::IconPixmap(u"arrow-square-out", 14,
                                                      Spotlight::Color::Info(),
                                                      devicePixelRatioF()));
        auto* link_row = new QHBoxLayout;
        link_row->setSpacing(6);
        link_row->addWidget(get_key);
        link_row->addWidget(get_key_icon);
        link_row->addStretch(1);
        layout->addLayout(link_row);

        auto* field = new QVBoxLayout;
        field->setSpacing(8);
        auto* label = new QLabel(tr("Personal API key:"), this);
        label->setFont(Spotlight::Font(13, 600));
        Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
        edit = new QLineEdit(this);
        edit->setEchoMode(QLineEdit::Password);
        edit->setMaxLength(513);
        edit->setFixedHeight(40);
        QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        mono.setPixelSize(14);
        edit->setFont(mono);
        label->setBuddy(edit);
        field->addWidget(label);
        field->addWidget(edit);
        layout->addLayout(field);
        remember = new QCheckBox(tr("Remember in Windows Credential Manager"), this);
        remember->setFont(Spotlight::Font(14, 500));
#ifdef _WIN32
        layout->addWidget(remember);
#else
        remember->hide();
#endif

        storage_status = new Spotlight::Notice(this);
        layout->addWidget(storage_status);
        test_status = new Spotlight::Notice(this);
        test_status->hide();
        layout->addWidget(test_status);
        const auto test_hint = tr("Test the entered key, or the current key if the field is blank. "
                                  "Testing does not save it.");
        auto* hint = new QLabel(test_hint + QLatin1Char(' ') +
                                    tr("The key is not stored in regular config."),
                                this);
        hint->setWordWrap(true);
        hint->setFont(Spotlight::Font(13, 400));
        Spotlight::SetTextColor(hint, Spotlight::Color::TextSecondary());
        layout->addWidget(hint);

        auto* buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 12, 0, 0);
        buttons->setSpacing(10);
        auto* forget = new QPushButton(tr("Forget Key"), this);
        auto* test = new QPushButton(tr("Test Connection"), this);
        auto* use = new QPushButton(tr("Use Key"), this);
        auto* close = new QPushButton(tr("Close"), this);
        buttons->addWidget(forget);
        buttons->addWidget(test);
        buttons->addStretch(1);
        buttons->addWidget(use);
        buttons->addWidget(close);
        for (auto* button : {forget, test, use, close}) {
            button->setAutoDefault(false);
            button->setMinimumHeight(34);
            button->setFont(Spotlight::Font(12, 600));
        }
        test->setToolTip(test_hint);
        auto status_size_policy = test_status->sizePolicy();
        status_size_policy.setRetainSizeWhenHidden(true);
        test_status->setSizePolicy(status_size_policy);
        layout->setSizeConstraint(QLayout::SetMinimumSize);
        layout->addLayout(buttons);
        connect(close, &QPushButton::clicked, this, &QDialog::reject);
        connect(edit, &QLineEdit::textChanged, this, [this, use, test] {
            client.Cancel();
            test_status->hide();
            use->setEnabled(IsValidKey(EnteredKey()));
            test->setEnabled(true);
        });
        connect(use, &QPushButton::clicked, this, [this] {
            auto key = EnteredKey();
            if (IsValidKey(key)) {
                UseKey(std::move(key), remember->isChecked());
                edit->setText({});
                UpdateStorageStatus();
            }
        });
        connect(forget, &QPushButton::clicked, this, [this, test] {
            client.Cancel();
            edit->setText({});
            test_status->hide();
            ForgetKey();
            UpdateStorageStatus();
            test->setEnabled(true);
        });
        connect(test, &QPushButton::clicked, this, [this, test] {
            test->setEnabled(false);
            test_status->SetTone(Spotlight::Notice::Tone::Neutral);
            test_status->setText(tr("Testing iiDB access…"));
            test_status->show();
            client.TestAccess(edit->text().trimmed().isEmpty() ? SessionKey() : EnteredKey(),
                              [this, test](Error error) {
                                  test->setEnabled(true);
                                  test_status->SetTone(error == Error::None
                                                           ? Spotlight::Notice::Tone::Success
                                                           : Spotlight::Notice::Tone::Error);
                                  test_status->setText(error == Error::None
                                                           ? tr("iiDB accepted the key.")
                                                           : ErrorText(error));
                              });
        });
        use->setEnabled(false);
        UpdateStorageStatus();
        setFixedWidth(608);
        ensurePolished();
        adjustSize();
    }

private:
    QByteArray EnteredKey() const {
        return edit->text().trimmed().toUtf8();
    }

    void UpdateStorageStatus() {
        edit->setPlaceholderText(SessionKey().isEmpty() ? tr("Paste your key")
                                                        : tr("Enter a replacement key"));
        const auto storage = CurrentCredentials().storage;
        remember->setChecked(storage == Storage::Saved || storage == Storage::SaveFailed);
        storage_status->SetTone(storage == Storage::Saved ? Spotlight::Notice::Tone::Success
                                : storage == Storage::Empty || storage == Storage::SessionOnly
                                    ? Spotlight::Notice::Tone::Neutral
                                    : Spotlight::Notice::Tone::Warning);
        switch (storage) {
        case Storage::Empty:
            storage_status->setText(tr("No key set. You can use a key for this session only."));
            break;
        case Storage::SessionOnly:
            storage_status->setText(tr("The key is available only until Lichii closes."));
            break;
        case Storage::Saved:
            storage_status->setText(tr("The key is saved in Windows Credential Manager."));
            break;
        case Storage::VaultUnavailable:
            storage_status->setText(tr("Windows Credential Manager is unavailable. "
                                       "Keys are available for this session only."));
            break;
        case Storage::SaveFailed:
            storage_status->setText(
                tr("Lichii couldn't save this key in Windows Credential Manager. "
                   "It is available for this session only. Any saved key is unchanged."));
            break;
        case Storage::DeleteFailed:
            storage_status->setText(
                tr("Lichii couldn't remove a saved key. Remove “Lichii/iiDB API Key” "
                   "in Windows Credential Manager. Any key entered here is session-only."));
            break;
        }
    }

    Client client;
    QLineEdit* edit;
    QCheckBox* remember;
    Spotlight::Notice* storage_status;
    Spotlight::Notice* test_status;
};

} // namespace

const QByteArray& SessionKey() {
    return CurrentCredentials().key;
}

RateLimits& SessionRateLimits() {
    static RateLimits rate_limits;
    return rate_limits;
}

void ConfigureKey(QWidget* parent) {
    KeyDialog dialog(parent);
    dialog.exec();
}

} // namespace IiDB

#include "iidb_key_dialog.moc"
