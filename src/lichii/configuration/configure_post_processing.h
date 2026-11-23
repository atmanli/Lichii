// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSet>

#include "lichii/spotlight/dialog.h"

class QAbstractButton;
class QComboBox;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace Spotlight {
class StatusBanner;
}

namespace VideoCore {
struct FxChainEntry;
struct FxEffectDesc;
struct FxUniformDesc;
}

class ConfigurePostProcessing : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit ConfigurePostProcessing(QWidget* parent = nullptr);
    ~ConfigurePostProcessing() override;

private:
    void RebuildRows();
    void BuildSlot(int index, const VideoCore::FxChainEntry& entry);
    void BuildUniformRow(int index, const VideoCore::FxUniformDesc& uniform,
                         const QString& category, QList<QWidget*>& body);
    void PopulateEffectCombo(QComboBox* combo, const VideoCore::FxChainEntry& entry) const;
    void ApplyStructuralChange();
    void PopulatePresetCombo();
    void RefreshPresetStatus();
    void SavePreset();
    void FitChain();

    QVBoxLayout* slots_layout{};
    QWidget* slots_card{};
    QWidget* chain_heading{};
    QScrollArea* scroll{};
    QWidget* sources{};
    QComboBox* preset_combo{};
    QComboBox* add_combo{};
    Spotlight::StatusBanner* preset_status{};
    QAbstractButton* enabled_box{};
    QSet<int> expanded;
};
