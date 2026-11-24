// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <memory>
#include <system_error>
#include <utility>

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "common/settings.h"
#include "lichii/shader_warm_dialog.h"
#include "lichii/spotlight/notice.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "video_core/shader_warm/shader_warm_builder.h"

namespace ShaderWarm = VideoCommon::ShaderWarm;

bool ShaderWarmFilesExist(u64 title_id) {
    if (title_id == 0) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(ShaderWarm::ComputeWarmPath(title_id), ec);
}

bool ShaderWarmRequested(u64 title_id) {
    return title_id != 0 && ShaderWarm::WarmRequested(ShaderWarm::ComputeWarmPath(title_id));
}

bool SetShaderWarmRequested(u64 title_id, bool requested) {
    return title_id != 0 &&
           ShaderWarm::SetWarmRequested(ShaderWarm::ComputeWarmPath(title_id), requested);
}

bool DeleteShaderWarmFiles(u64 title_id, std::string& error) {
    return ShaderWarm::DeleteWarmFiles(title_id, &error);
}

namespace {

constexpr int StageTile = 26;

// The builder's stages in the order it runs them, with the names the dialog shows.
struct StageName {
    const char* builder;
    const char* shown;
};
constexpr std::array<StageName, 5> StageNames{{
    {"Reading game files", QT_TRANSLATE_NOOP("ShaderWarmDialog", "Reading game files")},
    {"Extracting shaders", QT_TRANSLATE_NOOP("ShaderWarmDialog", "Extracting shaders")},
    {"Converting", QT_TRANSLATE_NOOP("ShaderWarmDialog", "Converting shaders")},
    {"Reading asset packs (MiB)", QT_TRANSLATE_NOOP("ShaderWarmDialog", "Reading asset packs")},
    {"Writing", QT_TRANSLATE_NOOP("ShaderWarmDialog", "Writing the precache file")},
}};

// The position of a stage the builder reports, or -1 for one the dialog does not list.
int StageIndex(const std::string& stage) {
    for (std::size_t i = 0; i < StageNames.size(); ++i) {
        if (stage == StageNames[i].builder) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

enum class StageState { Pending, Active, Done };

// A round 26 px tile: a dot while the stage waits, a ring while it runs, a check when it is done.
QPixmap StageIcon(StageState state, qreal ratio) {
    QPixmap tile(QSize(StageTile, StageTile) * ratio);
    tile.setDevicePixelRatio(ratio);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QColor tint = state == StageState::Done     ? Spotlight::Color::Success()
                        : state == StageState::Active ? Spotlight::Color::Accent()
                                                      : Spotlight::Color::TextMuted();
    QColor fill = state == StageState::Pending ? Spotlight::Color::Fill() : tint;
    if (state != StageState::Pending) {
        fill.setAlpha(state == StageState::Done ? 0x2e : 0x24);
    }
    painter.setBrush(fill);
    painter.drawEllipse(QRectF(0, 0, StageTile, StageTile));
    if (state == StageState::Pending) {
        painter.setBrush(tint);
        painter.drawEllipse(QRectF(10, 10, 6, 6));
    } else {
        painter.drawPixmap(
            6, 6,
            Spotlight::IconPixmap(state == StageState::Done ? u"check" : u"circle-notch", 14, tint,
                                  ratio));
    }
    return tile;
}

} // namespace

/// Builds the stage list, progress bar, result notice and the Stop/Close button.
ShaderWarmDialog::ShaderWarmDialog(Core::System& system_, std::filesystem::path game_path_,
                                   u64 title_id_, const QString& game_name, QWidget* parent)
    : Spotlight::Dialog(parent), system{system_}, game_path{std::move(game_path_)},
      title_id{title_id_} {
    setWindowTitle(tr("Preparing shaders"));
    SetSubtitle(tr("%1 \u00b7 fewer hitches the first time you play").arg(game_name));
    SetIcon(u"sparkle", Spotlight::Color::Warning());
    SetCardWidth(520);
    setModal(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* list = new QVBoxLayout;
    list->setSpacing(12);
    for (std::size_t i = 0; i < stages.size(); ++i) {
        auto* row = new QHBoxLayout;
        row->setSpacing(12);
        Stage& stage = stages[i];
        stage.tile = new QLabel;
        stage.tile->setFixedSize(StageTile, StageTile);
        stage.name = new QLabel(tr(StageNames[i].shown));
        stage.value = new QLabel;
        stage.value->setFont(Spotlight::Font(12, 600));
        Spotlight::SetTextColor(stage.value, Spotlight::Color::TextMuted());
        row->addWidget(stage.tile);
        row->addWidget(stage.name, 1);
        row->addWidget(stage.value);
        list->addLayout(row);
    }
    layout->addLayout(list);

    progress_bar = new QProgressBar;
    progress_bar->setTextVisible(false);
    progress_bar->setRange(0, 0);
    layout->addWidget(progress_bar);

    result_notice = new Spotlight::Notice;
    result_notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_notice->hide();
    layout->addWidget(result_notice);

    auto* bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 6, 0, 0);
    bottom->setSpacing(10);
    footer = new QLabel(tr("Closing this stops the build"));
    footer->setFont(Spotlight::Font(12, 400));
    Spotlight::SetTextColor(footer, Spotlight::Color::TextMuted());
    bottom->addWidget(footer, 1);
    auto* buttons = new QDialogButtonBox;
    button = buttons->addButton(tr("Stop"), QDialogButtonBox::RejectRole);
    button->setProperty("kind", QStringLiteral("destructive"));
    connect(buttons, &QDialogButtonBox::rejected, this, &ShaderWarmDialog::reject);
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    SetStage(-1, {});
}

/// Marks the stages before `index` done, `index` running with `value`, and the rest waiting.
void ShaderWarmDialog::SetStage(int index, const QString& value) {
    const qreal ratio = devicePixelRatioF();
    for (int i = 0; i < static_cast<int>(stages.size()); ++i) {
        Stage& stage = stages[static_cast<std::size_t>(i)];
        const StageState state = i < index    ? StageState::Done
                                 : i == index ? StageState::Active
                                              : StageState::Pending;
        stage.tile->setPixmap(StageIcon(state, ratio));
        stage.name->setFont(Spotlight::Font(14, state == StageState::Active ? 600 : 500));
        Spotlight::SetTextColor(stage.name, state == StageState::Pending
                                                ? Spotlight::Color::TextMuted()
                                                : Spotlight::Color::Text());
        if (state == StageState::Active) {
            stage.value->setText(value);
            Spotlight::SetTextColor(stage.value, Spotlight::Color::AccentLight());
        } else if (state == StageState::Pending) {
            stage.value->clear();
        } else {
            Spotlight::SetTextColor(stage.value, Spotlight::Color::TextMuted());
        }
    }
    current_stage = index;
}

/// Stops and joins a running build.
ShaderWarmDialog::~ShaderWarmDialog() {
    if (worker.joinable()) {
        worker.request_stop();
        worker.join();
    }
}

/// Starts the build on a worker thread. Progress (at most every 100 ms unless the stage changes)
/// and the result, failures included, come back on the UI thread.
void ShaderWarmDialog::Start() {
    if (running) {
        return;
    }
    running = true;
    stopping = false;
    worker = std::jthread([this](std::stop_token stop) {
        auto last_update = std::chrono::steady_clock::time_point{};
        std::string last_stage;
        const auto on_progress = [this, &last_update, &last_stage](const ShaderWarm::Progress& p) {
            const auto now = std::chrono::steady_clock::now();
            const bool stage_changed = p.stage != last_stage;
            const bool stage_done = p.total != 0 && p.done >= p.total;
            if (!stage_changed && !stage_done &&
                now - last_update < std::chrono::milliseconds(100)) {
                return;
            }
            last_update = now;
            last_stage = p.stage;
            QMetaObject::invokeMethod(
                this,
                [this, stage = QString::fromStdString(p.stage),
                 done = static_cast<qulonglong>(p.done),
                 total = static_cast<qulonglong>(p.total)] { OnProgress(stage, done, total); },
                Qt::QueuedConnection);
        };
        // An exception must not leave the thread (std::terminate would close the emulator):
        // report it as a failed build instead.
        auto result = std::make_shared<ShaderWarm::Result>();
        try {
            *result = ShaderWarm::BuildWarmFiles(system, game_path, title_id, on_progress, stop);
        } catch (const std::exception& e) {
            result->ok = false;
            result->error = e.what();
        } catch (...) {
            result->ok = false;
            result->error = "unknown error";
        }
        const bool stopped = stop.stop_requested();
        QMetaObject::invokeMethod(
            this, [this, result, stopped] { OnFinished(*result, stopped); },
            Qt::QueuedConnection);
    });
}

/// Asks a running build to stop and disables the button until it has.
void ShaderWarmDialog::RequestStop() {
    if (!running || stopping) {
        return;
    }
    stopping = true;
    worker.request_stop();
    footer->setText(tr("Stopping\u2026"));
    button->setEnabled(false);
}

/// Shows the stage and its progress; a busy bar when the total is unknown.
void ShaderWarmDialog::OnProgress(const QString& stage, qulonglong done, qulonglong total) {
    if (stopping) {
        return;
    }
    const int index = StageIndex(stage.toStdString());
    QString value;
    if (total > 0) {
        const auto permille = static_cast<int>((std::min<qulonglong>)(done * 1000 / total, 1000));
        value = tr("%1%").arg(permille / 10);
        progress_bar->setRange(0, 1000);
        progress_bar->setValue(permille);
    } else {
        value = done > 0 ? tr("%L1").arg(done) : QString{};
        progress_bar->setRange(0, 0);
    }
    // A finished stage keeps the value it ended on.
    if (index >= 0) {
        SetStage(index, value);
    }
}

/// Joins the worker and shows the result: program counts, stopped, or the error text.
void ShaderWarmDialog::OnFinished(const ShaderWarm::Result& result, bool stopped) {
    running = false;
    if (worker.joinable()) {
        worker.join();
    }
    progress_bar->setRange(0, 1);
    progress_bar->setValue(result.ok ? 1 : 0);

    QString text;
    if (result.ok) {
        SetStage(static_cast<int>(stages.size()), {});
        result_notice->SetTone(Spotlight::Notice::Tone::Success);
        text = tr("Shader precache is ready.") + QLatin1Char('\n');
        if (result.compute_programs == 0) {
            text += result.archives == 0 && result.packs == 0
                        ? tr("This game keeps its shaders in a format Lichii can't read, so none "
                             "are prepared from the game files.")
                        : tr("The game files have no compute shaders to prepare.");
        } else {
            text +=
                tr("Compute programs: %L1").arg(static_cast<qulonglong>(result.compute_programs));
        }
        if (result.skipped > 0) {
            text += QLatin1Char('\n') +
                    tr("Skipped (could not be read or converted): %L1")
                        .arg(static_cast<qulonglong>(result.skipped));
        }
        text += QStringLiteral("\n\n") +
                (ShaderWarmRequested(title_id)
                     ? tr("They are compiled on the loading screen the next time you start the "
                          "game, on every CPU thread, with every pipeline your earlier sessions "
                          "saved. Skip stops it; the next start goes on from there.")
                     : tr("It is built on the loading screen when Precache shaders before game "
                          "start is on."));
    } else if (stopped) {
        result_notice->SetTone(Spotlight::Notice::Tone::Neutral);
        text = tr("Shader precache stopped.");
    } else {
        result_notice->SetTone(Spotlight::Notice::Tone::Error);
        text = tr("Shader precache could not be built.") + QLatin1Char('\n') +
               (result.error.empty() ? tr("No details were given.")
                                     : QString::fromStdString(result.error));
    }
    result_notice->setText(text);
    result_notice->setVisible(!text.isEmpty());
    footer->clear();
    button->setText(tr("Close"));
    button->setProperty("kind", QStringLiteral("primary"));
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->setEnabled(true);
    button->setDefault(true);
    button->setFocus();
    resize(width(), (std::max)(sizeHint().height(), heightForWidth(width())));
    emit BuildFinished(result.ok);
}

/// Closing while a build runs only asks it to stop.
void ShaderWarmDialog::closeEvent(QCloseEvent* event) {
    if (running) {
        RequestStop();
        event->ignore();
        return;
    }
    Spotlight::Dialog::closeEvent(event);
}

/// Escape or Cancel while a build runs only asks it to stop.
void ShaderWarmDialog::reject() {
    if (running) {
        RequestStop();
        return;
    }
    Spotlight::Dialog::reject();
}
