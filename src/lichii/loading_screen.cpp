// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/loading_screen.h"

#include <algorithm>
#include <array>
#include <QAbstractButton>
#include <QAction>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>

#include "common/settings.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "lichii/spotlight/hero_art.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/util/game.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader_warm/shader_warm_builder.h"

namespace {

using VideoCore::LoadCallbackStage;
using namespace Spotlight;

QColor Secondary() {
    return QColor(0xff, 0xff, 0xff, 0xb8);
}

class LoadingButton final : public QAbstractButton {
public:
    LoadingButton(const QString& text, QString icon, bool accent, QWidget* parent)
        : QAbstractButton(parent), icon_name(std::move(icon)), accent(accent) {
        setText(text);
        setFixedHeight(42);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

    void SetHint(QString text) {
        hint = std::move(text);
        resize(sizeHint());
        update();
    }

    QSize sizeHint() const override {
        int width = 18 + 16 + 8 + QFontMetrics(Font(14, 600)).horizontalAdvance(text()) + 18;
        if (!hint.isEmpty()) {
            width += 8 + QFontMetrics(Font(12)).horizontalAdvance(hint);
        }
        return {width, 42};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hovered = underMouse() || isDown();
        QColor fill = accent ? (hovered ? Color::AccentLight() : Color::Accent())
                             : QColor(0xff, 0xff, 0xff, hovered ? 0x33 : 0x24);
        QColor ink = accent ? Color::OnAccent() : Color::Text();
        if (!isEnabled()) {
            fill.setAlphaF(fill.alphaF() * 0.4f);
            ink.setAlphaF(0.45f);
        }
        painter.setPen(hasFocus() ? QPen(Color::Text()) : Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 21, 21);
        painter.drawPixmap(18, 13, IconPixmap(icon_name, 16, ink, devicePixelRatioF()));
        painter.setFont(Font(14, 600));
        painter.setPen(ink);
        const int label = painter.fontMetrics().horizontalAdvance(text());
        painter.drawText(QRect{42, 0, label + 1, height()}, Qt::AlignVCenter, text());
        if (!hint.isEmpty()) {
            painter.setFont(Font(12));
            painter.setPen(Secondary());
            painter.drawText(QRect{50 + label, 0, width() - 50 - label, height()}, Qt::AlignVCenter,
                             hint);
        }
    }

    void enterEvent(QEnterEvent*) override {
        update();
    }
    void leaveEvent(QEvent*) override {
        update();
    }

private:
    QString icon_name;
    QString hint;
    bool accent;
};

} // namespace

LoadingScreen::LoadingScreen(QWidget* parent) : QWidget(parent), stage(LoadCallbackStage::Prepare) {
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);

    repaint.setSingleShot(true);
    repaint.setInterval(33);
    connect(&repaint, &QTimer::timeout, this, qOverload<>(&QWidget::update));

    fade.setDuration(Motion::Fade);
    fade.setStartValue(1.0);
    fade.setEndValue(0.0);
    fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&fade, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    connect(&HeroArt::Instance(), &HeroArt::Ready, this, [this](u64 id) {
        if (id == program_id) {
            hero_art = HeroArt::Instance().Image(id);
            UpdateBackdrop();
            update();
        }
    });
    connect(&fade, &QVariantAnimation::finished, this, [this] {
        hide();
        emit Hidden();
    });

    auto* skip = new LoadingButton(tr("Start anyway"), QStringLiteral("play-fill"), true, this);
    skip->setToolTip(tr("Start anyway: starts the game now. Shaders not precached yet are "
                        "precached on a later launch."));
    skip->hide();
    skip_button = skip;
    connect(skip_button, &QAbstractButton::clicked, this, [this] {
        VideoCommon::ShaderWarm::RequestSkipWarm();
        skip_button->setEnabled(false);
    });

    cancel_button = new LoadingButton(tr("Cancel"), QStringLiteral("x"), false, this);
    cancel_button->hide();
}

LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::SetCancelAction(QAction* action) {
    cancel_action = action;
    const auto update_cancel = [this] {
        static_cast<LoadingButton*>(cancel_button)->SetHint(KeyText(cancel_action->shortcut()));
        cancel_button->setVisible(cancel_action->isEnabled() &&
                                  fade.state() != QAbstractAnimation::Running);
        LayoutButtons();
    };
    connect(action, &QAction::changed, this, update_cancel);
    connect(cancel_button, &QAbstractButton::clicked, action, &QAction::trigger);
    update_cancel();
}

void LoadingScreen::Prepare(Loader::AppLoader& loader, QString game_title, QString game_version) {
    fade.stop();
    icon = {};
    precompile_requested = false;
    u64 title_id = 0;
    if (loader.ReadProgramId(title_id) == Loader::ResultStatus::Success && title_id != 0) {
        const auto image = QtCommon::Game::ReadCustomArtwork(title_id);
        if (!image.empty()) {
            icon.loadFromData(image.data(), static_cast<uint>(image.size()));
        }
        if (game_title.isEmpty()) {
            game_title = QString::fromStdString(QtCommon::Game::ReadCustomTitle(title_id));
        }
        precompile_requested = VideoCommon::ShaderWarm::WarmRequested(
            VideoCommon::ShaderWarm::ComputeWarmPath(title_id));
    }
    std::vector<u8> image;
    if (icon.isNull() && loader.ReadIcon(image) == Loader::ResultStatus::Success) {
        icon.loadFromData(image.data(), static_cast<uint>(image.size()));
    }
    if (game_title.isEmpty()) {
        std::string metadata;
        loader.ReadTitle(metadata);
        game_title = QString::fromStdString(metadata);
    }
    if (game_version.isEmpty()) {
        std::string metadata;
        loader.ReadDisplayVersion(metadata);
        game_version = QString::fromStdString(metadata);
    }
    program_id = 0;
    loader.ReadProgramId(program_id);
    HeroArt::Instance().Request(program_id, game_title);
    hero_art = HeroArt::Instance().Image(program_id);
    tint = HeroArt::Tint(icon);
    title = game_title.isEmpty() ? tr("Starting game") : std::move(game_title);
    version = game_version.isEmpty() ? QString{} : tr("v%1").arg(game_version);
    switch2 = Settings::CurrentProgramIsSwitch2();
    previous_time = {};
    slow_shader_start = {};
    UpdateBackdrop();
    OnLoadProgress(LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::Clear() {
    fade.stop();
    repaint.stop();
    icon = {};
    art = {};
    hero_art = {};
    tint = {};
    program_id = 0;
    backdrop = {};
    title.clear();
    version.clear();
}

void LoadingScreen::OnLoadComplete() {
    OnLoadProgress(LoadCallbackStage::Complete, 0, 0);
    if (!isVisible()) {
        return;
    }
    skip_button->hide();
    cancel_button->hide();
    fade.start();
}

void LoadingScreen::OnLoadProgress(LoadCallbackStage next, std::size_t value, std::size_t target) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    const bool stage_changed = next != stage;
    if (stage_changed) {
        stage = next;
        slow_shader_start = {};
        skip_button->setVisible(stage == LoadCallbackStage::Warm);
        skip_button->setEnabled(true);
        LayoutButtons();
    }
    current = std::min(value, target);
    total = target;
    estimate.clear();
    if (total != 0 && current < total) {
        if (slow_shader_start == steady_clock::time_point{} &&
            now - previous_time > milliseconds{50}) {
            slow_shader_start = now;
            slow_shader_first_value = current;
        }
        if (slow_shader_start != steady_clock::time_point{} && current > slow_shader_first_value) {
            const auto elapsed = duration<double>(now - slow_shader_start).count();
            if (elapsed > 1.0) {
                const auto remaining = static_cast<qulonglong>(
                    elapsed * static_cast<double>(total - current) /
                        static_cast<double>(current - slow_shader_first_value) +
                    1.0);
                estimate = tr("Estimated Time %1:%2")
                               .arg(remaining / 60, 2, 10, QLatin1Char('0'))
                               .arg(remaining % 60, 2, 10, QLatin1Char('0'));
            }
        }
    }
    previous_time = now;
    if (stage_changed) {
        repaint.stop();
        update();
    } else if (!repaint.isActive()) {
        repaint.start();
    }
}

QRect LoadingScreen::ContentRect() const {
    const int margin = std::min(64, width() / 18);
    const int art_size = std::min(200, width() / 4);
    const int gap = std::min(40, width() / 24);
    const int top = std::clamp(height() * 170 / 800, 18, std::max(18, height() - 345));
    return QRect{margin + art_size + gap, top, width() - 2 * margin - art_size - gap, art_size};
}

void LoadingScreen::LayoutButtons() {
    const QRect content = ContentRect();
    int x = content.left();
    const int y = content.top() + 245;
    for (auto* button : {skip_button, cancel_button}) {
        if (!button->isHidden()) {
            const QSize size = button->sizeHint();
            button->setGeometry(QRect{QPoint{x, y}, size});
            x += size.width() + 10;
        }
    }
}

void LoadingScreen::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateBackdrop();
    LayoutButtons();
}

void LoadingScreen::UpdateBackdrop() {
    if (size().isEmpty()) {
        return;
    }
    const qreal ratio = devicePixelRatioF();
    const int art_size = ContentRect().height();
    art = icon.isNull() ? QPixmap{}
                        : icon.scaled(QSize{art_size, art_size} * ratio, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation);
    art.setDevicePixelRatio(ratio);

    backdrop = QPixmap(size() * ratio);
    backdrop.setDevicePixelRatio(ratio);
    backdrop.fill(Color::Scrim());
    QPainter painter(&backdrop);
    if (!hero_art.isNull()) {
        HeroArt::Paint(painter, QRectF(rect()), hero_art);
    } else if (tint.isValid()) {
        painter.fillRect(rect(), tint);
    }
    auto shade = Color::Scrim();
    QLinearGradient horizontal(0, 0, width(), 0);
    shade.setAlpha(0xf5);
    horizontal.setColorAt(0, shade);
    shade.setAlpha(0xe0);
    horizontal.setColorAt(0.4, shade);
    shade.setAlpha(0x73);
    horizontal.setColorAt(0.75, shade);
    shade.setAlpha(0x8c);
    horizontal.setColorAt(1, shade);
    painter.fillRect(rect(), horizontal);
    QLinearGradient vertical(0, 0, 0, height());
    shade.setAlpha(0);
    vertical.setColorAt(0.5, shade);
    shade.setAlpha(0xf2);
    vertical.setColorAt(1, shade);
    painter.fillRect(rect(), vertical);
}

void LoadingScreen::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (fade.state() == QAbstractAnimation::Running) {
        painter.setOpacity(fade.currentValue().toReal());
    }
    if (backdrop.isNull()) {
        painter.fillRect(rect(), Color::Scrim());
    } else {
        painter.drawPixmap(0, 0, backdrop);
    }

    const QRect content = ContentRect();
    const int margin = std::min(64, width() / 18);
    const int art_size = content.height();
    PaintWholeIcon(painter, QRect{margin, content.top(), art_size, art_size}, art, 22);

    painter.translate(content.topLeft());
    const int available = content.width();
    const auto draw_line = [&](QRectF area, const QString& text, const QFont& font,
                               const QColor& color, Qt::Alignment alignment = Qt::AlignLeft) {
        painter.setPen(color);
        painter.setFont(font);
        painter.drawText(area, alignment | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(text, Qt::ElideRight, area.width()));
        return painter.fontMetrics().horizontalAdvance(text);
    };

    const QString platform = switch2 ? tr("Switch 2") : tr("Switch 1");
    const auto badge_font = Font(12, 600);
    const int badge = QFontMetrics(badge_font).horizontalAdvance(platform) + 20;
    painter.setPen(Qt::NoPen);
    painter.setBrush(switch2 ? Color::Accent() : Color::Control());
    painter.drawRoundedRect(QRectF{0, 0, qreal(badge), 24}, 12, 12);
    draw_line(QRectF{0, 0, qreal(badge), 24}, platform, badge_font,
              switch2 ? Color::OnAccent() : Color::Text(), Qt::AlignHCenter);
    draw_line(QRectF{qreal(badge + 8), 0, qreal(available - badge - 8), 24}, version, Font(13),
              Secondary());
    draw_line(QRectF{0, 30, qreal(available), 48}, title, DisplayFont(40), Color::Text());

    const bool warm = stage == LoadCallbackStage::Warm;
    const int active = stage == LoadCallbackStage::Prepare    ? 0
                       : stage == LoadCallbackStage::Complete ? 2
                                                              : 1;
    const std::array steps{tr("Loading…"), warm ? tr("Precaching shaders") : tr("Loading Shaders"),
                           tr("Launching…")};
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        const qreal y = 90 + 32 * i;
        const QRectF marker{0, y + 4, 22, 22};
        if (i < active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Color::Accent());
            painter.drawEllipse(marker);
            painter.drawPixmap(QPointF{4, y + 8},
                               IconPixmap(u"check", 14, Color::OnAccent(), devicePixelRatioF()));
        } else if (i == active) {
            painter.setPen(QPen(Color::Accent(), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(marker.adjusted(1, 1, -1, -1));
            painter.setPen(Qt::NoPen);
            painter.setBrush(Color::Accent());
            painter.drawEllipse(marker.center(), 4, 4);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0xff, 0xff, 0xff, 0x24));
            painter.drawEllipse(marker);
        }
        const int label =
            draw_line(QRectF{32, y, qreal(available - 32), 30}, steps[i],
                      Font(14, i == active ? 600 : 500), i <= active ? Color::Text() : Secondary());
        if (i == 1 && total > 0) {
            draw_line(QRectF{qreal(42 + label), y, qreal(available - 42 - label), 30},
                      tr("%1 / %2").arg(locale().toString(static_cast<qulonglong>(current)),
                                        locale().toString(static_cast<qulonglong>(total))),
                      Font(13), Secondary());
        }
    }

    if (total > 0 || stage == LoadCallbackStage::Complete) {
        const qreal track = std::min(440, available);
        const qreal progress = total > 0 ? static_cast<qreal>(current) / total : 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xff, 0xff, 0xff, 0x33));
        painter.drawRoundedRect(QRectF{0, 196, track, 8}, 4, 4);
        painter.setBrush(Color::Accent());
        painter.drawRoundedRect(QRectF{0, 196, std::round(track * progress), 8}, 4, 4);
        draw_line(QRectF{0, 212, 56, 17}, tr("%1%").arg(qRound(progress * 100)), Font(13, 700),
                  Color::Text());
        draw_line(QRectF{56, 212, track - 56, 17}, estimate, Font(13), Secondary(), Qt::AlignRight);
    }

    const QString note = !warm ? tr("First launch after a driver update takes longer")
                         : precompile_requested
                             ? tr("Precompile All Shaders is on for this game · right-click it "
                                  "in the library to turn off")
                             : tr("Precache Shaders is on in Graphics settings");
    painter.drawPixmap(QPointF{0, 311},
                       IconPixmap(warm ? u"cpu" : u"info", 16, QColor(0xff, 0xff, 0xff, 0xb8),
                                  devicePixelRatioF()));
    painter.setFont(Font(12));
    painter.setPen(Secondary());
    painter.drawText(QRect{24, 311, available - 24, 40}, Qt::TextWordWrap, note);
}
