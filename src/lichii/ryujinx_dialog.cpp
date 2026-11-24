// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/qt_common.h"
#include "qt_common/util/fs.h"
#include "ryujinx_dialog.h"

namespace {

// When a save folder last changed and how much it holds, as "Saved <date> · <size>".
QString SaveSummary(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(path, ec)) {
        return RyujinxDialog::tr("No save yet");
    }
    qulonglong size = 0;
    auto newest = fs::last_write_time(path, ec);
    for (auto it = fs::recursive_directory_iterator(path, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            size += it->file_size(ec);
        }
        newest = std::max(newest, it->last_write_time(ec));
    }
    const auto system_time = std::chrono::system_clock::now() +
                             std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                 newest - fs::file_time_type::clock::now());
    const QDateTime saved = QDateTime::fromSecsSinceEpoch(
        std::chrono::duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count());
    const qint64 days = saved.date().daysTo(QDate::currentDate());
    const QString when =
        days == 0  ? RyujinxDialog::tr("today %1").arg(saved.toString(QStringLiteral("HH:mm")))
        : days < 7 ? RyujinxDialog::tr("%n day(s) ago", "", static_cast<int>(days))
                   : QLocale().toString(saved.date(), QLocale::ShortFormat);
    return RyujinxDialog::tr("Saved %1 \u00b7 %2").arg(when, QtCommon::ReadableByteSize(size));
}

// One side to keep: a card with a save icon, its name and its summary; the checked card wears the
// accent outline.
class SaveCard : public QAbstractButton {
public:
    SaveCard(const QString& title, const QString& summary, const QColor& tint) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(title);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(8);
        auto* icon = new QLabel;
        icon->setFixedSize(36, 36);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(Spotlight::IconPixmap(u"floppy-disk", 18, tint, devicePixelRatioF()));
        layout->addWidget(icon);
        auto* name = new QLabel(title);
        name->setFont(Spotlight::Font(15, 600));
        Spotlight::SetTextColor(name, Spotlight::Color::Text());
        layout->addWidget(name);
        auto* detail = new QLabel(summary);
        detail->setFont(Spotlight::Font(12, 400));
        Spotlight::SetTextColor(detail, Spotlight::Color::TextSecondary());
        layout->addWidget(detail);
        for (QLabel* label : {icon, name, detail}) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool on = isChecked();
        QColor fill = on ? Spotlight::Color::Accent() : Spotlight::Color::FillFaint();
        if (on) {
            fill.setAlpha(0x12);
        }
        const qreal stroke = on ? 2.0 : 1.0;
        const QColor ring = on || hasFocus() ? Spotlight::Color::Accent()
                            : underMouse()   ? Spotlight::Color::Stroke()
                                             : Spotlight::Color::StrokeSubtle();
        painter.setPen(QPen(ring, stroke));
        painter.setBrush(isEnabled() ? fill : Spotlight::Color::FillFaint());
        const qreal inset = stroke / 2;
        painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset),
                                Spotlight::Radius::Panel, Spotlight::Radius::Panel);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Spotlight::Color::Fill());
        painter.drawRoundedRect(QRectF(16, 16, 36, 36), Spotlight::Radius::Field,
                                Spotlight::Radius::Field);
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }

    void leaveEvent(QEvent*) override {
        update();
    }
};

} // namespace

RyujinxDialog::RyujinxDialog(std::filesystem::path lichii_path, std::filesystem::path ryujinx_path,
                             const QString& ryujinx_save_id, const QString& title,
                             const QPixmap& icon, QWidget* parent)
    : Spotlight::Dialog(parent), m_lichii(lichii_path.make_preferred()),
      m_ryu(ryujinx_path.make_preferred()) {
    setWindowTitle(tr("Share saves with Ryujinx"));
    SetSubtitle(
        tr("Both apps will use one save. The one you don\u2019t pick is kept as a backup."));
    SetIcon(u"link", Spotlight::Color::Info());
    SetCardWidth(600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* game = new QHBoxLayout;
    game->setSpacing(12);
    if (!icon.isNull()) {
        const qreal ratio = devicePixelRatioF();
        QPixmap rounded(QSize(44, 44) * ratio);
        rounded.setDevicePixelRatio(ratio);
        rounded.fill(Qt::transparent);
        QPainter painter(&rounded);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(0, 0, 44, 44), 11, 11);
        painter.setClipPath(clip);
        painter.drawPixmap(QRect(0, 0, 44, 44), icon);
        painter.end();
        auto* art = new QLabel;
        art->setFixedSize(44, 44);
        art->setPixmap(rounded);
        game->addWidget(art);
    }
    auto* names = new QVBoxLayout;
    names->setSpacing(2);
    auto* name = new QLabel(title);
    name->setFont(Spotlight::Font(15, 600));
    Spotlight::SetTextColor(name, Spotlight::Color::Text());
    name->setHidden(title.isEmpty());
    auto* save = new QLabel(tr("Ryujinx save %1").arg(ryujinx_save_id));
    save->setFont(Spotlight::Font(12, 400));
    Spotlight::SetTextColor(save, Spotlight::Color::TextSecondary());
    names->addWidget(name);
    names->addWidget(save);
    game->addLayout(names, 1);
    layout->addLayout(game);

    std::error_code ec;
    auto* cards = new QHBoxLayout;
    cards->setSpacing(12);
    m_keep_lichii = new SaveCard(tr("Keep Lichii\u2019s save"), SaveSummary(m_lichii),
                                 Spotlight::Color::Accent());
    m_keep_lichii->setEnabled(std::filesystem::is_directory(m_lichii, ec));
    auto* keep_ryujinx =
        new SaveCard(tr("Keep Ryujinx\u2019s save"), SaveSummary(m_ryu), Spotlight::Color::Info());
    auto* group = new QButtonGroup(this);
    group->addButton(m_keep_lichii);
    group->addButton(keep_ryujinx);
    (m_keep_lichii->isEnabled() ? m_keep_lichii : keep_ryujinx)->setChecked(true);
    cards->addWidget(m_keep_lichii, 1);
    cards->addWidget(keep_ryujinx, 1);
    layout->addLayout(cards);

    auto* notice = new Spotlight::Notice;
    notice->setText(tr("Close the game in both apps first."));
    layout->addWidget(notice);

    auto* buttons = new QDialogButtonBox;
    buttons->setContentsMargins(0, 6, 0, 0);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton* link = buttons->addButton(tr("Link saves"), QDialogButtonBox::AcceptRole);
    link->setIcon(Spotlight::Icon(u"link", Spotlight::Color::OnAccent(), 15));
    link->setIconSize({15, 15});
    link->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &RyujinxDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_keep_lichii->isChecked()) {
            fromLichii();
        } else {
            fromRyujinx();
        }
    });
    layout->addWidget(buttons);
}

RyujinxDialog::~RyujinxDialog() = default;

void RyujinxDialog::fromLichii() {
    if (QtCommon::FS::LinkRyujinx(m_lichii, m_ryu, Common::FS::RyujinxSaveOperation::FromLichii)) {
        accept();
    }
}

void RyujinxDialog::fromRyujinx() {
    if (QtCommon::FS::LinkRyujinx(m_lichii, m_ryu, Common::FS::RyujinxSaveOperation::FromRyujinx)) {
        accept();
    }
}
