// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include "dep_hashes.h"
#include "lichii/deps_dialog.h"
#include "lichii/spotlight/theme.h"

namespace {

// QHeaderView insets its labels by the style's 4 px header margin; rows indent their text to match.
constexpr int LabelMargin = 4;
constexpr int HeaderHeight = 26 + LabelMargin;
constexpr int RowHeight = 36;
constexpr int VersionWidth = 118;
constexpr int MaxVersionWidth = 230;
constexpr int RuleInset = 18;
constexpr int VisibleRows = 9;

QFont MonoFont() {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(13);
    return font;
}

/// A dependency row: a rule above it, the name as a link with an arrow, and the version.
class DependencyDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QRect cell = option.rect;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const int left = index.column() == 0 ? cell.left() + LabelMargin + RuleInset : cell.left();
        const int right = index.column() == 0 ? cell.right() + 1 : cell.right() + 1 - RuleInset;
        painter->fillRect(QRect(left, cell.top(), right - left, 1), QColor(0xff, 0xff, 0xff, 0x12));
        const QRect line(cell.left() + LabelMargin, cell.top() + 1, cell.width() - LabelMargin,
                         cell.height() - 1);
        if (index.column() == 0) {
            painter->setFont(Spotlight::Font(13, 600));
            painter->setPen(Spotlight::Color::Info());
            const QString name = index.data().toString();
            painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter, name);
            const int x = line.left() + painter->fontMetrics().horizontalAdvance(name) + 6;
            painter->drawPixmap(x, line.center().y() - 6,
                                Spotlight::IconPixmap(u"arrow-square-out", 14,
                                                      Spotlight::Color::Info(),
                                                      painter->device()->devicePixelRatioF()));
        } else {
            painter->setFont(MonoFont());
            painter->setPen(Spotlight::Color::TextSecondary());
            painter->drawText(line, Qt::AlignLeft | Qt::AlignVCenter,
                              painter->fontMetrics().elidedText(index.data().toString(),
                                                                Qt::ElideMiddle, line.width()));
        }
        painter->restore();
    }

    bool editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override {
        if (event->type() != QEvent::MouseButtonRelease || index.column() != 0 ||
            static_cast<QMouseEvent*>(event)->button() != Qt::LeftButton) {
            return false;
        }
        const int width =
            QFontMetrics(Spotlight::Font(13, 600)).horizontalAdvance(index.data().toString()) + 6 +
            14;
        if (static_cast<QMouseEvent*>(event)->position().x() - option.rect.left() - LabelMargin >
            width) {
            return false;
        }
        QDesktopServices::openUrl(QUrl(index.data(Qt::UserRole).toString()));
        return true;
    }
};

} // namespace

DepsDialog::DepsDialog(QWidget* parent) : Spotlight::Dialog(parent) {
    setWindowTitle(tr("Lichii Dependencies"));
    SetCardWidth(509);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* intro = new QHBoxLayout;
    intro->setSpacing(12);
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon::fromTheme(QStringLiteral("io.github.zortos293.lichii"),
                                     QIcon(QStringLiteral(":/icons/default/256x256/lichii.png")))
                        .pixmap(42));
    intro->addWidget(logo);
    auto* subtitle = new QLabel(tr("The projects that make Lichii possible"), this);
    subtitle->setFont(Spotlight::Font(13, 400));
    subtitle->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 158);"));
    intro->addWidget(subtitle, 1);
    layout->addLayout(intro);

    auto* group = new QFrame(this);
    group->setObjectName(QStringLiteral("depsGroup"));
    group->setStyleSheet(QStringLiteral("QFrame#depsGroup { background: rgba(255, 255, 255, 13); "
                                        "border: 1px solid rgba(255, 255, 255, 20); "
                                        "border-radius: 18px; }"));
    auto* rows = new QVBoxLayout(group);
    rows->setContentsMargins(12 - LabelMargin, 12 - LabelMargin, 12, 3);
    rows->setSpacing(9);

    const int count = int(Common::dep_names.size());
    // A full commit hash shows as git's short form; the tooltip keeps the whole version.
    const auto shown_version = [](const QString& version) {
        static const QRegularExpression commit(QStringLiteral("^[0-9a-f]{40}$"));
        return commit.match(version).hasMatch() ? version.left(10) : version;
    };
    int version_width = VersionWidth;
    for (const char* version : Common::dep_hashes) {
        version_width = std::max(
            version_width,
            QFontMetrics(MonoFont()).horizontalAdvance(shown_version(QString::fromUtf8(version))) +
                LabelMargin + 12);
    }
    version_width = std::min(version_width, MaxVersionWidth);
    auto* table = new QTableWidget(count, 2, group);
    table->setFrameShape(QFrame::NoFrame);
    table->setShowGrid(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalHeaderLabels({tr("Dependency"), tr("Version")});
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignTop);
    table->horizontalHeader()->setFixedHeight(HeaderHeight);
    table->horizontalHeader()->setFont(Spotlight::Font(13, 600));
    table->verticalHeader()->hide();
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(RowHeight);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, version_width);
    table->viewport()->setAutoFillBackground(false);
    table->setStyleSheet(
        QStringLiteral("QTableWidget { background: transparent; border: none; padding: 0; }"
                       "QHeaderView::section { color: #ffffff; border: 0; padding: 0; "
                       "min-height: 0; font-size: 13px; font-weight: 600; }"));
    table->setItemDelegate(new DependencyDelegate(table));
    table->setFixedHeight(HeaderHeight + RowHeight * std::min(count, VisibleRows));
    table->setMouseTracking(true);
    connect(table, &QTableWidget::entered, table, [table](const QModelIndex& index) {
        table->viewport()->setCursor(index.column() == 0 ? Qt::PointingHandCursor
                                                         : Qt::ArrowCursor);
    });
    for (int i = 0; i < count; ++i) {
        auto* name = new QTableWidgetItem(QString::fromUtf8(Common::dep_names.at(i)));
        name->setData(Qt::UserRole, QString::fromUtf8(Common::dep_urls.at(i)));
        name->setToolTip(QString::fromUtf8(Common::dep_urls.at(i)));
        table->setItem(i, 0, name);
        const QString version = QString::fromUtf8(Common::dep_hashes.at(i));
        auto* version_item = new QTableWidgetItem(shown_version(version));
        version_item->setToolTip(version);
        table->setItem(i, 1, version_item);
    }
    rows->addWidget(table);
    layout->addWidget(group);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->setContentsMargins(0, 6, 0, 0);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

DepsDialog::~DepsDialog() = default;
