// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <thread>

#include <array>

#include "common/common_types.h"
#include "lichii/spotlight/dialog.h"

namespace Core {
class System;
}

namespace VideoCommon::ShaderWarm {
struct Progress;
struct Result;
} // namespace VideoCommon::ShaderWarm

class QCloseEvent;
class QLabel;
class QProgressBar;
class QPushButton;

namespace Spotlight {
class Notice;
}

/// True when the title has a shader precache file.
bool ShaderWarmFilesExist(u64 title_id);

/// True when the user asked to precompile all of the title's shaders (Precompile All Shaders).
bool ShaderWarmRequested(u64 title_id);

/// Sets or clears that request. Returns false when it cannot be changed.
bool SetShaderWarmRequested(u64 title_id, bool requested);

/// Removes the title's precache files. Returns false and fills `error` on failure.
bool DeleteShaderWarmFiles(u64 title_id, std::string& error);

/// Window that builds a game's shader precache file on a worker thread and shows its stages, the
/// progress and the result. Closing it while it works stops the build first.
class ShaderWarmDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit ShaderWarmDialog(Core::System& system, std::filesystem::path game_path, u64 title_id,
                              const QString& game_name, QWidget* parent = nullptr);
    ~ShaderWarmDialog() override;

    void Start();
    bool IsRunning() const {
        return running;
    }
    /// The title whose precache file this dialog builds.
    u64 TitleId() const {
        return title_id;
    }

signals:
    void BuildFinished(bool ok);

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private:
    void RequestStop();
    void OnProgress(const QString& stage, qulonglong done, qulonglong total);
    void OnFinished(const VideoCommon::ShaderWarm::Result& result, bool stopped);

    Core::System& system;
    std::filesystem::path game_path;
    u64 title_id;

    struct Stage {
        QLabel* tile;
        QLabel* name;
        QLabel* value;
    };

    void SetStage(int index, const QString& value);

    std::array<Stage, 5> stages{};
    int current_stage = -1;
    QProgressBar* progress_bar{};
    Spotlight::Notice* result_notice{};
    QLabel* footer{};
    QPushButton* button{};

    bool running{};
    bool stopping{};
    std::jthread worker;
};
