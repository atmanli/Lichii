// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <utility>
#include <QAbstractButton>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QUrl>
#include <QVBoxLayout>
#include <fmt/ranges.h>
#include "common/scm_rev.h"
#include "lichii/about_dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace {

QLabel* AboutLabel(QWidget* parent, const QString& name, const QFont& font, const QColor& color) {
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setFont(font);
    label->setWordWrap(true);
    Spotlight::SetTextColor(label, color);
    return label;
}

// A row of the links card: an external-link glyph and the link's name.
class LinkRow final : public QAbstractButton {
public:
    LinkRow(const QString& text, const QString& url, QWidget* parent) : QAbstractButton(parent) {
        setText(text);
        setToolTip(url);
        setAccessibleName(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setFont(Spotlight::Font(13, 600));
        setMinimumHeight(44);
        connect(this, &QAbstractButton::clicked, this,
                [url] { QDesktopServices::openUrl(QUrl(url)); });
    }

    QSize sizeHint() const override {
        return {14 + 17 + 12 + fontMetrics().horizontalAdvance(text()) + 14, 44};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool focused = Spotlight::HasKeyboardFocus(this);
        const qreal shown = focused ? 1.0 : hover.Value();
        if (shown > 0) {
            QColor fill = Spotlight::Color::Fill();
            fill.setAlphaF(fill.alphaF() * float(shown));
            painter.setPen(focused ? QPen(Spotlight::Color::Text(), 1) : Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(QRectF(rect()).adjusted(4.5, 2.5, -4.5, -2.5),
                                    Spotlight::Radius::Small, Spotlight::Radius::Small);
        }
        painter.drawPixmap(14, (height() - 17) / 2,
                           Spotlight::IconPixmap(u"arrow-square-out", 17, Spotlight::Color::Info(),
                                                 devicePixelRatioF()));
        painter.setPen(Spotlight::Color::Text());
        painter.drawText(rect().adjusted(14 + 17 + 12, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
    }

    void enterEvent(QEnterEvent*) override {
        hover.SetHovered(true);
    }

    void leaveEvent(QEvent*) override {
        hover.SetHovered(false);
    }

private:
    Spotlight::HoverFade hover{this};
};

QWidget* Divider(QWidget* parent) {
    auto* holder = new QWidget(parent);
    auto* layout = new QHBoxLayout(holder);
    layout->setContentsMargins(18, 0, 18, 0);
    auto* line = new QWidget(holder);
    line->setFixedHeight(1);
    line->setAttribute(Qt::WA_StyledBackground);
    line->setStyleSheet(QStringLiteral("background-color: rgba(255, 255, 255, 18);"));
    layout->addWidget(line);
    return holder;
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent) : Spotlight::Dialog(parent) {
    static const std::string build_id = std::string{Common::g_build_id};
    // Published builds show only their tag (build-N), so add the commit a report came from.
    static const std::string version =
        Common::g_is_dev_build
            ? std::string{Common::g_build_version}
            : fmt::format("{} ({:.10})", Common::g_build_version, Common::g_scm_rev);
    static const std::string lichii_build =
        fmt::format("{} | {} | {}", std::string{Common::g_build_name}, version,
                    std::string{Common::g_compiler_id});

    const auto override_build =
        fmt::format(fmt::runtime(std::string(Common::g_title_bar_format_idle)), build_id);
    const auto lichii_build_version = override_build.empty() ? lichii_build : override_build;

    setObjectName(QStringLiteral("AboutDialog"));
    setWindowTitle(tr("About Lichii"));
    setFixedWidth(540);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* identity_row = new QHBoxLayout;
    identity_row->setSpacing(16);
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon::fromTheme(QStringLiteral("io.github.zortos293.lichii"),
                                     QIcon(QStringLiteral(":/img/lichii.ico")))
                        .pixmap(66));
    identity_row->addWidget(logo);
    auto* identity = new QVBoxLayout;
    identity->setSpacing(4);
    auto* name = new QLabel(QStringLiteral("Lichii"), this);
    name->setFont(Spotlight::DisplayFont(34));
    identity->addWidget(name);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(12);
    auto* build = AboutLabel(this, QStringLiteral("labelBuildInfo"), mono,
                             Spotlight::Color::TextSecondary());
    build->setTextFormat(Qt::PlainText);
    build->setTextInteractionFlags(Qt::TextSelectableByMouse);
    build->setText(QStringLiteral("%1 (%2)").arg(QString::fromStdString(lichii_build_version),
                                                 QString::fromUtf8(Common::g_build_date).left(10)));
    identity->addWidget(build);
    identity_row->addLayout(identity, 1);
    layout->addLayout(identity_row);

    auto* about = AboutLabel(this, QStringLiteral("labelAbout"), Spotlight::Font(13, 400),
                             Spotlight::Color::TextSecondary());
    about->setText(tr("Lichii is an experimental open-source emulator for the Nintendo Switch 2, "
                      "based on Eglantine and Eden.\n\nThis software should not be used to play "
                      "games you have not legally obtained."));
    layout->addWidget(about);

    auto* links = new QWidget(this);
    links->setObjectName(QStringLiteral("aboutLinks"));
    links->setAttribute(Qt::WA_StyledBackground);
    links->setStyleSheet(
        QStringLiteral("#aboutLinks { background-color: rgba(255, 255, 255, 13); "
                       "border: 1px solid rgba(255, 255, 255, 20); border-radius: 18px; }"));
    auto* links_layout = new QVBoxLayout(links);
    links_layout->setContentsMargins(0, 0, 0, 0);
    links_layout->setSpacing(0);
    const std::pair<QString, QString> link_targets[] = {
        {tr("Source Code"), QStringLiteral("https://github.com/zortos293/Eglantine")},
        {tr("Report an Issue"), QStringLiteral("https://github.com/zortos293/Eglantine/issues")},
        {tr("Credits and License"),
         QStringLiteral("https://github.com/zortos293/Eglantine#credits-and-license")},
    };
    for (const auto& [text, url] : link_targets) {
        if (links_layout->count() > 0) {
            links_layout->addWidget(Divider(links));
        }
        links_layout->addWidget(new LinkRow(text, url, links));
    }
    layout->addWidget(links);

    auto* liability = AboutLabel(this, QStringLiteral("labelLiability"), Spotlight::Font(13, 400),
                                 Spotlight::Color::TextSecondary());
    liability->setText(tr("\"Nintendo Switch\" is a trademark of Nintendo. Lichii is not "
                          "affiliated with Nintendo in any way."));
    layout->addWidget(liability);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 12, 0, 0);
    footer->addStretch(1);
    auto* done = new Spotlight::PillButton(tr("OK"), {}, Spotlight::PillButton::Kind::Accent, 40,
                                           this);
    done->setFont(Spotlight::Font(14, 600));
    connect(done, &QAbstractButton::clicked, this, &QDialog::accept);
    footer->addWidget(done);
    layout->addLayout(footer);
    done->setFocus();
}
