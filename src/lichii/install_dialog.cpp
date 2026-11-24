// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include "lichii/install_dialog.h"
#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/qt_common.h"

namespace {

constexpr int RowHeight = 54;
constexpr int VisibleRows = 5;

// The rounded panel the files sit in (Paper: inset list, radius 18).
class FilePanel : public QWidget {
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

// One file: a check box, the file name and its size. A click anywhere on the row toggles it; the
// rows after the first draw a divider above them.
class FileRow : public QWidget {
public:
    FileRow(QCheckBox* check_, const QString& name, const QString& details, bool divider_)
        : check{check_}, divider{divider_} {
        setMinimumHeight(RowHeight);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 8, 14, 8);
        layout->setSpacing(12);
        layout->addWidget(check);

        auto* text = new QVBoxLayout;
        text->setSpacing(2);
        auto* title = new QLabel(name);
        title->setFont(Spotlight::Font(14, 600));
        Spotlight::SetTextColor(title, Spotlight::Color::Text());
        title->setMinimumWidth(1);
        auto* subtitle = new QLabel(details);
        subtitle->setFont(Spotlight::Font(12, 400));
        Spotlight::SetTextColor(subtitle, Spotlight::Color::TextSecondary());
        text->addWidget(title);
        text->addWidget(subtitle);
        layout->addLayout(text, 1);
        check->setAccessibleName(name);
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            check->toggle();
        }
    }

    void paintEvent(QPaintEvent*) override {
        if (divider) {
            QPainter(this).fillRect(QRect(18, 0, width() - 36, 1), Spotlight::Color::Divider());
        }
    }

private:
    QCheckBox* check;
    bool divider;
};

} // namespace

InstallDialog::InstallDialog(QWidget* parent, const QStringList& files)
    : Spotlight::Dialog(parent) {
    setWindowTitle(tr("Install to the system"));
    SetSubtitle(tr("Copies games, updates and DLC into Lichii\u2019s system storage (NAND)."));
    SetIcon(u"download-simple", Spotlight::Color::Accent());
    SetCardWidth(600);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* panel = new FilePanel;
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(1, 1, 1, 1);
    file_list = new QWidget;
    rows = new QVBoxLayout(file_list);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    scroll->setWidget(file_list);
    panel_layout->addWidget(scroll);
    layout->addWidget(panel);

    for (const QString& file : files) {
        AddFile(file);
    }
    const auto fit_rows = [this, scroll] {
        const int shown = std::min(static_cast<int>(entries.size()), VisibleRows);
        scroll->setFixedHeight(shown * RowHeight +
                               (shown < static_cast<int>(entries.size()) ? RowHeight / 2 : 0));
    };
    fit_rows();

    auto* notice = new Spotlight::Notice;
    notice->SetTone(Spotlight::Notice::Tone::Warning);
    notice->setText(tr("Updates and DLC that are already installed are replaced. Games run fine "
                       "from a game folder without installing."));
    layout->addWidget(notice);

    auto* add = new QPushButton(Spotlight::Icon(u"plus", Spotlight::Color::Text(), 15),
                                tr("Add more files\u2026"));
    add->setProperty("kind", QStringLiteral("ghost"));
    add->setProperty("compact", true);
    add->setIconSize({15, 15});
    add->setAutoDefault(false);
    connect(add, &QPushButton::clicked, this, [this, fit_rows] {
        AddMoreFiles();
        fit_rows();
    });
    layout->addWidget(add, 0, Qt::AlignHCenter);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(0, 6, 0, 0);
    summary = new QLabel;
    summary->setFont(Spotlight::Font(13, 400));
    Spotlight::SetTextColor(summary, Spotlight::Color::TextSecondary());
    footer->addWidget(summary, 1);
    auto* buttons = new QDialogButtonBox;
    buttons->addButton(QDialogButtonBox::Cancel);
    install = buttons->addButton(tr("Install"), QDialogButtonBox::AcceptRole);
    install->setIcon(Spotlight::Icon(u"download-simple", Spotlight::Color::OnAccent(), 15));
    install->setIconSize({15, 15});
    install->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &InstallDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &InstallDialog::reject);
    footer->addWidget(buttons);
    layout->addLayout(footer);

    UpdateSummary();
    install->setFocus();
}

InstallDialog::~InstallDialog() = default;

void InstallDialog::AddFile(const QString& path) {
    const QFileInfo info(path);
    auto* check = new QCheckBox;
    check->setChecked(true);
    connect(check, &QCheckBox::toggled, this, &InstallDialog::UpdateSummary);
    rows->addWidget(new FileRow(check, info.fileName(), QtCommon::ReadableByteSize(info.size()),
                                !entries.empty()));
    entries.push_back({path, info.size(), check});
}

void InstallDialog::AddMoreFiles() {
    const QString file_filter =
        tr("Installable Switch File (*.nca *.nsp *.nsz *.xci *.xcz);;Nintendo Content Archive "
           "(*.nca);;Nintendo Submission Package (*.nsp *.nsz);;NX Cartridge "
           "Image (*.xci *.xcz)");
    const QString folder = entries.empty() ? QString::fromStdString(UISettings::values.roms_path)
                                           : QFileInfo(entries.back().path).path();
    for (const QString& file :
         QFileDialog::getOpenFileNames(this, tr("Install Files"), folder, file_filter)) {
        const bool listed = std::ranges::any_of(
            entries, [&file](const Entry& entry) { return entry.path == file; });
        if (!listed) {
            AddFile(file);
        }
    }
    UpdateSummary();
}

void InstallDialog::UpdateSummary() {
    int selected = 0;
    qint64 size = 0;
    for (const Entry& entry : entries) {
        if (entry.check->isChecked()) {
            ++selected;
            size += entry.size;
        }
    }
    summary->setText(tr("%1 of %2 selected \u00b7 %3")
                         .arg(selected)
                         .arg(entries.size())
                         .arg(QtCommon::ReadableByteSize(size)));
}

QStringList InstallDialog::GetFiles() const {
    QStringList files;
    for (const Entry& entry : entries) {
        if (entry.check->isChecked()) {
            files.append(entry.path);
        }
    }
    return files;
}

int InstallDialog::GetMinimumWidth() const {
    return file_list->width();
}
