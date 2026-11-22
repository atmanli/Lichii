// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/loading_screen.h"

#include <algorithm>
#include <array>
#include <QAction>
#include <QGraphicsOpacityEffect>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPropertyAnimation>

#include "common/settings.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/loader/loader.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/util/game.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/shader_warm/shader_warm_builder.h"

namespace {

using VideoCore::LoadCallbackStage;
using namespace Spotlight;

} // namespace

LoadingScreen::LoadingScreen(QWidget* parent) : QWidget(parent), stage(LoadCallbackStage::Prepare) {
    setMinimumSize(Layout::MinimumSize::Width, Layout::MinimumSize::Height);

    auto* opacity = new QGraphicsOpacityEffect(this);
    opacity->setOpacity(1);
    setGraphicsEffect(opacity);
    fadeout_animation = std::make_unique<QPropertyAnimation>(opacity, "opacity");
    fadeout_animation->setDuration(300);
    fadeout_animation->setStartValue(1);
    fadeout_animation->setEndValue(0);
    connect(fadeout_animation.get(), &QPropertyAnimation::finished, this, [this, opacity] {
        hide();
        opacity->setOpacity(1);
        emit Hidden();
    });

    skip_button = new PillButton(tr("Start anyway"), QStringLiteral("play-fill"),
                                 PillButton::Kind::Accent, 42, this);
    skip_button->setToolTip(
        tr("Starts the game now. The remaining shaders are precached on a later launch."));
    skip_button->hide();
    connect(skip_button, &QAbstractButton::clicked, this, [this] {
        VideoCommon::ShaderWarm::RequestSkipWarm();
        skip_button->setEnabled(false);
    });

    cancel_button =
        new PillButton(tr("Cancel"), QStringLiteral("x"), PillButton::Kind::Control, 42, this);
    cancel_button->hide();
}

LoadingScreen::~LoadingScreen() = default;

void LoadingScreen::SetCancelAction(QAction* action) {
    cancel_action = action;
    const auto update_cancel = [this] {
        const auto shortcut = cancel_action->shortcut().toString(QKeySequence::NativeText);
        cancel_button->setText(shortcut.isEmpty() ? tr("Cancel") : tr("Cancel  %1").arg(shortcut));
        cancel_button->setVisible(cancel_action->isEnabled());
        LayoutButtons();
    };
    connect(action, &QAction::changed, this, update_cancel);
    connect(cancel_button, &QAbstractButton::clicked, action, &QAction::trigger);
    update_cancel();
}

void LoadingScreen::Prepare(Loader::AppLoader& loader, QString game_title, QString game_version) {
    fadeout_animation->stop();
    static_cast<QGraphicsOpacityEffect*>(graphicsEffect())->setOpacity(1);
    icon = {};
    u64 program_id = 0;
    if (loader.ReadProgramId(program_id) == Loader::ResultStatus::Success && program_id != 0) {
        const auto image = QtCommon::Game::ReadCustomArtwork(program_id);
        if (!image.empty()) {
            icon.loadFromData(image.data(), static_cast<uint>(image.size()));
        }
        if (game_title.isEmpty()) {
            game_title = QString::fromStdString(QtCommon::Game::ReadCustomTitle(program_id));
        }
    }
    std::vector<u8> image;
    if (icon.isNull() && loader.ReadIcon(image) == Loader::ResultStatus::Success) {
        icon.loadFromData(image.data(), static_cast<uint>(image.size()));
    }
    UpdateBackdrop();
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
    title = game_title.isEmpty() ? tr("Starting game") : std::move(game_title);
    version = game_version.isEmpty() ? QString{} : tr("v%1").arg(game_version);
    switch2 = Settings::CurrentProgramIsSwitch2();
    previous_time = {};
    slow_shader_start = {};
    OnLoadProgress(LoadCallbackStage::Prepare, 0, 0);
}

void LoadingScreen::Clear() {
    fadeout_animation->stop();
    icon = {};
    backdrop = {};
    title.clear();
    version.clear();
}

void LoadingScreen::OnLoadComplete() {
    OnLoadProgress(LoadCallbackStage::Complete, 0, 0);
    fadeout_animation->start();
}

void LoadingScreen::OnLoadProgress(LoadCallbackStage next, std::size_t value, std::size_t target) {
    using namespace std::chrono;
    const auto now = steady_clock::now();
    if (next != stage) {
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
    update();
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
    if (icon.isNull() || size().isEmpty()) {
        backdrop = {};
        return;
    }

    auto cover = icon.deviceIndependentSize();
    cover.scale(size(), Qt::KeepAspectRatioByExpanding);
    cover *= 2;
    const QPointF center{width() * 0.65, height() * 0.5};
    const QRectF art{center - QPointF{cover.width() / 2, cover.height() / 2}, cover};
    backdrop = QPixmap(size().scaled(16, 16, Qt::KeepAspectRatio));
    backdrop.fill(Color::Scrim());
    QPainter painter(&backdrop);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.scale(qreal(backdrop.width()) / width(), qreal(backdrop.height()) / height());
    painter.drawPixmap(art, icon, QRectF(icon.rect()));
}

void LoadingScreen::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), Color::Scrim());
    if (!backdrop.isNull()) {
        painter.drawPixmap(rect(), backdrop);

        auto shade = Color::Scrim();
        QLinearGradient horizontal(0, 0, width(), 0);
        shade.setAlpha(245);
        horizontal.setColorAt(0, shade);
        shade.setAlpha(224);
        horizontal.setColorAt(0.4, shade);
        shade.setAlpha(115);
        horizontal.setColorAt(0.75, shade);
        shade.setAlpha(140);
        horizontal.setColorAt(1, shade);
        painter.fillRect(rect(), horizontal);

        QLinearGradient vertical(0, 0, 0, height());
        shade.setAlpha(0);
        vertical.setColorAt(0.5, shade);
        shade.setAlpha(242);
        vertical.setColorAt(1, shade);
        painter.fillRect(rect(), vertical);
    }

    const QRect content = ContentRect();
    const int margin = std::min(64, width() / 18);
    const int art = content.height();
    PaintWholeIcon(painter, QRect{margin, content.top(), art, art}, icon, 22);

    painter.translate(content.topLeft());
    const int available = content.width();
    const auto draw_line = [&](QRectF area, const QString& text, const QFont& font,
                               const QColor& color, Qt::Alignment alignment = Qt::AlignLeft) {
        painter.setPen(color);
        painter.setFont(font);
        painter.drawText(area, alignment | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(text, Qt::ElideRight, area.width()));
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
              Color::TextSecondary());
    draw_line(QRectF{0, 30, qreal(available), 48}, title, DisplayFont(40), Color::Text());

    const bool warm = stage == LoadCallbackStage::Warm;
    const int active = stage == LoadCallbackStage::Prepare    ? 0
                       : stage == LoadCallbackStage::Complete ? 2
                                                              : 1;
    const std::array steps{tr("Loading…"), warm ? tr("Precaching shaders") : tr("Loading Shaders"),
                           tr("Launching…")};
    for (int i = 0; i < static_cast<int>(steps.size()); ++i) {
        const qreal y = 88 + 30 * i;
        const QRectF marker{0, y + 4, 22, 22};
        painter.setPen(i == active ? QPen(Color::Accent(), 2) : Qt::NoPen);
        painter.setBrush(i < active    ? Color::Accent()
                         : i == active ? Qt::NoBrush
                                       : QBrush(Color::Control()));
        painter.drawEllipse(marker.adjusted(1, 1, -1, -1));
        if (i < active) {
            painter.drawPixmap(QPointF{4, y + 8},
                               IconPixmap(u"check", 14, Color::OnAccent(), devicePixelRatioF()));
        } else if (i == active) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Color::Accent());
            painter.drawEllipse(marker.center(), 4, 4);
        }
        QString step = steps[i];
        if (i == 1 && total > 0) {
            step = tr("%1  %2 / %3")
                       .arg(step, locale().toString(static_cast<qulonglong>(current)),
                            locale().toString(static_cast<qulonglong>(total)));
        }
        draw_line(QRectF{32, y, qreal(available - 32), 30}, step, Font(14, i == active ? 600 : 500),
                  i <= active ? Color::Text() : Color::TextSecondary());
    }

    if (total > 0 || stage == LoadCallbackStage::Complete) {
        const qreal track = std::min(440, available);
        const qreal progress = total > 0 ? static_cast<qreal>(current) / total : 1.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(Color::Control());
        painter.drawRoundedRect(QRectF{0, 196, track, 8}, 4, 4);
        painter.setBrush(Color::Accent());
        painter.drawRoundedRect(QRectF{0, 196, track * progress, 8}, 4, 4);
        draw_line(QRectF{0, 211, 56, 20}, tr("%1%").arg(qRound(progress * 100)), Font(13, 700),
                  Color::Text());
        draw_line(QRectF{56, 211, track - 56, 20}, estimate, Font(13), Color::TextSecondary(),
                  Qt::AlignRight);
    }

    painter.setFont(Font(12));
    painter.setPen(Color::TextSecondary());
    const QString note =
        warm ? tr("Start anyway: the remaining shaders will be precached on a later launch.")
             : tr("The first launch after a driver update may take longer.");
    painter.drawPixmap(QPointF{0, 305},
                       IconPixmap(u"info", 14, Color::TextSecondary(), devicePixelRatioF()));
    painter.drawText(QRect{22, 304, available - 22, 40}, Qt::TextWordWrap, note);
}
