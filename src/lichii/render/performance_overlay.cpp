// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "performance_overlay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <numeric>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

#include "common/settings.h"
#include "core/core.h"
#include "lichii/spotlight/theme.h"
#include "main_window.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/qt_common.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include "video_core/shader_notify.h"

namespace {

using Common::DkbCpuTrace::Role;
using namespace Spotlight;

constexpr int CardWidth = 300;
constexpr int CardRadius = 18;
constexpr int Padding = 16;
constexpr int GraphTop = Padding + 54 + 12;
constexpr int GraphWidth = 260;
constexpr int GraphHeight = 54;
constexpr int MetricsTop = GraphTop + GraphHeight + 12;
constexpr int ChipsTop = MetricsTop + 37 + 12;
constexpr int ChipHeight = 22;
constexpr int ChipGap = 6;
constexpr QMargins ShadowMargins{16, 8, 16, 24};
constexpr int ShadowOffset = 12;
constexpr auto HistoryWindow = std::chrono::seconds{5};
constexpr auto StatsWindow = std::chrono::milliseconds{500};

constexpr std::array CpuRoles{
    Role::GuestCore0, Role::GuestCore1,      Role::GuestCore2, Role::GuestCore3, Role::GuestCore4,
    Role::GuestCore5, Role::GuestSingleCore, Role::GpuCommand, Role::GpuBack,
};

QColor CardFill() {
    return QColor(0x0b, 0x0c, 0x10, 0xd9);
}

QColor GraphLine() {
    return QColor(0x7e, 0xe2, 0xa8);
}

QColor Generated() {
    return QColor(0xc9, 0xb3, 0xff);
}

QColor Building() {
    return QColor(0xff, 0xd3, 0x7a);
}

QColor Soft(QColor color) {
    color.setAlpha(0x2e);
    return color;
}

QFont MetricLabelFont() {
    QFont font = Font(11, 600);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.55);
    return font;
}

QString Number(const std::optional<double>& value, int precision = 0) {
    return value ? QString::number(*value, 'f', precision) : QStringLiteral("—");
}

/// The chips run left to right and wrap inside the card's padding.
std::vector<QRect> ChipRects(const QStringList& chips) {
    const QFontMetrics metrics(Font(11, 600));
    std::vector<QRect> rects;
    int x = Padding;
    int y = ChipsTop;
    for (const QString& chip : chips) {
        const int width = 18 + metrics.horizontalAdvance(chip);
        if (x > Padding && x + width > CardWidth - Padding) {
            x = Padding;
            y += ChipHeight + ChipGap;
        }
        rects.emplace_back(x, y, width, ChipHeight);
        x += width + ChipGap;
    }
    return rects;
}

} // namespace

PerformanceOverlay::PerformanceOverlay(MainWindow* parent, QWidget* anchor)
    : QWidget(anchor->window(), Qt::FramelessWindowHint | Qt::Tool), m_mainWindow(parent),
      m_anchor(anchor) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    UpdateHeight();
    m_anchor->installEventFilter(this);
    connect(parent, &MainWindow::positionChanged, this, &PerformanceOverlay::ResetPosition);
    connect(parent, &MainWindow::statsUpdated, this, &PerformanceOverlay::updateStats);
    ResetPosition();
}

QRect PerformanceOverlay::Card() const {
    return rect().marginsRemoved(ShadowMargins);
}

QRect PerformanceOverlay::CloseButton() const {
    return QRect{Card().left() + CardWidth - Padding - 24, Card().top() + Padding, 24, 24};
}

void PerformanceOverlay::UpdateHeight() {
    const auto chips = ChipRects(m_chips);
    const int bottom = chips.empty() ? ChipsTop - 12 : chips.back().bottom() + 1;
    setFixedSize(CardWidth + ShadowMargins.left() + ShadowMargins.right(),
                 bottom + Padding + ShadowMargins.top() + ShadowMargins.bottom());
}

void PerformanceOverlay::ResetPosition() {
    move(m_anchor->mapToGlobal(m_offset) - QPoint{ShadowMargins.left(), ShadowMargins.top()});
}

bool PerformanceOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_anchor && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        ResetPosition();
    }
    return QWidget::eventFilter(watched, event);
}

void PerformanceOverlay::updateStats(const Core::PerfStatsResults& results,
                                     const VideoCore::ShaderNotify& shaders) {
    auto& system = *QtCommon::system;
    auto& perf_stats = system.GetPerfStats();
    perf_stats.SetFrameTimeHistoryEnabled(isVisible());
    if (!isVisible()) {
        return;
    }

    m_fps = std::isfinite(results.average_game_fps) ? std::optional(results.average_game_fps)
                                                    : std::nullopt;
    m_speed = std::isfinite(results.emulation_speed) ? std::optional(results.emulation_speed * 100)
                                                     : std::nullopt;
    m_generated = m_mainWindow->FrameGenFps();
    m_shaders = std::max(shaders.ShadersLeft(), 0);
    m_frames = perf_stats.GetRecentFrameTimes(HistoryWindow);
    m_frameMs.reset();
    if (!m_frames.empty() &&
        Core::FrameTimeHistory::Clock::now() - m_frames.back().end >= std::chrono::seconds{1}) {
        m_frames.clear();
    }
    if (!m_frames.empty()) {
        double sum = 0;
        int count = 0;
        for (auto it = m_frames.rbegin();
             it != m_frames.rend() && m_frames.back().end - it->end < StatsWindow; ++it) {
            sum += it->ms;
            ++count;
        }
        m_frameMs = sum / count;
    }

    // Busy time over the window against the window is busy ms per frame against frame time.
    auto cpu_snapshot = Common::DkbCpuTrace::ReadSnapshot();
    m_cpuPercent.reset();
    if (m_cpuSnapshot) {
        const double wall_ns = std::chrono::duration<double, std::nano>(cpu_snapshot.wall_time -
                                                                        m_cpuSnapshot->wall_time)
                                   .count();
        std::optional<u64> busiest_ns;
        for (const Role role : CpuRoles) {
            const auto index = static_cast<size_t>(role);
            if (const auto delta = Common::DkbCpuTrace::CpuTimeDelta(m_cpuSnapshot->threads[index],
                                                                     cpu_snapshot.threads[index])) {
                busiest_ns = std::max(busiest_ns.value_or(0), *delta);
            }
        }
        if (busiest_ns && wall_ns > 0) {
            m_cpuPercent = std::min(100.0, static_cast<double>(*busiest_ns) / wall_ns * 100.0);
        }
    }
    m_cpuSnapshot = cpu_snapshot;

    auto& renderer = system.GPU().Renderer();
    renderer.RequestGpuBusySample();
    const auto gpu_ms = renderer.GetGpuBusyMs();
    m_gpuPercent.reset();
    if (gpu_ms && m_frameMs && *m_frameMs > 0) {
        m_gpuPercent = std::min(100.0, *gpu_ms / *m_frameMs * 100.0);
    }

    const auto label = [](const auto& options, auto value) {
        const auto it = options.find(value);
        return it == options.end()
                   ? QString{}
                   : QCoreApplication::translate("MainWindow", it->second.toUtf8().constData());
    };
    QStringList chips{
        label(ConfigurationShared::renderer_backend_texts_map,
              Settings::values.renderer_backend.GetValue()),
        tr("%1×").arg(Settings::values.resolution_info.up_factor),
        label(ConfigurationShared::scaling_filter_texts_map,
              Settings::values.scaling_filter.GetValue()),
    };
    if (Settings::values.frame_gen.GetValue()) {
        chips << label(ConfigurationShared::frame_gen_type_texts_map,
                       Settings::values.frame_gen_type.GetValue());
    }
    chips.removeAll(QString{});
    if (m_shaders > 0) {
        chips << tr("%n shader(s) building", nullptr, m_shaders);
    }
    if (chips != m_chips) {
        m_chips = chips;
        UpdateHeight();
    }

    std::vector<float> intervals;
    intervals.reserve(m_frames.size());
    for (const auto& frame : m_frames) {
        if (std::isfinite(frame.ms) && frame.ms > 0) {
            intervals.push_back(frame.ms);
        }
    }
    m_low_fps.reset();
    QStringList details;
    if (const auto summary = Core::Summarize(intervals)) {
        if (intervals.size() >= 100) {
            const std::size_t count = std::max<std::size_t>(1, (intervals.size() + 99) / 100);
            std::partial_sort(intervals.begin(), intervals.begin() + count, intervals.end(),
                              std::greater{});
            const double sum = std::accumulate(intervals.begin(), intervals.begin() + count, 0.0);
            m_low_fps = 1000 * count / sum;
        }
        details << tr("Frame time %1 ms · min %2 · max %3")
                       .arg(m_frameMs.value_or(summary->median_ms), 0, 'f', 1)
                       .arg(summary->min_ms, 0, 'f', 1)
                       .arg(summary->max_ms, 0, 'f', 1);
    }
    if (!m_mainWindow->DlssStatusText().isEmpty()) {
        details << m_mainWindow->DlssStatusText();
    }
    setToolTip(details.join(QLatin1Char('\n')));
    update();
}

void PerformanceOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const qreal ratio = devicePixelRatioF();
    m_shadow = QPixmap(size() * ratio);
    m_shadow.setDevicePixelRatio(ratio);
    m_shadow.fill(Qt::transparent);
    QPainter painter(&m_shadow);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 10));
    const QRectF card = QRectF(Card()).translated(0, ShadowOffset);
    for (int spread = 12; spread > 0; spread -= 2) {
        painter.drawRoundedRect(card.adjusted(-spread, -spread, spread, spread),
                                CardRadius + spread, CardRadius + spread);
    }
}

void PerformanceOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawPixmap(0, 0, m_shadow);
    painter.setPen(QPen(Color::StrokeSubtle(), 1));
    painter.setBrush(CardFill());
    painter.drawRoundedRect(QRectF(Card()).adjusted(0.5, 0.5, -0.5, -0.5), CardRadius, CardRadius);

    const QRect close = CloseButton().translated(-Card().topLeft());
    painter.translate(Card().topLeft());
    const auto text = [&](const QRect& area, const QString& value, const QFont& font,
                          const QColor& ink, Qt::Alignment alignment = Qt::AlignLeft) {
        painter.setFont(font);
        painter.setPen(ink);
        painter.drawText(area, alignment | Qt::AlignVCenter, value);
    };

    text(QRect{Padding, Padding, 200, 36}, Number(m_fps), Font(34, 700), Color::Text());
    text(QRect{Padding, Padding + 38, 200, 16}, tr("FPS · 1% low %1").arg(Number(m_low_fps)),
         Font(12), Color::TextSecondary());

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_close_hovered ? Color::ControlHover() : Color::Fill());
    painter.drawEllipse(QRectF(close));
    painter.drawPixmap(close.left() + 6, close.top() + 6,
                       IconPixmap(u"x", 12, Color::TextSecondary(), devicePixelRatioF()));

    if (m_fps && m_generated >= 0.5) {
        const QString generated = tr("+%1 generated").arg(m_generated, 0, 'f', 0);
        const int width = 18 + QFontMetrics(Font(11, 600)).horizontalAdvance(generated);
        const QRect pill{close.left() - 10 - width, Padding, width, ChipHeight};
        painter.setPen(Qt::NoPen);
        painter.setBrush(Soft(Generated()));
        painter.drawRoundedRect(QRectF(pill), ChipHeight / 2.0, ChipHeight / 2.0);
        text(pill, generated, Font(11, 600), Generated(), Qt::AlignHCenter);
    }

    paintGraph(painter, QRectF{Padding, GraphTop, GraphWidth, GraphHeight});

    const std::array<std::pair<QString, QString>, 4> metrics{{
        {tr("Frame"), m_frameMs ? tr("%1 ms").arg(Number(m_frameMs, 1)) : Number(m_frameMs)},
        {tr("Speed"), m_speed ? tr("%1%").arg(Number(m_speed)) : Number(m_speed)},
        {tr("GPU"), m_gpuPercent ? tr("%1%").arg(Number(m_gpuPercent)) : Number(m_gpuPercent)},
        {tr("CPU"), m_cpuPercent ? tr("%1%").arg(Number(m_cpuPercent)) : Number(m_cpuPercent)},
    }};
    const QFontMetrics label_metrics(MetricLabelFont());
    const QFontMetrics value_metrics(Font(15, 600));
    int x = Padding;
    for (const auto& [name, value] : metrics) {
        const QString caption = QLocale().toUpper(name);
        const int width = std::max(label_metrics.horizontalAdvance(caption),
                                   value_metrics.horizontalAdvance(value));
        text(QRect{x, MetricsTop, width, 15}, caption, MetricLabelFont(), Color::TextMuted());
        text(QRect{x, MetricsTop + 17, width, 20}, value, Font(15, 600), Color::Text());
        x += width + 18;
    }

    const auto chips = ChipRects(m_chips);
    for (std::size_t i = 0; i < chips.size(); ++i) {
        const bool building = m_shaders > 0 && i + 1 == chips.size();
        painter.setPen(Qt::NoPen);
        painter.setBrush(building ? Soft(Building()) : Color::Control());
        painter.drawRoundedRect(QRectF(chips[i]), ChipHeight / 2.0, ChipHeight / 2.0);
        text(chips[i], m_chips[static_cast<qsizetype>(i)], Font(11, 600),
             building ? Building() : Color::Text(), Qt::AlignHCenter);
    }
}

void PerformanceOverlay::paintGraph(QPainter& painter, const QRectF& area) {
    const qreal target_ms =
        1000.0 /
        (Settings::CurrentProgramIsSwitch2() ? Settings::values.frame_rate_limit.GetValue() : 60);
    constexpr qreal target_y = 12;
    if (m_frames.size() >= 2) {
        const auto newest = m_frames.back().end;
        const double window = std::chrono::duration<double>(HistoryWindow).count();
        QPolygonF points;
        points.reserve(static_cast<qsizetype>(m_frames.size()));
        for (const auto& frame : m_frames) {
            const double age = std::chrono::duration<double>(newest - frame.end).count();
            points << QPointF{area.right() - area.width() * std::clamp(age / window, 0.0, 1.0),
                              std::clamp(area.top() + target_y * frame.ms / target_ms,
                                         area.top() + 1, area.bottom() - 1)};
        }
        painter.setPen(QPen(GraphLine(), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(points);
    }
    QPen dash(QColor(0xff, 0xff, 0xff, 0x24), 1);
    dash.setDashPattern({3, 4});
    painter.setPen(dash);
    painter.drawLine(QPointF{area.left(), area.top() + target_y + 0.5},
                     QPointF{area.right(), area.top() + target_y + 0.5});
}

void PerformanceOverlay::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (QtCommon::system && QtCommon::system->IsPoweredOn()) {
        QtCommon::system->GetPerfStats().SetFrameTimeHistoryEnabled(false);
    }
    m_fps.reset();
    m_low_fps.reset();
    m_speed.reset();
    m_frameMs.reset();
    m_cpuPercent.reset();
    m_gpuPercent.reset();
    m_cpuSnapshot.reset();
    m_frames.clear();
    m_shaders = 0;
    m_generated = 0;
    m_chips.clear();
    m_close_hovered = false;
    UpdateHeight();
}

void PerformanceOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (CloseButton().contains(event->pos())) {
        m_close_pressed = true;
        return;
    }
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) && windowHandle() &&
        windowHandle()->startSystemMove()) {
        return;
    }
    m_dragging = true;
    m_drag_start_pos = event->pos();
}

void PerformanceOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint target = event->globalPosition().toPoint() - m_drag_start_pos;
        m_offset =
            m_anchor->mapFromGlobal(target + QPoint{ShadowMargins.left(), ShadowMargins.top()});
        move(target);
        return;
    }
    const bool hovered = CloseButton().contains(event->pos());
    if (hovered != m_close_hovered) {
        m_close_hovered = hovered;
        setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update(CloseButton());
    }
}

void PerformanceOverlay::mouseReleaseEvent(QMouseEvent* event) {
    m_dragging = false;
    if (std::exchange(m_close_pressed, false) && CloseButton().contains(event->pos())) {
        close();
    }
}

void PerformanceOverlay::leaveEvent(QEvent*) {
    if (m_close_hovered) {
        m_close_hovered = false;
        unsetCursor();
        update(CloseButton());
    }
}

void PerformanceOverlay::closeEvent(QCloseEvent*) {
    emit closed();
}
