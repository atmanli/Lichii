// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include "lichii/spotlight/dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QWindow>

#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {

namespace {

constexpr char ChromeProperty[] = "spotlightChrome";
constexpr char SubtitleProperty[] = "spotlightSubtitle";
constexpr char TitleHiddenProperty[] = "spotlightTitleHidden";
constexpr char CloseHiddenProperty[] = "spotlightCloseHidden";
constexpr char IconProperty[] = "spotlightIcon";
constexpr char IconTintProperty[] = "spotlightIconTint";
constexpr char ToneProperty[] = "spotlightTone";
constexpr char ContentChromeProperty[] = "spotlightContentChrome";

// Room around the card for its shadow (Paper: 0 30 80 #000000a6), which falls down.
constexpr QMargins ShadowMargins{36, 20, 36, 52};
constexpr int ShadowBlur = 28;
constexpr int ShadowOffset = 18;
constexpr int Padding = 24;
constexpr int HeaderGap = 20;
constexpr int HeaderIconSize = 44;
constexpr int HeaderIconGap = 14;
constexpr int SubtitleGap = 4;
constexpr int CloseSize = 32;
constexpr int ButtonGap = 10;

// Message boxes: a 412 px card with the icon beside the title and text.
constexpr int MessageIconGap = 16;
constexpr int MessageTextWidth = 304;
constexpr int MessageWideTextWidth = 456;
constexpr int MessageTitleGap = 6;
constexpr int MessageButtonGap = 18;

// Progress dialogs: a 520 px card with the label and percentage over an 8 px bar.
constexpr int ProgressCardWidth = 520;
constexpr int ProgressRowGap = 10;
constexpr int ProgressBarHeight = 8;

struct MessageLook {
    QString icon;
    QColor color;
    int fill_alpha = 0;
};

MessageLook LookOf(const QMessageBox* box) {
    if (box->property(ToneProperty).toString() == QStringLiteral("success")) {
        return {QStringLiteral("check-circle"), Color::Success(), 0x1f};
    }
    switch (box->icon()) {
    case QMessageBox::Information:
        return {QStringLiteral("info"), Color::Info(), 0x1f};
    case QMessageBox::Warning:
        return {QStringLiteral("warning-circle"), Color::Warning(), 0x1f};
    case QMessageBox::Critical:
        return {QStringLiteral("warning-circle"), Color::Destructive(), 0x1f};
    case QMessageBox::Question:
        return {QStringLiteral("question"), Color::Accent(), 0x24};
    case QMessageBox::NoIcon:
        break;
    }
    return {};
}

QPixmap MessageIcon(const MessageLook& look, qreal ratio) {
    QPixmap tile(qRound(HeaderIconSize * ratio), qRound(HeaderIconSize * ratio));
    tile.setDevicePixelRatio(ratio);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    QColor fill = look.color;
    fill.setAlpha(look.fill_alpha);
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(0, 0, HeaderIconSize, HeaderIconSize));
    painter.drawPixmap(11, 11, IconPixmap(look.icon, 22, look.color, ratio));
    return tile;
}

QPixmap HeaderIcon(const QString& name, const QColor& tint, qreal ratio) {
    QPixmap tile(qRound(HeaderIconSize * ratio), qRound(HeaderIconSize * ratio));
    tile.setDevicePixelRatio(ratio);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Color::Fill());
    painter.drawRoundedRect(QRectF(0, 0, HeaderIconSize, HeaderIconSize), Radius::Tile,
                            Radius::Tile);
    painter.drawPixmap(11, 11, IconPixmap(name, 22, tint, ratio));
    return tile;
}

// Dims the window under a modal dialog, as Paper draws every dialog.
class Scrim final : public QWidget {
public:
    explicit Scrim(QWidget* window) : QWidget(window) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // The style sheet gives a plain QWidget the window background; the scrim only dims.
        ensurePolished();
        setAttribute(Qt::WA_StyledBackground, false);
        setGeometry(window->rect());
        window->installEventFilter(this);
        raise();
        show();
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Resize) {
            setGeometry(parentWidget()->rect());
        }
        return false;
    }

    void paintEvent(QPaintEvent*) override {
        QPainter(this).fillRect(rect(), Color::DialogScrim());
    }
};

class DialogChrome final : public QObject {
public:
    DialogChrome(QDialog* dialog_, bool close_button)
        : QObject(dialog_), dialog(dialog_), box(qobject_cast<QMessageBox*>(dialog_)),
          progress(qobject_cast<QProgressDialog*>(dialog_)) {
        title = new QLabel(dialog);
        title->setObjectName(QStringLiteral("spotlightDialogTitle"));
        title->setFont(DisplayFont(box ? 20 : 22));
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(true);
        SetTextColor(title, Color::Text());
        subtitle = new QLabel(dialog);
        subtitle->setObjectName(QStringLiteral("spotlightDialogSubtitle"));
        subtitle->setFont(Font(14, 400));
        subtitle->setTextFormat(Qt::PlainText);
        subtitle->setWordWrap(true);
        SetTextColor(subtitle, Color::TextSecondary());
        icon = new QLabel(dialog);
        icon->setFixedSize(HeaderIconSize, HeaderIconSize);
        icon->hide();
        if (close_button) {
            close = new CircleButton(QStringLiteral("x"), CloseSize, dialog);
            close->setToolTip(QDialog::tr("Close"));
            close->setFocusPolicy(Qt::NoFocus);
            connect(close, &QAbstractButton::clicked, dialog, &QDialog::reject);
        }
        dialog->installEventFilter(this);
        Update();
    }

    ~DialogChrome() override {
        delete scrim;
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        switch (event->type()) {
        case QEvent::WindowTitleChange:
        case QEvent::DynamicPropertyChange:
            Update();
            return false;
        case QEvent::Show:
            if (box) {
                PrepareMessageBox();
            }
            PrepareButtons();
            PrepareProgress();
            Update();
            PlaceProgress();
            ShowScrim();
            if (!shown) {
                shown = true;
                keep_position = dialog->testAttribute(Qt::WA_Moved);
                MakeRoomForChrome();
            }
            Center();
            return false;
        case QEvent::Hide:
            delete scrim;
            return false;
        case QEvent::Resize:
            if (box) {
                FitMessageBox();
            } else {
                UpdateMargins();
            }
            Place();
            if (shown) {
                Center();
            }
            // QProgressDialog::resizeEvent would put its widgets back where its own layout has
            // them.
            return PlaceProgress();
        case QEvent::LayoutRequest:
            PlaceProgress();
            return false;
        case QEvent::Paint:
            Paint();
            return false;
        case QEvent::MouseButtonPress:
            if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton &&
                dialog->windowHandle()) {
                keep_position = true;
                dialog->windowHandle()->startSystemMove();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

private:
    QRect Card() const {
        return dialog->rect().marginsRemoved(ShadowMargins);
    }

    QLabel* MessageLabel() const {
        return dialog->findChild<QLabel*>(QStringLiteral("qt_msgbox_label"),
                                          Qt::FindDirectChildrenOnly);
    }

    int TextLeft() const {
        return Card().left() + Padding + (icon->isHidden() ? 0 : HeaderIconSize + HeaderIconGap);
    }

    int TextRight() const {
        return Card().right() + 1 - Padding - (CloseShown() ? CloseSize + HeaderIconGap : 0);
    }

    int HeaderHeight() const {
        const int width = std::max(1, TextRight() - TextLeft());
        int height = title->isHidden() ? 0 : title->heightForWidth(width);
        if (!subtitle->isHidden()) {
            height += SubtitleGap + subtitle->heightForWidth(width);
        }
        if (!icon->isHidden()) {
            height = std::max(height, HeaderIconSize);
        }
        if (CloseShown()) {
            height = std::max(height, CloseSize);
        }
        return height;
    }

    bool CloseShown() const {
        return close && !dialog->property(CloseHiddenProperty).toBool();
    }

    void Update() {
        if (dialog->property(ContentChromeProperty).toBool()) {
            title->hide();
            subtitle->hide();
            icon->hide();
            dialog->setContentsMargins(ShadowMargins);
            return;
        }
        title->setText(dialog->windowTitle());
        if (box) {
            title->setHidden(title->text().isEmpty());
            subtitle->hide();
            dialog->setContentsMargins(ShadowMargins +
                                       QMargins(Padding, Padding, Padding, Padding));
            Place();
            return;
        }
        title->setHidden(title->text().isEmpty() || dialog->property(TitleHiddenProperty).toBool());
        subtitle->setText(dialog->property(SubtitleProperty).toString());
        subtitle->setHidden(subtitle->text().isEmpty());
        const QString icon_name = dialog->property(IconProperty).toString();
        icon->setHidden(icon_name.isEmpty());
        if (close) {
            close->setHidden(!CloseShown());
        }
        if (!icon_name.isEmpty()) {
            icon->setPixmap(HeaderIcon(icon_name,
                                       dialog->property(IconTintProperty).value<QColor>(),
                                       dialog->devicePixelRatioF()));
        }
        UpdateMargins();
        Place();
    }

    // The header grows when the title or subtitle wraps to another line at the dialog's width.
    void UpdateMargins() {
        if (dialog->property(ContentChromeProperty).toBool()) {
            return;
        }
        const int header = HeaderHeight();
        const QMargins margins(ShadowMargins.left() + Padding,
                               ShadowMargins.top() + Padding + header + (header ? HeaderGap : 0),
                               ShadowMargins.right() + Padding, ShadowMargins.bottom() + Padding);
        if (dialog->contentsMargins() != margins) {
            dialog->setContentsMargins(margins);
        }
    }

    void Place() {
        if (dialog->property(ContentChromeProperty).toBool()) {
            return;
        }
        if (box) {
            PlaceMessageTitle();
            return;
        }
        const QRect card = Card();
        const int top = card.top() + Padding;
        const int left = TextLeft();
        const int width = std::max(1, TextRight() - left);
        icon->move(card.left() + Padding, top);
        const int title_height = title->heightForWidth(width);
        int y = top;
        if (!icon->isHidden() && subtitle->isHidden()) {
            y += std::max(0, HeaderIconSize - title_height) / 2;
        }
        if (!title->isHidden()) {
            title->setGeometry(left, y, width, title_height);
            y += title_height + SubtitleGap;
        }
        subtitle->setGeometry(left, y, width, subtitle->heightForWidth(width));
        if (close) {
            close->move(card.right() + 1 - Padding - CloseSize, top);
        }
    }

    void PlaceMessageTitle() {
        const QLabel* label = MessageLabel();
        if (!label || title->isHidden()) {
            return;
        }
        const QRect text = label->geometry();
        title->setGeometry(text.left(), text.top(), text.width(),
                           title->heightForWidth(text.width()));
    }

    void Paint() {
        QPainter painter(dialog);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(Card()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(Qt::NoPen);
        QColor shadow = Color::Shadow();
        for (int step = ShadowBlur; step > 0; --step) {
            const qreal t = 1.0 - qreal(step) / ShadowBlur;
            shadow.setAlphaF(0.05f * float(t * t));
            painter.setBrush(shadow);
            const qreal grow = step;
            painter.drawRoundedRect(
                card.translated(0, ShadowOffset * t).adjusted(-grow, -grow, grow, grow),
                Radius::Dialog + grow, Radius::Dialog + grow);
        }
        painter.setPen(QPen(Color::StrokeSubtle(), 1));
        painter.setBrush(Color::DialogSolid());
        painter.drawRoundedRect(card, Radius::Dialog, Radius::Dialog);
    }

    // Centres the card, shadow excluded, over the window the dialog belongs to: its parent's window
    // when that is shown, else the active window, else the screen. A dialog the user or its owner
    // placed keeps its position.
    void Center() {
        if (keep_position || dialog->windowState() != Qt::WindowNoState) {
            return;
        }
        QWidget* host = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
        if (!host || !host->isVisible()) {
            host = QApplication::activeWindow();
        }
        if (!host || host == dialog) {
            const auto windows = QApplication::topLevelWidgets();
            const auto main = std::find_if(windows.begin(), windows.end(), [this](QWidget* window) {
                return window != dialog && window->isVisible() && window->isWindow() &&
                       window->windowType() == Qt::Window;
            });
            host = main == windows.end() ? nullptr : *main;
        }
        const QScreen* screen = host && host != dialog ? host->screen() : dialog->screen();
        if (!screen) {
            return;
        }
        const QRect area = screen->availableGeometry();
        QRect over = area;
        if (host && host != dialog) {
            over = host->property(ChromeProperty).toBool()
                       ? host->geometry().marginsRemoved(ShadowMargins)
                       : host->frameGeometry();
        }
        const QRect card = Card();
        QPoint origin = over.center() - card.center();
        origin.setX(std::clamp(origin.x(), area.left() - card.left(),
                               std::max(area.left() - card.left(), area.right() - card.right())));
        origin.setY(std::clamp(origin.y(), area.top() - card.top(),
                               std::max(area.top() - card.top(), area.bottom() - card.bottom())));
        dialog->move(origin);
    }

    void ShowScrim() {
        QWidget* parent = dialog->parentWidget();
        if (scrim || !parent || !dialog->isModal()) {
            return;
        }
        QWidget* window = parent->window();
        if (window->property(ChromeProperty).toBool()) {
            return;
        }
        scrim = new Scrim(window);
    }

    // setupUi sizes a dialog from its form, which was drawn without the card around it, so a dialog
    // given a size grows by the card's margins, then to what its layout asks for, within the
    // screen.
    void MakeRoomForChrome() {
        if (box) {
            return;
        }
        // The minimum and Qt's size before show used the header margins of the unsized dialog, so a
        // dialog nothing resized starts from its size hint rather than from that size.
        if (dialog->layout()) {
            dialog->layout()->activate();
        }
        QSize given;
        if (dialog->testAttribute(Qt::WA_Resized)) {
            given = dialog->size();
            const QMargins margins = dialog->contentsMargins();
            const QSize minimum = dialog->minimumSize();
            if (given.width() > minimum.width()) {
                given.rwidth() += margins.left() + margins.right();
            }
            if (given.height() > minimum.height()) {
                given.rheight() += margins.top() + margins.bottom();
            }
        }
        QSize available(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (const QScreen* screen = dialog->screen()) {
            available = screen->availableGeometry().size();
        }
        // The header wraps at the final width, so settle the width and the header's margin first.
        const int width = std::min({std::max(given.width(), dialog->sizeHint().width()),
                                    dialog->maximumWidth(), available.width()});
        dialog->resize(width, dialog->height());
        UpdateMargins();
        if (dialog->layout()) {
            dialog->layout()->activate();
        }
        int height = std::max(given.height(), dialog->sizeHint().height());
        // sizeHint ignores word-wrapped labels, which need more height at the final width.
        if (dialog->hasHeightForWidth()) {
            height = std::max(height, dialog->heightForWidth(width));
        }
        height = std::min({height, dialog->maximumHeight(), available.height()});
        dialog->resize(QSize(width, height).expandedTo(dialog->minimumSizeHint()));
    }

    // Runs before QMessageBox::showEvent, which sizes the box from its layout.
    void PrepareMessageBox() {
        const MessageLook look = LookOf(box);
        if (!look.icon.isEmpty()) {
            box->setIconPixmap(MessageIcon(look, box->devicePixelRatioF()));
        }
        ApplyMessageSpacing();
    }

    void ApplyMessageSpacing() {
        if (auto* grid = qobject_cast<QGridLayout*>(box->layout())) {
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(0);
            grid->setVerticalSpacing(0);
        }
        if (auto* icon_label = box->findChild<QLabel*>(QStringLiteral("qt_msgboxex_icon_label"))) {
            icon_label->setContentsMargins(0, 0, MessageIconGap - 7, 0);
        }
        if (auto* informative =
                box->findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"))) {
            informative->setContentsMargins(0, MessageTitleGap, 0, 0);
        }
        if (auto* buttons = box->findChild<QDialogButtonBox*>()) {
            buttons->setContentsMargins(0, MessageButtonGap, 0, 0);
        }
        if (QCheckBox* check = box->checkBox()) {
            check->setContentsMargins(0, 12, 0, 0);
        }
    }

    // QMessageBox fits its text to the screen; Paper's message card wraps it at a fixed width
    // with the title above it, so the box is refitted each time QMessageBox sizes it.
    void FitMessageBox() {
        QLabel* label = MessageLabel();
        if (fitting || !label || !box->layout()) {
            return;
        }
        fitting = true;
        ApplyMessageSpacing();
        auto* informative = box->findChild<QLabel*>(QStringLiteral("qt_msgbox_informativelabel"));
        label->setWordWrap(true);
        int width = MessageTextWidth;
        if (label->heightForWidth(width) > 8 * label->fontMetrics().lineSpacing()) {
            width = MessageWideTextWidth;
        }
        const int title_height = title->isHidden() ? 0 : title->heightForWidth(width);
        label->setContentsMargins(0, title_height ? title_height + MessageTitleGap : 0, 0, 0);
        label->setFixedWidth(width);
        if (informative) {
            informative->setWordWrap(true);
            informative->setFixedWidth(width);
        }
        QLayout* layout = box->layout();
        layout->invalidate();
        layout->activate();
        const int total_width = layout->totalMinimumSize().width();
        const int height = layout->hasHeightForWidth() ? layout->totalHeightForWidth(total_width)
                                                       : layout->totalMinimumSize().height();
        box->setFixedSize(total_width, height);
        fitting = false;
    }

    QLabel* ProgressLabel() const {
        for (auto* label : dialog->findChildren<QLabel*>(Qt::FindDirectChildrenOnly)) {
            if (label != title && label != subtitle && label != icon && label != percent) {
                return label;
            }
        }
        return nullptr;
    }

    void PrepareProgress() {
        if (!progress || percent) {
            return;
        }
        percent = new QLabel(dialog);
        percent->setFont(Font(14, 600));
        SetTextColor(percent, Color::Text());
        if (QLabel* label = ProgressLabel()) {
            label->setFont(Font(14, 600));
            label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            label->setWordWrap(true);
            SetTextColor(label, Color::Text());
        }
        if (auto* bar = dialog->findChild<QProgressBar*>(Qt::FindDirectChildrenOnly)) {
            bar->setTextVisible(false);
            bar->style()->unpolish(bar);
            bar->style()->polish(bar);
            const auto show_percent = [this, bar] {
                const int range = bar->maximum() - bar->minimum();
                percent->setVisible(range > 0);
                if (range > 0) {
                    percent->setText(QStringLiteral("%1%").arg(
                        qRound(100.0 * (bar->value() - bar->minimum()) / range)));
                }
            };
            connect(bar, &QProgressBar::valueChanged, this, show_percent);
            show_percent();
        }
        if (auto* cancel = dialog->findChild<QPushButton*>(Qt::FindDirectChildrenOnly);
            cancel && cancel->property("kind").isNull()) {
            cancel->setProperty("kind", QStringLiteral("destructive"));
            cancel->style()->unpolish(cancel);
            cancel->style()->polish(cancel);
        }
        dialog->setFixedWidth(ProgressCardWidth + ShadowMargins.left() + ShadowMargins.right());
    }

    // Places a progress dialog's label, percentage, bar and cancel button as Paper's progress
    // card does, and fits the card's height to them. False when the dialog is no progress dialog.
    bool PlaceProgress() {
        if (!progress || !percent) {
            return false;
        }
        const QRect area = dialog->contentsRect();
        int y = area.top();
        int row = percent->isHidden() ? 0 : percent->sizeHint().height();
        if (!percent->isHidden()) {
            const int width = percent->sizeHint().width();
            percent->setGeometry(area.right() + 1 - width, y, width, row);
        }
        if (QLabel* label = ProgressLabel(); label && !label->text().isEmpty()) {
            const int width = area.width() - (percent->isHidden() ? 0 : percent->width() + 8);
            const int height = label->heightForWidth(width);
            label->setGeometry(area.left(), y, width, height);
            row = std::max(row, height);
        }
        y += row ? row + ProgressRowGap : 0;
        if (auto* bar = dialog->findChild<QProgressBar*>(Qt::FindDirectChildrenOnly)) {
            bar->setGeometry(area.left(), y, area.width(), ProgressBarHeight);
            y += ProgressBarHeight;
        }
        if (auto* cancel = dialog->findChild<QPushButton*>(Qt::FindDirectChildrenOnly);
            cancel && !cancel->isHidden()) {
            y += HeaderGap + 6;
            const QSize size = cancel->sizeHint();
            cancel->setGeometry(area.right() + 1 - size.width(), y, size.width(), size.height());
            y += size.height();
        }
        const int height =
            y - area.top() + dialog->contentsMargins().top() + dialog->contentsMargins().bottom();
        if (dialog->height() != height) {
            dialog->setFixedHeight(height);
        }
        return true;
    }

    void PrepareButtons() {
        for (auto* buttons : dialog->findChildren<QDialogButtonBox*>()) {
            if (buttons->layout()) {
                buttons->layout()->setSpacing(ButtonGap);
            }
            for (auto* button : buttons->buttons()) {
                if (buttons->buttonRole(button) == QDialogButtonBox::DestructiveRole &&
                    button->property("kind").isNull()) {
                    button->setProperty("kind", QStringLiteral("destructive"));
                    button->style()->unpolish(button);
                    button->style()->polish(button);
                }
            }
        }
        // Qt moves the default to whichever auto-default button has focus, and the accent with it;
        // Paper keeps the accent on the dialog's default button, the first accept button unless
        // one is set.
        const auto pushes = dialog->findChildren<QPushButton*>();
        QPushButton* chosen = nullptr;
        for (auto* push : pushes) {
            if (push->isDefault()) {
                chosen = push;
            }
        }
        for (auto* buttons : dialog->findChildren<QDialogButtonBox*>()) {
            for (auto* button : buttons->buttons()) {
                const auto role = buttons->buttonRole(button);
                if (!chosen &&
                    (role == QDialogButtonBox::AcceptRole || role == QDialogButtonBox::YesRole)) {
                    chosen = qobject_cast<QPushButton*>(button);
                }
            }
        }
        if (!chosen) {
            return;
        }
        // A button made default before it joined the dialog is unknown to QDialog, which then
        // loses the default when focus leaves it.
        chosen->setDefault(false);
        chosen->setDefault(true);
        for (auto* push : pushes) {
            push->setAutoDefault(push == chosen);
        }
        // When No is the default, Yes is the answer that removes or overwrites something.
        for (auto* buttons : dialog->findChildren<QDialogButtonBox*>()) {
            if (buttons->buttonRole(chosen) != QDialogButtonBox::NoRole) {
                continue;
            }
            for (auto* button : buttons->buttons()) {
                if (buttons->buttonRole(button) == QDialogButtonBox::YesRole &&
                    button->property("kind").isNull()) {
                    button->setProperty("kind", QStringLiteral("destructive"));
                    button->style()->unpolish(button);
                    button->style()->polish(button);
                }
            }
        }
        // Paper's message cards end the button row with the default button.
        auto* row = box ? qobject_cast<QBoxLayout*>(chosen->parentWidget()->layout()) : nullptr;
        if (!row || !qobject_cast<QDialogButtonBox*>(chosen->parentWidget())) {
            return;
        }
        int last = -1;
        for (int i = 0; i < row->count(); ++i) {
            if (row->itemAt(i)->widget()) {
                last = i;
            }
        }
        const int at = row->indexOf(chosen);
        if (at < 0 || at >= last) {
            return;
        }
        QWidget* before = row->itemAt(last)->widget();
        row->removeWidget(chosen);
        row->insertWidget(last, chosen);
        QWidget::setTabOrder(before, chosen);
    }

    QDialog* dialog;
    QMessageBox* box;
    QProgressDialog* progress;
    QLabel* percent = nullptr;
    QLabel* title;
    QLabel* subtitle;
    QLabel* icon;
    CircleButton* close = nullptr;
    QPointer<Scrim> scrim;
    bool shown = false;
    bool fitting = false;
    bool keep_position = false;
};

// Masks a window to a rounded rectangle. A mask has hard edges, so the corner is one pixel smaller
// than Radius::Tile and the masked window keeps no shadow; the window manager draws its frame.
class WindowCorners final : public QObject {
public:
    explicit WindowCorners(QWidget* window_) : QObject(window_), window(window_) {
        window->installEventFilter(this);
        Update();
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::WindowStateChange) {
            Update();
        }
        return false;
    }

private:
    void Update() {
        if (window->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen)) {
            window->clearMask();
            return;
        }
        QPainterPath path;
        path.addRoundedRect(QRectF(window->rect()), Radius::Tile, Radius::Tile);
        window->setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    QWidget* window;
};

} // namespace

void RoundWindowCorners(QWidget* window) {
#ifdef _WIN32
    // Windows 11 rounds the window itself (DWMWA_WINDOW_CORNER_PREFERENCE).
    static_cast<void>(window);
#else
    new WindowCorners(window);
#endif
}

Dialog::Dialog(QWidget* parent, Qt::WindowFlags flags) : QDialog(parent, flags) {
    AdoptDialogChrome(this);
}

Dialog::Dialog(QWidget* parent, Chrome) : QDialog(parent) {
    setProperty(ContentChromeProperty, true);
    AdoptDialogChrome(this, false);
}

void Dialog::SetSubtitle(const QString& subtitle) {
    setProperty(SubtitleProperty, subtitle);
}

void Dialog::SetIcon(QStringView name, const QColor& tint) {
    setProperty(IconTintProperty, tint);
    setProperty(IconProperty, name.toString());
}

void Dialog::SetCardWidth(int width) {
    setFixedWidth(width + ShadowMargins.left() + ShadowMargins.right());
}

void Dialog::SetTitleVisible(bool visible) {
    setProperty(TitleHiddenProperty, !visible);
}

void Dialog::SetCloseVisible(bool visible) {
    setProperty(CloseHiddenProperty, !visible);
}

void MarkSuccess(QMessageBox* box) {
    box->setProperty(ToneProperty, QStringLiteral("success"));
}

void AdoptDialogChrome(QDialog* dialog, bool close_button) {
    if (dialog->property(ChromeProperty).toBool() || dialog->testAttribute(Qt::WA_WState_Created)) {
        return;
    }
    dialog->setProperty(ChromeProperty, true);
    dialog->setAttribute(Qt::WA_TranslucentBackground);
    dialog->overrideWindowFlags(dialog->windowFlags() | Qt::FramelessWindowHint);
    new DialogChrome(dialog, close_button);
}

} // namespace Spotlight
