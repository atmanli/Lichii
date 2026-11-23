// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "data_dialog.h"
#include "frontend_common/data_manager.h"
#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/qt_string_lookup.h"
#include "qt_common/util/content.h"
#include "util/util.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace {

constexpr int BarHeight = 4;

// The rounded panel the rows sit in (Paper: inset list, radius 18).
class DataPanel : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Spotlight::Color::StrokeSubtle(), 1));
        painter.setBrush(Spotlight::Color::FillFaint());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                Spotlight::Radius::Inset, Spotlight::Radius::Inset);
    }
};

// A 40 px tile with the kind's icon in its tint.
QLabel* IconTile(QStringView name, const QColor& tint, qreal ratio) {
    constexpr int Size = 40;
    QPixmap tile(QSize(Size, Size) * ratio);
    tile.setDevicePixelRatio(ratio);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Spotlight::Color::Fill());
    painter.drawRoundedRect(QRectF(0, 0, Size, Size), Spotlight::Radius::Row,
                            Spotlight::Radius::Row);
    painter.drawPixmap(10, 10, Spotlight::IconPixmap(name, 20, tint, ratio));
    painter.end();
    auto* label = new QLabel;
    label->setFixedSize(Size, Size);
    label->setPixmap(tile);
    return label;
}

} // namespace

DataDialog::DataDialog(QWidget* parent) : Spotlight::Dialog(parent) {
    using FrontendCommon::DataManager::DataDir;
    namespace Lookup = QtCommon::StringLookup;
    setWindowTitle(tr("Storage"));
    SetIcon(u"hard-drives", Spotlight::Color::Info());
    SetCardWidth(760);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* panel = new DataPanel;
    auto* list = new QVBoxLayout(panel);
    list->setContentsMargins(1, 1, 1, 1);
    list->setSpacing(0);
    const auto add = [&](std::size_t index, DataDir dir, Lookup::StringKey tooltip,
                         const char* export_name, const QString& title, const QString& description,
                         QStringView icon, const QColor& tint) {
        auto* row = new DataWidget(dir, tooltip, QString::fromLatin1(export_name), title,
                                   description, icon, tint, index != 0);
        connect(row, &DataWidget::SizeChanged, this, &DataDialog::UpdateTotal);
        list->addWidget(row);
        rows[index] = row;
    };
    add(0, DataDir::Shaders, Lookup::DataManagerShadersTooltip, "Shaders", tr("Shaders"),
        tr("Pipeline caches. Safe to clear; games rebuild them."), u"stack",
        Spotlight::Color::Info());
    add(1, DataDir::UserNand, Lookup::DataManagerUserNandTooltip, "UserNand",
        tr("Installed games, updates and DLC"), tr("User NAND"), u"game-controller",
        Spotlight::Color::Purple());
    add(2, DataDir::SysNand, Lookup::DataManagerSysNandTooltip, "SysNand",
        tr("Firmware and system"), tr("System NAND. Clearing removes firmware and applets."),
        u"cpu", Spotlight::Color::Accent());
    add(3, DataDir::Mods, Lookup::DataManagerModsTooltip, "Mods", tr("Mods"),
        tr("Mods, patches and cheats"), u"puzzle-piece", Spotlight::Color::Warning());
    add(4, DataDir::Saves, Lookup::DataManagerSavesTooltip, "Saves", tr("Saves"),
        tr("Every profile\u2019s saves. Export before clearing."), u"floppy-disk",
        Spotlight::Color::Success());
    layout->addWidget(panel);

    auto* notice = new Spotlight::Notice;
    notice->SetTone(Spotlight::Notice::Tone::Warning);
    notice->setText(tr("Clearing cannot be undone. Export first if you might want it back."));
    layout->addWidget(notice);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 6, 0, 0);
    auto* legend = new QLabel(tr("Open \u00b7 Export \u00b7 Import \u00b7 Clear"));
    legend->setFont(Spotlight::Font(12, 400));
    Spotlight::SetTextColor(legend, Spotlight::Color::TextMuted());
    footer->addWidget(legend, 1);
    auto* buttons = new QDialogButtonBox;
    QPushButton* done = buttons->addButton(tr("Done"), QDialogButtonBox::AcceptRole);
    done->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    footer->addWidget(buttons);
    layout->addLayout(footer);

    UpdateTotal();
}

DataDialog::~DataDialog() = default;

void DataDialog::UpdateTotal() {
    u64 total = 0;
    bool measured = true;
    for (const DataWidget* row : rows) {
        const auto size = row->Size();
        measured = measured && size.has_value();
        total += size.value_or(0);
    }
    for (DataWidget* row : rows) {
        row->SetTotal(total);
    }
    SetSubtitle(measured ? tr("%1 used by Lichii")
                               .arg(QString::fromStdString(
                                   FrontendCommon::DataManager::ReadableBytesSize(total)))
                         : tr("Calculating\u2026"));
}

DataWidget::DataWidget(FrontendCommon::DataManager::DataDir data_dir,
                       QtCommon::StringLookup::StringKey tooltip, const QString& exportName,
                       const QString& title, const QString& description, QStringView icon,
                       const QColor& tint, bool divider, QWidget* parent)
    : QWidget(parent), m_dir(data_dir), m_exportName(exportName), m_tint(tint), m_divider(divider) {
    setToolTip(QtCommon::StringLookup::Lookup(tooltip));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(14);
    layout->addWidget(IconTile(icon, tint, devicePixelRatioF()));

    auto* text = new QVBoxLayout;
    text->setSpacing(5);
    auto* heading = new QHBoxLayout;
    heading->setSpacing(8);
    auto* name = new QLabel(title);
    name->setFont(Spotlight::Font(15, 600));
    Spotlight::SetTextColor(name, Spotlight::Color::Text());
    m_size = new QLabel;
    m_size->setFont(Spotlight::Font(14, 600));
    Spotlight::SetTextColor(m_size, Spotlight::Color::Text());
    heading->addWidget(name, 1);
    heading->addWidget(m_size);
    text->addLayout(heading);
    auto* detail = new QLabel(description);
    detail->setFont(Spotlight::Font(12, 400));
    Spotlight::SetTextColor(detail, Spotlight::Color::TextSecondary());
    text->addWidget(detail);
    m_bar = new QWidget;
    m_bar->setFixedHeight(BarHeight);
    text->addWidget(m_bar);
    layout->addLayout(text, 1);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(6);
    const auto action = [&](const char* icon_name, const QString& tip, void (DataWidget::*slot)()) {
        auto* button = new Spotlight::CircleButton(QString::fromLatin1(icon_name), 32);
        button->setToolTip(tip);
        button->setAccessibleName(tr("%1: %2").arg(tip, title));
        connect(button, &QAbstractButton::clicked, this, slot);
        actions->addWidget(button);
        return button;
    };
    action("folder-open", tr("Open"), &DataWidget::open);
    action("export", tr("Export"), &DataWidget::upload);
    action("download-simple", tr("Import"), &DataWidget::download);
    action("trash", tr("Clear"), &DataWidget::clear)->SetTint(Spotlight::Color::Destructive());
    layout->addLayout(actions);

    scan();
}

std::optional<u64> DataWidget::Size() const {
    return m_bytes;
}

void DataWidget::SetTotal(u64 total) {
    m_total = total;
    update();
}

void DataWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (m_divider) {
        painter.fillRect(QRect(18, 0, width() - 36, 1), Spotlight::Color::Divider());
    }
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QRectF track = m_bar->geometry();
    painter.setBrush(Spotlight::Color::Stroke());
    painter.drawRoundedRect(track, BarHeight / 2.0, BarHeight / 2.0);
    if (!m_bytes || m_total == 0 || *m_bytes == 0) {
        return;
    }
    const qreal share = static_cast<qreal>(*m_bytes) / static_cast<qreal>(m_total);
    painter.setBrush(m_tint);
    painter.drawRoundedRect(QRectF(track.left(), track.top(),
                                   std::max<qreal>(BarHeight, std::round(track.width() * share)),
                                   track.height()),
                            BarHeight / 2.0, BarHeight / 2.0);
}

void DataWidget::clear() {
    std::optional<std::string> user_id = selectProfile();
    if (!user_id)
        return;
    QtCommon::Content::ClearDataDir(m_dir, user_id.value());
    scan();
}

void DataWidget::open() {
    std::optional<std::string> user_id = selectProfile();
    if (!user_id)
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(
        FrontendCommon::DataManager::GetDataDirString(m_dir, user_id.value()))));
}

void DataWidget::upload() {
    std::optional<std::string> user_id = selectProfile();
    if (!user_id)
        return;
    QtCommon::Content::ExportDataDir(m_dir, user_id.value(), m_exportName);
}

void DataWidget::download() {
    std::optional<std::string> user_id = selectProfile();
    if (!user_id)
        return;
    QtCommon::Content::ImportDataDir(m_dir, user_id.value(), std::bind(&DataWidget::scan, this));
}

void DataWidget::scan() {
    m_size->setText(tr("Calculating..."));
    m_bytes.reset();
    emit SizeChanged();

    QFutureWatcher<u64>* watcher = new QFutureWatcher<u64>(this);

    connect(watcher, &QFutureWatcher<u64>::finished, this, [=, this]() {
        m_bytes = watcher->result();
        m_size->setText(
            QString::fromStdString(FrontendCommon::DataManager::ReadableBytesSize(*m_bytes)));
        watcher->deleteLater();
        emit SizeChanged();
    });

    watcher->setFuture(
        QtConcurrent::run([this]() { return FrontendCommon::DataManager::DataDirSize(m_dir); }));
}

std::optional<std::string> DataWidget::selectProfile() {
    std::string user_id{};
    if (m_dir == FrontendCommon::DataManager::DataDir::Saves) {
        user_id = GetProfileIDString();
        if (user_id.empty())
            return std::nullopt;
    }

    return user_id;
}
