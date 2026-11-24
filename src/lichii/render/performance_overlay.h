// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <vector>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

#include "common/dkb_cpu_trace.h"
#include "core/perf_stats.h"

namespace VideoCore {
class ShaderNotify;
}

class MainWindow;

/// The performance card (Paper 2CGI-1): FPS, frame-time graph, frame, speed, GPU and CPU, and the
/// renderer chips. It follows the game window, can be dragged, and closing it turns the action off.
class PerformanceOverlay : public QWidget {
    Q_OBJECT

public:
    explicit PerformanceOverlay(MainWindow* parent, QWidget* anchor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ResetPosition();
    void updateStats(const Core::PerfStatsResults& results, const VideoCore::ShaderNotify& shaders);
    void paintGraph(QPainter& painter, const QRectF& area);
    void UpdateHeight();
    QRect Card() const;
    QRect CloseButton() const;

    MainWindow* m_mainWindow = nullptr;
    QWidget* m_anchor = nullptr;

    QPoint m_offset{20, 20};

    // drag
    QPoint m_drag_start_pos;
    bool m_dragging = false;
    bool m_close_hovered = false;
    bool m_close_pressed = false;

    std::optional<double> m_fps;
    std::optional<double> m_low_fps;
    std::optional<double> m_speed;
    double m_generated = 0;
    int m_shaders = 0;
    std::optional<double> m_frameMs;
    std::optional<double> m_cpuPercent;
    std::optional<double> m_gpuPercent;
    std::optional<Common::DkbCpuTrace::Snapshot> m_cpuSnapshot;
    std::vector<Core::FrameTimeHistory::Entry> m_frames;
    QStringList m_chips;
    QPixmap m_shadow;

signals:
    void closed();
};
