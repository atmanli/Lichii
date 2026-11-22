// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFocusEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include "common/settings.h"
#include "lichii/configuration/configuration_shared.h"
#include "lichii/configuration/shared_widget.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace Spotlight {
namespace {

QString CssColor(const QColor& color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

class Surface final : public QWidget {
public:
    Surface(QColor fill_, int radius_, QColor border_ = Qt::transparent)
        : fill{fill_}, border{border_}, radius{radius_} {
        setProperty("spotlightTransparent", true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(fill);
        painter.setPen(border.alpha() == 0 ? QPen(Qt::NoPen) : QPen(border));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

private:
    QColor fill;
    QColor border;
    int radius;
};

class SettingsBackdrop final : public QWidget {
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QLinearGradient fill(0, 0, width() * 0.3, height());
        fill.setColorAt(0, SettingsColor::BackdropTop());
        fill.setColorAt(1, SettingsColor::BackdropBottom());
        painter.fillRect(rect(), fill);
    }
};

class IconTile final : public QWidget {
public:
    IconTile(QString icon_, QColor color_) : icon{std::move(icon_)}, color{color_} {
        setFixedSize(40, 40);
        setProperty("spotlightTransparent", true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(rect(), Radius::Row, Radius::Row);
        painter.drawPixmap(10, 10, IconPixmap(icon, 20, color, devicePixelRatioF()));
    }

private:
    QString icon;
    QColor color;
};

class SettingsRow final : public QWidget {
public:
    explicit SettingsRow(bool divider_) : divider{divider_} {
        setAttribute(Qt::WA_Hover);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget* old, QWidget* current) {
            if ((old && isAncestorOf(old)) || (current && isAncestorOf(current))) {
                update();
            }
        });
    }

protected:
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto* focused = QApplication::focusWidget();
        if (underMouse() || (focused && isAncestorOf(focused))) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(Color::Accent(), 2));
            painter.setBrush(Color::AccentSoft());
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), Radius::Card,
                                    Radius::Card);
        } else if (divider) {
            painter.setPen(Color::StrokeSubtle());
            painter.drawLine(0, 0, width(), 0);
        }
    }

private:
    bool divider;
};

class NavigationButton final : public QAbstractButton {
public:
    NavigationButton(const QString& title, QString icon_, QColor icon_color_)
        : icon{std::move(icon_)}, icon_color{icon_color_} {
        setText(title);
        setCheckable(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(44);
    }

protected:
    void focusInEvent(QFocusEvent* event) override {
        keyboard_focus = event->reason() != Qt::MouseFocusReason;
        QAbstractButton::focusInEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        keyboard_focus = false;
        QAbstractButton::mousePressEvent(event);
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool outlined = hasFocus() && keyboard_focus;
        painter.setPen(outlined ? QPen(Color::Accent()) : QPen(Qt::NoPen));
        if (isChecked() || underMouse() || outlined) {
            painter.setBrush(isChecked()    ? Color::AccentSoft()
                             : underMouse() ? Color::Fill()
                                            : Qt::transparent);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Radius::Row,
                                    Radius::Row);
        }
        painter.drawPixmap(12, 12, IconPixmap(icon, 20, icon_color, devicePixelRatioF()));
        painter.setFont(Font(14, isChecked() ? 600 : 500));
        painter.setPen(Color::Text());
        painter.drawText(rect().adjusted(44, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, width() - 52));
    }

private:
    QString icon;
    QColor icon_color;
    bool keyboard_focus = false;
};

class ManageButton final : public QAbstractButton {
public:
    explicit ManageButton(const QString& label) {
        setText(label);
        setFont(Font(13, 600));
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setProperty("spotlightTransparent", true);
        setFixedSize(fontMetrics().horizontalAdvance(label) + (label.isEmpty() ? 32 : 48), 36);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (hasFocus()) {
            painter.setPen(Color::Accent());
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        }
        const auto ink = !isEnabled()   ? Color::TextFaint()
                         : underMouse() ? Color::Text()
                                        : Color::TextSecondary();
        painter.setPen(ink);
        painter.drawText(rect().adjusted(12, 0, -26, 0), Qt::AlignLeft | Qt::AlignVCenter, text());
        painter.drawPixmap(width() - 22, (height() - 14) / 2,
                           IconPixmap(QStringLiteral("caret-right"), 14, ink, devicePixelRatioF()));
    }
};

class ComboPill final : public QComboBox {
public:
    explicit ComboPill(std::function<QString(const QComboBox&, int)> label_ = {})
        : label{std::move(label_)} {
        setFont(Font(13, 600));
        setFixedHeight(36);
        setCursor(Qt::PointingHandCursor);
        setProperty("spotlightTransparent", true);
        connect(this, &QComboBox::currentIndexChanged, this, [this] { Refresh(); });
    }

    void Refresh() {
        const auto display = ClosedText();
        setFixedWidth(qBound(64, fontMetrics().horizontalAdvance(display) + 48, 224));
        setToolTip(currentText().isEmpty() ? display : currentText());
        update();
    }

    void showPopup() override {
        const QFontMetrics popup_font(view()->font());
        int popup_width = width();
        for (int i = 0; i < count(); ++i) {
            popup_width = qMax(popup_width, popup_font.horizontalAdvance(itemText(i)) + 48);
        }
        view()->setMinimumWidth(popup_width);
        QComboBox::showPopup();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? QPen(Color::Accent()) : QPen(Qt::NoPen));
        painter.setBrush(underMouse() && isEnabled() ? Color::ControlHover() : Color::Control());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
        const auto ink = isEnabled() ? Color::Text() : Color::TextFaint();
        const QRect label_rect = rect().adjusted(12, 0, -26, 0);
        painter.setFont(font());
        painter.setPen(ink);
        painter.drawText(
            label_rect, Qt::AlignLeft | Qt::AlignVCenter,
            fontMetrics().elidedText(ClosedText(), Qt::ElideRight, label_rect.width()));
        painter.drawPixmap(width() - 22, (height() - 12) / 2,
                           IconPixmap(QStringLiteral("caret-down"), 12, ink, devicePixelRatioF()));
    }

private:
    QString ClosedText() const {
        if (currentIndex() < 0) {
            return placeholderText();
        }
        const auto short_text = label ? label(*this, currentIndex()) : QString{};
        return short_text.isEmpty() ? currentText() : short_text;
    }

    std::function<QString(const QComboBox&, int)> label;
};

class WatchWidget final : public QObject {
public:
    WatchWidget(QWidget* source, QWidget* target, QEvent::Type event_, std::function<void()> sync_)
        : QObject(target), event{event_}, sync{std::move(sync_)} {
        source->installEventFilter(this);
        sync();
    }

protected:
    bool eventFilter(QObject*, QEvent* change) override {
        if (change->type() == event) {
            sync();
        }
        return false;
    }

private:
    QEvent::Type event;
    std::function<void()> sync;
};

void MirrorEnabled(QWidget* source, QWidget* target) {
    new WatchWidget(source, target, QEvent::EnabledChange,
                    [source, target] { target->setEnabled(source->isEnabled()); });
}

QLabel* DisplayText(const QString& text, int size) {
    auto* label = SettingsPage::Text(text, size);
    label->setFont(DisplayFont(size));
    return label;
}

bool Matches(const QString& text, const QString& query) {
    return text.contains(query, Qt::CaseInsensitive);
}

bool MatchesWidget(QWidget* widget, const QString& query) {
    if (Matches(widget->accessibleName(), query)) {
        return true;
    }
    for (auto* label : widget->findChildren<QLabel*>()) {
        if (Matches(label->text(), query)) {
            return true;
        }
    }
    for (auto* button : widget->findChildren<QAbstractButton*>()) {
        if (Matches(button->text(), query)) {
            return true;
        }
    }
    for (auto* group : widget->findChildren<QGroupBox*>()) {
        if (Matches(group->title(), query)) {
            return true;
        }
    }
    return false;
}

bool SelectMatchingTab(QTabWidget* tabs, const QString& query) {
    for (int i = 0; i < tabs->count(); ++i) {
        if (!tabs->isTabVisible(i)) {
            continue;
        }
        auto* page = tabs->widget(i);
        for (auto* nested : page->findChildren<QTabWidget*>()) {
            if (SelectMatchingTab(nested, query)) {
                tabs->setCurrentIndex(i);
                return true;
            }
        }
        if (Matches(tabs->tabText(i), query) || MatchesWidget(page, query)) {
            tabs->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

} // namespace

int PerformancePresetIndex(const PerformancePreset& values) {
    for (int index = 0; index < static_cast<int>(PerformancePresets.size()); ++index) {
        if (PerformancePresets[index] == values) {
            return index;
        }
    }
    return -1;
}

QString PerformancePresetName(int index) {
    switch (index) {
    case 0:
        return SettingsPage::tr("Low-end");
    case 1:
        return SettingsPage::tr("Mid-range");
    case 2:
        return SettingsPage::tr("High-end");
    default:
        return SettingsPage::tr("Custom");
    }
}

SettingsPage::SettingsPage(QWidget* parent, bool per_game) : QWidget(parent) {
    setStyleSheet(
        QStringLiteral("QWidget[spotlightTransparent=true] { background: transparent; }"));
    setProperty("spotlightTransparent", true);
    auto* shell = new QHBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* navigation = new Surface(SettingsColor::Sidebar(), 0);
    navigation->setFixedWidth(264);
    sidebar = new QVBoxLayout(navigation);
    sidebar->setContentsMargins(14, 18, 14, 18);
    sidebar->setSpacing(2);
    shell->addWidget(navigation);

    auto* back_row = new QHBoxLayout;
    back_row->setContentsMargins(4, 0, 4, 0);
    back_row->setSpacing(8);
    auto* back = new CircleButton(QStringLiteral("caret-left"), 32);
    back->setAccessibleName(tr("Library"));
    back->setToolTip(tr("Return to the library without applying changes"));
    back_row->addWidget(back);
    back_row->addWidget(Text(tr("Library"), 13, 500, true), 1);
    sidebar->addLayout(back_row);
    connect(back, &QAbstractButton::clicked, this, &SettingsPage::BackRequested);
    sidebar->addSpacing(10);

    if (per_game) {
        auto* identity = new QHBoxLayout;
        identity->setContentsMargins(4, 0, 4, 6);
        identity->setSpacing(10);
        game_icon = new QLabel;
        game_icon->setFixedSize(44, 44);
        identity->addWidget(game_icon);
        auto* names = new QVBoxLayout;
        names->setSpacing(2);
        game_title = Text({}, 15, 600);
        game_title->setWordWrap(true);
        names->addWidget(game_title);
        names->addWidget(Text(tr("Game settings"), 12, 500, true));
        identity->addLayout(names, 1);
        sidebar->addLayout(identity);
    } else {
        auto* title = DisplayText(tr("Settings"), 22);
        title->setContentsMargins(6, 2, 6, 6);
        sidebar->addWidget(title);
    }

    search = new QLineEdit;
    search->setFont(Font(13));
    search->setClearButtonEnabled(true);
    search->setPlaceholderText(per_game ? tr("Search this game's settings")
                                        : tr("Search settings"));
    search->setAccessibleName(search->placeholderText());
    search->addAction(Icon(QStringLiteral("magnifying-glass"), Color::TextSecondary(), 16),
                      QLineEdit::LeadingPosition);
    search->setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; color: %1; border-radius: 19px; "
                       "border: 1px solid transparent; padding-left: 5px; } "
                       "QLineEdit:focus { border-color: %2; }")
            .arg(CssColor(Color::Text()), CssColor(Color::Accent())));
    auto* search_pill = new Surface(Color::Fill(), 19);
    search_pill->setFixedHeight(38);
    auto* search_layout = new QVBoxLayout(search_pill);
    search_layout->setContentsMargins(0, 0, 0, 0);
    search_layout->addWidget(search);
    sidebar->addWidget(search_pill);
    sidebar->addSpacing(8);
    sidebar->addStretch();

    auto* advanced_row = new QHBoxLayout;
    advanced_row->setContentsMargins(10, 8, 10, 8);
    advanced_row->addWidget(Text(tr("Show advanced"), 13, 500, true), 1);
    advanced = new ToggleSwitch;
    advanced->setAccessibleName(tr("Show advanced"));
    advanced_row->addWidget(advanced);
    sidebar->addLayout(advanced_row);

    auto* background = new SettingsBackdrop;
    shell->addWidget(background, 1);
    main_column = new QVBoxLayout(background);
    main_column->setContentsMargins(0, 0, 0, 0);
    main_column->setSpacing(0);
    scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    auto* body = new QWidget;
    body->setProperty("spotlightTransparent", true);
    scroll->setWidget(body);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setProperty("spotlightTransparent", true);
    main_column->addWidget(scroll, 1);
    content = new QVBoxLayout(body);
    content->setContentsMargins(40, 22, 40, 24);
    content->setSpacing(14);
    auto* header = new QVBoxLayout;
    header->setSpacing(4);
    heading = DisplayText({}, 28);
    subtitle = Text({}, 14, 500, true);
    subtitle->setWordWrap(true);
    header->addWidget(heading);
    header->addWidget(subtitle);
    content->addLayout(header);
    empty = Text(tr("No matching settings"), 15, 500, true);
    empty->hide();
    content->addWidget(empty);
    content->addStretch();

    connect(search, &QLineEdit::textChanged, this, &SettingsPage::UpdateSearch);
    connect(advanced, &QAbstractButton::toggled, this, &SettingsPage::UpdateSearch);
    auto* focus_search = new QShortcut(QKeySequence::Find, this);
    connect(focus_search, &QShortcut::activated, search, [this] {
        search->setFocus();
        search->selectAll();
    });
}

int SettingsPage::AddCategory(const QString& title, const QString& subtitle_, const QString& icon,
                              const QColor& color, QString page_title) {
    const int index = static_cast<int>(categories.size());
    auto* button = new NavigationButton(title, icon, color);
    sidebar->insertWidget(sidebar->count() - 2, button);
    QWidget::setTabOrder(categories.isEmpty() ? static_cast<QWidget*>(search)
                                             : categories.back().button,
                         button);
    QWidget::setTabOrder(button, advanced);
    connect(button, &QAbstractButton::clicked, this, [this, index] { SetCategory(index); });

    auto* panel = new QWidget;
    panel->setProperty("spotlightTransparent", true);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    content->insertWidget(content->count() - 1, panel);
    panel->hide();
    if (page_title.isEmpty()) {
        page_title = title;
    }
    categories.push_back({title, std::move(page_title), subtitle_, button, panel, {}});
    return index;
}

QVBoxLayout* SettingsPage::AddSection(int category, const QString& title) {
    auto* section = new QWidget;
    section->setProperty("spotlightTransparent", true);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    if (!title.isEmpty()) {
        auto* label = Text(title, 13, 600, true);
        label->setContentsMargins(6, 3, 6, 0);
        layout->addWidget(label);
    }
    auto* surface = new Surface(Color::FillFaint(), Radius::Card, Color::StrokeSubtle());
    auto* rows = new QVBoxLayout(surface);
    rows->setContentsMargins(1, 1, 1, 1);
    rows->setSpacing(0);
    layout->addWidget(surface);
    AddBlock(category, section, title);
    return rows;
}

void SettingsPage::AddBlock(int category, QWidget* widget, QString search_label, bool advanced_) {
    auto& entry = categories[category];
    auto* layout = qobject_cast<QVBoxLayout*>(entry.panel->layout());
    const int index =
        !advanced_ && entry.advanced_tabs != nullptr ? layout->count() - 1 : layout->count();
    layout->insertWidget(index, widget);
    if (!qobject_cast<QTabWidget*>(widget)) {
        widget->setProperty("spotlightTransparent", true);
    }
    entry.blocks.push_back({widget, std::move(search_label), advanced_});
    widget->setVisible(!advanced_ || advanced->isChecked());
}

void SettingsPage::AddPage(int category, QWidget* page, const QString& name, bool advanced_) {
    auto& entry = categories[category];
    auto*& tabs = advanced_ ? entry.advanced_tabs : entry.basic_tabs;
    if (tabs == nullptr) {
        tabs = new QTabWidget;
        tabs->setUsesScrollButtons(true);
        tabs->setDocumentMode(true);
        tabs->setMinimumHeight(450);
        AddBlock(category, tabs, advanced_ ? tr("Advanced") : name, advanced_);
    }
    tabs->addTab(ConfigurationShared::ScrollTab(page), name);
    tabs->tabBar()->setVisible(tabs->count() > 1);
}

void SettingsPage::SetCategory(int category) {
    if (category < 0 || category >= categories.size()) {
        return;
    }
    if (filtering && categories[category].button->isHidden()) {
        search->clear();
    }
    if (current_category != -1 && current_category != category) {
        categories[current_category].panel->hide();
        categories[current_category].button->setChecked(false);
    }
    current_category = category;
    const auto& entry = categories[category];
    entry.panel->show();
    entry.button->setChecked(true);
    heading->setText(entry.page_title);
    subtitle->setText(entry.subtitle);
    subtitle->setVisible(!entry.subtitle.isEmpty());
    empty->hide();
    scroll->verticalScrollBar()->setValue(0);
}

void SettingsPage::ShowAdvancedPage(int category, QWidget* page) {
    search->clear();
    SetCategory(category);
    advanced->setChecked(true);
    auto* tabs = categories[category].advanced_tabs;
    if (tabs == nullptr) {
        return;
    }
    for (int i = 0; i < tabs->count(); ++i) {
        if (!tabs->widget(i)->isAncestorOf(page)) {
            continue;
        }
        tabs->setCurrentIndex(i);
        QTimer::singleShot(0, scroll, [this, tabs] {
            scroll->verticalScrollBar()->setValue(tabs->mapTo(scroll->widget(), QPoint{}).y());
        });
        return;
    }
}

void SettingsPage::SetFooter(QWidget* widget) {
    widget->setProperty("spotlightTransparent", true);
    main_column->addWidget(widget);
}

void SettingsPage::SetBanner(QWidget* banner) {
    banner->setProperty("spotlightTransparent", true);
    content->insertWidget(1, banner);
}

void SettingsPage::SetGame(const QString& title, const QPixmap& icon) {
    game_title->setText(title);
    game_title->setToolTip(title);
    game_icon->setPixmap(WholeIcon(icon, 44, devicePixelRatioF()));
}

void SettingsPage::UpdateSearch() {
    const QString query = search->text().trimmed();
    if (!query.isEmpty() && !filtering) {
        unfiltered_category = current_category;
    }
    filtering = !query.isEmpty();
    int first = -1;
    for (int i = 0; i < categories.size(); ++i) {
        auto& entry = categories[i];
        const bool category_match = Matches(entry.title, query);
        bool any = category_match;
        for (const auto& block : entry.blocks) {
            if (block.advanced && !advanced->isChecked()) {
                block.widget->hide();
                continue;
            }
            const bool all = category_match || Matches(block.search_label, query);
            bool match = all;
            if (!all) {
                if (auto* tabs = qobject_cast<QTabWidget*>(block.widget)) {
                    match = SelectMatchingTab(tabs, query);
                } else {
                    for (auto* nested_tabs : block.widget->findChildren<QTabWidget*>()) {
                        match |= SelectMatchingTab(nested_tabs, query);
                    }
                    match |= MatchesWidget(block.widget, query);
                }
            }
            if (auto* rows = block.widget->findChild<QWidget*>(QStringLiteral("settingsGroup"))) {
                for (auto* row : rows->findChildren<QWidget*>(QStringLiteral("settingsRow"),
                                                              Qt::FindDirectChildrenOnly)) {
                    row->setVisible(all || MatchesWidget(row, query));
                }
            }
            block.widget->setVisible(match);
            any |= match;
        }
        entry.button->setVisible(any);
        if (any && first == -1) {
            first = i;
        }
    }

    if (first == -1) {
        if (current_category != -1) {
            categories[current_category].panel->hide();
        }
        heading->setText(tr("Search results"));
        subtitle->hide();
        empty->show();
        return;
    }
    if (!filtering && unfiltered_category != -1) {
        SetCategory(std::exchange(unfiltered_category, -1));
    } else if (current_category == -1 || categories[current_category].button->isHidden()) {
        SetCategory(first);
    } else if (filtering) {
        SetCategory(current_category);
    }
}

QLabel* SettingsPage::Text(const QString& text, int size, int weight, bool secondary) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setFont(Font(size, weight));
    label->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; border: none; color: %1; }")
            .arg(CssColor(secondary ? Color::TextSecondary() : Color::Text())));
    return label;
}

QPixmap SettingsPage::WholeIcon(const QPixmap& icon, int size, qreal dpr) {
    QPixmap result(qRound(size * dpr), qRound(size * dpr));
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    if (icon.isNull()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(QRectF(0, 0, size, size), 9, 9);
        painter.drawPixmap(
            size / 4, size / 4,
            IconPixmap(QStringLiteral("game-controller"), size / 2, Color::TextMuted(), dpr));
        return result;
    }
    const QSizeF image_size =
        icon.size().scaled(QSize(qRound(size * dpr), qRound(size * dpr)), Qt::KeepAspectRatio) /
        dpr;
    const QRectF target((size - image_size.width()) / 2, (size - image_size.height()) / 2,
                        image_size.width(), image_size.height());
    QPainterPath path;
    path.addRoundedRect(target, 9, 9);
    painter.setClipPath(path);
    painter.drawPixmap(target, icon, icon.rect());
    return result;
}

QWidget* SettingsPage::Card(const QString& icon, int image_size) {
    if (image_size > 0) {
        auto* tile = new Surface(Color::Fill(), Radius::Card);
        tile->setFixedSize(image_size, image_size);
        auto* layout = new QVBoxLayout(tile);
        auto* label = new QLabel;
        label->setPixmap(
            IconPixmap(icon, image_size / 2, Color::Text(), tile->devicePixelRatioF()));
        layout->addWidget(label, 0, Qt::AlignCenter);
        return tile;
    }
    return new Surface(Color::FillFaint(), Radius::Card, Color::StrokeSubtle());
}

QWidget* SettingsPage::Badge(const QString& text, const QColor& fill, const QColor& ink) {
    auto* badge = new Surface(fill, 12);
    auto* layout = new QHBoxLayout(badge);
    layout->setContentsMargins(10, 0, 10, 0);
    auto* label = Text(text, 12, 600);
    label->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; color: %1; }").arg(CssColor(ink)));
    layout->addWidget(label, 0, Qt::AlignVCenter);
    badge->setFixedHeight(24);
    badge->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return badge;
}

QLabel* SettingsPage::AddRow(QVBoxLayout* group, const QString& title, const QString& description,
                             const QString& icon, const QColor& color, QWidget* control) {
    auto* row = new SettingsRow(group->count() > 0);
    row->setObjectName(QStringLiteral("settingsRow"));
    row->setProperty("spotlightTransparent", true);
    group->parentWidget()->setObjectName(QStringLiteral("settingsGroup"));
    row->setMinimumHeight(72);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(13, 8, 17, 8);
    layout->setSpacing(14);
    layout->addWidget(new IconTile(icon, color), 0, Qt::AlignVCenter);
    auto* copy = new QVBoxLayout;
    copy->setSpacing(3);
    auto* name = Text(title, 15, 600);
    auto* detail = Text(description, 13, 400, true);
    detail->setWordWrap(true);
    detail->setVisible(!description.isEmpty());
    copy->addStretch();
    copy->addWidget(name);
    copy->addWidget(detail);
    copy->addStretch();
    layout->addLayout(copy, 1);
    if (control != nullptr) {
        control->setAccessibleName(title);
        layout->addWidget(control);
    }
    group->addWidget(row);
    return detail;
}

QAbstractButton* SettingsPage::Manage(const QString& text) {
    return new ManageButton(text);
}

ConfigurationShared::Widget* SettingsPage::Editor(QWidget* page,
                                                  const Settings::BasicSetting& setting) {
    for (auto* editor : page->findChildren<ConfigurationShared::Widget*>()) {
        if (editor->Valid() && editor->SettingId() == setting.Id()) {
            return editor;
        }
    }
    return nullptr;
}

QString SettingsPage::ClosedSettingLabel(const QComboBox* source, u32 setting_id, int index) {
    const auto value = source->itemData(index).toUInt();
    if (setting_id == Settings::values.resolution_setup.Id()) {
        Settings::ResolutionScalingInfo info{};
        Settings::TranslateResolutionInfo(static_cast<Settings::ResolutionSetup>(value), info);
        return tr("%1×").arg(info.up_factor);
    } else if (setting_id == Settings::values.scaling_filter.Id()) {
        switch (static_cast<Settings::ScalingFilter>(value)) {
        case Settings::ScalingFilter::Fsr:
            return tr("FSR 1");
        case Settings::ScalingFilter::Fsr3:
            return tr("FSR 1 Soft");
        case Settings::ScalingFilter::Sgsr:
            return tr("SGSR");
        case Settings::ScalingFilter::SgsrEdge:
            return tr("SGSR EdgeDir");
        default:
            break;
        }
    }
    return source->itemText(index);
}

QComboBox* SettingsPage::BindCombo(QComboBox* source, const QString& placeholder,
                                   std::function<QString(const QComboBox&, int)> closed_label) {
    auto* proxy = new ComboPill(std::move(closed_label));
    proxy->setModel(source->model());
    proxy->setPlaceholderText(placeholder);
    const auto sync = [source, proxy] {
        if (proxy->currentIndex() != source->currentIndex()) {
            const QSignalBlocker blocker(proxy);
            proxy->setCurrentIndex(source->currentIndex());
        }
        proxy->Refresh();
    };
    MirrorEnabled(source, proxy);
    sync();
    connect(source, &QComboBox::currentIndexChanged, proxy, sync);
    connect(source, &QComboBox::currentTextChanged, proxy, sync);
    connect(proxy, &QComboBox::activated, source,
            [source](int index) { SetComboIndex(source, index); });
    return proxy;
}

QWidget* SettingsPage::BindFrameGeneration(QWidget* graphics) {
    const auto& settings = Settings::values;
    auto* frame = Editor(graphics, settings.frame_gen);
    if (frame == nullptr) {
        return nullptr;
    }
    auto* type_editor = Editor(graphics, settings.frame_gen_type);
    auto* multiplier_editor = Editor(graphics, settings.frame_gen_multiplier);
    if (frame->restore_button || !type_editor || !type_editor->combobox || !multiplier_editor ||
        !multiplier_editor->spinbox) {
        return BindSetting(frame);
    }
    auto* enabled = frame->checkbox;
    auto* method = type_editor->combobox;
    auto* multiplier = multiplier_editor->spinbox;
    if (!method->isEnabled() || !multiplier->isEnabled() || method->count() == 0) {
        return BindSetting(frame);
    }
    struct Choice {
        int method;
        int multiplier;
    };
    QList<Choice> choices{{-1, 0}};
    QStringList names{tr("Off")};
    QStringList short_names{tr("Off")};
    for (int i = 0; i < method->count(); ++i) {
        if (!(method->model()->flags(method->model()->index(i, method->modelColumn())) &
              Qt::ItemIsEnabled)) {
            continue;
        }
        QString short_name;
        switch (static_cast<Settings::FrameGenType>(method->itemData(i).toUInt())) {
        case Settings::FrameGenType::Lsfg:
            short_name = tr("LSFG");
            break;
        case Settings::FrameGenType::OpticalFlow:
            short_name = tr("Optical Flow");
            break;
        case Settings::FrameGenType::NvidiaDlssG:
            short_name = tr("DLSS FG");
            break;
        case Settings::FrameGenType::FlyHigh:
            short_name = tr("FlyHigh");
            break;
        }
        for (int rate = multiplier->minimum(); rate <= multiplier->maximum(); ++rate) {
            choices.append({i, rate});
            names.append(tr("%1 %2×").arg(method->itemText(i)).arg(rate));
            short_names.append(tr("%1 %2×").arg(short_name).arg(rate));
        }
    }
    if (choices.size() < 2) {
        return BindSetting(frame);
    }
    auto* combo =
        new ComboPill([short_names](const QComboBox&, int i) { return short_names.value(i); });
    combo->setPlaceholderText(tr("On"));
    combo->addItems(names);
    MirrorEnabled(enabled, combo);
    const auto sync = [combo, enabled, method, multiplier, choices] {
        int index = -1;
        if (!enabled->isChecked()) {
            index = 0;
        } else {
            for (int i = 1; i < choices.size(); ++i) {
                if (choices[i].method == method->currentIndex() &&
                    choices[i].multiplier == multiplier->value()) {
                    index = i;
                    break;
                }
            }
        }
        combo->setCurrentIndex(index);
        combo->Refresh();
    };
    connect(combo, &QComboBox::activated, combo,
            [enabled, method, multiplier, choices, sync](int i) {
                const Choice choice = choices.at(i);
                if (i > 0) {
                    SetComboIndex(method, choice.method);
                    multiplier->setValue(choice.multiplier);
                }
                if (enabled->isChecked() != (i > 0)) {
                    enabled->click();
                }
                sync();
            });
    connect(enabled, &QCheckBox::toggled, combo, sync);
    connect(method, &QComboBox::currentIndexChanged, combo, sync);
    connect(multiplier, &QSpinBox::valueChanged, combo, sync);
    sync();
    return combo;
}

void SettingsPage::SetComboIndex(QComboBox* source, int index) {
    if (source->isEnabled() && index >= 0 && index < source->count()) {
        source->setCurrentIndex(index);
        emit source->activated(index);
    }
}

QWidget* SettingsPage::BindToggle(QAbstractButton* source) {
    auto* toggle = new ToggleSwitch;
    toggle->setChecked(source->isChecked());
    MirrorEnabled(source, toggle);
    connect(source, &QAbstractButton::toggled, toggle, &QAbstractButton::setChecked);
    connect(toggle, &QAbstractButton::clicked, source, [source](bool checked) {
        if (source->isChecked() != checked) {
            source->click();
        }
    });
    return toggle;
}

QWidget* SettingsPage::BindToggle(QGroupBox* source) {
    auto* toggle = new ToggleSwitch;
    toggle->setChecked(source->isChecked());
    MirrorEnabled(source, toggle);
    connect(source, &QGroupBox::toggled, toggle, &QAbstractButton::setChecked);
    connect(toggle, &QAbstractButton::toggled, source, &QGroupBox::setChecked);
    return toggle;
}

void SettingsPage::WatchOverride(ConfigurationShared::Widget* source, QWidget* context,
                                 std::function<void()> update) {
    new WatchWidget(source, context, QEvent::DynamicPropertyChange, std::move(update));
}

QWidget* SettingsPage::BindSetting(ConfigurationShared::Widget* source) {
    QWidget* editor = nullptr;
    if (source->combobox != nullptr) {
        editor = BindCombo(source->combobox, {},
                           [id = source->SettingId()](const QComboBox& combo, int index) {
                               return ClosedSettingLabel(&combo, id, index);
                           });
    } else if (auto* slider = source->slider) {
        auto* proxy = new QSlider(Qt::Horizontal);
        proxy->setFixedWidth(150);
        proxy->setRange(slider->minimum(), slider->maximum());
        proxy->setValue(slider->value());
        auto* value = Text(QString::number(proxy->value()), 13);
        MirrorEnabled(slider, proxy);
        connect(slider, &QSlider::valueChanged, proxy, &QSlider::setValue);
        connect(proxy, &QSlider::valueChanged, proxy, [slider, value](int next) {
            value->setText(QString::number(next));
            if (slider->value() != next) {
                slider->setValue(next);
                emit slider->actionTriggered(QAbstractSlider::SliderMove);
            }
        });
        editor = new QWidget;
        editor->setProperty("spotlightTransparent", true);
        auto* horizontal = new QHBoxLayout(editor);
        horizontal->setContentsMargins(0, 0, 0, 0);
        horizontal->addWidget(proxy);
        horizontal->addWidget(value);
    } else if (auto* spin = source->spinbox) {
        auto* proxy = new QSpinBox;
        proxy->setFont(Font(13, 600));
        proxy->setFixedHeight(36);
        proxy->setRange(spin->minimum(), spin->maximum());
        proxy->setSpecialValueText(tr("Auto"));
        proxy->setValue(spin->value());
        proxy->setStyleSheet(
            QStringLiteral("QSpinBox { background: %1; color: %2; border: 1px solid transparent; "
                           "border-radius: 17px; padding-left: 10px; } "
                           "QSpinBox:focus { border-color: %3; }")
                .arg(CssColor(Color::Control()), CssColor(Color::Text()),
                     CssColor(Color::Accent())));
        MirrorEnabled(spin, proxy);
        connect(spin, &QSpinBox::valueChanged, proxy, &QSpinBox::setValue);
        connect(proxy, &QSpinBox::valueChanged, spin, &QSpinBox::setValue);
        editor = proxy;
    } else if (source->checkbox != nullptr) {
        editor = BindToggle(source->checkbox);
    }
    if (editor == nullptr || source->restore_button == nullptr) {
        return editor;
    }
    auto* combined = new QWidget;
    combined->setProperty("spotlightTransparent", true);
    auto* layout = new QHBoxLayout(combined);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* badge = Badge(tr("Changed"), Color::AccentSoft(), Color::AccentLight());
    auto* reset = new CircleButton(QStringLiteral("arrow-counter-clockwise"), 32);
    reset->setAccessibleName(tr("Use global setting"));
    reset->setToolTip(reset->accessibleName());
    reset->setFocusPolicy(Qt::StrongFocus);
    connect(reset, &QAbstractButton::clicked, source->restore_button, &QAbstractButton::click);
    WatchOverride(source, combined, [source, badge, reset] {
        const bool custom = source->property("custom").toBool();
        badge->setVisible(custom);
        reset->setVisible(custom);
    });
    MirrorEnabled(source, reset);
    layout->addWidget(badge);
    layout->addWidget(editor);
    layout->addWidget(reset);
    return combined;
}

} // namespace Spotlight
