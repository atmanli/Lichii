// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <charconv>
#include <functional>
#include <utility>
#include <QDesktopServices>
#include <QFile>
#include <QGridLayout>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "lichii/whats_new_dialog.h"

namespace {

constexpr auto app_icon = ":/icons/default/256x256/lichii.png";

QLabel* Label(const QString& text, const QFont& font, const QColor& color) {
    auto* const label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setFont(font);
    Spotlight::SetTextColor(label, color);
    return label;
}

QJsonDocument ReadNotes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll());
}

QString ChangeIcon(const QString& icon) {
    static const QHash<QString, QString> phosphor{
        {QStringLiteral("apps"), QStringLiteral("squares-four")},
        {QStringLiteral("bug"), QStringLiteral("bug")},
        {QStringLiteral("clock"), QStringLiteral("clock")},
        {QStringLiteral("cpu"), QStringLiteral("cpu")},
        {QStringLiteral("download"), QStringLiteral("download-simple")},
        {QStringLiteral("gamepad"), QStringLiteral("game-controller")},
        {QStringLiteral("globe"), QStringLiteral("globe")},
        {QStringLiteral("image"), QStringLiteral("image")},
        {QStringLiteral("layers"), QStringLiteral("stack")},
    };
    return phosphor.value(icon, QStringLiteral("sparkle"));
}

QWidget* Highlight(const QString& icon, const QString& title, const QString& text) {
    auto* const row = new QWidget;
    row->setMinimumHeight(56);
    auto* const layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 8, 14, 8);
    layout->setSpacing(12);

    auto* const glyph = new QLabel;
    glyph->setObjectName(QStringLiteral("WhatsNewBadge"));
    glyph->setStyleSheet(QStringLiteral("background: transparent;"));
    glyph->setPixmap(Spotlight::IconPixmap(ChangeIcon(icon), 22, Spotlight::Color::Accent(),
                                           row->devicePixelRatioF()));
    layout->addWidget(glyph, 0, Qt::AlignVCenter);

    auto* const copy = new QVBoxLayout;
    copy->setSpacing(2);
    auto* const heading = Label(title, Spotlight::Font(14, 600), Spotlight::Color::Text());
    heading->setWordWrap(true);
    copy->addWidget(heading);
    auto* const body = Label(text, Spotlight::Font(12, 400), Spotlight::Color::TextSecondary());
    body->setWordWrap(true);
    copy->addWidget(body);
    layout->addLayout(copy, 1);
    return row;
}

QWidget* Divider() {
    auto* const holder = new QWidget;
    auto* const layout = new QHBoxLayout(holder);
    layout->setContentsMargins(18, 0, 18, 0);
    auto* const line = new QWidget;
    line->setFixedHeight(1);
    line->setAttribute(Qt::WA_StyledBackground);
    line->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 18);"));
    layout->addWidget(line);
    return holder;
}

QToolButton* NavigationButton(const QString& name, const QString& icon, bool filled) {
    auto* const button = new QToolButton;
    button->setAccessibleName(name);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setIcon(Spotlight::Icon(icon, Spotlight::Color::Text(), 15));
    button->setIconSize(QSize(15, 15));
    button->setFixedHeight(34);
    button->setFont(Spotlight::Font(12, 600));
    button->setStyleSheet(
        QStringLiteral("QToolButton { background-color: %1; border: 1px solid transparent; "
                       "border-radius: 17px; padding: 0 13px; color: #ffffff; }"
                       "QToolButton:hover { background-color: rgba(255, 255, 255, 31); }"
                       "QToolButton:disabled { background-color: %2; "
                       "color: rgba(255, 255, 255, 89); }"
                       "QToolButton[keyboardFocus=\"true\"] { border-color: #ffffff; }")
            .arg(filled ? QStringLiteral("rgba(255, 255, 255, 20)") : QStringLiteral("transparent"),
                 filled ? QStringLiteral("rgba(255, 255, 255, 15)")
                        : QStringLiteral("transparent")));
    return button;
}

} // namespace

quint32 WhatsNewDialog::ParseBuildTag(std::string_view tag) {
    if (!tag.starts_with("build-")) {
        return 0;
    }
    tag.remove_prefix(6);
    quint32 build{};
    const auto [end, error] = std::from_chars(tag.data(), tag.data() + tag.size(), build);
    return error == std::errc{} && end == tag.data() + tag.size() ? build : 0;
}

WhatsNewDialog::StartupAction WhatsNewDialog::OnStartup(quint32 build, quint32 last_seen,
                                                        bool first_start, bool launching_content,
                                                        bool include_pending) {
    if (!build || build == last_seen) {
        return StartupAction::None;
    }
    if (first_start || LoadReleases(build, {}, include_pending).first().build != build) {
        return StartupAction::Remember;
    }
    return launching_content ? StartupAction::None : StartupAction::Show;
}

QList<WhatsNewDialog::Release> WhatsNewDialog::LoadReleases(quint32 build, QDate date,
                                                            bool include_pending) {
    const auto read = [](const QJsonObject& object, quint32 number, QDate release_date) {
        Release release{
            number, release_date, object.value(QStringLiteral("summary")).toString(), {}};
        for (const auto& item : object.value(QStringLiteral("changes")).toArray()) {
            const auto change = item.toObject();
            auto title = change.value(QStringLiteral("title")).toString().trimmed();
            auto body = change.value(QStringLiteral("body")).toString().trimmed();
            if (!title.isEmpty() && !body.isEmpty()) {
                release.changes.append({change.value(QStringLiteral("icon")).toString(),
                                        std::move(title), std::move(body)});
            }
        }
        return release;
    };

    QList<Release> result;
    for (const auto& item : ReadNotes(QStringLiteral(":/whats-new/releases.json")).array()) {
        const auto object = item.toObject();
        const auto number = object.value(QStringLiteral("build")).toInt();
        const auto release_date =
            QDate::fromString(object.value(QStringLiteral("date")).toString(), Qt::ISODate);
        if (number <= 0 || !release_date.isValid() || (build && quint32(number) > build)) {
            continue;
        }
        auto release = read(object, quint32(number), release_date);
        if (!release.changes.isEmpty()) {
            result.append(std::move(release));
        }
    }
    std::ranges::sort(result, std::greater{}, &Release::build);
    if (result.isEmpty() || result.first().build != build) {
        const bool preview = !build || !include_pending;
        auto pending = read(ReadNotes(QStringLiteral(":/whats-new/next.json")).object(),
                            preview ? 0 : build, preview ? QDate{} : date);
        pending.preview = preview;
        if (!pending.changes.isEmpty()) {
            result.prepend(std::move(pending));
        }
    }
    if (result.isEmpty()) {
        result.append(Release{});
    }
    return result;
}

WhatsNewDialog::WhatsNewDialog(quint32 build, QDate build_date, bool include_pending,
                               QWidget* parent)
    : Spotlight::Dialog(parent), releases(LoadReleases(build, build_date, include_pending)) {
    setObjectName(QStringLiteral("WhatsNewDialog"));
    setWindowTitle(tr("What's new in Lichii"));
    SetIcon(u"newspaper", Spotlight::Color::Accent());
    const QIcon icon(QString::fromLatin1(app_icon));
    setWindowIcon(icon);

    auto* const root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(20);

    scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* const content = new QWidget;
    auto* const content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(14);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* const release_line = new QHBoxLayout;
    release_line->setSpacing(10);
    auto* const logo = new QLabel;
    logo->setPixmap(icon.pixmap(QSize(38, 38), devicePixelRatioF()));
    release_line->addWidget(logo);
    metadata = Label({}, Spotlight::Font(13, 600), Spotlight::Color::TextSecondary());
    metadata->setObjectName(QStringLiteral("WhatsNewMetadata"));
    metadata->setWordWrap(true);
    release_line->addWidget(metadata, 1);
    content_layout->addLayout(release_line);

    content_layout->addWidget(
        Label(tr("What's new"), Spotlight::DisplayFont(42), Spotlight::Color::Text()));

    summary = Label({}, Spotlight::Font(13, 400), Spotlight::Color::TextSecondary());
    summary->setWordWrap(true);
    content_layout->addWidget(summary);

    highlight_card = new QWidget;
    highlight_card->setObjectName(QStringLiteral("highlightCard"));
    highlight_card->setAttribute(Qt::WA_StyledBackground);
    highlight_card->setStyleSheet(
        QStringLiteral("#highlightCard { background-color: rgba(255, 255, 255, 13); "
                       "border: 1px solid rgba(255, 255, 255, 20); border-radius: 18px; }"));
    highlights = new QVBoxLayout(highlight_card);
    highlights->setContentsMargins(0, 0, 0, 0);
    highlights->setSpacing(0);
    content_layout->addWidget(highlight_card);
    content_layout->addStretch(1);

    auto* const controls = new QVBoxLayout;
    controls->setContentsMargins(0, 6, 0, 0);
    controls->setSpacing(12);
    auto* const got_it = new QPushButton(tr("Got it"));
    got_it->setObjectName(QStringLiteral("WhatsNewDone"));
    got_it->setFixedHeight(40);
    got_it->setStyleSheet(
        QStringLiteral("font-size: 14px; font-weight: 600; min-height: 40px; border-radius: 20px;"));
    got_it->setDefault(true);
    connect(got_it, &QPushButton::clicked, this, &QDialog::accept);
    controls->addWidget(got_it);

    auto* const history = new QGridLayout;
    history->setContentsMargins(0, 0, 0, 0);
    history->setHorizontalSpacing(7);
    for (int i = 0; i < 3; ++i) {
        history->setColumnStretch(i, 1);
    }
    previous = NavigationButton(tr("Previous release"), QStringLiteral("caret-left"), true);
    previous->setObjectName(QStringLiteral("WhatsNewPrevious"));
    connect(previous, &QToolButton::clicked, this, [this] { ShowRelease(current_index + 1); });
    history->addWidget(previous, 0, 0, Qt::AlignLeading);

    auto* const archive = NavigationButton(tr("All releases on GitHub"),
                                           QStringLiteral("arrow-square-out"), false);
    archive->setObjectName(QStringLiteral("WhatsNewArchive"));
    archive->setText(tr("All releases"));
    connect(archive, &QToolButton::clicked, this, [] {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/zortos293/Lichii/releases")));
    });
    history->addWidget(archive, 0, 1, Qt::AlignHCenter);

    next = NavigationButton(tr("Next release"), QStringLiteral("caret-right"), true);
    next->setObjectName(QStringLiteral("WhatsNewNext"));
    next->setText(tr("Next"));
    connect(next, &QToolButton::clicked, this, [this] { ShowRelease(current_index - 1); });
    history->addWidget(next, 0, 2,
                       (isRightToLeft() ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignAbsolute);
    controls->addLayout(history);
    root->addLayout(controls);

    ShowRelease(0);
    got_it->setFocus();
}

void WhatsNewDialog::ShowRelease(qsizetype index) {
    if (index < 0 || index >= releases.size()) {
        return;
    }
    current_index = index;
    const auto& release = releases[index];
    const auto build = release.build ? tr("Lichii build %1").arg(release.build) : tr("Lichii");
    metadata->setText(
        release.preview ? tr("Coming in the next release (preview)")
        : release.date.isValid()
            ? tr("%1 · %2").arg(build,
                                locale().toString(release.date, QStringLiteral("MMM d, yyyy")))
            : build);
    summary->setText(release.changes.isEmpty() ? tr("No release highlights were bundled.")
                                               : release.summary);

    while (auto* const item = highlights->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (qsizetype i = 0; i < release.changes.size(); ++i) {
        const auto& change = release.changes[i];
        if (i > 0) {
            highlights->addWidget(Divider());
        }
        highlights->addWidget(Highlight(change.icon, change.title, change.body));
    }
    highlight_card->setHidden(release.changes.isEmpty());
    scroll->verticalScrollBar()->setValue(0);
    previous->setEnabled(index + 1 < releases.size());
    previous->setText(previous->isEnabled() ? tr("Build %1").arg(releases[index + 1].build)
                                            : tr("Previous"));
    next->setEnabled(index > 0);
}
