// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include "lichii/configuration/addon/mod_select_dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace {

namespace Color = Spotlight::Color;

constexpr int ContentWidth = 452;
constexpr int VisibleRows = 5;

class Panel final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Color::StrokeSubtle(), 1));
        painter.setBrush(Color::FillFaint());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                Spotlight::Radius::Panel, Spotlight::Radius::Panel);
    }
};

QLabel* Text(const QString& text, int size, int weight, const QColor& color) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setFont(Spotlight::Font(size, weight));
    Spotlight::SetTextColor(label, color);
    return label;
}

QFrame* Divider() {
    auto* line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background: rgba(255, 255, 255, %1); border: none;")
                            .arg(Color::StrokeSubtle().alpha()));
    return line;
}

QString CommonParent(const QStringList& paths) {
    QString parent = QFileInfo(paths.front()).absolutePath();
    for (const auto& path : paths) {
        while (!parent.isEmpty() && !QFileInfo(path).absoluteFilePath().startsWith(parent + u'/')) {
            const QString up = QFileInfo(parent).absolutePath();
            parent = up == parent ? QString{} : up;
        }
    }
    return parent;
}

} // namespace

ModSelectDialog::ModSelectDialog(const QStringList& mods_, QWidget* parent)
    : Spotlight::Dialog(parent), mods{mods_} {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Select mods to import"));
    SetIcon(u"puzzle-piece", Color::Accent());
    SetSubtitle(tr("Lichii found more than one mod in this file or folder."));

    auto* panel = new Panel;
    panel->setFixedWidth(ContentWidth);
    auto* panel_layout = new QVBoxLayout(panel);
    panel_layout->setContentsMargins(16, 0, 16, 0);
    panel_layout->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 18, 0, 18);
    header->setSpacing(12);
    auto* folder = new QLabel;
    folder->setPixmap(Spotlight::IconPixmap(u"folder", 16, Color::TextSecondary(),
                                            devicePixelRatioF()));
    header->addWidget(folder);
    header->addWidget(Text(tr("Discovered mods"), 14, 600, Color::Text()), 1);
    panel_layout->addLayout(header);

    auto* list = new QWidget;
    auto* rows = new QVBoxLayout(list);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    const QString root = CommonParent(mods);
    const int indent = style()->pixelMetric(QStyle::PM_IndicatorWidth) +
                       style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
    for (const auto& mod : mods) {
        rows->addWidget(Divider());
        auto* row = new QVBoxLayout;
        row->setContentsMargins(0, 10, 0, 10);
        row->setSpacing(2);
        auto* check = new QCheckBox(QFileInfo(mod).fileName());
        check->setFont(Spotlight::Font(14, 600));
        check->setChecked(true);
        row->addWidget(check);
        QString detail = root.isEmpty() ? mod : QDir(root).relativeFilePath(mod);
        const QStringList contents = QDir(mod).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (contents.size() == 1) {
            detail += u'/' + contents.front();
        }
        auto* path = Text(detail, 12, 400, Color::TextSecondary());
        path->setContentsMargins(indent, 0, 0, 0);
        path->setToolTip(mod);
        row->addWidget(path);
        rows->addLayout(row);
        checks.append(check);
    }
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(list);
    const int row_height = list->sizeHint().height() / static_cast<int>(mods.size());
    scroll->setFixedHeight(row_height * std::min(static_cast<int>(mods.size()), VisibleRows));
    panel_layout->addWidget(scroll);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    auto* cancel = new Spotlight::PillButton(tr("Cancel"), {},
                                             Spotlight::PillButton::Kind::Control, 40);
    auto* ok = new Spotlight::PillButton(tr("OK"), {}, Spotlight::PillButton::Kind::Accent, 40);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    connect(ok, &QAbstractButton::clicked, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(26);
    layout->addWidget(panel);
    layout->addLayout(buttons);

    connect(this, &QDialog::accepted, this, [this] {
        QStringList selected;
        for (qsizetype i = 0; i < checks.size(); ++i) {
            if (checks[i]->isChecked()) {
                selected << mods[i];
            }
        }
        emit modsSelected(selected);
    });
}

ModSelectDialog::~ModSelectDialog() = default;

void ModSelectDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        accept();
        return;
    }
    Spotlight::Dialog::keyPressEvent(event);
}
