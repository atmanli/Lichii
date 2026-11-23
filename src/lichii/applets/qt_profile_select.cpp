// SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <mutex>
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>
#include "common/fs/path_util.h"
#include "common/string_util.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "lichii/applets/qt_profile_select.h"
#include "lichii/main_window.h"
#include "lichii/util/controller_navigation.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace {
QString FormatUserEntryText(const QString& username, Common::UUID uuid) {
    return QtProfileSelectionDialog::tr(
               "%1\n%2", "%1 is the profile username, %2 is the formatted UUID (e.g. "
                         "00112233-4455-6677-8899-AABBCCDDEEFF))")
        .arg(username, QString::fromStdString(uuid.FormattedString()));
}

QString GetImagePath(Common::UUID uuid) {
    const auto path =
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
        fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
    return QString::fromStdString(Common::FS::PathToUTF8String(path));
}

QPixmap GetIcon(Common::UUID uuid) {
    QPixmap icon{GetImagePath(uuid)};

    if (!icon) {
        icon.fill(Qt::black);
        icon.loadFromData(Core::Constants::ACCOUNT_BACKUP_JPEG.data(),
                          static_cast<u32>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
    }

    return icon.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

constexpr int NameRole = Qt::UserRole + 1;
constexpr int UuidRole = Qt::UserRole + 2;
constexpr int UserRowHeight = 58;
constexpr int VisibleUserRows = 5;

class UserDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
        return {option.rect.width(), UserRowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto* view = qobject_cast<const QAbstractItemView*>(option.widget);
        QRect row = option.rect;
        if (view) {
            row.setWidth(view->viewport()->width() - row.left());
        }
        const bool first = index.row() == 0;
        const bool last = index.row() + 1 == index.model()->rowCount();
        const bool selected = option.state & QStyle::State_Selected;
        if (selected || option.state & QStyle::State_MouseOver) {
            const QRectF area = QRectF(row).adjusted(1, 1, -1, -1);
            const qreal top = first ? Spotlight::Radius::Inset - 2 : 0;
            const qreal bottom = last ? Spotlight::Radius::Inset - 2 : 0;
            QPainterPath path;
            path.moveTo(area.left(), area.top() + top);
            path.arcTo(area.left(), area.top(), top * 2, top * 2, 180, -90);
            path.lineTo(area.right() - top, area.top());
            path.arcTo(area.right() - top * 2, area.top(), top * 2, top * 2, 90, -90);
            path.lineTo(area.right(), area.bottom() - bottom);
            path.arcTo(area.right() - bottom * 2, area.bottom() - bottom * 2, bottom * 2,
                       bottom * 2, 0, -90);
            path.lineTo(area.left() + bottom, area.bottom());
            path.arcTo(area.left(), area.bottom() - bottom * 2, bottom * 2, bottom * 2, 270, -90);
            path.closeSubpath();
            if (selected) {
                painter->setPen(QPen(Spotlight::Color::Accent(), 2));
                painter->setBrush(Spotlight::Color::AccentSoft());
            } else {
                painter->setPen(Qt::NoPen);
                painter->setBrush(Spotlight::Color::FillFaint());
            }
            painter->drawPath(path);
        }
        const bool next_selected =
            view && view->selectionModel()->isRowSelected(index.row() + 1, {});
        if (!last && !selected && !next_selected) {
            painter->fillRect(QRect(row.left() + 18, row.bottom(), row.width() - 36, 1),
                              Spotlight::Color::Divider());
        }
        const QRectF avatar(row.left() + 14, row.top() + (row.height() - 38) / 2.0, 38, 38);
        Spotlight::PaintWholeIcon(*painter, avatar,
                                  index.data(Qt::DecorationRole).value<QIcon>().pixmap(64, 64),
                                  Spotlight::Radius::Row);
        const int text_left = row.left() + 14 + 38 + 12;
        const int text_right = row.right() - 14 - (selected ? 16 + 12 : 0);
        const int text_width = text_right - text_left;
        const QFont name_font = Spotlight::Font(14, 600);
        const QFont uuid_font = Spotlight::Font(12, 400);
        const int text_top = row.top() + (row.height() - 19 - 2 - 16) / 2;
        painter->setFont(name_font);
        painter->setPen(Spotlight::Color::Text());
        painter->drawText(QRect(text_left, text_top, text_width, 19),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(name_font).elidedText(index.data(NameRole).toString(),
                                                             Qt::ElideRight, text_width));
        painter->setFont(uuid_font);
        painter->setPen(Spotlight::Color::TextSecondary());
        painter->drawText(QRect(text_left, text_top + 21, text_width, 16),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(uuid_font).elidedText(index.data(UuidRole).toString(),
                                                             Qt::ElideRight, text_width));
        if (selected) {
            const qreal ratio = painter->device()->devicePixelRatioF();
            painter->drawPixmap(
                QPointF(row.right() - 14 - 16, row.top() + (row.height() - 16) / 2.0),
                Spotlight::IconPixmap(u"check", 16, Spotlight::Color::Accent(), ratio));
        }
        painter->restore();
    }
};
} // Anonymous namespace

QtProfileSelectionDialog::QtProfileSelectionDialog(
    Core::System& system, QWidget* parent,
    const Core::Frontend::ProfileSelectParameters& parameters)
    : Spotlight::Dialog(parent), profile_manager{system.GetProfileManager()} {
    SetIcon(u"users", Spotlight::Color::Info());
    SetCardWidth(425);
    outer_layout = new QVBoxLayout(this);
    outer_layout->setContentsMargins(0, 0, 0, 0);
    outer_layout->setSpacing(0);

    users_label = new QLabel(tr("Users"));
    users_label->setFont(Spotlight::Font(13, 600));
    Spotlight::SetTextColor(users_label, Spotlight::Color::TextSecondary());

    scroll_area = new QScrollArea;
    scroll_area->setFrameShape(QFrame::NoFrame);

    buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    connect(buttons, &QDialogButtonBox::accepted, this, &QtProfileSelectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QtProfileSelectionDialog::reject);

    outer_layout->addWidget(users_label);
    outer_layout->addSpacing(8);
    outer_layout->addWidget(scroll_area);
    outer_layout->addSpacing(14);
    outer_layout->addWidget(buttons);

    layout = new QVBoxLayout(scroll_area);
    tree_view = new QTreeView;
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);
    controller_navigation = new ControllerNavigation(system.HIDCore(), this);

    tree_view->setHeaderHidden(true);
    tree_view->setRootIsDecorated(false);
    tree_view->setItemDelegate(new UserDelegate(tree_view));
    tree_view->setMouseTracking(true);
    tree_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_view->setStyleSheet(QStringLiteral(
        "QTreeView { background-color: rgba(255, 255, 255, 13); border: 1px solid "
        "rgba(255, 255, 255, 20); border-radius: 18px; padding: 0; }"
        "QTreeView::item:hover, QTreeView::item:selected { background: transparent; }"));
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setUniformRowHeights(true);
    tree_view->setIconSize({64, 64});
    tree_view->setContextMenuPolicy(Qt::NoContextMenu);

    item_model->insertColumns(0, 1);
    item_model->setHeaderData(0, Qt::Horizontal, tr("Users"));

    // We must register all custom types with the Qt Automoc system so that we are able to use it
    // with signals/slots. In this case, QList falls under the umbrella of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);

    connect(tree_view, &QTreeView::clicked, this, &QtProfileSelectionDialog::SelectUser);
    connect(tree_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &QtProfileSelectionDialog::SelectUser);
    connect(tree_view, &QTreeView::doubleClicked, this, &QtProfileSelectionDialog::accept);
    connect(controller_navigation, &ControllerNavigation::TriggerKeyboardEvent,
            [this](Qt::Key key) {
                if (!this->isActiveWindow()) {
                    return;
                }
                QKeyEvent* event = new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier);
                QCoreApplication::postEvent(tree_view, event);
                SelectUser(tree_view->currentIndex());
            });

    const auto& profiles = profile_manager.GetAllUsers();
    for (const auto& user : profiles) {
        Service::Account::ProfileBase profile{};
        if (!profile_manager.GetProfileBase(user, profile))
            continue;

        const auto username = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(profile.username.data()), profile.username.size());

        auto* item = new QStandardItem{GetIcon(user),
                                       FormatUserEntryText(QString::fromStdString(username), user)};
        item->setData(QString::fromStdString(username), NameRole);
        item->setData(QString::fromStdString(user.FormattedString()), UuidRole);
        list_items.push_back(QList<QStandardItem*>{item});
    }

    for (const auto& item : list_items)
        item_model->appendRow(item);

    tree_view->ensurePolished();
    const int rows = std::clamp(item_model->rowCount(), 1, VisibleUserRows);
    tree_view->setFixedHeight(rows * UserRowHeight + 2 * tree_view->frameWidth());
    scroll_area->setFixedHeight(tree_view->height());
    tree_view->setCurrentIndex(item_model->index(user_index, 0));

    SetWindowTitle(parameters);
    SetDialogPurpose(parameters);
}

QtProfileSelectionDialog::~QtProfileSelectionDialog() {
    controller_navigation->UnloadController();
};

int QtProfileSelectionDialog::exec() {
    // Skip profile selection when there's only one.
    if (profile_manager.GetUserCount() == 1) {
        user_index = 0;
        return QDialog::Accepted;
    }
    setWindowFlag(Qt::FramelessWindowHint);
    return QDialog::exec();
}

void QtProfileSelectionDialog::accept() {
    QDialog::accept();
}

void QtProfileSelectionDialog::reject() {
    user_index = 0;
    QDialog::reject();
}

int QtProfileSelectionDialog::GetIndex() const {
    return user_index;
}

void QtProfileSelectionDialog::SelectUser(const QModelIndex& index) {
    user_index = index.row();
}

void QtProfileSelectionDialog::SetWindowTitle(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    using Service::AM::Frontend::UiMode;
    switch (parameters.mode) {
    case UiMode::UserCreator:
    case UiMode::UserCreatorForStarter:
        setWindowTitle(tr("Profile Creator"));
        return;
    case UiMode::EnsureNetworkServiceAccountAvailable:
        setWindowTitle(tr("Profile Selector"));
        return;
    case UiMode::UserIconEditor:
        setWindowTitle(tr("Profile Icon Editor"));
        return;
    case UiMode::UserNicknameEditor:
        setWindowTitle(tr("Profile Nickname Editor"));
        return;
    case UiMode::NintendoAccountAuthorizationRequestContext:
    case UiMode::IntroduceExternalNetworkServiceAccount:
    case UiMode::IntroduceExternalNetworkServiceAccountForRegistration:
    case UiMode::NintendoAccountNnidLinker:
    case UiMode::LicenseRequirementsForNetworkService:
    case UiMode::LicenseRequirementsForNetworkServiceWithUserContextImpl:
    case UiMode::UserCreatorForImmediateNaLoginTest:
    case UiMode::UserQualificationPromoter:
    case UiMode::UserSelector:
    default:
        setWindowTitle(tr("Profile Selector"));
    }
}

void QtProfileSelectionDialog::SetDialogPurpose(
    const Core::Frontend::ProfileSelectParameters& parameters) {
    using Service::AM::Frontend::UserSelectionPurpose;

    switch (parameters.purpose) {
    case UserSelectionPurpose::GameCardRegistration:
        SetSubtitle(tr("Who will receive the points?"));
        return;
    case UserSelectionPurpose::EShopLaunch:
        SetSubtitle(tr("Who is using Nintendo eShop?"));
        return;
    case UserSelectionPurpose::EShopItemShow:
        SetSubtitle(tr("Who is making this purchase?"));
        return;
    case UserSelectionPurpose::PicturePost:
        SetSubtitle(tr("Who is posting?"));
        return;
    case UserSelectionPurpose::NintendoAccountLinkage:
        SetSubtitle(tr("Select a user to link to a Nintendo Account."));
        return;
    case UserSelectionPurpose::SettingsUpdate:
        SetSubtitle(tr("Change settings for which user?"));
        return;
    case UserSelectionPurpose::SaveDataDeletion:
        SetSubtitle(tr("Format data for which user?"));
        return;
    case UserSelectionPurpose::UserMigration:
        SetSubtitle(tr("Which user will be transferred to another console?"));
        return;
    case UserSelectionPurpose::SaveDataTransfer:
        SetSubtitle(tr("Send save data for which user?"));
        return;
    case UserSelectionPurpose::General:
    default:
        SetSubtitle(tr("Select a user:"));
        return;
    }
}

QtProfileSelector::QtProfileSelector(MainWindow& parent) {
    connect(this, &QtProfileSelector::MainWindowSelectProfile, &parent,
            &MainWindow::ProfileSelectorSelectProfile, Qt::QueuedConnection);
    connect(this, &QtProfileSelector::MainWindowRequestExit, &parent,
            &MainWindow::ProfileSelectorRequestExit, Qt::QueuedConnection);
    connect(&parent, &MainWindow::ProfileSelectorFinishedSelection, this,
            &QtProfileSelector::MainWindowFinishedSelection, Qt::DirectConnection);
}

QtProfileSelector::~QtProfileSelector() = default;

void QtProfileSelector::Close() const {
    callback = {};
    emit MainWindowRequestExit();
}

void QtProfileSelector::SelectProfile(
    SelectProfileCallback callback_,
    const Core::Frontend::ProfileSelectParameters& parameters) const {
    callback = std::move(callback_);
    emit MainWindowSelectProfile(parameters);
}

void QtProfileSelector::MainWindowFinishedSelection(std::optional<Common::UUID> uuid) {
    if (callback) {
        callback(uuid);
    }
}
