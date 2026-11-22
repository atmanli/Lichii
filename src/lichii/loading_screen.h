// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <memory>
#include <QPixmap>
#include <QWidget>

namespace Loader {
class AppLoader;
}
namespace VideoCore {
enum class LoadCallbackStage;
}
namespace Spotlight {
class PillButton;
}
class QAction;
class QPropertyAnimation;

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
    QPixmap backdrop;
    QString title;
    QString version;
    QString estimate;
    bool switch2 = false;
    Spotlight::PillButton* skip_button = nullptr;
    Spotlight::PillButton* cancel_button = nullptr;
    QAction* cancel_action = nullptr;
    std::unique_ptr<QPropertyAnimation> fadeout_animation;
    std::chrono::steady_clock::time_point slow_shader_start{};
    std::chrono::steady_clock::time_point previous_time{};
    std::size_t slow_shader_first_value = 0;
};
