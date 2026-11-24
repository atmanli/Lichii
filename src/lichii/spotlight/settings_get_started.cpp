// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <utility>
#include <QAbstractButton>
#include <QDir>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "core/core.h"
#include "frontend_common/content_manager.h"
#include "frontend_common/firmware_manager.h"
#include "lichii/spotlight/settings_get_started.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/top_bar.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/content.h"

namespace Spotlight {
namespace {

constexpr int StepCount = 5;
constexpr int SidebarWidth = 264;
constexpr qreal TileSize = 50;

QLabel* Label(const QString& text, const QFont& font, const QColor& color, int line_height = 0) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setFont(font);
    label->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; border: none; color: %1; }")
            .arg(color.name(QColor::HexArgb)));
    if (line_height > 0) {
        label->setFixedHeight(line_height);
    }
    return label;
}

QString FirmwareVersion(Core::System& system) {
    if (!FirmwareManager::CheckFirmwarePresence(system)) {
        return {};
    }
    const auto [firmware, result] = FirmwareManager::GetFirmwareVersion(system);
    return result.IsError() ? QString{} : QString::fromUtf8(firmware.display_version.data());
}

bool IsKeyboardFocus(const QFocusEvent* event) {
    return event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
}

bool TitleKeysPresent() {
    return Common::FS::Exists(Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir) /
                              "title.keys");
}

class Panel final : public QWidget {
public:
    explicit Panel(QWidget* parent) : QWidget(parent) {
        setProperty("spotlightTransparent", true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Color::StrokeSubtle());
        painter.setBrush(Color::FillFaint());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Radius::Row,
                                Radius::Row);
    }
};

} // namespace

class GetStarted::Step final : public QAbstractButton {
public:
    Step(int number_, const QString& name, QWidget* parent)
        : QAbstractButton(parent), number{number_} {
        setText(name);
        setAccessibleName(name);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(56);
    }

    void SetState(bool selected_, bool done_, QString detail_) {
        selected = selected_;
        done = done_;
        detail = std::move(detail_);
        update();
    }

protected:
    void focusInEvent(QFocusEvent* event) override {
        keyboard_focus = IsKeyboardFocus(event);
        QAbstractButton::focusInEvent(event);
    }

    void enterEvent(QEnterEvent*) override {
        hover.SetHovered(true);
    }

    void leaveEvent(QEvent*) override {
        hover.SetHovered(false);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() && keyboard_focus ? QPen(Color::Accent()) : QPen(Qt::NoPen));
        QColor clear = Color::FillFaint();
        clear.setAlpha(0);
        painter.setBrush(selected ? Color::AccentSoft()
                                  : Mix(clear, Color::FillFaint(), hover.Value()));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Radius::Tile,
                                Radius::Tile);

        const QRectF circle(8, 13, 30, 30);
        painter.setPen(Qt::NoPen);
        painter.setBrush(selected ? Color::AccentSoft() : Color::Fill());
        painter.drawEllipse(circle);
        if (selected) {
            painter.setPen(QPen(Color::Accent(), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(circle.adjusted(1, 1, -1, -1));
        }
        painter.setPen(Color::Text());
        painter.setFont(Font(13, 700));
        painter.drawText(circle, Qt::AlignCenter, QString::number(number));

        const int text_width = width() - 58;
        painter.setPen(selected ? Color::Text() : Color::TextSecondary());
        painter.setFont(Font(15, selected ? 600 : 500));
        painter.drawText(QRect(50, detail.isEmpty() ? 18 : 10, text_width, 20),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         painter.fontMetrics().elidedText(text(), Qt::ElideRight, text_width));
        if (!detail.isEmpty()) {
            painter.setPen(done ? SettingsColor::Green() : Color::TextSecondary());
            painter.setFont(Font(12, 400));
            painter.drawText(QRect(50, 30, text_width, 16), Qt::AlignVCenter | Qt::AlignLeft,
                             detail);
        }
    }

private:
    int number;
    bool selected = false;
    bool done = false;
    bool keyboard_focus = false;
    QString detail;
    HoverFade hover{this};
};

class GetStarted::Card final : public QAbstractButton {
public:
    Card(const Option& option, QWidget* parent)
        : QAbstractButton(parent), icon{option.icon}, icon_color{option.icon_color},
          done{option.done}, margin{option.done ? 22 : 21} {
        setAccessibleName(option.title);
        setAccessibleDescription(option.description);
        setCheckable(true);
        setChecked(done);
        if (option.action) {
            setFocusPolicy(Qt::TabFocus);
            setCursor(Qt::PointingHandCursor);
            connect(this, &QAbstractButton::clicked, this, option.action);
        } else {
            setFocusPolicy(Qt::NoFocus);
            setAttribute(Qt::WA_TransparentForMouseEvents);
        }
        setMinimumHeight(option.status.isEmpty() ? 94 : 112);

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(margin, margin, margin, margin);
        row->setSpacing(0);
        row->addSpacing(TileSize + 18);
        auto* copy = new QVBoxLayout;
        copy->setSpacing(4);
        copy->addStretch();
        copy->addWidget(Label(option.title, Font(16, 600),
                              option.locked ? Color::TextSecondary() : Color::Text(), 21));
        auto* detail_row = new QHBoxLayout;
        auto* detail = Label(option.description, Font(13, 400), Color::TextSecondary());
        detail->setWordWrap(true);
        detail->setMaximumWidth(430);
        detail_row->addWidget(detail, 1);
        detail_row->addStretch();
        copy->addLayout(detail_row);
        if (!option.status.isEmpty()) {
            copy->addWidget(Label(option.status, Font(12, 600),
                                  option.locked ? SettingsColor::Gold()
                                  : done        ? Color::Accent()
                                                : Color::TextSecondary(),
                                  16));
        }
        copy->addStretch();
        row->addLayout(copy, 1);
        row->addSpacing(18 + 26);
        for (auto* label : findChildren<QLabel*>()) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
        }
    }

protected:
    void nextCheckState() override {}

    void focusInEvent(QFocusEvent* event) override {
        keyboard_focus = IsKeyboardFocus(event);
        QAbstractButton::focusInEvent(event);
    }

    void enterEvent(QEnterEvent*) override {
        hover.SetHovered(true);
    }

    void leaveEvent(QEvent*) override {
        hover.SetHovered(false);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPen border = hasFocus() && keyboard_focus ? QPen(Color::Text(), 2)
                            : done                       ? QPen(Color::Accent(), 2)
                                                         : QPen(Color::StrokeSubtle(), 1);
        const qreal inset = border.widthF() / 2;
        painter.setPen(border);
        painter.setBrush(Mix(Color::FillFaint(), Color::Fill(), hover.Value()));
        painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), 22 - inset,
                                22 - inset);

        const QRectF tile(margin, (height() - TileSize) / 2, TileSize, TileSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Fill());
        painter.drawRoundedRect(tile, 15, 15);
        painter.drawPixmap(tile.topLeft() + QPointF(12, 12),
                           IconPixmap(icon, 25, icon_color, devicePixelRatioF()));

        const QRectF check(width() - margin - 26, (height() - 26) / 2.0, 26, 26);
        if (done) {
            painter.setBrush(Color::Accent());
            painter.drawEllipse(check);
            painter.drawPixmap(
                check.topLeft() + QPointF(5.5, 5.5),
                IconPixmap(QStringLiteral("check"), 15, Color::OnAccent(), devicePixelRatioF()));
        } else {
            painter.setPen(QPen(Color::Stroke(), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(check.adjusted(1, 1, -1, -1));
        }
    }

private:
    QString icon;
    QColor icon_color;
    bool done;
    int margin;
    bool keyboard_focus = false;
    HoverFade hover{this};
};

class GetStarted::Button final : public QAbstractButton {
public:
    enum class Kind { Back, Skip, Continue };

    Button(const QString& label, QString icon_, Kind kind_, QWidget* parent)
        : QAbstractButton(parent), icon{std::move(icon_)}, kind{kind_} {
        setText(label);
        setFont(Font(14, 600));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setFixedHeight(42);
    }

    void SetIcon(QString icon_) {
        icon = std::move(icon_);
        update();
    }

    QSize sizeHint() const override {
        const int label = fontMetrics().horizontalAdvance(text());
        switch (kind) {
        case Kind::Back:
            return {18 + 15 + 8 + label + 18, 42};
        case Kind::Skip:
            return {18 + label + 18, 42};
        case Kind::Continue:
            break;
        }
        return {18 + label + 8 + 14 + 18, 42};
    }

protected:
    void focusInEvent(QFocusEvent* event) override {
        keyboard_focus = IsKeyboardFocus(event);
        QAbstractButton::focusInEvent(event);
    }

    void enterEvent(QEnterEvent*) override {
        hover.SetHovered(true);
    }

    void leaveEvent(QEvent*) override {
        hover.SetHovered(false);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal lit = hover.Value();
        QColor fill = Qt::transparent;
        QColor ink = Color::Text();
        switch (kind) {
        case Kind::Back:
            fill = Mix(Color::Fill(), Color::ControlHover(), lit);
            break;
        case Kind::Skip:
            fill = Mix(fill, Color::Fill(), lit);
            break;
        case Kind::Continue:
            fill = Mix(Color::Accent(), Color::AccentLight(), lit);
            ink = Color::OnAccent();
            break;
        }
        if (!isEnabled()) {
            fill.setAlphaF(fill.alphaF() * 0.4f);
            ink.setAlphaF(0.45f);
        }
        painter.setPen(hasFocus() && keyboard_focus ? QPen(Color::Text()) : QPen(Qt::NoPen));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 21, 21);

        int x = 18;
        if (kind == Kind::Back) {
            painter.drawPixmap(x, 13, IconPixmap(icon, 15, ink, devicePixelRatioF()));
            x += 15 + 8;
        }
        const int label = fontMetrics().horizontalAdvance(text());
        painter.setPen(ink);
        painter.setFont(font());
        painter.drawText(QRect(x, 0, label + 1, height()), Qt::AlignVCenter | Qt::AlignLeft,
                         text());
        if (kind == Kind::Continue) {
            painter.drawPixmap(x + label + 8, 14, IconPixmap(icon, 14, ink, devicePixelRatioF()));
        }
    }

private:
    QString icon;
    Kind kind;
    bool keyboard_focus = false;
    HoverFade hover{this};
};

class GetStarted::Progress final : public QWidget {
public:
    explicit Progress(QWidget* parent) : QWidget(parent) {
        setFixedHeight(6);
        slide.setDuration(Motion::Slide);
        slide.setEasingCurve(QEasingCurve::OutCubic);
        connect(&slide, &QVariantAnimation::valueChanged, this, [this](const QVariant& animated) {
            value = animated.toReal();
            update();
        });
    }

    void SetValue(qreal target) {
        slide.stop();
        slide.setStartValue(value);
        slide.setEndValue(target);
        slide.start();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Stroke());
        painter.drawRoundedRect(rect(), 3, 3);
        painter.setBrush(Color::Accent());
        painter.drawRoundedRect(QRectF(0, 0, qRound(width() * value), height()), 3, 3);
    }

private:
    QVariantAnimation slide;
    qreal value = 0;
};

GetStarted::GetStarted(QWidget* parent, Core::System& system_, Setup setup_)
    : QDialog(parent), system{system_}, setup{std::move(setup_)} {
    setWindowTitle(tr("Get Started"));
    setStyleSheet(
        QStringLiteral("QWidget[spotlightTransparent=true] { background: transparent; }"));
    setWindowFlag(Qt::FramelessWindowHint, ClientFrame);
    setGeometry(parent->geometry());
    FadeInOnShow(this);

    auto* shell = new QHBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* sidebar = new QWidget(this);
    sidebar->setProperty("spotlightTransparent", true);
    sidebar->setFixedWidth(SidebarWidth);
    auto* side = new QVBoxLayout(sidebar);
    side->setContentsMargins(14, 24, 14, 24);
    side->setSpacing(0);
    auto* header = new QHBoxLayout;
    header->setContentsMargins(6, 0, 6, 22);
    header->setSpacing(10);
    auto* logo = new QLabel(sidebar);
    logo->setPixmap(
        QIcon(QStringLiteral(":/img/lichii.ico")).pixmap({30, 30}, devicePixelRatioF()));
    logo->setFixedSize(30, 30);
    header->addWidget(logo);
    header->addWidget(Label(tr("Get Started"), DisplayFont(20), Color::Text(), 24), 1);
    side->addLayout(header);
    const std::array names{tr("Keys"), tr("Firmware"), tr("Game folders"), tr("Controller"),
                           tr("Preset")};
    for (int i = 0; i < StepCount; ++i) {
        auto* step = new Step(i + 1, names[i], sidebar);
        connect(step, &QAbstractButton::clicked, this, [this, i] { SetStep(i); });
        side->addWidget(step);
        steps.append(step);
    }
    side->addStretch();
    auto* note = Label(tr("You can change all of this later in Settings."), Font(12, 400),
                       Color::TextSecondary());
    note->setWordWrap(true);
    note->setContentsMargins(8, 0, 8, 0);
    note->setMaximumWidth(220);
    side->addWidget(note);
    shell->addWidget(sidebar);

    auto* main = new QVBoxLayout;
    main->setContentsMargins(56, 62, 20, 28);
    main->setSpacing(0);
    shell->addLayout(main, 1);
    progress = new Progress(this);
    main->addWidget(progress);
    main->addSpacing(20);
    counter = Label({}, Font(13, 600), Color::TextSecondary(), 17);
    main->addWidget(counter);
    main->addSpacing(6);
    title = Label({}, DisplayFont(32), Color::Text(), 38);
    main->addWidget(title);
    main->addSpacing(6);
    description = Label({}, Font(15, 400), Color::TextSecondary());
    description->setWordWrap(true);
    description->setMaximumWidth(620);
    main->addWidget(description);
    main->addSpacing(24);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    scroll->viewport()->setAutoFillBackground(false);
    scroll->viewport()->setProperty("spotlightTransparent", true);
    auto* body = new QWidget(scroll);
    body->setProperty("spotlightTransparent", true);
    page_slot = new QVBoxLayout(body);
    page_slot->setContentsMargins(0, 0, 0, 0);
    page_slot->addStretch();
    scroll->setWidget(body);
    main->addWidget(scroll, 1);

    QStringList keys;
    for (const auto& shortcut : UISettings::values.shortcuts) {
        if (shortcut.group != "Main Window" || shortcut.name != "In-game Menu") {
            continue;
        }
        const auto keyboard = QKeySequence(QString::fromStdString(shortcut.shortcut.keyseq));
        if (!keyboard.isEmpty()) {
            keys.append(KeyText(keyboard));
        }
        if (!shortcut.shortcut.controller_keyseq.empty()) {
            keys.append(QString::fromStdString(shortcut.shortcut.controller_keyseq)
                            .replace(QLatin1Char('+'), QStringLiteral(" + ")));
        }
    }
    if (!keys.isEmpty()) {
        main->addSpacing(14);
        auto* hint = new Panel(this);
        auto* hint_row = new QHBoxLayout(hint);
        hint_row->setContentsMargins(11, 11, 11, 11);
        hint_row->setSpacing(8);
        auto* controller = new QLabel(hint);
        controller->setPixmap(IconPixmap(QStringLiteral("game-controller"), 16,
                                         Color::TextSecondary(), devicePixelRatioF()));
        hint_row->addWidget(controller);
        const auto add_text = [hint_row](const QString& text) {
            hint_row->addWidget(Label(text, Font(13, 400), Color::TextSecondary()));
        };
        add_text(tr("While playing, open the in-game menu with"));
        for (int i = 0; i < keys.size(); ++i) {
            if (i > 0) {
                add_text(tr("or"));
            }
            hint_row->addWidget(new KeyCap(keys[i], hint));
        }
        add_text(tr("· change it in Shortcuts"));
        hint_row->addStretch();
        main->addWidget(hint);
    }

    main->addSpacing(14);
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    auto* back = new Button(tr("Back"), QStringLiteral("caret-left"), Button::Kind::Back, this);
    connect(back, &QAbstractButton::clicked, this, [this] {
        if (current == 0) {
            reject();
        } else {
            SetStep(current - 1);
        }
    });
    const auto next = [this] {
        if (current + 1 < StepCount) {
            SetStep(current + 1);
        } else {
            accept();
        }
    };
    auto* skip = new Button(tr("Skip"), {}, Button::Kind::Skip, this);
    connect(skip, &QAbstractButton::clicked, this, next);
    continue_button = new Button({}, {}, Button::Kind::Continue, this);
    connect(continue_button, &QAbstractButton::clicked, this, next);
    actions->addWidget(back);
    actions->addStretch();
    actions->addWidget(skip);
    actions->addWidget(continue_button);
    main->addLayout(actions);
    footer = {back, skip, continue_button};

    auto* bar = new WindowDragBar(this);
    bar->setProperty("spotlightTransparent", true);
    auto* bar_column = new QVBoxLayout(bar);
    bar_column->setContentsMargins(0, 8, 14, 0);
    auto* bar_row = new QHBoxLayout;
    bar_row->addStretch();
    bar->AddWindowControls(bar_row);
    bar_column->addLayout(bar_row);
    bar_column->addStretch();
    title_bar = bar;
    if constexpr (ClientFrame) {
        edges = new FrameEdges(this);
    }

    SetStep(0);
}

void GetStarted::SetStep(int step) {
    const std::array<std::pair<QString, QString>, StepCount> texts{{
        {tr("Add your keys"), tr("Lichii needs the keys from your own Switch to read games. They "
                                 "stay on this computer.")},
        {tr("Add firmware"), tr("Some games need the system files from your own Switch. Choose "
                                "the folder or ZIP you dumped.")},
        {tr("Add game folders"),
         tr("Lichii looks for games in these folders and shows them in your library.")},
        {tr("Check your controller"),
         tr("Player 1 plays with this controller. Map its buttons if they don't match.")},
        {tr("Pick a preset"), tr("Sets resolution, scaling and anti-aliasing for this computer. "
                                 "Game overrides stay.")},
    }};
    current = step;
    progress->SetValue((step + 1) / static_cast<qreal>(StepCount));
    counter->setText(tr("Step %1 of %2").arg(step + 1).arg(StepCount));
    title->setText(texts[step].first);
    description->setText(texts[step].second);
    const bool last = step + 1 == StepCount;
    continue_button->setText(last ? tr("Finish") : tr("Continue"));
    continue_button->SetIcon(last ? QStringLiteral("check") : QStringLiteral("caret-right"));
    ShowStep();
    FadeIn(page);
}

void GetStarted::ShowStep() {
    auto* focused = qobject_cast<QAbstractButton*>(focusWidget());
    const int focus_index =
        page != nullptr ? page->findChildren<QAbstractButton*>().indexOf(focused) : -1;
    if (page != nullptr) {
        page->hide();
        page->deleteLater();
    }

    const bool running = system.IsPoweredOn();
    switch (current) {
    case 0: {
        std::function<void()> install;
        if (!running) {
            install = [this] {
                QtCommon::Content::InstallKeys();
                emit ContentInstalled();
                ShowStep();
            };
        }
        const bool keys = ContentManager::AreKeysPresent();
        const bool title_keys = TitleKeysPresent();
        const QString locked = tr("Stop the game to change keys");
        page = Options({
            {QStringLiteral("key"), Color::Accent(), tr("Choose prod.keys"),
             tr("Found in the switch folder after you dump them from your console"),
             running ? locked
             : keys  ? tr("prod.keys · installed")
                     : QString{},
             keys, running, install},
            {QStringLiteral("key"), SettingsColor::Green(), tr("Add title.keys (optional)"),
             tr("Needed for some updates and DLC"),
             running      ? locked
             : title_keys ? tr("title.keys · installed")
                          : tr("Put it next to prod.keys, then choose prod.keys again"),
             title_keys, running, install},
        });
        break;
    }
    case 1: {
        std::function<void()> folder;
        std::function<void()> zip;
        if (!running) {
            folder = [this] {
                QtCommon::Content::InstallFirmware();
                emit ContentInstalled();
                ShowStep();
            };
            zip = [this] {
                QtCommon::Content::InstallFirmwareZip();
                emit ContentInstalled();
                ShowStep();
            };
        }
        const QString version = FirmwareVersion(system);
        const QString status = running              ? tr("Stop the game to change firmware")
                               : !version.isEmpty() ? tr("Firmware %1 installed").arg(version)
                               : !ContentManager::AreKeysPresent() ? tr("Add your keys first")
                                                                   : QString{};
        page = Options({
            {QStringLiteral("folder-open"), Color::Accent(), tr("Choose a firmware folder"),
             tr("The folder of .nca files you dumped from your console"), status,
             !version.isEmpty(), running, folder},
            {QStringLiteral("archive"),
             SettingsColor::Green(),
             tr("Choose a firmware ZIP"),
             tr("The same files packed in a .zip archive"),
             {},
             false,
             running,
             zip},
        });
        break;
    }
    case 2: {
        QList<Option> options;
        const QFontMetrics path_metrics(Font(13, 400));
        for (const auto& folder : setup.game_folders()) {
            options.append(
                {QStringLiteral("folder"),
                 SettingsColor::Green(),
                 QDir(folder).dirName(),
                 path_metrics.elidedText(QDir::toNativeSeparators(folder), Qt::ElideMiddle, 430),
                 {},
                 true,
                 false,
                 {}});
        }
        options.append({QStringLiteral("folder-open"),
                        Color::Accent(),
                        tr("Add a game folder"),
                        tr("Lichii scans it and lists its games in your library"),
                        {},
                        false,
                        false,
                        [this] {
                            setup.add_game_folder();
                            ShowStep();
                        }});
        page = Options(options);
        break;
    }
    case 3:
        page = Options({
            {QStringLiteral("game-controller"),
             SettingsColor::Lavender(),
             setup.controller,
             setup.controller_connected ? tr("Player 1 · connected")
                                        : tr("Player 1 · not connected"),
             setup.controller_device.isEmpty() ? QString{}
                                               : tr("Input: %1").arg(setup.controller_device),
             setup.controller_connected,
             false,
             {}},
            {QStringLiteral("keyboard"), SettingsColor::Gold(), tr("Map buttons"),
             tr("Opens player 1's buttons in Controls when you finish"),
             map_buttons ? tr("Opens after Finish") : QString{}, map_buttons, false,
             [this] {
                 map_buttons = !map_buttons;
                 ShowStep();
             }},
        });
        break;
    default: {
        page = new QWidget(page_slot->parentWidget());
        page->setProperty("spotlightTransparent", true);
        auto* row = new QHBoxLayout(page);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(10);
        for (auto* preset : setup.presets) {
            row->addWidget(preset->Mirror(page), 1);
        }
        break;
    }
    }
    page_slot->insertWidget(0, page);
    page->show();

    for (int i = 0; i < StepCount; ++i) {
        const bool done = i != current && StepDone(i);
        steps[i]->SetState(i == current, done,
                           i == current ? tr("Now")
                           : done       ? tr("Done")
                                        : QString{});
    }
    QList<QWidget*> chain(steps.cbegin(), steps.cend());
    const auto buttons = page->findChildren<QAbstractButton*>();
    for (auto* button : buttons) {
        if (button->focusPolicy() != Qt::NoFocus) {
            chain.append(button);
        }
    }
    chain.append(footer);
    for (qsizetype i = 1; i < chain.size(); ++i) {
        setTabOrder(chain[i - 1], chain[i]);
    }
    if (focus_index >= 0 && focus_index < buttons.size()) {
        buttons[focus_index]->setFocus(Qt::TabFocusReason);
    }
}

QWidget* GetStarted::Options(const QList<Option>& options) {
    auto* cards = new QWidget(page_slot->parentWidget());
    cards->setProperty("spotlightTransparent", true);
    auto* layout = new QVBoxLayout(cards);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    for (const auto& option : options) {
        layout->addWidget(new Card(option, cards));
    }
    return cards;
}

bool GetStarted::StepDone(int step) const {
    switch (step) {
    case 0:
        return ContentManager::AreKeysPresent();
    case 1:
        return !FirmwareVersion(system).isEmpty();
    case 2:
        return !setup.game_folders().isEmpty();
    case 3:
        return setup.controller_connected;
    default:
        return std::any_of(setup.presets.cbegin(), setup.presets.cend(),
                           [](const auto* preset) { return preset->isChecked(); });
    }
}

void GetStarted::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    QLinearGradient backdrop(SidebarWidth, 0, SidebarWidth + (width() - SidebarWidth) * 0.3,
                             height());
    backdrop.setColorAt(0, SettingsColor::BackdropTop());
    backdrop.setColorAt(1, SettingsColor::BackdropBottom());
    painter.fillRect(rect(), backdrop);
    QLinearGradient sidebar(0, 0, 0, height());
    sidebar.setColorAt(0, SettingsColor::Sidebar());
    sidebar.setColorAt(0.55, SettingsColor::SidebarBottom());
    painter.fillRect(QRect(0, 0, SidebarWidth, height()), sidebar);
    painter.setPen(Color::Divider());
    painter.drawLine(SidebarWidth - 1, 0, SidebarWidth - 1, height());
}

void GetStarted::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    title_bar->setGeometry(0, 0, width(), 52);
    if (edges != nullptr) {
        edges->setGeometry(rect());
        edges->setVisible(!isMaximized() && !isFullScreen());
        edges->raise();
    }
}

void GetStarted::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        auto* button = qobject_cast<QAbstractButton*>(focusWidget());
        (button != nullptr ? button : continue_button)->click();
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace Spotlight
