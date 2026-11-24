// SPDX-License-Identifier: GPL-3.0-or-later

#include "compatibility_report.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

#include <cmath>
#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>

namespace CompatibilityReporting {
namespace {
constexpr auto Site = "https://lichii.zortos.me";
constexpr qsizetype ReplyLimit = 256 * 1024;
const QMap<QString, int> Limits{
    {QStringLiteral("name"), 60},      {QStringLiteral("handle"), 16},
    {QStringLiteral("build"), 100},    {QStringLiteral("gameVersion"), 60},
    {QStringLiteral("cpu"), 160},      {QStringLiteral("gpu"), 160},
    {QStringLiteral("driver"), 160},   {QStringLiteral("os"), 160},
    {QStringLiteral("ram"), 60},       {QStringLiteral("settings"), 3000},
    {QStringLiteral("progress"), 500}, {QStringLiteral("summary"), 120},
    {QStringLiteral("details"), 6000}, {QStringLiteral("reproduction"), 3000},
    {QStringLiteral("updates"), 500},  {QStringLiteral("mods"), 500},
};
const QStringList OptionalFields{QStringLiteral("handle"), QStringLiteral("ram"),
                                 QStringLiteral("reproduction"), QStringLiteral("updates"),
                                 QStringLiteral("mods")};

QString Known(const QString& value) {
    return value.trimmed().isEmpty() ? QStringLiteral("Unknown") : value.trimmed();
}

QString Card(const QString& name) {
    return QStringLiteral("#%1 { background-color: rgba(255, 255, 255, 13); "
                          "border: 1px solid rgba(255, 255, 255, 20); border-radius: 16px; }")
        .arg(name);
}

QLabel* FieldLabel(const QString& text, QWidget* parent, int size = 12) {
    auto* label = new QLabel(text, parent);
    label->setFont(Spotlight::Font(size, 600));
    Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
    return label;
}

QLabel* Secondary(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(Spotlight::Font(13, 400));
    label->setWordWrap(true);
    Spotlight::SetTextColor(label, Spotlight::Color::TextSecondary());
    return label;
}

QWidget* Divider(QWidget* parent) {
    auto* line = new QWidget(parent);
    line->setFixedHeight(1);
    line->setAttribute(Qt::WA_StyledBackground);
    line->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 18);"));
    return line;
}

// The preview lists the fields in the order the form asks for them.
QString PreviewText(const QJsonObject& object) {
    static const QStringList order{
        QStringLiteral("gameId"),       QStringLiteral("name"),
        QStringLiteral("handle"),       QStringLiteral("outcome"),
        QStringLiteral("summary"),      QStringLiteral("progress"),
        QStringLiteral("details"),      QStringLiteral("reproduction"),
        QStringLiteral("build"),        QStringLiteral("gameVersion"),
        QStringLiteral("cpu"),          QStringLiteral("gpu"),
        QStringLiteral("driver"),       QStringLiteral("os"),
        QStringLiteral("ram"),          QStringLiteral("graphicsApi"),
        QStringLiteral("settings"),     QStringLiteral("updates"),
        QStringLiteral("mods"),         QStringLiteral("publicConsent"),
        QStringLiteral("safeFilesConsent")};
    QStringList lines;
    for (const auto& key : order) {
        const auto value =
            QString::fromUtf8(QJsonDocument(QJsonArray{object.value(key)}).toJson(
                                  QJsonDocument::Compact))
                .chopped(1)
                .mid(1);
        lines.append(QStringLiteral("\"%1\": %2").arg(key, value));
    }
    return QStringLiteral("{\n") + lines.join(QStringLiteral(",\n")) + QStringLiteral("\n}");
}

bool HasControls(const QString& value) {
    for (const auto ch : value) {
        if (ch.category() == QChar::Other_Control && ch != QLatin1Char('\t') &&
            ch != QLatin1Char('\n') && ch != QLatin1Char('\r')) {
            return true;
        }
    }
    return false;
}
} // namespace

QString SummarizeEnabledAddons(const QList<Addon>& addons) {
    QStringList labels;
    for (const auto& addon : addons) {
        if (!addon.enabled || addon.name.trimmed().isEmpty()) {
            continue;
        }
        auto label = addon.name.trimmed();
        if (!addon.version.trimmed().isEmpty()) {
            label += QStringLiteral(" (%1)").arg(addon.version.trimmed());
        }
        if (!labels.contains(label)) {
            labels.append(std::move(label));
        }
    }
    if (labels.isEmpty()) {
        return QStringLiteral("None listed as enabled at launch.");
    }
    const QString summary =
        QStringLiteral("Enabled at launch: ") + labels.join(QStringLiteral("; "));
    constexpr int limit = 500;
    if (summary.size() <= limit) {
        return summary;
    }
    const auto suffix = QStringLiteral("… (list truncated; review)");
    auto shortened = summary.left(limit - suffix.size());
    if (!shortened.isEmpty() && shortened.back().isHighSurrogate()) {
        shortened.chop(1);
    }
    return shortened + suffix;
}

Dialog::Dialog(const Session& session, QWidget* parent) : Dialog(session, nullptr, parent) {}

Dialog::Dialog(const Session& session, QNetworkAccessManager* network_, QWidget* parent)
    : Spotlight::Dialog(parent), network(network_ ? network_ : new QNetworkAccessManager(this)) {
    setWindowTitle(tr("Submit compatibility report"));
    SetSubtitle(tr("Share your experience with the latest completed session. Review the report "
                   "before submitting."));
    SetIcon(u"clipboard-text", Spotlight::Color::Accent());
    resize(982, 640);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    layout->addLayout(body, 1);

    auto* catalog = new QWidget(this);
    catalog->setObjectName(QStringLiteral("catalogCard"));
    catalog->setAttribute(Qt::WA_StyledBackground);
    catalog->setStyleSheet(Card(catalog->objectName()));
    catalog->setFixedWidth(320);
    auto* catalog_layout = new QVBoxLayout(catalog);
    catalog_layout->setContentsMargins(14, 14, 14, 14);
    catalog_layout->setSpacing(9);
    auto* catalog_title = new QLabel(tr("Catalog game"), catalog);
    catalog_title->setFont(Spotlight::Font(15, 600));
    auto* query_group = new QVBoxLayout;
    query_group->setSpacing(8);
    query_group->addWidget(catalog_title);
    query_group->addWidget(FieldLabel(tr("Game title to search"), catalog, 13));
    query = new QLineEdit(session.title, catalog);
    query->setObjectName(QStringLiteral("catalogQuery"));
    query->setMaxLength(150);
    query->setPlaceholderText(tr("Game title to search"));
    query->setFixedHeight(36);
    query->setFont(Spotlight::Font(14, 500));
    query->addAction(Spotlight::Icon(QStringLiteral("magnifying-glass"),
                                     Spotlight::Color::TextSecondary(), 16),
                     QLineEdit::LeadingPosition);
    query_group->addWidget(query);
    catalog_layout->addLayout(query_group);
    QIcon search_icon =
        Spotlight::Icon(QStringLiteral("magnifying-glass"), Spotlight::Color::OnAccent(), 16);
    search_icon.addPixmap(Spotlight::IconPixmap(u"magnifying-glass", 16,
                                                Spotlight::Color::TextFaint(), devicePixelRatioF()),
                          QIcon::Disabled);
    search = new QPushButton(search_icon, tr("Search catalog"), catalog);
    search->setObjectName(QStringLiteral("search"));
    search->setProperty("kind", QStringLiteral("primary"));
    auto* search_row = new QHBoxLayout;
    search_row->addWidget(search);
    search_row->addStretch(1);
    catalog_layout->addLayout(search_row);
    catalog_layout->addWidget(Secondary(
        tr("Search sends only this text to lichii.zortos.me, not your local title ID or files."),
        catalog));
    auto* divider_row = new QHBoxLayout;
    divider_row->setContentsMargins(18, 0, 18, 0);
    divider_row->addWidget(Divider(catalog));
    catalog_layout->addLayout(divider_row);
    games = new QComboBox(catalog);
    games->setObjectName(QStringLiteral("catalogResults"));
    games->setPlaceholderText(tr("Catalog results"));
    games->setFont(Spotlight::Font(13, 600));
    catalog_layout->addWidget(games);
    previous = new QPushButton(
        Spotlight::Icon(QStringLiteral("caret-left"), Spotlight::Color::Text(), 14), tr("Previous"),
        catalog);
    next = new QPushButton(Spotlight::Icon(QStringLiteral("caret-right"), Spotlight::Color::Text(), 14),
                           tr("Next"), catalog);
    previous->setObjectName(QStringLiteral("previous"));
    next->setObjectName(QStringLiteral("next"));
    auto* paging = new QHBoxLayout;
    paging->setSpacing(8);
    paging->addWidget(previous);
    paging->addWidget(next);
    paging->addStretch(1);
    catalog_layout->addLayout(paging);
    confirm = new QPushButton(tr("Confirm game"), catalog);
    confirm->setObjectName(QStringLiteral("confirm"));
    page_label = new QLabel(catalog);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(12);
    page_label->setFont(mono);
    Spotlight::SetTextColor(page_label, Spotlight::Color::TextSecondary());
    auto* confirm_row = new QHBoxLayout;
    confirm_row->setSpacing(12);
    confirm_row->addWidget(confirm);
    confirm_row->addWidget(page_label);
    confirm_row->addStretch(1);
    catalog_layout->addLayout(confirm_row);
    page_hint = Secondary(QString{}, catalog);
    page_hint->hide();
    catalog_layout->addWidget(page_hint);
    catalog_status = new Spotlight::Notice(catalog);
    catalog_status->hide();
    catalog_layout->addWidget(catalog_status);
    match = new Spotlight::Notice(catalog);
    match->setText(tr("No catalog game confirmed."));
    catalog_layout->addWidget(match);
    for (auto* button : {search, previous, next, confirm}) {
        button->setMinimumHeight(34);
        button->setFont(Spotlight::Font(12, 600));
    }
    body->addWidget(catalog, 0, Qt::AlignTop);

    auto* report = new QVBoxLayout;
    report->setSpacing(10);
    auto* page_track = new QWidget(this);
    page_track->setObjectName(QStringLiteral("pageTrack"));
    page_track->setAttribute(Qt::WA_StyledBackground);
    page_track->setStyleSheet(QStringLiteral(
        "#pageTrack { background-color: rgba(255, 255, 255, 15); border-radius: 19px; }"));
    auto* track_layout = new QHBoxLayout(page_track);
    track_layout->setContentsMargins(3, 3, 3, 3);
    pages = new QTabBar(page_track);
    pages->setExpanding(false);
    pages->setDrawBase(false);
    pages->setDocumentMode(true);
    pages->setFont(Spotlight::Font(13, 500));
    pages->setStyleSheet(QStringLiteral(
        "QTabBar { background: transparent; }"
        "QTabBar::tab { background: transparent; color: #ffffff; border: 0; border-radius: 16px; "
        "height: 32px; padding: 0 14px; margin: 0 1px 0 1px; }"
        "QTabBar::tab:hover { background-color: rgba(255, 255, 255, 20); }"
        "QTabBar::tab:selected { background-color: #ffffff; color: #14151b; font-weight: 600; }"));
    track_layout->addWidget(pages);
    auto* page_row = new QHBoxLayout;
    page_row->addWidget(page_track);
    page_row->addStretch(1);
    report->addLayout(page_row);
    tabs = new QTabWidget(this);
    tabs->tabBar()->hide();
    tabs->setDocumentMode(true);
    tabs->setStyleSheet(QStringLiteral("QTabWidget::pane { border: 0; top: 0; }"));
    report->addWidget(tabs, 1);
    body->addLayout(report, 1);

    const auto add_page = [this](const QString& title) {
        auto* content = new QWidget;
        auto* grid = new QGridLayout(content);
        grid->setContentsMargins(0, 0, 4, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(10);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(content);
        tabs->addTab(scroll, title);
        pages->addTab(title);
        return grid;
    };
    const auto place = [](QGridLayout* grid, QWidget* field, const QString& label, int row,
                          int column, int span) {
        auto* cell = new QVBoxLayout;
        cell->setSpacing(4);
        auto* caption = FieldLabel(label, field->parentWidget());
        caption->setBuddy(field);
        cell->addWidget(caption);
        cell->addWidget(field);
        grid->addLayout(cell, row, column, 1, span);
    };
    const auto line = [this, place](QGridLayout* grid, const QString& key, const QString& label,
                                    int row, int column, int span,
                                    const QString& value = QString{}) {
        auto* edit = new QLineEdit(value, this);
        edit->setObjectName(key);
        edit->setMaxLength(Limits.value(key));
        edit->setFixedHeight(34);
        edit->setFont(Spotlight::Font(14, 500));
        fields.insert(key, edit);
        place(grid, edit, label, row, column, span);
    };
    const auto paragraph = [this, place](QGridLayout* grid, const QString& key,
                                         const QString& label, int row, int column, int span,
                                         int height, const QString& value = QString{}) {
        auto* edit = new QPlainTextEdit(value, this);
        edit->setObjectName(key);
        edit->setFixedHeight(height);
        edit->setFont(Spotlight::Font(13, 400));
        paragraphs.insert(key, edit);
        place(grid, edit, label, row, column, span);
    };
    const auto choice = [](QComboBox* box) {
        box->setFont(Spotlight::Font(13, 600));
        box->setMinimumHeight(34);
    };
    auto* experience = add_page(tr("Experience"));
    line(experience, QStringLiteral("name"), tr("Public nickname"), 0, 0, 1,
         QStringLiteral("Anonymous"));
    line(experience, QStringLiteral("handle"), tr("X handle (optional)"), 0, 1, 1);
    outcome = new QComboBox(this);
    outcome->setObjectName(QStringLiteral("outcome"));
    outcome->addItem(tr("Choose your assessment"), QString{});
    for (const auto& value : {"Perfect", "Playable", "Runs", "Loads", "Unplayable"}) {
        outcome->addItem(QString::fromLatin1(value), QString::fromLatin1(value));
    }
    choice(outcome);
    place(experience, outcome, tr("Compatibility"), 1, 0, 2);
    line(experience, QStringLiteral("summary"), tr("Summary"), 2, 0, 2);
    paragraph(experience, QStringLiteral("progress"), tr("How far did you play?"), 3, 0, 2, 46);
    paragraph(experience, QStringLiteral("details"), tr("What worked or failed?"), 4, 0, 2, 62);
    paragraph(experience, QStringLiteral("reproduction"), tr("Steps to reproduce (optional)"), 5,
              0, 2, 46);
    experience->setRowStretch(6, 1);
    auto* setup = add_page(tr("System and settings"));
    setup->addWidget(
        Secondary(tr("These are the settings recorded when the game started. They are not "
                     "performance measurements."),
                  this),
        0, 0, 1, 2);
    line(setup, QStringLiteral("build"), tr("Lichii build"), 1, 0, 1, Known(session.build));
    line(setup, QStringLiteral("gameVersion"), tr("Game version"), 1, 1, 1,
         Known(session.game_version));
    line(setup, QStringLiteral("cpu"), tr("CPU"), 2, 0, 1, Known(session.cpu));
    line(setup, QStringLiteral("gpu"), tr("GPU"), 2, 1, 1, Known(session.gpu));
    line(setup, QStringLiteral("driver"), tr("GPU driver / OpenGL version"), 3, 0, 1,
         Known(session.driver));
    line(setup, QStringLiteral("os"), tr("Operating system"), 3, 1, 1, Known(session.os));
    line(setup, QStringLiteral("ram"), tr("RAM (optional)"), 4, 0, 1, session.ram);
    graphics_api = new QComboBox(this);
    graphics_api->setObjectName(QStringLiteral("graphicsApi"));
    graphics_api->addItems(
        {QStringLiteral("Unknown"), QStringLiteral("Vulkan"), QStringLiteral("OpenGL")});
    graphics_api->setCurrentText(Known(session.graphics_api));
    choice(graphics_api);
    place(setup, graphics_api, tr("Graphics API"), 4, 1, 1);
    paragraph(setup, QStringLiteral("settings"), tr("Launch settings"), 5, 0, 2, 42,
              Known(session.settings));
    paragraph(setup, QStringLiteral("updates"), tr("Updates (optional)"), 6, 0, 1, 42,
              session.updates);
    paragraph(setup, QStringLiteral("mods"), tr("Mods (optional)"), 6, 1, 1, 42, session.mods);
    setup->setRowStretch(7, 1);
    preview = new QPlainTextEdit(this);
    preview->setObjectName(QStringLiteral("preview"));
    preview->setReadOnly(true);
    QFont preview_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    preview_font.setPixelSize(11);
    preview->setFont(preview_font);
    preview->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#preview { background-color: rgba(255, 255, 255, 13); "
        "border: 1px solid rgba(255, 255, 255, 20); border-radius: 16px; padding: 8px 6px; "
        "color: rgba(255, 255, 255, 184); }"));
    tabs->addTab(preview, tr("Public preview"));
    pages->addTab(tr("Public preview"));
    connect(pages, &QTabBar::currentChanged, tabs, &QTabWidget::setCurrentIndex);
    connect(tabs, &QTabWidget::currentChanged, pages, &QTabBar::setCurrentIndex);

    auto* consents = new QVBoxLayout;
    consents->setSpacing(8);
    consents->addWidget(
        Secondary(tr("All preview fields will be public. Logs, files, local title IDs and play "
                     "history are not sent. The server uses your IP address for rate limiting."),
                  this));
    public_consent = new QCheckBox(tr("I understand my name, optional X handle, report text, "
                                      "hardware and settings will be public."),
                                   this);
    safe_files_consent = new QCheckBox(tr("My report contains no game files, keys, firmware, "
                                          "saves or private account details."),
                                       this);
    public_consent->setObjectName(QStringLiteral("publicConsent"));
    safe_files_consent->setObjectName(QStringLiteral("safeFilesConsent"));
    for (auto* consent : {public_consent, safe_files_consent}) {
        consent->setFont(Spotlight::Font(14, 500));
        consents->addWidget(consent);
    }
    layout->addLayout(consents);
    status = new Spotlight::Notice(this);
    status->setObjectName(QStringLiteral("status"));
    status->hide();
    layout->addWidget(status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    submit = buttons->addButton(tr("Submit report"), QDialogButtonBox::AcceptRole);
    submit->setProperty("kind", QStringLiteral("primary"));
    clear = new QPushButton(tr("Clear draft"), this);
    clear->setProperty("kind", QStringLiteral("ghost"));
    submit->setObjectName(QStringLiteral("submit"));
    clear->setObjectName(QStringLiteral("clear"));
    auto* button_row = new QHBoxLayout;
    button_row->setContentsMargins(0, 6, 0, 0);
    button_row->addWidget(clear);
    button_row->addWidget(buttons, 1);
    layout->addLayout(button_row);
    for (auto* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    connect(buttons, &QDialogButtonBox::rejected, this, &Dialog::reject);
    connect(search, &QPushButton::clicked, this, [this] {
        page = 1;
        Search();
    });
    connect(previous, &QPushButton::clicked, this, [this] {
        --page;
        Search();
    });
    connect(next, &QPushButton::clicked, this, [this] {
        ++page;
        Search();
    });
    connect(confirm, &QPushButton::clicked, this, &Dialog::ConfirmGame);
    connect(submit, &QPushButton::clicked, this, &Dialog::Submit);
    connect(clear, &QPushButton::clicked, this, &Dialog::Clear);
    connect(query, &QLineEdit::textChanged, this, [this] {
        Abort();
        games->clear();
        confirmed_game = 0;
        page = page_count = 1;
        page_label->clear();
        page_hint->hide();
        catalog_status->hide();
        SetMatch(tr("No catalog game confirmed."), false);
        Edited();
    });
    connect(games, &QComboBox::currentIndexChanged, this, [this] {
        confirmed_game = 0;
        SetMatch(tr("No catalog game confirmed."), false);
        Edited();
    });
    for (auto* field : fields) {
        connect(field, &QLineEdit::textChanged, this, &Dialog::Edited);
    }
    for (auto* field : paragraphs) {
        connect(field, &QPlainTextEdit::textChanged, this, &Dialog::Edited);
    }
    connect(outcome, &QComboBox::currentIndexChanged, this, &Dialog::Edited);
    connect(graphics_api, &QComboBox::currentIndexChanged, this, &Dialog::Edited);
    connect(public_consent, &QCheckBox::toggled, this, &Dialog::Refresh);
    connect(safe_files_consent, &QCheckBox::toggled, this, &Dialog::Refresh);
    auto* cooldown = new QTimer(this);
    connect(cooldown, &QTimer::timeout, this, &Dialog::Refresh);
    cooldown->start(1000);
    Refresh();
}

void Dialog::SetStatus(const QString& text, Spotlight::Notice::Tone tone) {
    validation_shown = false;
    status->SetTone(tone);
    status->setText(text);
    status->show();
}

void Dialog::SetCatalogStatus(const QString& text, Spotlight::Notice::Tone tone) {
    catalog_status->SetTone(tone);
    catalog_status->setText(text);
    catalog_status->show();
}

void Dialog::SetMatch(const QString& text, bool confirmed) {
    match->SetTone(confirmed ? Spotlight::Notice::Tone::Success : Spotlight::Notice::Tone::Neutral);
    match->setText(text);
}

Dialog::~Dialog() {
    state = State::Closed;
    Abort();
}

void Dialog::reject() {
    state = State::Closed;
    Abort();
    QDialog::reject();
}

void Dialog::Abort() {
    if (reply) {
        auto* pending = reply.data();
        reply = nullptr;
        pending->abort();
        pending->deleteLater();
    }
    if (state == State::Searching) {
        state = State::Editing;
    }
}

void Dialog::Edited() {
    public_consent->setChecked(false);
    safe_files_consent->setChecked(false);
    submitted_payload.clear();
    submission_key.clear();
    Refresh();
}

void Dialog::Clear() {
    if (state != State::Editing && state != State::Searching) {
        return;
    }
    Abort();
    query->clear();
    games->clear();
    confirmed_game = 0;
    for (auto* field : fields) {
        field->clear();
    }
    for (auto* field : paragraphs) {
        field->clear();
    }
    outcome->setCurrentIndex(0);
    graphics_api->setCurrentIndex(0);
    Edited();
    SetStatus(tr("Draft cleared. Nothing was sent by clearing it."), Spotlight::Notice::Tone::Neutral);
}

QByteArray Dialog::Payload() const {
    QJsonObject object;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        object.insert(it.key(), it.value()->text().trimmed());
    }
    for (auto it = paragraphs.cbegin(); it != paragraphs.cend(); ++it) {
        object.insert(it.key(), it.value()->toPlainText().trimmed());
    }
    object.insert(QStringLiteral("gameId"), confirmed_game);
    object.insert(QStringLiteral("graphicsApi"), graphics_api->currentText());
    object.insert(QStringLiteral("outcome"), outcome->currentData().toString());
    object.insert(QStringLiteral("publicConsent"), public_consent->isChecked());
    object.insert(QStringLiteral("safeFilesConsent"), safe_files_consent->isChecked());
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString Dialog::ValidationError(Spotlight::Notice::Tone* tone) const {
    const auto hint = [tone](const QString& text) {
        if (tone) {
            *tone = Spotlight::Notice::Tone::Neutral;
        }
        return text;
    };
    if (tone) {
        *tone = Spotlight::Notice::Tone::Warning;
    }
    if (confirmed_game == 0) {
        return hint(tr("Search for and confirm the catalog game first."));
    }
    const auto object = QJsonDocument::fromJson(Payload()).object();
    for (auto it = Limits.cbegin(); it != Limits.cend(); ++it) {
        const auto value = object.value(it.key()).toString();
        if ((!OptionalFields.contains(it.key()) && value.isEmpty()) || value.size() > it.value() ||
            HasControls(value)) {
            return tr("Fill required fields within their length limits and remove control "
                      "characters.");
        }
    }
    static const QRegularExpression handle_pattern(QStringLiteral("^@?[A-Za-z0-9_]{1,15}$"));
    const auto handle = object.value(QStringLiteral("handle")).toString();
    if (!handle.isEmpty() && !handle_pattern.match(handle).hasMatch()) {
        return tr("Use 1–15 letters, digits or underscores, with an optional @.");
    }
    if (outcome->currentData().toString().isEmpty()) {
        return hint(tr("Choose your own compatibility assessment."));
    }
    if (Payload().size() > 128 * 1024) {
        return tr("The report is too large.");
    }
    return {};
}

void Dialog::Refresh() {
    const bool editing = state == State::Editing;
    const bool searching = state == State::Searching;
    const bool cooling_down = QDateTime::currentMSecsSinceEpoch() < retry_at;
    query->setEnabled(editing || searching);
    search->setEnabled(editing && !cooling_down);
    games->setEnabled(editing && games->count() > 0);
    confirm->setEnabled(editing && games->currentIndex() >= 0);
    previous->setEnabled(editing && page > 1 && !cooling_down);
    next->setEnabled(editing && page < page_count && !cooling_down);
    clear->setEnabled(editing || searching);
    tabs->setEnabled(editing || searching || state == State::Finished);
    pages->setEnabled(tabs->isEnabled());
    public_consent->setEnabled(editing);
    safe_files_consent->setEnabled(editing);
    auto tone = Spotlight::Notice::Tone::Warning;
    const auto validation = ValidationError(&tone);
    const bool consented = public_consent->isChecked() && safe_files_consent->isChecked();
    submit->setEnabled(editing && !cooling_down && validation.isEmpty() && consented);
    submit->setToolTip(validation);
    if (editing && consented && !validation.isEmpty()) {
        SetStatus(validation, tone);
        validation_shown = true;
    } else if (validation_shown) {
        status->hide();
        validation_shown = false;
    }
    const auto content = PreviewText(QJsonDocument::fromJson(Payload()).object());
    if (preview->toPlainText() != content) {
        preview->setPlainText(content);
    }
}

void Dialog::Search() {
    if (state != State::Editing || QDateTime::currentMSecsSinceEpoch() < retry_at) {
        return;
    }
    if (HasControls(query->text()) || query->text().trimmed().size() > 150) {
        SetCatalogStatus(tr("Check the search text."), Spotlight::Notice::Tone::Warning);
        return;
    }
    confirmed_game = 0;
    games->clear();
    page_label->clear();
    page_hint->hide();
    Edited();
    state = State::Searching;
    SetCatalogStatus(tr("Searching catalog… Cancel closes the dialog and stops the request."),
                     Spotlight::Notice::Tone::Neutral);
    Refresh();
    Request(false);
}

void Dialog::ConfirmGame() {
    if (state != State::Editing || games->currentIndex() < 0) {
        return;
    }
    if (confirmed_game == games->currentData().toLongLong()) {
        return;
    }
    confirmed_game = games->currentData().toLongLong();
    confirmed_title = games->currentText();
    page_label->clear();
    page_hint->hide();
    SetMatch(tr("Confirmed: %1 (catalog ID %2)").arg(confirmed_title).arg(confirmed_game), true);
    Edited();
}

void Dialog::Submit() {
    if (state != State::Editing || QDateTime::currentMSecsSinceEpoch() < retry_at ||
        !public_consent->isChecked() || !safe_files_consent->isChecked()) {
        return;
    }
    auto tone = Spotlight::Notice::Tone::Warning;
    const auto error = ValidationError(&tone);
    if (!error.isEmpty()) {
        SetStatus(error, tone);
        return;
    }
    const auto payload = Payload();
    if (submitted_payload != payload || submission_key.isEmpty()) {
        submitted_payload = payload;
        submission_key = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    }
    state = State::Submitting;
    SetStatus(tr("Submitting… Closing stops waiting, but cannot retract a report the server has "
                 "received."),
              Spotlight::Notice::Tone::Neutral);
    Refresh();
    Request(true, submitted_payload);
}

void Dialog::Request(bool submission, const QByteArray& payload) {
    QUrl url(QString::fromLatin1(Site) + (submission ? QStringLiteral("/api/v1/desktop/reports")
                                                     : QStringLiteral("/api/games")));
    if (!submission) {
        QUrlQuery params;
        params.addQueryItem(QStringLiteral("q"),
                            QString::fromLatin1(QUrl::toPercentEncoding(query->text().trimmed())));
        params.addQueryItem(QStringLiteral("page"), QString::number(page));
        params.addQueryItem(QStringLiteral("sort"), QStringLiteral("title"));
        url.setQuery(params);
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(20000);
    request.setRawHeader("Accept", "application/json");
    if (submission) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("X-Lichii-Client", "desktop/1");
        request.setRawHeader("Idempotency-Key", submission_key);
    }
    auto* pending = submission ? network->post(request, payload) : network->get(request);
    reply = pending;
    QTimer::singleShot(20000, this, [this, pending = QPointer<QNetworkReply>(pending)] {
        if (pending && reply == pending) {
            pending->abort();
        }
    });
    pending->setReadBufferSize(ReplyLimit + 1);
    auto body = std::make_shared<QByteArray>();
    connect(pending, &QNetworkReply::readyRead, this, [this, pending, body] {
        if (reply != pending) {
            return;
        }
        body->append(pending->read(ReplyLimit + 1 - body->size()));
        if (body->size() > ReplyLimit) {
            pending->abort();
        }
    });
    connect(pending, &QNetworkReply::finished, this, [this, pending, submission, body] {
        pending->deleteLater();
        if (reply != pending || state == State::Closed) {
            return;
        }
        reply = nullptr;
        body->append(pending->read(ReplyLimit + 1 - body->size()));
        Finished(submission, pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                 *body, pending->rawHeader("Retry-After"),
                 pending->error() != QNetworkReply::NoError || body->size() > ReplyLimit);
    });
}

void Dialog::Finished(bool submission, int http_status, const QByteArray& body,
                      const QByteArray& retry_after, bool network_error) {
    state = State::Editing;
    if (!submission) {
        catalog_status->hide();
    }
    const auto report = [this, submission](const QString& text, Spotlight::Notice::Tone tone) {
        submission ? SetStatus(text, tone) : SetCatalogStatus(text, tone);
    };
    if (http_status == 429) {
        bool valid = false;
        const auto seconds = retry_after.toLongLong(&valid);
        const auto delay = valid && seconds >= 0 && seconds <= 31536000 ? seconds : 3600;
        retry_at = QDateTime::currentMSecsSinceEpoch() + delay * 1000;
        report(tr("Wait %1 seconds, then retry explicitly. Nothing will be retried automatically.")
                   .arg(delay),
               Spotlight::Notice::Tone::Warning);
    } else if (http_status == 400) {
        const auto errors =
            QJsonDocument::fromJson(body).object().value(QStringLiteral("fields")).toObject();
        QStringList invalid_fields;
        for (auto it = errors.constBegin(); it != errors.constEnd(); ++it) {
            if (Limits.contains(it.key()) || it.key() == QStringLiteral("gameId") ||
                it.key() == QStringLiteral("outcome") ||
                it.key() == QStringLiteral("graphicsApi") ||
                it.key() == QStringLiteral("publicConsent") ||
                it.key() == QStringLiteral("safeFilesConsent")) {
                invalid_fields.append(it.key());
            }
        }
        report(invalid_fields.isEmpty()
                   ? tr("The server rejected the report. Review the fields and length limits "
                        "before submitting again.")
                   : tr("The server rejected these fields: %1. Review them before submitting "
                        "again.")
                         .arg(invalid_fields.join(QStringLiteral(", "))),
               Spotlight::Notice::Tone::Warning);
    } else if (http_status == 409) {
        report(tr("Edit the draft to start a new submission. Check whether the original report "
                  "was already accepted first."),
               Spotlight::Notice::Tone::Warning);
    } else if (http_status == 403 || http_status == 413 || http_status == 415) {
        report(tr("The server rejected the request (HTTP %1). No automatic retry was made.")
                   .arg(http_status),
               Spotlight::Notice::Tone::Warning);
    } else if (network_error || (http_status != 200 && !(submission && http_status == 201))) {
        report(submission ? tr("Delivery may have succeeded. Explicitly retry this unchanged draft "
                               "with the same identity. Closing loses that identity; do not "
                               "create a duplicate.")
                          : tr("Check your connection and retry explicitly."),
               Spotlight::Notice::Tone::Warning);
    } else {
        QJsonParseError parse_error;
        const auto document = QJsonDocument::fromJson(body, &parse_error);
        const auto object = document.object();
        bool valid = parse_error.error == QJsonParseError::NoError && document.isObject();
        if (submission) {
            const auto id = object.value(QStringLiteral("id")).toString();
            const QUrl url(object.value(QStringLiteral("url")).toString());
            const auto uuid = QUuid(id);
            valid = valid && !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id &&
                    url.isValid() && url.scheme() == QStringLiteral("https") &&
                    url.host() == QStringLiteral("lichii.zortos.me") && url.port(-1) == -1 &&
                    url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() &&
                    url.path() == QStringLiteral("/reports/") + id;
            if (valid) {
                state = State::Finished;
                receipt_url = url;
                status->hide();
                ShowReceipt();
            } else {
                SetStatus(tr("The report may have been accepted. Only retry this unchanged draft "
                             "explicitly."),
                          Spotlight::Notice::Tone::Warning);
            }
        } else {
            const auto entries = object.value(QStringLiteral("games")).toArray();
            const auto returned_page = object.value(QStringLiteral("page")).toInt(-1);
            const auto total_pages = object.value(QStringLiteral("pageCount")).toInt(-1);
            valid = valid && object.value(QStringLiteral("games")).isArray() &&
                    entries.size() <= 15 && returned_page == page && total_pages >= 1 &&
                    total_pages <= 10000;
            for (const auto& entry : entries) {
                const auto game = entry.toObject();
                const double id = game.value(QStringLiteral("sgdbId")).toDouble(-1);
                const auto title = game.value(QStringLiteral("title")).toString();
                valid = valid && std::isfinite(id) && id > 0 && id <= 9007199254740991.0 &&
                        std::floor(id) == id && !title.trimmed().isEmpty() &&
                        title.size() <= 1000 && !HasControls(title) &&
                        game.value(QStringLiteral("slug")).isString();
            }
            if (valid) {
                for (const auto& entry : entries) {
                    const auto game = entry.toObject();
                    games->addItem(game.value(QStringLiteral("title")).toString(),
                                   game.value(QStringLiteral("sgdbId")).toInteger());
                }
                games->setCurrentIndex(entries.isEmpty() ? -1 : 0);
                page_count = total_pages;
                page_label->setText(tr("Page %1 of %2").arg(page).arg(page_count));
                page_hint->setText(tr("Page %1 of %2. Select the exact game and press Confirm game.")
                                       .arg(page)
                                       .arg(page_count));
                page_hint->show();
            } else {
                SetCatalogStatus(
                    tr("The catalog returned an invalid response. No game was confirmed."),
                    Spotlight::Notice::Tone::Warning);
            }
        }
    }
    if (submission) {
        public_consent->setChecked(false);
        safe_files_consent->setChecked(false);
    }
    Refresh();
}

void Dialog::ShowReceipt() {
    auto* box = new QMessageBox(QMessageBox::Information, windowTitle(),
                                tr("Report submitted. It is public. Thank you."),
                                QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    Spotlight::MarkSuccess(box);
    open_receipt = box->addButton(tr("Open public report"), QMessageBox::ActionRole);
    open_receipt->setIcon(
        Spotlight::Icon(QStringLiteral("arrow-square-out"), Spotlight::Color::Text(), 16));
    box->setDefaultButton(box->addButton(QMessageBox::Close));
    connect(open_receipt, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(receipt_url); });
    connect(box, &QDialog::finished, this, &QDialog::accept);
    box->open();
}

} // namespace CompatibilityReporting
