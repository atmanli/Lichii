// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/mod_store/mod_store_dialog.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTemporaryFile>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include "common/scm_rev.h"
#include "lichii/mod_store/mod_store_client.h"
#include "lichii/mod_store/mod_store_install.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"
#include "lichii/spotlight/widgets.h"

namespace ModStore {
namespace {

constexpr int GamePageSize = 50;
constexpr int ModPageSize = 8;
constexpr qint64 MaxArchiveBytes = 64 * 1024 * 1024;
constexpr int CardColumns = 4;
constexpr qsizetype ThumbnailConcurrency = 3;
constexpr std::array Categories{ModType::Graphics, ModType::Fps, ModType::Cheats,
                                ModType::Translation, ModType::Gameplay};
using Spotlight::CircleButton;
using Spotlight::PillButton;
using Spotlight::SegmentedControl;
using Spotlight::ToggleSwitch;
namespace Color = Spotlight::Color;

enum class Compatibility { Match, Unknown, Mismatch };

std::optional<qulonglong> BuildOrdinal(const QString& tag) {
    static const QRegularExpression format(QStringLiteral("^build-[1-9][0-9]*$"));
    bool ok = false;
    const auto number = tag.mid(6).toULongLong(&ok);
    if (!ok || !format.match(tag).hasMatch()) {
        return std::nullopt;
    }
    return number;
}

bool KnownBuild(const QString& build) {
    static const QRegularExpression format(QStringLiteral("^[0-9A-Fa-f]{64}$"));
    return format.match(build).hasMatch() &&
           std::ranges::any_of(build, [](QChar digit) { return digit != QLatin1Char('0'); });
}

Compatibility MatchVersion(const QString& selected_title, const GameIdentity& game,
                           const Version& version) {
    if (selected_title != version.title_id ||
        (!game.display_version.isEmpty() && game.display_version != version.game_version) ||
        (KnownBuild(game.build_id) &&
         game.build_id.compare(version.build_id, Qt::CaseInsensitive) != 0)) {
        return Compatibility::Mismatch;
    }
    const auto running = BuildOrdinal(QString::fromLatin1(Common::g_build_release_tag));
    const auto required = BuildOrdinal(version.min_lichii_build);
    if (running && required && *running < *required) {
        return Compatibility::Mismatch;
    }
    return game.installed && !game.display_version.isEmpty() && KnownBuild(game.build_id) &&
                   running && required
               ? Compatibility::Match
               : Compatibility::Unknown;
}

Compatibility MatchGitHubBuild(const GameIdentity& game, const GitHubBuild& build) {
    if (!game.installed || !KnownBuild(game.build_id)) {
        return Compatibility::Unknown;
    }
    if (build.main) {
        return game.build_id.startsWith(build.id, Qt::CaseInsensitive) ? Compatibility::Match
                                                                      : Compatibility::Mismatch;
    }
    return build.id.size() == 64 && game.build_id.compare(build.id, Qt::CaseInsensitive) == 0
               ? Compatibility::Match
               : Compatibility::Unknown;
}

struct Offer {
    std::variant<Version, ExternalFile, GitHubBuild> file;
    QString version;
    Compatibility compatibility;
    bool supported;
    QString label;
    QString tooltip;
};

QString ExternalVersionPrefix(qint64 id) {
    return QStringLiteral("%1-").arg(id);
}

QString ExternalVersion(qint64 id, const QByteArray& sha256) {
    return ExternalVersionPrefix(id) + QString::fromLatin1(sha256.toHex().left(16));
}

QString CssColor(const QColor& color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QLabel* PlainLabel(QWidget* parent, bool wrap = false, const QColor& color = Color::Text(),
                   int size = 13, int weight = QFont::Normal) {
    auto* label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(wrap);
    label->setFont(Spotlight::Font(size, weight));
    label->setStyleSheet(
        QStringLiteral("color: %1; background: transparent;").arg(CssColor(color)));
    return label;
}

QColor ImageWell() {
    return {0x11, 0x11, 0x11};
}

QColor Rule() {
    return {0xff, 0xff, 0xff, 0x12};
}

QPixmap CachedIcon(QStringView name, int size, const QColor& color, qreal ratio) {
    static QHash<QString, QPixmap> cache;
    const auto key = QStringLiteral("%1/%2/%3/%4").arg(name).arg(size).arg(color.rgba()).arg(ratio);
    auto found = cache.constFind(key);
    if (found == cache.cend()) {
        found = cache.insert(key, Spotlight::IconPixmap(name, size, color, ratio));
    }
    return found.value();
}

class Picture {
public:
    Picture(int radius, bool crop) : radius(radius), crop(crop) {}

    void Set(QImage value) {
        source = std::move(value);
        cache = {};
    }

    const QImage& Source() const {
        return source;
    }

    void Paint(QPainter& painter, const QRect& bounds) {
        const qreal ratio = painter.device()->devicePixelRatioF();
        const QSize pixels = (QSizeF(bounds.size()) * ratio).toSize();
        if (cache.size() != pixels || cache.devicePixelRatio() != ratio) {
            Render(bounds.size(), ratio);
        }
        painter.drawPixmap(bounds.topLeft(), cache);
    }

private:
    void Render(QSize size, qreal ratio) {
        cache = QPixmap((QSizeF(size) * ratio).toSize());
        cache.setDevicePixelRatio(ratio);
        cache.fill(Qt::transparent);
        QPainter painter(&cache);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRect bounds(QPoint{}, size);
        QPainterPath clip;
        clip.addRoundedRect(bounds, radius, radius);
        painter.fillPath(clip, ImageWell());
        if (source.isNull()) {
            const int icon = qBound(14, size.height() / 4, 32);
            painter.drawPixmap(bounds.center() - QPoint(icon / 2, icon / 2),
                               Spotlight::IconPixmap(u"image", icon, Spotlight::Color::TextFaint(),
                                                     ratio));
            return;
        }
        const QSize fitted = source.size().scaled(
            bounds.size(), crop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio);
        QRect target(QPoint{}, fitted);
        target.moveCenter(bounds.center());
        painter.setClipPath(clip);
        painter.drawImage(target, source.scaled((QSizeF(fitted) * ratio).toSize(),
                                                Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    QImage source;
    QPixmap cache;
    int radius;
    bool crop;
};

QIcon RoundedGameIcon(const QIcon& icon) {
    if (icon.isNull()) {
        return Spotlight::Icon(u"game-controller", Color::TextSecondary(), 24);
    }
    QIcon rounded;
    for (const qreal ratio : {1.0, 1.5, 2.0}) {
        const int size = qRound(24 * ratio);
        QPixmap square(size, size);
        square.setDevicePixelRatio(ratio);
        square.fill(Qt::transparent);
        {
            QPainter painter(&square);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            Spotlight::PaintWholeIcon(painter, QRectF(0, 0, 24, 24),
                                      icon.pixmap(QSize(24, 24), ratio), 3);
        }
        rounded.addPixmap(square);
    }
    return rounded;
}

class PillCombo final : public QComboBox {
public:
    explicit PillCombo(QWidget* parent, int max_width = 320)
        : QComboBox(parent), max_width(max_width) {
        setFont(Spotlight::Font(13, QFont::DemiBold));
        setFixedHeight(36);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setMaxVisibleItems(12);
        connect(this, &QComboBox::currentIndexChanged, this, [this] { updateGeometry(); });
    }

    QSize sizeHint() const override {
        const int width = LeftPadding() + IconSpace() +
                          fontMetrics().horizontalAdvance(currentText()) + 2 + 6 + 16 + 12;
        return {qMin(width, max_width), 36};
    }

    QSize minimumSizeHint() const override {
        return {qMin(sizeHint().width(), LeftPadding() + IconSpace() + 80), 36};
    }

    void showPopup() override {
        int widest = 0;
        for (int i = 0; i < count(); ++i) {
            widest = qMax(widest, fontMetrics().horizontalAdvance(itemText(i)));
        }
        const int icons = iconSize().width() + 10;
        view()->setMinimumWidth(qMin(qMax(width(), widest + icons + 52), 480));
        QComboBox::showPopup();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse() && isEnabled();
        painter.setPen(hasFocus() ? QPen(Color::Accent(), 1) : Qt::NoPen);
        painter.setBrush(hovered ? Color::ControlHover() : Color::Fill());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
        painter.setOpacity(isEnabled() ? 1.0 : 0.45);
        int x = LeftPadding();
        if (const auto value = itemIcon(currentIndex()); !value.isNull()) {
            const int size = iconSize().height();
            value.paint(&painter, QRect(x, (height() - size) / 2, size, size));
            x += IconSpace();
        }
        const int caret = width() - 12 - 16;
        painter.drawPixmap(caret, 10, CachedIcon(u"caret-down", 16, Color::TextSecondary(),
                                                 devicePixelRatioF()));
        painter.setPen(Color::Text());
        painter.setFont(font());
        const QRect text_rect(x, 0, caret - 6 - x, height());
        painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(currentText(), Qt::ElideRight, text_rect.width()));
    }

    void enterEvent(QEnterEvent* event) override {
        update();
        QComboBox::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        update();
        QComboBox::leaveEvent(event);
    }

private:
    int LeftPadding() const {
        return itemIcon(currentIndex()).isNull() ? 14 : iconSize().height() >= 24 ? 6 : 12;
    }

    int IconSpace() const {
        return itemIcon(currentIndex()).isNull() ? 0 : iconSize().height() + (iconSize().height() >= 24 ? 8 : 7);
    }

    int max_width;
};

class PreviewImage final : public QWidget {
public:
    explicit PreviewImage(QWidget* parent, int radius = 20) : QWidget(parent), picture(radius, true) {
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }

    QSize sizeHint() const override {
        return {600, heightForWidth(600)};
    }

    int heightForWidth(int width) const override {
        return qRound(width * 9.0 / 16);
    }

    void SetImage(QImage value) {
        picture.Set(std::move(value));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        picture.Paint(painter, rect());
    }

private:
    Picture picture;
};

void SetButtonKind(QAbstractButton* button, const QString& kind) {
    button->setProperty("kind", kind);
    button->style()->unpolish(button);
    button->style()->polish(button);
}

class Chip final : public QLabel {
public:
    explicit Chip(const QString& text, const QColor& fill_, const QColor& color, QWidget* parent)
        : QLabel(text, parent), fill(fill_) {
        setTextFormat(Qt::PlainText);
        setFont(Spotlight::Font(11, QFont::DemiBold));
        setAlignment(Qt::AlignCenter);
        setContentsMargins(9, 0, 9, 0);
        setFixedHeight(22);
        Spotlight::SetTextColor(this, color);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
        }
        QLabel::paintEvent(event);
    }

private:
    QColor fill;
};

class PreviewButton final : public QAbstractButton {
public:
    explicit PreviewButton(QWidget* parent) : QAbstractButton(parent) {
        setFixedSize(76, 44);
        setCheckable(true);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
    }

    void SetImage(QImage value) {
        picture.Set(std::move(value));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (hasFocus() || isChecked() || underMouse()) {
            painter.setPen(QPen(hasFocus()   ? Color::Text()
                                : isChecked() ? Color::Accent()
                                              : Color::Stroke(),
                                2));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 9, 9);
        }
        picture.Paint(painter, rect().adjusted(2, 2, -2, -2));
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }

    void leaveEvent(QEvent*) override {
        update();
    }

private:
    Picture picture{7, true};
};

class ModCard final : public QAbstractButton {
public:
    ModCard(const QString& title, QString subtitle, QString source, QString verification,
            QWidget* parent)
        : QAbstractButton(parent), subtitle(std::move(subtitle)), source(std::move(source)),
          verification(std::move(verification)) {
        setText(title);
        setAccessibleName(title);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(160);
    }

    QSize sizeHint() const override {
        return {290, heightForWidth(290)};
    }

    int heightForWidth(int width) const override {
        return qRound((width - 16) * 9.0 / 16) + 94;
    }

    void SetImage(QImage value) {
        picture.Set(std::move(value));
        update();
    }

    const QImage& Image() const {
        return picture.Source();
    }

    void SetInstalled(QString label, bool enabled) {
        installed = std::move(label);
        installed_enabled = enabled;
        setAccessibleDescription(
            QStringList{subtitle, source, installed, verification}.join(u'\n'));
        update();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            click();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }

    void leaveEvent(QEvent*) override {
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(isEnabled() ? 1.0 : 0.5);
        painter.setPen(hasFocus()     ? Color::Accent()
                       : underMouse() ? Color::Stroke()
                                      : Color::StrokeSubtle());
        painter.setBrush(underMouse() || isDown() ? Color::Fill() : Color::FillFaint());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 20, 20);
        const QRect illustration(8, 8, width() - 16, height() - 94);
        picture.Paint(painter, illustration);
        const int title_top = illustration.y() + illustration.height() + 10;
        const int content_width = width() - 28;
        const auto mark = [&](QStringView icon, const QString& text, const QColor& color, int size,
                              int weight, int top, int line, bool right, int available, int gap) {
            painter.setFont(Spotlight::Font(size, weight));
            const auto label =
                painter.fontMetrics().elidedText(text, Qt::ElideRight, available - 14 - gap);
            const int text_width = painter.fontMetrics().horizontalAdvance(label);
            const int left = right ? width() - 14 - text_width - 14 - gap : 14;
            painter.drawPixmap(left, top + (line - 14) / 2,
                               CachedIcon(icon, 14, color, devicePixelRatioF()));
            painter.setPen(color);
            painter.drawText(QRect(left + 14 + gap, top, text_width, line), Qt::AlignVCenter,
                             label);
            return text_width + 14 + gap;
        };
        const int badge_width =
            installed.isEmpty()
                ? 0
                : mark(u"check-circle-fill", installed,
                       installed_enabled ? Color::Success() : Color::TextSecondary(), 11,
                       QFont::DemiBold, title_top, 20, true, content_width / 2, 4) +
                      8;
        painter.setFont(Spotlight::Font(15, QFont::DemiBold));
        painter.setPen(Color::Text());
        const QRect title_rect(14, title_top, content_width - badge_width, 20);
        painter.drawText(
            title_rect, Qt::AlignVCenter,
            painter.fontMetrics().elidedText(text(), Qt::ElideRight, title_rect.width()));
        painter.setFont(Spotlight::Font(12, QFont::Normal));
        painter.setPen(Color::TextSecondary());
        painter.drawText(QRect(14, title_top + 24, content_width, 16), Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(subtitle, Qt::ElideRight, content_width));
        const int verification_width =
            verification.isEmpty()
                ? 0
                : mark(u"warning-circle", verification, Color::Warning(), 12, QFont::DemiBold,
                       title_top + 46, 16, true, content_width * 3 / 5, 4) +
                      8;
        mark(u"globe", source, Color::TextSecondary(), 12, QFont::Medium, title_top + 46, 16,
             false, content_width - verification_width, 5);
    }

private:
    QString subtitle;
    QString source;
    QString verification;
    QString installed;
    Picture picture{14, true};
    bool installed_enabled{};
};

class Dialog final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(ModStore::Dialog)

public:
    Dialog(QWidget* host, DialogContext ctx)
        : QWidget(host), context(std::move(ctx)), client(context.base_url, this) {
        setObjectName(QStringLiteral("spotlightModStore"));
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFont(Spotlight::Font(13, QFont::Normal));
        setStyleSheet(
            QStringLiteral(
                "QWidget#spotlightModStore QScrollArea { background: transparent; border: none; }"
                "QWidget#spotlightModStore QWidget#storeContents { background: transparent; }"
                "QLineEdit#storeSearch { background: %1; color: %2; border: 1px solid transparent;"
                " border-radius: 19px; min-height: 36px; max-height: 36px; padding: 0 12px 0 8px;"
                " selection-background-color: %3; }"
                "QLineEdit#storeSearch:hover { background: %4; }"
                "QLineEdit#storeSearch:focus { border-color: %7; }"
                "QComboBox { combobox-popup: 0; }"
                "QComboBox QAbstractItemView { background: %6; color: %2; border: 1px solid %7;"
                " border-radius: 12px; padding: 6px; outline: 0; }"
                "QComboBox QAbstractItemView::item { min-height: 32px; padding: 0 10px;"
                " border: none; border-radius: 8px; }"
                "QComboBox QAbstractItemView::item:selected,"
                " QComboBox QAbstractItemView::item:hover { background: %4; color: %2; }"
                "QToolButton#storeDescription { background: transparent; border: 1px solid "
                "transparent; border-radius: 4px; color: %8; padding: 0; }"
                "QToolButton#storeDescription:focus { border-color: %5; }"
                "QToolButton#storeDescription:hover { color: %2; }"
                "QPushButton#storeRowTitle { background: transparent; border: 1px solid "
                "transparent; border-radius: 4px; color: %2; padding: 0; text-align: left;"
                " min-height: 0; }"
                "QPushButton#storeRowTitle:focus { border-color: %5; }"
                "QPushButton#storeRowTitle:hover { color: %9; }"
                "QFrame#storePanel { background: %10; border: 1px solid %12; border-radius: 18px; }"
                "QWidget#storeRow { background: transparent; }"
                "QWidget#storeRow:hover { background: %13; }"
                "QWidget#storeRow[first=\"true\"] { border-top-left-radius: 17px;"
                " border-top-right-radius: 17px; }"
                "QWidget#storeRow[last=\"true\"] { border-bottom-left-radius: 17px;"
                " border-bottom-right-radius: 17px; }"
                "QFrame#storeRule { background: %11; border: none; }"
                "QProgressBar#storeProgress { border: none; background: %4; border-radius: 3px; }"
                "QProgressBar#storeProgress::chunk { background: %5; border-radius: 3px; }")
                .arg(CssColor(Color::Fill()), CssColor(Color::Text()),
                     CssColor(QColor(0xff, 0x6b, 0x95, 0x73)), CssColor(Color::ControlHover()),
                     CssColor(Color::Accent()), CssColor(Color::Panel()), CssColor(Color::Stroke()),
                     CssColor(Color::TextSecondary()), CssColor(Color::AccentLight()),
                     CssColor(Color::FillFaint()), CssColor(Rule()),
                     CssColor(Color::StrokeSubtle()), CssColor(QColor(0xff, 0xff, 0xff, 0x0f))));
        auto palette = this->palette();
        palette.setColor(QPalette::Window, Color::PanelDeep());
        palette.setColor(QPalette::WindowText, Color::Text());
        palette.setColor(QPalette::Base, Color::PanelDeep());
        palette.setColor(QPalette::Text, Color::Text());
        palette.setColor(QPalette::PlaceholderText, Color::TextSecondary());
        palette.setColor(QPalette::Highlight, Color::Accent());
        palette.setColor(QPalette::HighlightedText, Color::OnAccent());
        setPalette(palette);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);
        auto* header = new Spotlight::WindowDragBar(this);
        header->setObjectName(QStringLiteral("storeContents"));
        header->setFixedHeight(64);
        auto* header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(20, 0, 20, 0);
        header_layout->setSpacing(10);
        back = new CircleButton(QStringLiteral("caret-left"), 34, header);
        back->setAccessibleName(tr("Close Mod Store"));
        back->setToolTip(tr("Close Mod Store"));
        auto* heading = PlainLabel(header);
        heading->setFont(Spotlight::DisplayFont(22));
        heading->setText(tr("Mod Store"));
        game_picker = new PillCombo(header, 290);
        game_picker->setAccessibleName(tr("Game"));
        game_picker->setIconSize({24, 24});
        installed_mode = new QAction(Spotlight::Icon(u"archive", Color::TextSecondary(), 16),
                                     tr("Installed mods"), this);
        installed_mode->setCheckable(true);
        refresh = new QAction(Spotlight::Icon(u"arrow-clockwise", Color::TextSecondary(), 16),
                              tr("Refresh catalog"), this);
        refresh->setShortcut(QKeySequence::Refresh);
        refresh->setShortcutContext(Qt::WidgetShortcut);
        store_menu = new CircleButton(QStringLiteral("dots-three"), 34, header);
        store_menu->setAccessibleName(tr("More Mod Store actions"));
        store_menu->setToolTip(tr("Installed mods and catalog refresh"));
        auto* menu = new Spotlight::Menu(store_menu);
        menu->addAction(installed_mode);
        menu->addAction(refresh);
        connect(store_menu, &QAbstractButton::clicked, this, [this, menu] {
            menu->popup(store_menu->mapToGlobal(
                QPoint(store_menu->width() - menu->sizeHint().width(), store_menu->height() + 5)));
        });
        search = new QLineEdit(header);
        search->setObjectName(QStringLiteral("storeSearch"));
        search->setAccessibleName(tr("Search mods"));
        search->setPlaceholderText(tr("Name, author or description"));
        search->setFont(Spotlight::Font(13, QFont::Normal));
        search->setClearButtonEnabled(true);
        search->setMaxLength(128);
        search->setMinimumWidth(170);
        search->setMaximumWidth(300);
        search->addAction(Spotlight::Icon(u"magnifying-glass", Color::TextSecondary(), 16),
                          QLineEdit::LeadingPosition);
        header_layout->addWidget(back);
        header_layout->addWidget(heading);
        header_layout->addSpacing(6);
        header_layout->addWidget(game_picker);
        header_layout->addStretch(1);
        header_layout->addWidget(search, 1);
        header_layout->addWidget(store_menu);
        if (host->windowFlags().testFlag(Qt::FramelessWindowHint)) {
            header->AddWindowControls(header_layout);
        }
        root->addWidget(header);

        pages = new QStackedWidget(this);
        connect(pages, &QStackedWidget::currentChanged, this,
                [this] { Spotlight::FadeIn(pages->currentWidget()); });
        pages->setObjectName(QStringLiteral("storeContents"));
        BuildBrowsePage();
        pages->addWidget(installed_page = new QScrollArea);
        BuildDetailPage();
        root->addWidget(pages, 1);
        status = new Spotlight::Notice(this);
        status->hide();
        auto* footer = new QVBoxLayout;
        footer->setContentsMargins(32, 0, 32, 16);
        footer->addWidget(status);
        root->addLayout(footer);

        debounce.setSingleShot(true);
        debounce.setInterval(240);
        connect(&debounce, &QTimer::timeout, this, [this] { ReloadMods(); });
        connect(search, &QLineEdit::textChanged, this, [this] {
            more->hide();
            debounce.start();
        });
        const auto filters_changed = [this] { ReloadMods(); };
        connect(type, &SegmentedControl::CurrentChanged, this, filters_changed);
        connect(sort, &QComboBox::currentIndexChanged, this, filters_changed);
        connect(refresh, &QAction::triggered, this, [this] {
            identities.clear();
            if (!installed_mode->isChecked()) {
                LoadGames();
            }
            ReloadMods();
        });
        connect(installed_mode, &QAction::toggled, this, [this](bool installed) {
            if (installed && installed_list == nullptr) {
                BuildInstalledPage();
            }
            ReloadMods();
        });
        connect(game_picker, &QComboBox::currentIndexChanged, this,
                [this] { SelectGame(game_picker->currentData().toString()); });
        connect(more, &QAbstractButton::clicked, this, [this] {
            if (next && !debounce.isActive() && !reload_after_install) {
                FetchMods(*next, list_epoch);
            }
        });
        connect(back, &QAbstractButton::clicked, this, [this] { NavigateBack(); });

        title = context.initial_title_id;
        PopulateGames();
        ClearDetail();
        QTimer::singleShot(0, this, [this] {
            LoadGames();
            ReloadMods();
        });
    }

    bool Changed() const {
        return changed;
    }

    void Exec() {
        auto* host = parentWidget();
        const QPointer<QWidget> previous_focus = host->focusWidget();
        host->installEventFilter(this);
        qApp->installEventFilter(this);
        setGeometry(host->rect());
        show();
        raise();
        search->setFocus(Qt::OtherFocusReason);
        Spotlight::FadeIn(this);
        loop.exec();
        qApp->removeEventFilter(this);
        host->removeEventFilter(this);
        hide();
        if (previous_focus) {
            previous_focus->setFocus(Qt::OtherFocusReason);
        }
        if (close_host) {
            QTimer::singleShot(0, host, [host] { host->close(); });
        }
    }

    void reject() {
        if (applying) {
            return;
        }
        if (busy) {
            CancelDownload();
            return;
        }
        loop.quit();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.fillRect(event->rect(), Color::PanelDeep());
        if (backdrop.size() != size() * devicePixelRatioF()) {
            backdrop = RenderBackdrop();
        }
        painter.drawPixmap(0, 0, backdrop);
    }

    QPixmap RenderBackdrop() const {
        const qreal ratio = devicePixelRatioF();
        QPixmap result(size() * ratio);
        result.setDevicePixelRatio(ratio);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto game = std::ranges::find(context.library_games, title, &LibraryGame::title_id);
        if (game != context.library_games.end() && !game->icon.isNull()) {
            const auto art = game->icon.pixmap(256)
                                 .toImage()
                                 .scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 .scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            const QSizeF area = QSizeF(art.size()).scaled(size(), Qt::KeepAspectRatioByExpanding);
            painter.setOpacity(0.18);
            painter.drawImage(QRectF(QPointF(width() - area.width(), height() - area.height()) / 2,
                                     area),
                              art);
            painter.setOpacity(1.0);
        }
        auto wash = Color::Info();
        wash.setAlpha(0x14);
        QLinearGradient gradient(0, 0, 0, height());
        gradient.setStops({{0.0, wash}, {0.55, Qt::transparent}});
        painter.fillRect(rect(), gradient);
        return result;
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::Resize:
            if (watched == parentWidget()) {
                setGeometry(parentWidget()->rect());
            } else if (watched == browse_scroll->viewport()) {
                FitCards();
            }
            break;
        case QEvent::Close:
            if (watched == parentWidget()) {
                event->ignore();
                if (!applying) {
                    close_host = true;
                    if (busy) {
                        CancelDownload();
                    }
                    loop.quit();
                }
                return true;
            }
            break;
        case QEvent::ShortcutOverride:
            if (auto* widget = qobject_cast<QWidget*>(watched);
                widget && (widget == this || isAncestorOf(widget))) {
                event->accept();
                return true;
            }
            break;
        case QEvent::Shortcut:
            if (auto* widget = qobject_cast<QWidget*>(watched)) {
                if (widget->window() == window() && widget != this && !isAncestorOf(widget)) {
                    return true;
                }
            } else if (qobject_cast<QAction*>(watched) != nullptr) {
                return true;
            }
            break;
        default:
            break;
        }
        return QWidget::eventFilter(watched, event);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->matches(QKeySequence::Find)) {
            if (search->isEnabled()) {
                search->setFocus(Qt::ShortcutFocusReason);
                search->selectAll();
            }
        } else if (event->matches(QKeySequence::Refresh)) {
            if (refresh->isEnabled()) {
                refresh->trigger();
            }
        } else if (event->key() == Qt::Key_Escape || event->matches(QKeySequence::Back)) {
            if (back->isEnabled() || busy) {
                NavigateBack();
            }
        }
        event->accept();
    }

    void keyReleaseEvent(QKeyEvent* event) override {
        event->accept();
    }

    bool focusNextPrevChild(bool next_child) override {
        QWidget* candidate = focusWidget() ? focusWidget() : this;
        for (int i = 0; i < 4096; ++i) {
            candidate = next_child ? candidate->nextInFocusChain() : candidate->previousInFocusChain();
            if (candidate == this) {
                continue;
            }
            if (isAncestorOf(candidate) && candidate->isVisibleTo(this) && candidate->isEnabled() &&
                (candidate->focusPolicy() & Qt::TabFocus)) {
                candidate->setFocus(next_child ? Qt::TabFocusReason : Qt::BacktabFocusReason);
                return true;
            }
        }
        return true;
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
    }

private:
    void NavigateBack() {
        if (pages->currentWidget() != detail_page) {
            reject();
            return;
        }
        if (busy) {
            CancelDownload();
            return;
        }
        const auto id = local_id;
        const auto view_title = local_title;
        ClearDetail();
        RestoreListPage();
        if (installed_mode->isChecked()) {
            ShowInstalled();
            FocusInstalledRow(id, view_title);
        } else if (auto* card = cards.value(id)) {
            card->setFocus(Qt::OtherFocusReason);
        }
    }

    static QScrollArea* ScrollPage(QWidget* contents, QScrollArea* scroll = nullptr) {
        if (scroll == nullptr) {
            scroll = new QScrollArea;
        }
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(contents);
        scroll->viewport()->setObjectName(QStringLiteral("storeContents"));
        scroll->viewport()->setAutoFillBackground(false);
        contents->setAutoFillBackground(false);
        contents->setObjectName(QStringLiteral("storeContents"));
        return scroll;
    }

    void BuildBrowsePage() {
        browse_page = new QWidget(pages);
        browse_page->setObjectName(QStringLiteral("storeContents"));
        auto* body = new QVBoxLayout(browse_page);
        body->setContentsMargins(32, 6, 32, 0);
        body->setSpacing(16);
        auto* tools = new QHBoxLayout;
        tools->setSpacing(12);
        QStringList categories{tr("All types")};
        for (const auto category : Categories) {
            categories.append(TypeName(category));
        }
        type = new SegmentedControl(categories, SegmentedControl::Kind::Accent, 38, browse_page);
        type->setAccessibleName(tr("Mod category"));
        count = PlainLabel(browse_page, false, Color::TextSecondary(), 13, QFont::Medium);
        sort = new PillCombo(browse_page);
        sort->setAccessibleName(tr("Sort mods"));
        sort->setIconSize({16, 16});
        const auto sort_icon = Spotlight::Icon(u"sort-ascending", Color::Text(), 16);
        sort->addItem(sort_icon, tr("Recently updated"), static_cast<int>(Sort::Updated));
        sort->addItem(sort_icon, tr("Name A–Z"), static_cast<int>(Sort::Name));
        tools->addWidget(type);
        tools->addStretch();
        tools->addWidget(count);
        tools->addWidget(sort);
        body->addLayout(tools);

        auto* contents = new QWidget;
        auto* list = new QVBoxLayout(contents);
        list->setContentsMargins(0, 0, 0, 24);
        list->setSpacing(16);
        browse_message = PlainLabel(contents, true, Color::TextSecondary(), 14);
        browse_message->setAlignment(Qt::AlignCenter);
        browse_message->setMinimumHeight(140);
        list->addWidget(browse_message);
        auto* grid_host = new QWidget(contents);
        grid_host->setObjectName(QStringLiteral("storeContents"));
        card_grid = new QGridLayout(grid_host);
        card_grid->setContentsMargins(0, 0, 0, 0);
        card_grid->setSpacing(18);
        for (int i = 0; i < CardColumns; ++i) {
            card_grid->setColumnStretch(i, 1);
        }
        list->addWidget(grid_host);
        more = new PillButton(tr("Load more"), {}, PillButton::Kind::Control, 38, contents);
        more->hide();
        list->addWidget(more, 0, Qt::AlignHCenter);
        list->addStretch();
        browse_scroll = ScrollPage(contents);
        browse_scroll->viewport()->installEventFilter(this);
        body->addWidget(browse_scroll, 1);
        pages->insertWidget(0, browse_page);
    }

    void BuildInstalledPage() {
        auto* contents = new QWidget;
        auto* body = new QVBoxLayout(contents);
        body->setContentsMargins(40, 22, 40, 24);
        body->setSpacing(0);
        auto* heading = PlainLabel(contents);
        heading->setText(tr("Installed mods"));
        heading->setFont(Spotlight::DisplayFont(25));
        auto* subtitle = PlainLabel(contents, true, Color::TextSecondary(), 13);
        subtitle->setText(tr("Turn mods on or off. Changes apply on the next launch."));
        installed_count = PlainLabel(contents, false, Color::TextSecondary(), 13, QFont::DemiBold);
        auto* panel = new QFrame(contents);
        panel->setObjectName(QStringLiteral("storePanel"));
        installed_list = new QVBoxLayout(panel);
        installed_list->setContentsMargins(1, 1, 1, 1);
        installed_list->setSpacing(0);
        auto* return_to_store =
            new PillButton(tr("Browse the Mod Store"), QStringLiteral("storefront"),
                           PillButton::Kind::Accent, 36, contents);
        connect(return_to_store, &QAbstractButton::clicked, this,
                [this] { installed_mode->setChecked(false); });
        auto* actions = new QHBoxLayout;
        actions->setSpacing(10);
        actions->addWidget(return_to_store);
        actions->addStretch();
        auto* notice = PlainLabel(contents, false, Color::TextSecondary(), 12);
        notice->setText(tr("Installed mods are available offline."));
        actions->addWidget(notice);
        body->addWidget(heading);
        body->addSpacing(4);
        body->addWidget(subtitle);
        body->addSpacing(16);
        body->addWidget(installed_count);
        body->addSpacing(16);
        body->addWidget(panel);
        body->addSpacing(16);
        body->addLayout(actions);
        body->addStretch();
        ScrollPage(contents, installed_page);
    }

    void BuildDetailPage() {
        auto* contents = new QWidget;
        auto* body = new QHBoxLayout(contents);
        body->setContentsMargins(40, 8, 84, 18);
        body->setSpacing(36);
        gallery = new QWidget(contents);
        gallery->setObjectName(QStringLiteral("storeContents"));
        gallery->setMaximumWidth(720);
        auto* media = new QVBoxLayout(gallery);
        media->setContentsMargins(0, 0, 0, 0);
        media->setSpacing(12);
        preview = new PreviewImage(gallery);
        preview->setAccessibleName(tr("Mod preview"));
        media->addWidget(preview);

        auto* strip = new QWidget(gallery);
        auto* strip_layout = new QHBoxLayout(strip);
        strip_layout->setSpacing(8);
        strip_layout->setContentsMargins(0, 0, 0, 0);
        thumbnails = strip_layout;
        preview_strip = ScrollPage(strip);
        preview_strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        preview_strip->setFixedHeight(44);
        auto* strip_row = new QHBoxLayout;
        strip_row->setSpacing(8);
        strip_row->addWidget(preview_strip, 1);
        preview_count = PlainLabel(gallery, false, Color::TextSecondary(), 12, QFont::Medium);
        preview_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        strip_row->addWidget(preview_count);
        media->addLayout(strip_row);
        media->addStretch();
        body->addWidget(gallery, 600);

        auto* pane = new QWidget(contents);
        pane->setObjectName(QStringLiteral("storeContents"));
        pane->setMaximumWidth(620);
        auto* detail = new QVBoxLayout(pane);
        detail->setContentsMargins(0, 0, 0, 0);
        detail->setSpacing(8);
        name = PlainLabel(pane, true);
        name->setFont(Spotlight::DisplayFont(24));
        byline = PlainLabel(pane, true, Color::TextSecondary(), 13, QFont::Medium);
        source_info = PlainLabel(pane, false, Color::TextSecondary(), 12, QFont::Medium);
        verification = PlainLabel(pane, false, Color::Warning(), 12, QFont::DemiBold);
        auto* source_icon = PlainLabel(pane);
        source_icon->setPixmap(
            Spotlight::IconPixmap(u"globe", 14, Color::TextSecondary(), devicePixelRatioF()));
        verification_icon = PlainLabel(pane);
        source_info_row = new QWidget(pane);
        source_info_row->setObjectName(QStringLiteral("storeContents"));
        auto* tags = new QHBoxLayout(source_info_row);
        tags->setContentsMargins(0, 2, 0, 0);
        tags->setSpacing(5);
        tags->addWidget(source_icon);
        tags->addWidget(source_info);
        tags->addSpacing(9);
        tags->addWidget(verification_icon);
        tags->addSpacing(-1);
        tags->addWidget(verification);
        tags->addStretch();
        detail->addWidget(name);
        detail->addWidget(byline);
        detail->addWidget(source_info_row);

        target_picker = new PillCombo(pane, 520);
        target_picker->setAccessibleName(tr("Target game"));
        target_picker->setToolTip(tr("Game to install this mod for"));
        release = new PillCombo(pane, 520);
        release->setAccessibleName(tr("Release or file"));
        auto* pickers = new QHBoxLayout;
        pickers->setContentsMargins(0, 6, 0, 0);
        pickers->setSpacing(8);
        pickers->addWidget(target_picker);
        pickers->addWidget(release);
        pickers->addStretch();
        detail->addLayout(pickers);

        action_row = new QWidget(pane);
        action_row->setObjectName(QStringLiteral("storeContents"));
        mod_actions = new QHBoxLayout(action_row);
        mod_actions->setContentsMargins(0, 6, 0, 0);
        mod_actions->setSpacing(8);
        install = new PillButton(tr("Install"), QStringLiteral("download-simple"),
                                 PillButton::Kind::Accent, 36, action_row);
        enable = new PillButton({}, {}, PillButton::Kind::Control, 36, action_row);
        uninstall = new CircleButton(QStringLiteral("trash"), 36, action_row);
        uninstall->setAccessibleName(tr("Uninstall"));
        uninstall->setToolTip(tr("Uninstall"));
        source = new PillButton({}, QStringLiteral("globe"), PillButton::Kind::Control, 36,
                                action_row);
        mod_actions->addWidget(install);
        mod_actions->addWidget(enable);
        mod_actions->addWidget(uninstall);
        mod_actions->addWidget(source);
        mod_actions->addStretch();
        detail->addWidget(action_row);

        progress_panel = new QWidget(pane);
        progress_panel->setObjectName(QStringLiteral("storeContents"));
        auto* progress_layout = new QVBoxLayout(progress_panel);
        progress_layout->setContentsMargins(0, 6, 0, 0);
        progress_layout->setSpacing(6);
        progress_title = PlainLabel(progress_panel, true, Color::Text(), 13, QFont::DemiBold);
        progress_detail = PlainLabel(progress_panel, true, Color::TextSecondary(), 11);
        progress_note = PlainLabel(progress_panel, true, Color::Warning(), 11, QFont::DemiBold);
        progress_note->setText(tr("Not verified by the catalog"));
        progress_bar = new QProgressBar(progress_panel);
        progress_bar->setObjectName(QStringLiteral("storeProgress"));
        progress_bar->setTextVisible(false);
        progress_bar->setFixedSize(300, 6);
        cancel_button = new PillButton(tr("Cancel"), {}, PillButton::Kind::Control, 30,
                                       progress_panel);
        progress_layout->addWidget(progress_title);
        progress_layout->addWidget(progress_detail);
        progress_layout->addWidget(progress_note);
        progress_layout->addWidget(progress_bar);
        progress_layout->addSpacing(4);
        progress_layout->addWidget(cancel_button, 0, Qt::AlignLeft);
        progress_panel->hide();
        detail->addWidget(progress_panel);
        connect(cancel_button, &QAbstractButton::clicked, this, [this] { CancelDownload(); });

        hint = PlainLabel(pane, true, Color::TextSecondary(), 12);
        detail->addWidget(hint);

        description = PlainLabel(pane, true);
        description->setTextInteractionFlags(Qt::TextSelectableByMouse);
        description->setContentsMargins(0, 4, 0, 0);
        description_toggle = new QToolButton(pane);
        description_toggle->setObjectName(QStringLiteral("storeDescription"));
        description_toggle->setFont(Spotlight::Font(12, QFont::DemiBold));
        description_toggle->setCheckable(true);
        description_toggle->setCursor(Qt::PointingHandCursor);
        description_toggle->setFocusPolicy(Qt::TabFocus);
        detail->addWidget(description);
        detail->addWidget(description_toggle, 0, Qt::AlignLeft);

        info = new QWidget(pane);
        info->setObjectName(QStringLiteral("storeContents"));
        auto* info_layout = new QGridLayout(info);
        info_layout->setContentsMargins(0, 0, 0, 0);
        info_layout->setHorizontalSpacing(10);
        info_layout->setVerticalSpacing(7);
        info_layout->setColumnMinimumWidth(0, 140);
        info_layout->setColumnStretch(1, 1);
        auto* rule = new QFrame(info);
        rule->setObjectName(QStringLiteral("storeRule"));
        rule->setFixedHeight(1);
        info_layout->addWidget(rule, 0, 0, 1, 2);
        info_layout->setRowMinimumHeight(1, 1);
        for (size_t i = 0; i < info_rows.size(); ++i) {
            auto& [key, value] = info_rows[i];
            key = PlainLabel(info, false, Color::TextSecondary(), 12, QFont::Medium);
            value = PlainLabel(info, true, Color::Text(), 12, QFont::DemiBold);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            const auto row = static_cast<int>(i) + 2;
            info_layout->addWidget(key, row, 0, Qt::AlignTop);
            info_layout->addWidget(value, row, 1, Qt::AlignTop);
        }
        detail->addSpacing(4);
        detail->addWidget(info);
        detail->addStretch();
        body->addWidget(pane, 520);
        body->addStretch(0);
        detail_page = ScrollPage(contents);
        pages->addWidget(detail_page);

        connect(release, &QComboBox::currentIndexChanged, this, [this] { UpdateOffer(); });
        connect(target_picker, &QComboBox::currentIndexChanged, this, [this] {
            local_title = target_picker->currentData().toString();
            ReadLocal();
            BuildOffers();
        });
        connect(description_toggle, &QToolButton::toggled, this, [this](bool expanded) {
            if (selected) {
                description_toggle->setText(expanded ? tr("Show less")
                                                     : tr("Show full description"));
                description->setText(expanded ? selected->description : description_excerpt);
            }
        });
        connect(source, &QAbstractButton::clicked, this, [this] { OpenSource(); });
        connect(install, &QAbstractButton::clicked, this, [this] { StartInstall(); });
        connect(enable, &QAbstractButton::clicked, this, [this] { ToggleEnabled(); });
        connect(uninstall, &QAbstractButton::clicked, this, [this] { RemoveInstalled(); });
    }

    static QString TypeName(ModType value) {
        switch (value) {
        case ModType::Graphics:
            return tr("Graphics");
        case ModType::Fps:
            return tr("FPS");
        case ModType::Cheats:
            return tr("Cheats");
        case ModType::Translation:
            return tr("Translation");
        case ModType::Gameplay:
            return tr("Gameplay");
        }
        return {};
    }

    static QString CompatibilityName(Compatibility value) {
        switch (value) {
        case Compatibility::Match:
            return tr("Match");
        case Compatibility::Unknown:
            return tr("Unverified");
        case Compatibility::Mismatch:
            return tr("Mismatch");
        }
        return {};
    }

    void Status(const QString& text,
                Spotlight::Notice::Tone tone = Spotlight::Notice::Tone::Neutral) {
        status->SetTone(tone);
        status->setText(text);
        status->setVisible(!text.isEmpty());
    }

    GameIdentity Identity(const QString& title_id) {
        const auto found = identities.constFind(title_id);
        if (found != identities.cend()) {
            return found.value();
        }
        const auto identity = !title_id.isEmpty() && context.game_identity
                                  ? context.game_identity(title_id)
                                  : GameIdentity{};
        identities.insert(title_id, identity);
        return identity;
    }

    QString GameName(const QString& id) const {
        const auto local_game =
            std::ranges::find(context.library_games, id, &LibraryGame::title_id);
        if (local_game != context.library_games.end()) {
            return local_game->name;
        }
        const auto game = std::ranges::find(games, id, &Game::title_id);
        return game == games.end() ? id : game->name;
    }

    void PopulateGames() {
        const QSignalBlocker block_picker(game_picker);
        game_picker->clear();
        game_picker->addItem(Spotlight::Icon(u"game-controller", Color::TextSecondary(), 24),
                             tr("All games"), QString{});
        for (const auto& game : context.library_games) {
            if (game.title_id.isEmpty() || game_picker->findData(game.title_id) >= 0) {
                continue;
            }
            game_picker->addItem(RoundedGameIcon(game.icon), game.name, game.title_id);
        }
        for (const auto& game : games) {
            int index = game_picker->findData(game.title_id);
            if (index < 0) {
                game_picker->addItem(game.name, game.title_id);
                index = game_picker->count() - 1;
            }
            game_picker->setItemData(index, tr("%n mod(s)", nullptr, game.mod_count),
                                     Qt::ToolTipRole);
        }
        if (!title.isEmpty() && game_picker->findData(title) < 0) {
            game_picker->addItem(title, title);
        }
        game_picker->setCurrentIndex(game_picker->findData(title));
        UpdateGamePicker();
    }

    void UpdateGamePicker() {
        const auto game_name = game_picker->currentText();
        const auto mods = game_picker->currentData(Qt::ToolTipRole).toString();
        game_picker->setToolTip(mods.isEmpty() ? game_name : tr("%1 · %2").arg(game_name, mods));
    }

    void LoadGames(int offset = 0) {
        if (offset == 0) {
            if (games_request) {
                games_request->Cancel();
            }
            ++game_epoch;
            incoming_games.clear();
        }
        const auto epoch = game_epoch;
        games_request = client.GetGames(offset, GamePageSize, [this, epoch, offset](auto response) {
            if (epoch != game_epoch) {
                return;
            }
            if (const auto* page = std::get_if<Page<Game>>(&response)) {
                incoming_games.append(page->items);
                if (page->next_offset && *page->next_offset > offset &&
                    incoming_games.size() < 1000) {
                    LoadGames(*page->next_offset);
                    return;
                }
                games = std::move(incoming_games);
                PopulateGames();
            }
        });
    }

    void SelectGame(const QString& id) {
        if (title == id) {
            return;
        }
        title = id;
        backdrop = {};
        update();
        UpdateGamePicker();
        ReloadMods();
    }

    void RestoreListPage() {
        pages->setCurrentWidget(installed_mode->isChecked() ? installed_page : browse_page);
        back->setAccessibleName(tr("Close Mod Store"));
        back->setToolTip(tr("Close Mod Store"));
    }

    void ReloadMods() {
        debounce.stop();
        ++list_epoch;
        if (list_request) {
            list_request->Cancel();
        }
        for (const auto& request : thumbnail_requests) {
            if (request) {
                request->Cancel();
            }
        }
        thumbnail_requests.clear();
        thumbnail_queue.clear();
        next.reset();
        more->hide();
        ClearDetail();
        RestoreListPage();
        Status({});
        if (installed_mode->isChecked()) {
            ShowInstalled();
            return;
        }
        qDeleteAll(cards);
        cards.clear();
        catalog.clear();
        installed_by_title.clear();
        browse_scroll->verticalScrollBar()->setValue(0);
        count->clear();
        const int category = type->CurrentIndex();
        list_filter = {title, search->text().trimmed(),
                       category > 0 ? std::optional{Categories[category - 1]} : std::nullopt,
                       static_cast<Sort>(sort->currentData().toInt())};
        FetchMods(0, list_epoch);
    }

    QString Byline(const Mod& mod, bool detail = false) const {
        const auto local_date = mod.updated_at.toLocalTime().date();
        const auto now = QDate::currentDate();
        const auto date = local_date == now ? tr("today")
                          : local_date == now.addDays(-1)
                              ? tr("yesterday")
                              : locale().toString(local_date, QLocale::ShortFormat);
        return detail ? tr("by %1 · %2 · updated %3").arg(mod.author, TypeName(mod.type), date)
                      : tr("by %1 · %2 · %3").arg(mod.author, TypeName(mod.type), date);
    }

    const QList<InstalledMod>& InstalledFor(const QString& id) {
        auto found = installed_by_title.constFind(id);
        if (found == installed_by_title.cend()) {
            found = installed_by_title.insert(id, ReadInstalledMods(context.mods_root, id));
        }
        return found.value();
    }

    bool IsEnabled(const QString& target, const QString& id) const {
        return !context.is_enabled || context.is_enabled(target, id);
    }

    void SetCardStatus(ModCard* card, const Mod& mod) {
        bool installed = false;
        for (const auto& target : mod.title_ids) {
            if (!title.isEmpty() && title != target) {
                continue;
            }
            const auto& mods = InstalledFor(target);
            if (std::ranges::find(mods, mod.id, &InstalledMod::id) != mods.cend()) {
                if (IsEnabled(target, mod.id)) {
                    card->SetInstalled(tr("Installed"), true);
                    return;
                }
                installed = true;
            }
        }
        card->SetInstalled(installed ? tr("Off") : QString{}, false);
    }

    void LoadCardThumbnails() {
        while (thumbnail_requests.size() < ThumbnailConcurrency && !thumbnail_queue.isEmpty()) {
            const auto id = thumbnail_queue.takeFirst();
            const auto epoch = list_epoch;
            thumbnail_requests.insert(
                id, client.GetPreview(catalog[id], [this, id, epoch](auto response) {
                    if (epoch != list_epoch) {
                        return;
                    }
                    thumbnail_requests.remove(id);
                    if (const auto* image = std::get_if<QImage>(&response)) {
                        cards[id]->SetImage(
                            image->scaled(600, 338, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    }
                    LoadCardThumbnails();
                }));
        }
    }

    void FetchMods(int offset, int epoch) {
        more->setEnabled(false);
        if (cards.isEmpty()) {
            browse_message->setText(tr("Loading mods…"));
            browse_message->show();
        } else {
            Status(tr("Loading mods…"));
        }
        list_request =
            client.GetMods(list_filter, offset, ModPageSize, [this, offset, epoch](auto response) {
                if (epoch != list_epoch) {
                    return;
                }
                if (const auto* error = std::get_if<Error>(&response)) {
                    const auto message =
                        tr("Could not load the catalog: %1. Retry with Refresh; installed mods "
                           "are still available.")
                            .arg(error->message);
                    if (cards.isEmpty()) {
                        browse_message->setText(message);
                        browse_message->show();
                    } else {
                        Status(message, Spotlight::Notice::Tone::Error);
                    }
                    more->setEnabled(true);
                    return;
                }
                auto page = std::get<Page<Mod>>(std::move(response));
                for (auto& mod : page.items) {
                    if (catalog.contains(mod.id)) {
                        continue;
                    }
                    const auto id = mod.id;
                    const auto position = static_cast<int>(cards.size());
                    auto* card =
                        new ModCard(mod.name, Byline(mod), SourceName(mod.source),
                                    mod.source == Source::GameBanana ? tr("Unverified") : QString{},
                                    browse_page);
                    card->setToolTip(mod.summary);
                    SetCardStatus(card, mod);
                    card_grid->addWidget(card, position / CardColumns, position % CardColumns);
                    cards.insert(id, card);
                    connect(card, &QAbstractButton::clicked, this, [this, id] { SelectMod(id); });
                    if (mod.preview_url.isValid() && !mod.preview_url.isEmpty()) {
                        thumbnail_queue.append(id);
                    }
                    catalog.insert(id, std::move(mod));
                }
                FitCards();
                LoadCardThumbnails();
                next = page.next_offset && *page.next_offset > offset ? page.next_offset
                                                                      : std::nullopt;
                more->setVisible(next.has_value() && !debounce.isActive() && !reload_after_install);
                more->setEnabled(true);
                count->setText(catalog.size() == 1
                                   ? tr("1 result")
                                   : tr("%n results", nullptr, static_cast<int>(catalog.size())));
                browse_message->setText(
                    tr("No matching mods. Change the game, type, or search filter."));
                browse_message->setVisible(catalog.isEmpty());
                Status({});
            });
    }

    void FitCards() {
        const int spacing = card_grid->horizontalSpacing();
        const int width =
            (browse_scroll->viewport()->width() - spacing * (CardColumns - 1)) / CardColumns;
        for (auto* card : std::as_const(cards)) {
            card->setFixedHeight(card->heightForWidth(width));
        }
    }

    void FocusInstalledRow(const QString& id, const QString& target) {
        if (auto* button = installed_rows.value(target + u'/' + id)) {
            button->setFocus();
        }
    }

    void ShowInstalled() {
        installed_rows.clear();
        while (auto* item = installed_list->takeAt(0)) {
            item->widget()->hide();
            item->widget()->deleteLater();
            delete item;
        }
        QStringList titles{title};
        if (title.isEmpty()) {
            titles = QDir(context.mods_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        }
        int displayed = 0;
        QWidget* last_row = nullptr;
        const auto query = search->text().trimmed();
        auto* panel = installed_list->parentWidget();
        for (const auto& target : titles) {
            for (const auto& installed : ReadInstalledMods(context.mods_root, target)) {
                if (!query.isEmpty() && !installed.name.contains(query, Qt::CaseInsensitive)) {
                    continue;
                }
                if (displayed++ > 0) {
                    auto* rule = new QFrame(panel);
                    rule->setObjectName(QStringLiteral("storeRule"));
                    rule->setFixedHeight(1);
                    auto* inset = new QWidget(panel);
                    inset->setObjectName(QStringLiteral("storeContents"));
                    auto* inset_layout = new QHBoxLayout(inset);
                    inset_layout->setContentsMargins(18, 0, 18, 0);
                    inset_layout->addWidget(rule);
                    installed_list->addWidget(inset);
                }
                auto* row = new QWidget(panel);
                row->setObjectName(QStringLiteral("storeRow"));
                row->setAttribute(Qt::WA_StyledBackground);
                row->setProperty("first", displayed == 1);
                last_row = row;
                auto* layout = new QHBoxLayout(row);
                layout->setContentsMargins(14, 12, 14, 12);
                layout->setSpacing(13);
                const auto id = installed.id;
                auto* art = new PreviewImage(row, 10);
                art->setFixedSize(96, 54);
                if (auto* card = cards.value(id); card && !card->Image().isNull()) {
                    art->SetImage(card->Image());
                } else if (!installed.thumbnail_path.isEmpty()) {
                    art->SetImage(QImage(installed.thumbnail_path));
                }
                layout->addWidget(art);
                auto* info_column = new QVBoxLayout;
                info_column->setSpacing(4);
                auto button_text = installed.name;
                button_text.replace(u'&', QStringLiteral("&&"));
                auto* heading = new QPushButton(button_text, row);
                heading->setObjectName(QStringLiteral("storeRowTitle"));
                heading->setAccessibleName(installed.name);
                heading->setFont(Spotlight::Font(15, QFont::DemiBold));
                heading->setAutoDefault(false);
                heading->setFocusPolicy(Qt::TabFocus);
                heading->setCursor(Qt::PointingHandCursor);
                heading->setFixedHeight(22);
                heading->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
                heading->setToolTip(tr("View details for %1").arg(installed.name));
                connect(heading, &QAbstractButton::clicked, this,
                        [this, id, target] { SelectMod(id, target); });
                auto* meta = new QHBoxLayout;
                meta->setSpacing(6);
                const auto entry = catalog.constFind(id);
                const bool known = entry != catalog.cend();
                const auto mod_source = known ? std::optional{entry->source} : installed.source;
                const bool external = mod_source == Source::GameBanana;
                const auto author = known ? entry->author : installed.author;
                QStringList parts;
                if (!author.isEmpty()) {
                    parts.append(tr("by %1").arg(author));
                }
                if (title.isEmpty()) {
                    parts.append(GameName(target));
                }
                if (!external) {
                    parts.append(tr("Release %1").arg(installed.version));
                }
                auto* subtitle = PlainLabel(row, false, Color::TextSecondary(), 12);
                subtitle->setText(parts.join(QStringLiteral(" · ")));
                meta->addWidget(subtitle);
                if (mod_source) {
                    meta->addWidget(
                        new Chip(SourceName(*mod_source), Color::Control(), Color::Text(), row));
                }
                if (known ? external : installed.verified == false) {
                    auto tint = Color::Warning();
                    tint.setAlpha(0x2e);
                    meta->addWidget(new Chip(tr("Unverified"), tint, Color::Warning(), row));
                }
                if (installed.update_available == true) {
                    meta->addWidget(
                        new Chip(tr("Update"), Color::Accent(), Color::OnAccent(), row));
                }
                if (!update_checked.contains(target + u'/' + id)) {
                    update_queue.append({target, id});
                }
                meta->addStretch();
                info_column->addWidget(heading);
                info_column->addLayout(meta);
                layout->addLayout(info_column, 1);
                auto* toggle = new ToggleSwitch(row);
                toggle->setAccessibleName(tr("Enable %1").arg(installed.name));
                toggle->setToolTip(tr("Enable or disable %1").arg(installed.name));
                toggle->setEnabled(static_cast<bool>(context.set_enabled));
                toggle->setChecked(IsEnabled(target, id));
                connect(toggle, &QAbstractButton::toggled, this,
                        [this, toggle, target, id](bool on) {
                            if (!SetEnabledFor(target, id, on)) {
                                const QSignalBlocker block(toggle);
                                toggle->setChecked(!on);
                            }
                        });
                layout->addWidget(toggle);
                auto* actions = new CircleButton(QStringLiteral("dots-three"), 34, row);
                actions->setAccessibleName(tr("Actions for %1").arg(installed.name));
                actions->setToolTip(actions->accessibleName());
                auto* menu = new Spotlight::Menu(actions);
                menu->addAction(Spotlight::Icon(u"info", Color::MenuInk(), 16), tr("View details"),
                                this, [this, id, target] { SelectMod(id, target); });
                menu->addAction(Spotlight::Icon(u"trash", Color::AccentLight(), 16),
                                tr("Uninstall"), this,
                                [this, installed, target] { RemoveLocal(installed, target); })
                    ->setProperty("spotlightDestructive", true);
                connect(actions, &QAbstractButton::clicked, menu, [actions, menu] {
                    menu->popup(actions->mapToGlobal(QPoint(
                        actions->width() - menu->sizeHint().width(), actions->height() + 5)));
                });
                layout->addWidget(actions);
                installed_rows.insert(target + u'/' + id, heading);
                installed_list->addWidget(row);
            }
        }
        if (last_row) {
            last_row->setProperty("last", true);
        }
        installed_count->setText(tr("From the Mod Store · %n", nullptr, displayed));
        CheckInstalledUpdates();
        if (displayed == 0) {
            auto* empty = PlainLabel(panel, true, Color::TextSecondary(), 14);
            empty->setMinimumHeight(94);
            empty->setAlignment(Qt::AlignCenter);
            empty->setText(tr("No installed store mods match this filter."));
            installed_list->addWidget(empty);
        }
    }

    void CheckInstalledUpdates() {
        while (update_requests < ThumbnailConcurrency && !update_queue.isEmpty()) {
            const auto [target, id] = update_queue.takeFirst();
            const auto key = target + u'/' + id;
            if (update_checked.contains(key)) {
                continue;
            }
            update_checked.insert(key);
            ++update_requests;
            client.GetMod(id, [this, target, id](auto response) {
                --update_requests;
                if (const auto* mod = std::get_if<Mod>(&response)) {
                    const auto installed = ReadInstalledMods(context.mods_root, target);
                    const auto it = std::ranges::find(installed, id, &InstalledMod::id);
                    if (it != installed.cend()) {
                        const bool update = HasUpdate(*mod, target, *it);
                        RecordUpdateCheck(context.mods_root, target, id, it->version, update);
                        update_changed |= it->update_available.value_or(false) != update;
                    }
                }
                if (update_requests == 0 && update_queue.isEmpty() && update_changed) {
                    update_changed = false;
                    if (installed_mode->isChecked()) {
                        ShowInstalled();
                    }
                    return;
                }
                CheckInstalledUpdates();
            });
        }
    }

    QByteArray ThumbnailJpeg() const {
        auto image = preview_images.value(0);
        if (image.isNull()) {
            if (const auto* card = cards.value(selected->id)) {
                image = card->Image();
            }
        }
        if (image.isNull()) {
            return {};
        }
        if (image.width() > 480 || image.height() > 270) {
            image = image.scaled(480, 270, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        return image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPEG", 85)
                   ? bytes
                   : QByteArray{};
    }

    void ClearDetail() {
        ++mod_epoch;
        if (mod_request) {
            mod_request->Cancel();
        }
        if (preview_request) {
            preview_request->Cancel();
        }
        selected.reset();
        offer_list.clear();
        local.reset();
        local_id.clear();
        local_title.clear();
        name->clear();
        byline->clear();
        source_info_row->hide();
        description->clear();
        description_toggle->hide();
        detail_page->verticalScrollBar()->setValue(0);
        preview->SetImage({});
        gallery->hide();
        preview_count->hide();
        preview_strip->hide();
        preview_buttons.clear();
        preview_images.clear();
        preview_urls.clear();
        current_preview = 0;
        preview_loading = false;
        while (auto* item = thumbnails->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        info->hide();
        target_picker->hide();
        release->hide();
        hint->clear();
        hint->hide();
        source->hide();
        install->hide();
        enable->hide();
        uninstall->hide();
    }

    void SelectMod(const QString& id, const QString& target = {}) {
        ClearDetail();
        Status({});
        local_id = id;
        local_title = target;
        pages->setCurrentWidget(detail_page);
        back->setAccessibleName(tr("Back to mod list"));
        back->setToolTip(tr("Back to mod list"));
        detail_page->setFocus(Qt::OtherFocusReason);
        if (installed_mode->isChecked()) {
            ReadLocal();
            if (local) {
                name->setText(local->name);
                byline->setText(tr("Installed release: %1").arg(local->version));
                UpdateButtons();
            }
        } else {
            const auto& entry = catalog[id];
            gallery->setVisible(!entry.preview_url.isEmpty());
            name->setText(entry.name);
            byline->setText(Byline(entry, true));
            description->setText(entry.summary);
        }
        FetchDetail(id);
    }

    void FetchDetail(const QString& mod_id) {
        const auto epoch = mod_epoch;
        mod_request = client.GetMod(mod_id, [this, epoch](auto response) {
            if (epoch != mod_epoch) {
                return;
            }
            if (const auto* error = std::get_if<Error>(&response)) {
                hint->setText(tr("Could not load details: %1").arg(error->message));
                hint->show();
                return;
            }
            selected = std::get<Mod>(std::move(response));
            if (local_title.isEmpty()) {
                local_title = title;
                if (local_title.isEmpty()) {
                    const auto found =
                        std::ranges::find_if(selected->title_ids, [this](const QString& id) {
                            return Identity(id).installed;
                        });
                    local_title =
                        found == selected->title_ids.cend() ? selected->title_ids.value(0) : *found;
                }
            }
            {
                const QSignalBlocker block(target_picker);
                target_picker->clear();
                for (const auto& id : selected->title_ids) {
                    target_picker->addItem(GameName(id), id);
                }
                target_picker->setCurrentIndex(target_picker->findData(local_title));
                target_picker->setVisible(title.isEmpty() && !installed_mode->isChecked() &&
                                          target_picker->count() > 1);
            }
            ReadLocal();
            name->setText(selected->name);
            byline->setText(Byline(*selected, true));
            source_info->setText(SourceName(selected->source));
            source_info_row->show();
            const auto full = selected->description.trimmed();
            description_excerpt = full.section(QLatin1Char('\n'), 0, 1).simplified();
            if (description_excerpt.size() > 200) {
                description_excerpt.truncate(200);
                if (description_excerpt.back().isHighSurrogate()) {
                    description_excerpt.chop(1);
                }
            }
            const bool truncated = description_excerpt != full.simplified();
            if (truncated) {
                description_excerpt = description_excerpt.trimmed() + QStringLiteral("…");
            }
            const QSignalBlocker description_signals(description_toggle);
            description_toggle->setChecked(false);
            description_toggle->setText(tr("Show full description"));
            description_toggle->setVisible(truncated);
            description->setText(truncated ? description_excerpt : full);
            source->setText(tr("View on %1").arg(SourceName(selected->source)));
            source->setVisible(!selected->source_url.isEmpty());
            BuildGallery();
            BuildOffers();
        });
    }

    void BuildGallery() {
        if (selected->external) {
            preview_urls = selected->external->preview_urls;
        }
        if (preview_urls.isEmpty() && selected->preview_url.isValid() &&
            !selected->preview_url.isEmpty()) {
            preview_urls.append(selected->preview_url);
        }
        gallery->setVisible(!preview_urls.isEmpty());
        if (preview_urls.isEmpty()) {
            return;
        }
        const bool show_strip = preview_urls.size() > 1;
        preview_strip->setVisible(show_strip);
        preview_count->setVisible(show_strip);
        if (show_strip) {
            for (int i = 0; i < preview_urls.size(); ++i) {
                auto* button = new PreviewButton(thumbnails->parentWidget());
                button->setAccessibleName(tr("Preview %1").arg(i + 1));
                button->setToolTip(button->accessibleName());
                thumbnails->addWidget(button);
                preview_buttons.append(button);
                connect(button, &QAbstractButton::clicked, this, [this, i] { SelectPreview(i); });
            }
            thumbnails->addStretch();
        }
        SelectPreview(0);
    }

    void SelectPreview(int index) {
        current_preview = index;
        for (int i = 0; i < preview_buttons.size(); ++i) {
            preview_buttons[i]->setChecked(i == index);
        }
        preview_count->setText(tr("Preview %1 of %2").arg(index + 1).arg(preview_urls.size()));
        const auto image = preview_images.constFind(index);
        preview->SetImage(image == preview_images.cend() ? QImage{} : image.value());
        preview->setToolTip(image == preview_images.cend() ? tr("Loading preview…")
                            : image->isNull()              ? tr("Preview unavailable")
                                                           : QString{});
        FetchNextPreview();
    }

    void FetchNextPreview() {
        if (preview_loading) {
            return;
        }
        int index = current_preview;
        if (preview_images.contains(index)) {
            index = 0;
            while (index < preview_urls.size() && preview_images.contains(index)) {
                ++index;
            }
            if (index >= preview_urls.size()) {
                return;
            }
        }
        auto mod = *selected;
        mod.preview_url = preview_urls[index];
        preview_loading = true;
        const auto epoch = mod_epoch;
        preview_request = client.GetPreview(mod, [this, epoch, index](auto response) {
            if (epoch != mod_epoch) {
                return;
            }
            preview_loading = false;
            QImage picture;
            if (const auto* image = std::get_if<QImage>(&response)) {
                picture =
                    image->width() > 1200 || image->height() > 675
                        ? image->scaled(1200, 675, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        : *image;
            }
            preview_images.insert(index, picture);
            if (!picture.isNull() && index < preview_buttons.size()) {
                preview_buttons[index]->SetImage(
                    picture.scaled(152, 88, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            if (index == current_preview) {
                SelectPreview(index);
                return;
            }
            FetchNextPreview();
        });
    }

    struct OfferSet {
        QList<Offer> offers;
        int preferred{};
    };

    OfferSet OffersFor(const Mod& mod, const QString& target) {
        const auto identity = mod.title_ids.contains(target) ? Identity(target) : GameIdentity{};
        OfferSet set;
        auto& offers = set.offers;
        int first_match = -1;
        if (mod.github) {
            int first_installable = -1;
            int first_unknown = -1;
            for (const auto& build : mod.github->builds) {
                const auto match = MatchGitHubBuild(identity, build);
                QString version;
                if (build.download) {
                    const auto& build_file = *build.download;
                    const QJsonArray selection{QString::fromLatin1(build_file.sha256.toHex()),
                                               static_cast<int>(build_file.patch.kind),
                                               build_file.patch.member, build_file.patch.tag};
                    const auto fingerprint = QCryptographicHash::hash(
                        QJsonDocument(selection).toJson(QJsonDocument::Compact),
                        QCryptographicHash::Sha256);
                    version = build.id.toLower() + u'-' + QString::fromLatin1(fingerprint.toHex());
                }
                const auto display = build.id.size() == 16
                                         ? build.id
                                         : QStringLiteral("%1…%2").arg(build.id.left(12),
                                                                      build.id.right(8));
                offers.append({build, version, match, build.download.has_value(),
                               tr("%1 · %2").arg(display, CompatibilityName(match)), build.id});
                const auto index = static_cast<int>(offers.size()) - 1;
                if (first_match < 0 && match == Compatibility::Match && build.download) {
                    first_match = index;
                } else if (first_installable < 0 && CanInstall(mod, target, offers.back())) {
                    first_installable = index;
                }
                if (first_unknown < 0 && match != Compatibility::Mismatch) {
                    first_unknown = index;
                }
            }
            if (first_match < 0) {
                first_match = first_installable < 0 ? first_unknown : first_installable;
            }
        } else if (mod.external) {
            bool first_match_has_checksum = false;
            for (const auto& file : mod.external->files) {
                const auto entry =
                    tr("%1 · %2 · %3")
                        .arg(file.name, locale().formattedDataSize(file.size_bytes),
                             locale().toString(file.uploaded_at.date(), QLocale::ShortFormat));
                offers.append({file, {}, Compatibility::Unknown,
                               file.name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) &&
                                   file.size_bytes <= MaxArchiveBytes,
                               entry, entry});
                if ((first_match < 0 || (!first_match_has_checksum && !file.sha256.isEmpty())) &&
                    CanInstall(mod, target, offers.back())) {
                    first_match = static_cast<int>(offers.size()) - 1;
                    first_match_has_checksum = !file.sha256.isEmpty();
                }
            }
        } else {
            for (const auto& version : mod.versions) {
                const auto match = MatchVersion(target, identity, version);
                offers.append({version, version.id, match, true,
                               tr("%1 · %2 · %3")
                                   .arg(version.id, version.game_version, CompatibilityName(match)),
                               {}});
                if (first_match < 0 && match == Compatibility::Match) {
                    first_match = static_cast<int>(offers.size()) - 1;
                }
            }
        }
        set.preferred = std::max(first_match, 0);
        return set;
    }

    void BuildOffers() {
        const QSignalBlocker block(release);
        auto set = OffersFor(*selected, local_title);
        offer_list = std::move(set.offers);
        release->clear();
        for (const auto& offer : offer_list) {
            release->addItem(offer.label);
            if (!offer.tooltip.isEmpty()) {
                release->setItemData(release->count() - 1, offer.tooltip, Qt::ToolTipRole);
            }
        }
        release->setCurrentIndex(set.preferred);
        release->setVisible(release->count() > 1);
        UpdateOffer();
        RecordLocalUpdate();
    }

    void RecordLocalUpdate() {
        if (selected && local) {
            RecordUpdateCheck(context.mods_root, local_title, local->id, local->version,
                              HasUpdate(*selected, local_title, *local));
        }
    }

    bool HasUpdate(const Mod& mod, const QString& target, const InstalledMod& installed) {
        const auto set = OffersFor(mod, target);
        if (set.offers.isEmpty()) {
            return false;
        }
        const auto& offer = set.offers[set.preferred];
        return Compare(installed, offer) == LocalState::Outdated &&
               CanInstall(mod, target, offer) && (!mod.github || offer.supported);
    }

    enum class LocalState { Current, Reinstall, Outdated };

    static LocalState Compare(const InstalledMod& installed, const Offer& offer) {
        const auto* external = std::get_if<ExternalFile>(&offer.file);
        if (!external) {
            return installed.version == offer.version ? LocalState::Current : LocalState::Outdated;
        }
        if (!installed.version.startsWith(ExternalVersionPrefix(external->id))) {
            return LocalState::Outdated;
        }
        if (external->sha256.isEmpty()) {
            return LocalState::Reinstall;
        }
        const bool same = installed.sha256.isEmpty()
                              ? installed.version == ExternalVersion(external->id, external->sha256)
                              : installed.sha256 == external->sha256;
        return same ? LocalState::Current : LocalState::Outdated;
    }

    const Offer* CurrentOffer() const {
        const auto index = release->currentIndex();
        return index >= 0 && index < offer_list.size() ? &offer_list[index] : nullptr;
    }

    bool CanInstall(const Offer& offer) {
        return selected && CanInstall(*selected, local_title, offer);
    }

    bool CanInstall(const Mod& mod, const QString& target, const Offer& offer) {
        if (mod.demo || !mod.title_ids.contains(target) || !offer.supported ||
            !Identity(target).installed || offer.compatibility == Compatibility::Mismatch) {
            return false;
        }
        if (const auto* file = std::get_if<ExternalFile>(&offer.file)) {
            return !file->ambiguous_layout && (file->sha256.isEmpty() || file->sha256.size() == 32);
        }
        if (const auto* github = std::get_if<GitHubBuild>(&offer.file)) {
            return github->download.has_value();
        }
        return offer.compatibility == Compatibility::Match &&
               std::get<Version>(offer.file).sha256.size() == 32;
    }

    void UpdateOffer() {
        if (!selected) {
            return;
        }
        const auto identity =
            selected->title_ids.contains(local_title) ? Identity(local_title) : GameIdentity{};
        const auto* offer = CurrentOffer();
        const auto unknown = tr("Unknown");
        const auto short_build = [](const QString& build) {
            return build.size() > 16 ? QStringLiteral("%1…%2").arg(build.left(12), build.right(8))
                                     : build;
        };
        size_t rows = 0;
        const auto add_row = [&](const QString& key, const QString& value,
                                 const QString& tooltip = {}) {
            if (rows >= info_rows.size()) {
                return;
            }
            const auto [left, right] = info_rows[rows++];
            left->setText(key);
            right->setText(value);
            right->setToolTip(tooltip);
            left->show();
            right->show();
        };
        const auto* external = offer ? std::get_if<ExternalFile>(&offer->file) : nullptr;
        const auto* version = offer ? std::get_if<Version>(&offer->file) : nullptr;
        const auto* github_build = offer ? std::get_if<GitHubBuild>(&offer->file) : nullptr;
        const auto match = offer ? offer->compatibility : Compatibility::Unknown;
        const bool unverified_file = external && external->sha256.isEmpty();
        if (title.isEmpty() || !selected->title_ids.contains(title)) {
            add_row(tr("Game"), local_title.isEmpty() ? unknown : GameName(local_title));
        }
        if (selected->github) {
            const auto& github = *selected->github;
            add_row(tr("Source"), tr("%1 @ %2").arg(github.repository, github.commit.left(12)),
                    github.commit);
            add_row(tr("License"), tr("Not declared"));
        } else if (selected->external) {
            add_row(tr("Source"),
                    tr("%1 · %2").arg(SourceName(selected->source), selected->external->category));
        }
        if (external) {
            add_row(tr("File"),
                    tr("%1 · %2").arg(external->name,
                                      locale().formattedDataSize(external->size_bytes)));
            add_row(tr("Uploaded"),
                    locale().toString(external->uploaded_at.toLocalTime().date(),
                                      QLocale::ShortFormat));
            if (!external->build_ids.isEmpty()) {
                auto visible_builds = short_build(external->build_ids.front());
                if (external->build_ids.size() > 1) {
                    visible_builds = tr("%1 and %n more", nullptr,
                                        static_cast<int>(external->build_ids.size() - 1))
                                         .arg(visible_builds);
                }
                add_row(tr("Patch build IDs"), visible_builds,
                        external->build_ids.join(QLatin1Char('\n')));
            }
        }
        if (version) {
            add_row(tr("Title ID"), version->title_id);
            add_row(tr("Target game version"), version->game_version);
            add_row(tr("Target build ID"), short_build(version->build_id), version->build_id);
            add_row(tr("Minimum Lichii"), version->min_lichii_build);
        }
        if (github_build) {
            add_row(github_build->main && github_build->id.size() == 16 ? tr("Main build ID prefix")
                    : github_build->main                                ? tr("Main build ID")
                                                                        : tr("Patch build ID"),
                    short_build(github_build->id), github_build->id);
        }
        if (version || selected->github) {
            add_row(tr("Installed version"),
                    identity.display_version.isEmpty()
                        ? (identity.installed ? unknown : tr("Not in your library"))
                        : identity.display_version);
            add_row(tr("Installed build ID"),
                    KnownBuild(identity.build_id) ? short_build(identity.build_id) : unknown,
                    KnownBuild(identity.build_id) ? identity.build_id : QString{});
            add_row(tr("Running Lichii"),
                    Common::g_build_release_tag[0]
                        ? QString::fromLatin1(Common::g_build_release_tag)
                        : tr("Release number unavailable"));
        }
        add_row(tr("Compatibility"),
                unverified_file ? tr("Not verified by the catalog") : CompatibilityName(match));
        installed_row = rows;
        add_row(tr("Installed"), {});
        for (size_t i = rows; i < info_rows.size(); ++i) {
            info_rows[i].first->hide();
            info_rows[i].second->hide();
        }
        verification->setText(CompatibilityName(match));
        const bool show_state = offer || selected->github.has_value();
        verification->setVisible(show_state);
        verification_icon->setVisible(show_state);
        const auto color = match == Compatibility::Match      ? Color::Success()
                           : match == Compatibility::Mismatch ? Color::Accent()
                                                              : Color::Warning();
        verification->setStyleSheet(
            QStringLiteral("color: %1; background: transparent;").arg(CssColor(color)));
        verification_icon->setPixmap(Spotlight::IconPixmap(
            match == Compatibility::Match ? u"seal-check" : u"warning-circle", 14, color,
            devicePixelRatioF()));
        info->show();
        UpdateButtons();
    }

    void ReadLocal() {
        local.reset();
        if (local_title.isEmpty() || local_id.isEmpty()) {
            return;
        }
        const auto installed = ReadInstalledMods(context.mods_root, local_title);
        const auto it = std::ranges::find(installed, local_id, &InstalledMod::id);
        if (it != installed.cend()) {
            local = *it;
        }
    }

    void UpdateButtons() {
        const auto* offer = CurrentOffer();
        const auto* external = offer ? std::get_if<ExternalFile>(&offer->file) : nullptr;
        const bool installed = local.has_value();
        const auto state = offer && installed ? Compare(*local, *offer) : LocalState::Outdated;
        const bool reinstall = state == LocalState::Reinstall;
        const bool current = state == LocalState::Current;
        install->setVisible(offer && !current && (!selected->github || offer->supported));
        install->setText(reinstall ? tr("Reinstall") : installed ? tr("Update") : tr("Install"));
        install->SetIcon(installed && !reinstall ? QStringLiteral("arrow-circle-up")
                                                 : QStringLiteral("download-simple"));
        install->setEnabled(!busy && offer && !current && CanInstall(*offer));
        source->setEnabled(!busy);
        action_row->setVisible(!busy);
        const bool install_primary = install->isEnabled();
        const bool source_primary = !install_primary && !installed && !source->isHidden() && !busy;
        install->SetKind(install_primary ? PillButton::Kind::Accent : PillButton::Kind::Control);
        source->SetKind(source_primary ? PillButton::Kind::Accent : PillButton::Kind::Control);
        if (mod_actions->indexOf(source_primary ? source : install) != 0) {
            auto* first = source_primary ? source : install;
            mod_actions->removeWidget(first);
            mod_actions->insertWidget(0, first);
        }
        enable->setVisible(installed);
        uninstall->setVisible(installed);
        enable->setEnabled(!busy && static_cast<bool>(context.set_enabled));
        uninstall->setEnabled(!busy);
        const bool enabled = installed && IsEnabled(local_title, local_id);
        if (selected && installed_row < info_rows.size()) {
            info_rows[installed_row].second->setText(
                installed ? (enabled ? tr("Installed and enabled") : tr("Installed and disabled"))
                          : tr("Not installed"));
        }
        enable->setText(enabled ? tr("Disable") : tr("Enable"));
        enable->SetKind(installed && !enabled && !install_primary ? PillButton::Kind::Accent
                                                                  : PillButton::Kind::Control);
        hint->clear();
        if (selected && selected->github && (!offer || !offer->supported)) {
            hint->setText(tr("No verified download is available for this patch. "
                             "Open GitHub for the files and instructions."));
        } else if (offer && selected) {
            if (selected->demo) {
                hint->setText(tr("This sample is not available for installation."));
            } else if (external && external->ambiguous_layout) {
                hint->setText(tr("This ZIP requires choosing components. Open GameBanana to "
                                 "install it manually."));
            } else if (!selected->title_ids.contains(local_title)) {
                hint->setText(tr("This listing does not target the selected game."));
            } else if (offer->compatibility == Compatibility::Mismatch) {
                hint->setText(
                    tr("This release does not match the installed game or Lichii build."));
            } else if (!offer->supported) {
                hint->setText(tr("Only ZIP files up to 64 MiB can be installed. Open GameBanana "
                                 "for this file."));
            } else if (!Identity(local_title).installed) {
                hint->setText(tr("Add this game to your library to install its mods."));
            } else if (!CanInstall(*offer)) {
                hint->setText(tr("The installed game or Lichii build could not be verified."));
            } else if (current || reinstall) {
                hint->setText(enabled ? tr("Installed and enabled") : tr("Installed and disabled"));
            } else {
                hint->setText(installed ? tr("A different release is available.") : QString{});
            }
        } else if (installed) {
            hint->setText(enabled ? tr("Installed and enabled") : tr("Installed and disabled"));
        }
        hint->setVisible(!hint->text().isEmpty());
    }

    void OpenSource() {
        if (selected && !selected->source_url.isEmpty()) {
            QDesktopServices::openUrl(selected->source_url);
        }
    }

    void InstallFailed(const QString& error) {
        Status(error, Spotlight::Notice::Tone::Error);
        if (!selected || selected->source_url.isEmpty()) {
            return;
        }
        const QUrl source_url = selected->source_url;
        QMessageBox prompt(QMessageBox::Warning, tr("Could not install the mod"), error,
                           QMessageBox::Close, this);
        auto* open = prompt.addButton(source->text(), QMessageBox::AcceptRole);
        SetButtonKind(open, QStringLiteral("primary"));
        prompt.exec();
        if (prompt.clickedButton() == open && !source_url.isEmpty()) {
            QDesktopServices::openUrl(source_url);
        }
    }

    bool Confirm(QMessageBox::Icon icon, const QString& heading, const QString& text,
                 const QString& yes_kind) {
        QMessageBox prompt(icon, heading, text, QMessageBox::No | QMessageBox::Yes, this);
        SetButtonKind(prompt.button(QMessageBox::Yes), yes_kind);
        auto* no = prompt.button(QMessageBox::No);
        no->setStyleSheet(
            QStringLiteral("QPushButton:default { background-color: %1; border-color: %2;"
                           " color: %3; }"
                           "QPushButton:default:hover { background-color: %2; }"
                           "QPushButton[keyboardFocus=\"true\"] { border-color: %3; }")
                .arg(CssColor(Color::Control()), CssColor(Color::Stroke()),
                     CssColor(Color::Text())));
        prompt.setDefaultButton(QMessageBox::No);
        prompt.setEscapeButton(QMessageBox::No);
        return prompt.exec() == QMessageBox::Yes;
    }

    void SetProgress(const QString& heading, const QString& detail = {}, int percent = -1,
                     bool can_cancel = false) {
        if (heading.isEmpty()) {
            progress_panel->hide();
            return;
        }
        progress_title->setText(heading);
        progress_detail->setText(detail);
        progress_detail->setVisible(!detail.isEmpty());
        cancel_button->setVisible(can_cancel);
        progress_bar->setRange(0, percent < 0 ? 0 : 100);
        if (percent >= 0) {
            progress_bar->setValue(percent);
        }
        progress_panel->show();
    }

    void SetBusy(bool value) {
        busy = value;
        for (QWidget* widget :
             {static_cast<QWidget*>(game_picker), static_cast<QWidget*>(search),
              static_cast<QWidget*>(preview_strip), static_cast<QWidget*>(browse_page),
              static_cast<QWidget*>(installed_page), static_cast<QWidget*>(release),
              static_cast<QWidget*>(target_picker), static_cast<QWidget*>(back)}) {
            widget->setEnabled(!value);
        }
        refresh->setEnabled(!value);
        installed_mode->setEnabled(!value);
        UpdateButtons();
    }

    void StartInstall() {
        const auto* offer = CurrentOffer();
        if (!offer || !CanInstall(*offer) || busy) {
            return;
        }
        const auto chosen = *offer;
        const auto* external_file = std::get_if<ExternalFile>(&chosen.file);
        const auto* github = std::get_if<GitHubBuild>(&chosen.file);
        const auto patch = github ? std::optional{github->download->patch} : std::nullopt;
        const bool external = external_file != nullptr;
        const bool unverified = external && external_file->sha256.isEmpty();
        const auto label = selected->name;
        const auto epoch = mod_epoch;
        const InstallDetails details{
            .author = selected->author,
            .source = selected->source,
            .file_date = external ? external_file->uploaded_at : selected->updated_at,
            .verified = !unverified && chosen.compatibility == Compatibility::Match,
            .thumbnail_jpeg = ThumbnailJpeg(),
        };
        if ((external || patch) && !unverified && chosen.compatibility == Compatibility::Unknown &&
            !Confirm(QMessageBox::Warning, tr("Compatibility unverified"),
                     tr("Lichii could not confirm that this file works with your game version. "
                        "It will check the download's SHA-256 before installing. Continue?"),
                     QStringLiteral("primary"))) {
            return;
        }
        if (epoch != mod_epoch) {
            return;
        }
        reload_after_install = debounce.isActive();
        debounce.stop();
        SetBusy(true);
        progress_note->setVisible(unverified);
        const auto from = tr("Downloading from %1…").arg(SourceName(selected->source));
        SetProgress(tr("Starting the download…"), from, 0, true);
        const auto generation = ++install_epoch;
        const auto version = chosen.version;
        const auto file_id = external ? external_file->id : 0;
        auto receive = [this, generation, version, file_id, label, external, patch,
                        details](auto response) {
            if (generation != install_epoch) {
                return;
            }
            if (const auto* error = std::get_if<Error>(&response)) {
                FinishOperation();
                InstallFailed(tr("Could not download the mod: %1").arg(error->message));
                return;
            }
            auto received = std::get<DownloadedFile>(std::move(response));
            auto owned = std::shared_ptr<QTemporaryFile>(std::move(received.file));
            ApplyDownloaded(owned, external ? ExternalVersion(file_id, received.sha256) : version,
                            received.sha256, label, external, patch, details);
        };
        auto report = [this, generation, unverified, from](qint64 received, qint64 total) {
            if (generation != install_epoch || total <= 0) {
                return;
            }
            if (received >= total) {
                SetProgress(unverified ? tr("Checking the download…") : tr("Verifying SHA-256…"),
                            tr("Then checking the ZIP and installing the mod…"), 100);
            } else {
                SetProgress(tr("%1 of %2").arg(locale().formattedDataSize(received),
                                               locale().formattedDataSize(total)),
                            from, static_cast<int>(100 * received / total), true);
            }
        };
        download = std::visit(
            [&](const auto& file) {
                if constexpr (std::is_same_v<std::decay_t<decltype(file)>, GitHubBuild>) {
                    return client.Download(*file.download, std::move(receive), std::move(report));
                } else {
                    return client.Download(file, std::move(receive), std::move(report));
                }
            },
            chosen.file);
    }

    void ApplyDownloaded(const std::shared_ptr<QTemporaryFile>& file, const QString& version,
                         const QByteArray& digest, const QString& label, bool external,
                         const std::optional<GitHubPatch>& patch, const InstallDetails& details) {
        SetProgress(patch ? tr("Checking and installing the patch…")
                          : tr("Checking the ZIP and installing the mod…"),
                    {}, 100);
        applying = true;
        const auto root = context.mods_root;
        const auto target = local_title;
        const auto id = local_id;
        auto* install_task = new QFutureWatcher<QString>(this);
        connect(install_task, &QFutureWatcher<QString>::finished, this, [this, install_task] {
            const auto error = install_task->result();
            install_task->deleteLater();
            if (error.isEmpty()) {
                changed = true;
                InstalledChanged(local_title, local_id);
                ReadLocal();
                RecordLocalUpdate();
            }
            FinishOperation();
            if (error.isEmpty()) {
                Status(tr("Installation complete. The mod is available in Add-Ons."),
                       Spotlight::Notice::Tone::Success);
            } else {
                InstallFailed(error);
            }
        });
        install_task->setFuture(QtConcurrent::run([root, target, id, label, version, file, digest,
                                                   external, patch, details] {
            if (patch) {
                return InstallGitHubPatch(root, target, id, label, version, file->fileName(), digest,
                                          *patch, details);
            }
            return InstallMod(root, target, id, label, version, file->fileName(), digest, external,
                              details);
        }));
    }

    void FinishOperation() {
        applying = false;
        SetProgress({});
        SetBusy(false);
        if (reload_after_install) {
            reload_after_install = false;
            debounce.start();
        }
    }

    void CancelDownload() {
        if (applying || !busy) {
            return;
        }
        ++install_epoch;
        if (download) {
            download->Cancel();
        }
        FinishOperation();
        Status(tr("Download canceled. No add-ons were changed."));
    }

    void ToggleEnabled() {
        if (!local || !context.set_enabled || busy) {
            return;
        }
        if (SetEnabledFor(local_title, local_id, !IsEnabled(local_title, local_id))) {
            UpdateButtons();
        }
    }

    bool SetEnabledFor(const QString& target, const QString& id, bool enabled) {
        if (!context.set_enabled || busy || !context.set_enabled(target, id, enabled)) {
            Status(tr("Could not save the change to this mod."), Spotlight::Notice::Tone::Error);
            return false;
        }
        changed = true;
        InstalledChanged(target, id);
        Status(enabled ? tr("Mod enabled.") : tr("Mod disabled."));
        return true;
    }

    void InstalledChanged(const QString& target, const QString& id) {
        installed_by_title.remove(target);
        if (auto* card = cards.value(id)) {
            SetCardStatus(card, catalog[id]);
        }
    }

    void RemoveInstalled() {
        if (local) {
            RemoveLocal(*local, local_title);
        }
    }

    void RemoveLocal(InstalledMod installed, QString target) {
        if (busy) {
            return;
        }
        const auto epoch = mod_epoch;
        if (!Confirm(QMessageBox::Question, tr("Uninstall mod"),
                     tr("Uninstall %1?").arg(installed.name), QStringLiteral("destructive")) ||
            epoch != mod_epoch) {
            return;
        }
        const auto error = UninstallMod(context.mods_root, target, installed.id);
        if (!error.isEmpty()) {
            Status(error, Spotlight::Notice::Tone::Error);
            return;
        }
        const bool preference_reset =
            !context.set_enabled || context.set_enabled(target, installed.id, true);
        changed = true;
        local.reset();
        InstalledChanged(target, installed.id);
        if (installed_mode->isChecked()) {
            ClearDetail();
            RestoreListPage();
            ShowInstalled();
        } else {
            UpdateButtons();
        }
        Status(preference_reset
                   ? tr("Mod uninstalled.")
                   : tr("Mod uninstalled, but Lichii could not clear its saved disabled state."));
    }

    DialogContext context;
    Client client;
    QTimer debounce;
    QList<Game> games;
    QList<Game> incoming_games;
    QHash<QString, Mod> catalog;
    QHash<QString, ModCard*> cards;
    QHash<QString, QPushButton*> installed_rows;
    QHash<QString, QList<InstalledMod>> installed_by_title;
    QList<QString> thumbnail_queue;
    QHash<QString, QPointer<Request>> thumbnail_requests;
    QHash<QString, GameIdentity> identities;
    QList<Offer> offer_list;
    QList<std::pair<QString, QString>> update_queue;
    QSet<QString> update_checked;
    int update_requests{};
    bool update_changed{};
    ModFilter list_filter{};
    std::optional<int> next;
    std::optional<Mod> selected;
    std::optional<InstalledMod> local;
    QString description_excerpt;
    QString title;
    QString local_title;
    QString local_id;
    QPointer<Request> games_request;
    QPointer<Request> list_request;
    QPointer<Request> mod_request;
    QPointer<Request> preview_request;
    QPointer<Request> download;
    QList<QUrl> preview_urls;
    QList<PreviewButton*> preview_buttons;
    QHash<int, QImage> preview_images;
    int current_preview{};
    bool preview_loading{};
    int game_epoch{};
    int list_epoch{};
    int mod_epoch{};
    int install_epoch{};
    bool changed{};
    bool busy{};
    bool applying{};
    bool reload_after_install{};
    QStackedWidget* pages{};
    QWidget* browse_page{};
    QScrollArea* browse_scroll{};
    QScrollArea* installed_page{};
    QScrollArea* detail_page{};
    QScrollArea* preview_strip{};
    QWidget* gallery{};
    QGridLayout* card_grid{};
    QVBoxLayout* installed_list{};
    QHBoxLayout* thumbnails{};
    QLabel* count{};
    QLabel* installed_count{};
    QLabel* browse_message{};
    QComboBox* game_picker{};
    SegmentedControl* type{};
    QComboBox* sort{};
    QComboBox* release{};
    QComboBox* target_picker{};
    QLineEdit* search{};
    QLabel* name{};
    QLabel* byline{};
    QLabel* source_info{};
    QWidget* source_info_row{};
    QLabel* verification{};
    QLabel* description{};
    QToolButton* description_toggle{};
    PreviewImage* preview{};
    QLabel* preview_count{};
    QLabel* verification_icon{};
    QLabel* hint{};
    Spotlight::Notice* status{};
    QWidget* info{};
    std::array<std::pair<QLabel*, QLabel*>, 14> info_rows{};
    size_t installed_row{};
    QWidget* action_row{};
    QHBoxLayout* mod_actions{};
    QWidget* progress_panel{};
    QLabel* progress_title{};
    QLabel* progress_detail{};
    QLabel* progress_note{};
    QProgressBar* progress_bar{};
    PillButton* cancel_button{};
    QAction* refresh{};
    QAction* installed_mode{};
    CircleButton* store_menu{};
    QPixmap backdrop;
    PillButton* more{};
    PillButton* install{};
    PillButton* source{};
    PillButton* enable{};
    CircleButton* uninstall{};
    CircleButton* back{};
    QEventLoop loop;
    bool close_host{};
};

} // namespace

bool ShowDialog(QWidget* parent, DialogContext context) {
    const QPointer<Dialog> dialog = new Dialog(parent->window(), std::move(context));
    dialog->Exec();
    const bool changed = dialog && dialog->Changed();
    delete dialog.data();
    return changed;
}

} // namespace ModStore
