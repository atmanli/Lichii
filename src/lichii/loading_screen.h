// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

#include "common/common_types.h"

namespace Loader {
class AppLoader;
}
namespace VideoCore {
enum class LoadCallbackStage;
}
class QAbstractButton;
class QAction;

class LoadingScreen : public QWidget {
    Q_OBJECT

public:
    explicit LoadingScreen(QWidget* parent = nullptr);
    ~LoadingScreen() override;

    void Prepare(Loader::AppLoader& loader, QString game_title = {}, QString game_version = {});
    void SetCancelAction(QAction* action);
    void Clear();
    void OnLoadProgress(VideoCore::LoadCallbackStage stage, std::size_t value, std::size_t total);
    void OnLoadComplete();

signals:
    void Hidden();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect ContentRect() const;
    void LayoutButtons();
    void UpdateBackdrop();

    VideoCore::LoadCallbackStage stage;
    std::size_t current = 0;
    std::size_t total = 0;
    QPixmap icon;
    QPixmap art;
    QImage hero_art;
    QColor tint;
    u64 program_id = 0;
    QPixmap backdrop;
    QString title;
    QString version;
    QString estimate;
    bool switch2 = false;
    bool precompile_requested = false;
    QAbstractButton* skip_button = nullptr;
    QAbstractButton* cancel_button = nullptr;
    QAction* cancel_action = nullptr;
    QTimer repaint;
    QVariantAnimation fade;
    std::chrono::steady_clock::time_point slow_shader_start{};
    std::chrono::steady_clock::time_point previous_time{};
    std::size_t slow_shader_first_value = 0;
};
