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
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>
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

constexpr int OverlayWidth = 256;
constexpr int OverlayHeight = 152;
constexpr auto HistoryWindow = std::chrono::seconds{5};
constexpr auto StatsWindow = std::chrono::milliseconds{500};

constexpr std::array CpuRoles{
    Role::GuestCore0, Role::GuestCore1,      Role::GuestCore2, Role::GuestCore3, Role::GuestCore4,
    Role::GuestCore5, Role::GuestSingleCore, Role::GpuCommand, Role::GpuBack,
};

QString Number(const std::optional<double>& value, int precision = 0) {
    return value ? QString::number(*value, 'f', precision) : QStringLiteral("—");
}

} // namespace

PerformanceOverlay::PerformanceOverlay(MainWindow* parent, QWidget* anchor)
    : QWidget(anchor->window(), Qt::FramelessWindowHint | Qt::Tool), m_mainWindow(parent),
      m_anchor(anchor) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedSize(OverlayWidth, OverlayHeight);
    m_anchor->installEventFilter(this);
    connect(parent, &MainWindow::positionChanged, this, &PerformanceOverlay::ResetPosition);
    connect(parent, &MainWindow::statsUpdated, this, &PerformanceOverlay::updateStats);
    ResetPosition();
}

void PerformanceOverlay::ResetPosition() {
    move(m_anchor->mapToGlobal(m_offset));
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
    m_at_target = m_speed && Settings::values.use_speed_limit.GetValue() &&
                  std::abs(*m_speed - Settings::SpeedLimit()) < 3.0;
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

    const auto filter = Settings::values.scaling_filter.GetValue();
    const auto backend = Settings::values.renderer_backend.GetValue();
    const auto label = [](const auto& options, auto value) {
        return QCoreApplication::translate("MainWindow", options.at(value).toUtf8().constData());
    };
    m_tags = tr("%1 · %2× · %3")
                 .arg(label(ConfigurationShared::renderer_backend_texts_map, backend))
                 .arg(Settings::values.resolution_info.up_factor)
                 .arg(label(ConfigurationShared::scaling_filter_texts_map, filter));

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
        details << tr("Frame time (min / median / max): %1 / %2 / %3 ms")
                       .arg(summary->min_ms, 0, 'f', 1)
                       .arg(summary->median_ms, 0, 'f', 1)
                       .arg(summary->max_ms, 0, 'f', 1);
    }
    if (!m_mainWindow->DlssStatusText().isEmpty()) {
        details << m_mainWindow->DlssStatusText();
    }
    setToolTip(details.join(QLatin1Char('\n')));
    update();
}

void PerformanceOverlay::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Color::Hud());
    painter.drawRoundedRect(rect(), Radius::Card, Radius::Card);

    const auto line = [&](const QRect& area, const QString& text, const QColor& ink,
                          int pixels = 11, int weight = 500,
                          Qt::Alignment alignment = Qt::AlignLeft) {
        painter.setFont(Font(pixels, weight));
        painter.setPen(ink);
        painter.drawText(area, alignment | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(text, Qt::ElideRight, area.width()));
    };

    const QString fps = Number(m_fps);
    const int fps_width = QFontMetrics(Font(26, 700)).horizontalAdvance(fps);
    line(QRect{12, 10, fps_width + 1, 35}, fps, m_at_target ? Color::Success() : Color::Text(), 26,
         700);
    const int label_x = 20 + fps_width;
    line(QRect{label_x, 12, 96, 15}, tr("FPS"), Color::TextSecondary(), 11, 600);
    line(QRect{label_x, 27, 96, 15}, tr("1% low %1").arg(Number(m_low_fps)), Color::TextMuted());
    if (m_fps && m_generated >= 0.5) {
        line(QRect{164, 11, 80, 20}, QString::number(*m_fps + m_generated, 'f', 0), Color::Text(),
             15, 700, Qt::AlignRight);
        line(QRect{164, 31, 80, 13}, tr("with frame gen"), Color::TextMuted(), 10, 500,
             Qt::AlignRight);
    }

    paintGraph(painter, QRectF{12, 50, 232, 30});

    line(QRect{12, 85, 120, 15}, tr("Frame %1 ms").arg(Number(m_frameMs, 1)),
         Color::TextSecondary());
    if (m_speed) {
        line(QRect{132, 85, 112, 15}, tr("Speed %1%").arg(Number(m_speed)), Color::TextSecondary(),
             11, 500, Qt::AlignRight);
    }

    const int tags_width = m_shaders > 0 ? 150 : 232;
    line(QRect{12, 105, tags_width, 15}, m_tags, Color::TextSecondary());
    if (m_shaders > 0) {
        line(QRect{164, 105, 80, 15}, tr("Shaders %1").arg(m_shaders), Color::Warning(), 11, 600,
             Qt::AlignRight);
    }
    const QString gpu = m_gpuPercent ? tr("GPU %1%").arg(Number(m_gpuPercent)) : tr("GPU —");
    const QString cpu = m_cpuPercent ? tr("CPU %1%").arg(Number(m_cpuPercent)) : tr("CPU —");
    line(QRect{12, 125, 232, 15}, tr("%1  %2").arg(gpu, cpu), Color::TextFaint());
}

void PerformanceOverlay::paintGraph(QPainter& painter, const QRectF& area) {
    if (m_frames.size() < 2) {
        return;
    }
    auto [min, max] = std::minmax_element(m_frames.begin(), m_frames.end(),
                                          [](const auto& a, const auto& b) { return a.ms < b.ms; });
    const qreal span = std::max(2.0f, max->ms - min->ms);
    const qreal floor = (max->ms + min->ms - span) / 2;
    const auto newest = m_frames.back().end;
    const double window = std::chrono::duration<double>(HistoryWindow).count();
    QPolygonF points;
    points.reserve(static_cast<qsizetype>(m_frames.size()));
    for (const auto& frame : m_frames) {
        const double age = std::chrono::duration<double>(newest - frame.end).count();
        points << QPointF{area.right() - area.width() * std::clamp(age / window, 0.0, 1.0),
                          area.bottom() - 2 - (area.height() - 6) * (frame.ms - floor) / span};
    }
    QPainterPath fill;
    fill.addPolygon(points);
    fill.lineTo(points.back().x(), area.bottom());
    fill.lineTo(points.front().x(), area.bottom());
    fill.closeSubpath();
    auto ink = Color::Success();
    ink.setAlpha(36);
    painter.fillPath(fill, ink);
    painter.setPen(QPen(Color::Success(), 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPolyline(points);
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
    m_tags.clear();
}

void PerformanceOverlay::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) && windowHandle() &&
        windowHandle()->startSystemMove()) {
        return;
    }
    m_drag_start_pos = event->pos();
}

void PerformanceOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint target = event->globalPosition().toPoint() - m_drag_start_pos;
        m_offset = m_anchor->mapFromGlobal(target);
        move(target);
    }
}

void PerformanceOverlay::closeEvent(QCloseEvent*) {
    emit closed();
}
