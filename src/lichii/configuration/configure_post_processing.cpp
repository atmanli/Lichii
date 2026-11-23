// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

#include "common/settings.h"
#include "lichii/configuration/configure_post_processing.h"
#include "lichii/spotlight/settings_page.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "video_core/post_processing/fx_chain.h"
#include "video_core/post_processing/fx_effect.h"
#include "video_core/post_processing/fx_preset.h"

namespace {

using Spotlight::PillButton;
using Spotlight::SettingsPage;
using Spotlight::StatusBanner;
namespace Color = Spotlight::Color;

std::array<float, 4> CurrentValue(int index, const VideoCore::FxUniformDesc& uniform) {
    auto& chain = VideoCore::FxChain::Instance();
    if (chain.HasValue(static_cast<size_t>(index), uniform.name)) {
        return chain.GetValue(static_cast<size_t>(index), uniform.name);
    }
    return uniform.default_value;
}

int SliderSteps(const VideoCore::FxUniformDesc& uniform) {
    const float span = uniform.ui_max - uniform.ui_min;
    const int steps = static_cast<int>(std::lround(span / uniform.ui_step));
    if (steps < 1) {
        return 1;
    }
    return steps;
}

QString FormatValue(const VideoCore::FxUniformDesc& uniform, float value) {
    if (uniform.kind == VideoCore::FxUniformKind::Floating) {
        return QString::number(value, 'f', 3);
    }
    return QString::number(static_cast<int>(std::lround(value)));
}

QString SlotLabel(const VideoCore::FxEffectDesc& effect, const std::string& technique) {
    const QString name = QString::fromStdString(effect.label);
    if (effect.techniques.size() == 1) {
        return name;
    }
    return name + QStringLiteral(" · ") + QString::fromStdString(technique);
}

QWidget* Line(QWidget* first, QWidget* second = nullptr) {
    auto* line = new QWidget;
    line->setProperty("spotlightTransparent", true);
    auto* layout = new QHBoxLayout(line);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(first);
    if (second != nullptr) {
        layout->addWidget(second);
    }
    return line;
}

} // Anonymous namespace

ConfigurePostProcessing::ConfigurePostProcessing(QWidget* parent) : Spotlight::Dialog(parent) {
    setWindowTitle(tr("Post-Processing Effects"));
    SetIcon(u"magic-wand", Color::Accent());
    setFixedWidth(760);

    sources = new QWidget(this);
    sources->hide();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);

    auto* enable_row = new QHBoxLayout;
    enable_row->setContentsMargins(18, 0, 16, 0);
    enable_row->addWidget(SettingsPage::Text(tr("Enable post-processing"), 14, 600), 1);
    auto* enabled = new Spotlight::ToggleSwitch;
    enabled->setAccessibleName(tr("Enable post-processing"));
    enabled->setChecked(Settings::values.post_shader_enabled.GetValue());
    connect(enabled, &QAbstractButton::toggled, this,
            [](bool checked) { Settings::values.post_shader_enabled.SetValue(checked); });
    enabled_box = enabled;
    enable_row->addWidget(enabled);
    root->addLayout(enable_row);

    auto* preset_row = new QHBoxLayout;
    preset_row->setSpacing(8);
    preset_row->addWidget(SettingsPage::Text(tr("Preset"), 13, 600));
    preset_combo = new QComboBox(sources);
    auto* preset_pill = SettingsPage::BindCombo(preset_combo);
    preset_pill->setAccessibleName(tr("Preset"));
    preset_row->addWidget(preset_pill);

    auto* apply_button = new PillButton(tr("Apply"), {}, PillButton::Kind::Accent, 34);
    connect(apply_button, &QAbstractButton::clicked, this, [this]() {
        const QString name = preset_combo->currentData().toString();
        if (name.isEmpty()) {
            return;
        }
        VideoCore::ApplyFxPreset(name.toStdString());
        ApplyStructuralChange();
    });
    preset_row->addWidget(apply_button);

    auto* reset_button = new PillButton(tr("Reset Values"), {}, PillButton::Kind::Control, 34);
    reset_button->setToolTip(tr("Return every effect to the values the preset ships with."));
    connect(reset_button, &QAbstractButton::clicked, this, [this]() {
        const std::string active = VideoCore::GetActiveFxPreset();
        if (active.empty()) {
            return;
        }
        VideoCore::ApplyFxPreset(active);
        ApplyStructuralChange();
    });
    preset_row->addWidget(reset_button);

    auto* save_button = new PillButton(tr("Save As..."), {}, PillButton::Kind::Control, 34);
    connect(save_button, &QAbstractButton::clicked, this, [this] { SavePreset(); });
    preset_row->addWidget(save_button);

    auto* clear_button = new PillButton(tr("Clear"), {}, PillButton::Kind::Control, 34);
    clear_button->setToolTip(tr("Remove every effect in use and deselect the preset."));
    connect(clear_button, &QAbstractButton::clicked, this, [this]() {
        VideoCore::FxChain::Instance().Clear();
        VideoCore::SetActiveFxPreset(std::string());
        preset_combo->setCurrentIndex(0);
        ApplyStructuralChange();
    });
    preset_row->addWidget(clear_button);
    preset_row->addStretch();
    root->addLayout(preset_row);

    preset_status = new StatusBanner;
    root->addWidget(preset_status);

    auto* effects_row = new QHBoxLayout;
    effects_row->setSpacing(8);
    effects_row->addWidget(SettingsPage::Text(tr("Effects"), 14, 600), 1);
    add_combo = new QComboBox(sources);
    for (const auto& effect : VideoCore::GetFxCatalog()) {
        if (!effect.Valid()) {
            continue;
        }
        for (const auto& technique : effect.techniques) {
            add_combo->addItem(SlotLabel(effect, technique),
                               QString::fromStdString(effect.file + "|" + technique));
            add_combo->setItemData(add_combo->count() - 1,
                                   QString::fromStdString(effect.description), Qt::ToolTipRole);
        }
    }
    auto* add_pill = SettingsPage::BindCombo(add_combo, tr("Installed techniques"));
    add_pill->setAccessibleName(tr("Installed techniques"));
    effects_row->addWidget(add_pill);
    auto* add_button =
        new PillButton(tr("Add Effect"), QStringLiteral("plus"), PillButton::Kind::Accent, 34);
    add_button->setEnabled(add_combo->count() > 0);
    connect(add_button, &QAbstractButton::clicked, this, [this]() {
        const QString key = add_combo->currentData().toString();
        const qsizetype separator = key.indexOf(QLatin1Char('|'));
        if (separator < 0) {
            return;
        }
        VideoCore::FxChain::Instance().Append(key.left(separator).toStdString(),
                                              key.mid(separator + 1).toStdString());
        expanded.insert(static_cast<int>(VideoCore::FxChain::Instance().Size()) - 1);
        ApplyStructuralChange();
    });
    effects_row->addWidget(add_button);
    root->addLayout(effects_row);

    chain_heading = new QWidget;
    chain_heading->setProperty("spotlightTransparent", true);
    auto* heading_layout = new QHBoxLayout(chain_heading);
    heading_layout->setContentsMargins(6, 0, 6, 0);
    heading_layout->setSpacing(10);
    heading_layout->addWidget(SettingsPage::Text(tr("Effect chain"), 14, 600), 0,
                              Qt::AlignBaseline);
    heading_layout->addWidget(SettingsPage::Text(tr("Rendered in order"), 12, 500, true), 1,
                              Qt::AlignBaseline);
    root->addWidget(chain_heading);

    scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    slots_card = SettingsPage::Card();
    slots_card->setProperty("compactRows", true);
    slots_layout = new QVBoxLayout(slots_card);
    slots_layout->setContentsMargins(1, 1, 1, 1);
    slots_layout->setSpacing(0);
    scroll->setWidget(slots_card);
    root->addWidget(scroll);

    auto* footer = new QHBoxLayout;
    footer->addStretch();
    auto* close_button = new PillButton(tr("Close"), {}, PillButton::Kind::Control, 40);
    connect(close_button, &QAbstractButton::clicked, this, &QDialog::reject);
    footer->addWidget(close_button);
    root->addLayout(footer);

    VideoCore::ReloadFxCatalog();
    VideoCore::ReloadFxPresetCatalog();
    VideoCore::FxChain::Instance().LoadFromSettings();
    VideoCore::FxChain::Instance().DropUnknownEntries();
    PopulatePresetCombo();
    RebuildRows();
    RefreshPresetStatus();
}

ConfigurePostProcessing::~ConfigurePostProcessing() = default;

void ConfigurePostProcessing::ApplyStructuralChange() {
    VideoCore::FxChain::Instance().StoreToSettings();
    RebuildRows();
    RefreshPresetStatus();
}

void ConfigurePostProcessing::SavePreset() {
    Spotlight::Dialog ask(this);
    ask.setWindowTitle(tr("Save Preset"));
    ask.setFixedWidth(400);
    auto* layout = new QVBoxLayout(&ask);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(SettingsPage::Text(tr("Preset name"), 13, 600, true));
    auto* name = new QLineEdit;
    name->setPlaceholderText(tr("Enter a name"));
    name->setAccessibleName(tr("Preset name"));
    name->setMinimumHeight(40);
    layout->addWidget(name);
    layout->addSpacing(10);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    auto* cancel = new PillButton(tr("Cancel"), {}, PillButton::Kind::Control, 40);
    auto* ok = new PillButton(tr("OK"), {}, PillButton::Kind::Accent, 40);
    ok->setEnabled(false);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
    connect(name, &QLineEdit::textChanged, ok,
            [ok](const QString& text) { ok->setEnabled(!text.trimmed().isEmpty()); });
    connect(name, &QLineEdit::returnPressed, ok, &QAbstractButton::click);
    connect(cancel, &QAbstractButton::clicked, &ask, &QDialog::reject);
    connect(ok, &QAbstractButton::clicked, &ask, &QDialog::accept);
    name->setFocus();
    if (ask.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) {
        return;
    }
    VideoCore::SaveFxPreset(name->text().trimmed().toStdString(), std::string());
    PopulatePresetCombo();
    RefreshPresetStatus();
}

void ConfigurePostProcessing::PopulatePresetCombo() {
    const QString previous = preset_combo->currentData().toString();
    preset_combo->clear();
    preset_combo->addItem(tr("None"), QString());

    for (const auto& preset : VideoCore::GetFxPresetCatalog()) {
        const QString name = QString::fromStdString(preset.name);
        preset_combo->addItem(name, name);
        preset_combo->setItemData(preset_combo->count() - 1,
                                  QString::fromStdString(preset.description), Qt::ToolTipRole);
    }

    const int restored = preset_combo->findData(previous);
    if (!previous.isEmpty() && restored >= 0) {
        preset_combo->setCurrentIndex(restored);
        return;
    }

    const int active = preset_combo->findData(QString::fromStdString(VideoCore::GetActiveFxPreset()));
    if (active >= 0) {
        preset_combo->setCurrentIndex(active);
        return;
    }

    preset_combo->setCurrentIndex(0);
}

void ConfigurePostProcessing::RefreshPresetStatus() {
    const std::string active = VideoCore::GetActiveFxPreset();
    QString text;
    if (active.empty()) {
        text = VideoCore::FxChain::Instance().Size() == 0
                   ? tr("No effects in use. Pick a preset and press Apply, or add effects one "
                        "by one.")
                   : tr("Custom chain. Pick a preset above and press Apply to replace it.");
    } else if (VideoCore::IsActiveFxPresetModified()) {
        text = tr("Active preset: %1 (modified). Press Reset Values to go back to how it ships.")
                   .arg(QString::fromStdString(active));
    } else {
        text = tr("Active preset: %1").arg(QString::fromStdString(active));
    }
    preset_status->Set(text, StatusBanner::Tone::Info);
}

void ConfigurePostProcessing::PopulateEffectCombo(QComboBox* combo,
                                                  const VideoCore::FxChainEntry& entry) const {
    combo->clear();
    int selected = -1;

    for (const auto& effect : VideoCore::GetFxCatalog()) {
        if (!effect.Valid()) {
            continue;
        }
        for (const auto& technique : effect.techniques) {
            const QString key = QString::fromStdString(effect.file + "|" + technique);
            combo->addItem(SlotLabel(effect, technique), key);
            combo->setItemData(combo->count() - 1, QString::fromStdString(effect.description),
                               Qt::ToolTipRole);
            if (effect.file == entry.file && technique == entry.technique) {
                selected = combo->count() - 1;
            }
        }
    }

    if (selected >= 0) {
        combo->setCurrentIndex(selected);
    }
}

void ConfigurePostProcessing::BuildUniformRow(int index, const VideoCore::FxUniformDesc& uniform,
                                              const QString& category, QList<QWidget*>& body) {
    const auto value = CurrentValue(index, uniform);
    const QString label = QString::fromStdString(uniform.label);
    const QString tooltip = QString::fromStdString(uniform.tooltip);
    const std::string name = uniform.name;
    const auto add = [&](const QString& title, QWidget* control) {
        auto* detail =
            SettingsPage::AddRow(slots_layout, title, category, {}, {}, control, tooltip);
        SettingsPage::NestRow(detail);
        body.append(detail->parentWidget());
    };

    if (uniform.ui_type == VideoCore::FxUiType::CheckBox) {
        auto* toggle = new Spotlight::ToggleSwitch;
        toggle->setAccessibleName(label);
        toggle->setChecked(value[0] != 0.0f);
        connect(toggle, &QAbstractButton::toggled, this, [index, name](bool checked) {
            std::array<float, 4> next{};
            if (checked) {
                next[0] = 1.0f;
            }
            VideoCore::FxChain::Instance().SetValue(static_cast<size_t>(index), name, next);
            VideoCore::FxChain::Instance().StoreToSettings();
        });
        add(label, toggle);
        return;
    }

    if (uniform.ui_type == VideoCore::FxUiType::Combo ||
        uniform.ui_type == VideoCore::FxUiType::Radio) {
        auto* combo = new QComboBox(sources);
        for (size_t i = 0; i < uniform.items.size(); ++i) {
            combo->addItem(QString::fromStdString(uniform.items[i]), static_cast<int>(i));
        }
        if (combo->count() == 0) {
            combo->addItem(tr("Enabled"), 1);
            combo->addItem(tr("Disabled"), 0);
        }
        const int current = static_cast<int>(std::lround(value[0]));
        if (current >= 0 && current < combo->count()) {
            combo->setCurrentIndex(current);
        }
        connect(combo, &QComboBox::currentIndexChanged, this, [index, name](int selected) {
            std::array<float, 4> next{};
            next[0] = static_cast<float>(selected);
            VideoCore::FxChain::Instance().SetValue(static_cast<size_t>(index), name, next);
            VideoCore::FxChain::Instance().StoreToSettings();
        });
        auto* pill = SettingsPage::BindCombo(combo);
        pill->setAccessibleName(label);
        add(label, pill);
        return;
    }

    for (unsigned component = 0; component < uniform.components; ++component) {
        QString component_label = label;
        if (uniform.components > 1) {
            component_label = label + QStringLiteral(" [%1]").arg(component);
        }

        auto* value_label = SettingsPage::Text(FormatValue(uniform, value[component]), 12, 600);
        value_label->setAlignment(Qt::AlignCenter);
        value_label->setMinimumWidth(64);
        value_label->setStyleSheet(
            QStringLiteral("QLabel { background: %1; border: 1px solid %2; border-radius: 10px; "
                           "padding: 5px 10px; }")
                .arg(Color::FillFaint().name(QColor::HexArgb),
                     Color::StrokeSubtle().name(QColor::HexArgb)));

        auto* slider = new QSlider(Qt::Horizontal, sources);
        slider->setMinimum(0);
        slider->setMaximum(SliderSteps(uniform));
        slider->setValue(
            static_cast<int>(std::lround((value[component] - uniform.ui_min) / uniform.ui_step)));

        const auto desc = uniform;
        connect(slider, &QSlider::valueChanged, this,
                [index, name, desc, component, value_label](int steps) {
                    auto next = CurrentValue(index, desc);
                    next[component] = desc.ui_min + static_cast<float>(steps) * desc.ui_step;
                    VideoCore::FxChain::Instance().SetValue(static_cast<size_t>(index), name, next);
                    VideoCore::FxChain::Instance().StoreToSettings();
                    value_label->setText(FormatValue(desc, next[component]));
                });
        auto* bound = SettingsPage::BindSlider(slider);
        bound->setMinimumWidth(240);
        bound->setAccessibleName(component_label);
        add(component_label, Line(value_label, bound));
    }
}

void ConfigurePostProcessing::BuildSlot(int index, const VideoCore::FxChainEntry& entry) {
    const size_t count = VideoCore::FxChain::Instance().Size();
    const VideoCore::FxEffectDesc* effect = VideoCore::FindFxEffect(entry.file);
    const bool open = expanded.contains(index);

    auto* controls = new QWidget;
    controls->setProperty("spotlightTransparent", true);
    auto* controls_layout = new QHBoxLayout(controls);
    controls_layout->setContentsMargins(0, 0, 0, 0);
    controls_layout->setSpacing(10);
    auto* toggle = new Spotlight::CircleButton(
        open ? QStringLiteral("caret-up") : QStringLiteral("caret-down"), 32);
    toggle->setAccessibleName(open ? tr("Collapse") : tr("Expand"));
    auto* up_button = new Spotlight::CircleButton(QStringLiteral("arrow-up"), 32);
    up_button->setAccessibleName(tr("Move up"));
    up_button->setEnabled(index > 0);
    auto* down_button = new Spotlight::CircleButton(QStringLiteral("arrow-down"), 32);
    down_button->setAccessibleName(tr("Move down"));
    down_button->setEnabled(static_cast<size_t>(index) + 1 < count);
    auto* remove_button = new Spotlight::CircleButton(QStringLiteral("trash"), 32);
    remove_button->setAccessibleName(tr("Remove"));
    for (auto* button : {toggle, up_button, down_button, remove_button}) {
        controls_layout->addWidget(button);
    }

    const QString title =
        effect != nullptr && effect->Valid()
            ? SlotLabel(*effect, entry.technique)
            : QString::fromStdString(effect != nullptr ? effect->label : entry.file);
    const QString state = effect == nullptr        ? tr("Effect file missing")
                          : !effect->error.empty() ? tr("Effect failed to compile")
                                                   : QString{};
    SettingsPage::AddRow(slots_layout, QStringLiteral("%1. %2").arg(index + 1).arg(title), state,
                         {}, {}, controls);

    connect(up_button, &QAbstractButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().Move(static_cast<size_t>(index), -1);
        const bool moved_open = expanded.remove(index);
        if (expanded.remove(index - 1)) {
            expanded.insert(index);
        }
        if (moved_open) {
            expanded.insert(index - 1);
        }
        ApplyStructuralChange();
    });
    connect(down_button, &QAbstractButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().Move(static_cast<size_t>(index), 1);
        const bool moved_open = expanded.remove(index);
        if (expanded.remove(index + 1)) {
            expanded.insert(index);
        }
        if (moved_open) {
            expanded.insert(index + 1);
        }
        ApplyStructuralChange();
    });
    connect(remove_button, &QAbstractButton::clicked, this, [this, index]() {
        VideoCore::FxChain::Instance().Remove(static_cast<size_t>(index));
        QSet<int> shifted;
        for (const int open_index : expanded) {
            if (open_index != index) {
                shifted.insert(open_index > index ? open_index - 1 : open_index);
            }
        }
        expanded = shifted;
        ApplyStructuralChange();
    });

    QList<QWidget*> body;
    auto* combo = new QComboBox(sources);
    PopulateEffectCombo(combo, entry);
    connect(combo, &QComboBox::currentIndexChanged, this, [this, index, combo](int) {
        const QString key = combo->currentData().toString();
        const qsizetype separator = key.indexOf(QLatin1Char('|'));
        if (separator < 0) {
            return;
        }
        VideoCore::FxChain::Instance().Replace(static_cast<size_t>(index),
                                               key.left(separator).toStdString(),
                                               key.mid(separator + 1).toStdString());
        ApplyStructuralChange();
    });
    auto* technique = SettingsPage::AddRow(slots_layout, tr("Technique"), {}, {}, {},
                                           SettingsPage::BindCombo(combo));
    SettingsPage::NestRow(technique);
    body.append(technique->parentWidget());

    const auto add_banner = [&](const QString& message, StatusBanner::Tone tone) {
        auto* holder = new QWidget;
        holder->setProperty("spotlightTransparent", true);
        auto* holder_layout = new QHBoxLayout(holder);
        holder_layout->setContentsMargins(46, 6, 16, 12);
        auto* banner = new StatusBanner;
        banner->Set(message, tone);
        holder_layout->addWidget(banner);
        slots_layout->addWidget(holder);
        body.append(holder);
    };
    if (effect == nullptr) {
        add_banner(tr("The file for this effect, %1, is no longer in the post_shaders folder.")
                       .arg(QString::fromStdString(entry.file)),
                   StatusBanner::Tone::Warning);
    } else if (!effect->error.empty()) {
        add_banner(tr("Effect failed to compile:\n%1").arg(QString::fromStdString(effect->error)),
                   StatusBanner::Tone::Error);
    } else {
        for (const auto& uniform : effect->uniforms) {
            BuildUniformRow(index, uniform, QString::fromStdString(uniform.category), body);
        }
        auto* reset = new PillButton(tr("Reset to defaults"), {}, PillButton::Kind::Control, 32);
        connect(reset, &QAbstractButton::clicked, this, [this, index]() {
            VideoCore::FxChain::Instance().ResetValues(static_cast<size_t>(index));
            ApplyStructuralChange();
        });
        auto* values = SettingsPage::AddRow(slots_layout, tr("Effect values"), {}, {}, {}, reset);
        SettingsPage::NestRow(values);
        body.append(values->parentWidget());
    }

    for (auto* widget : body) {
        widget->setVisible(open);
    }
    connect(toggle, &QAbstractButton::clicked, this, [this, index, toggle, body]() {
        const bool now_open = !expanded.contains(index);
        if (now_open) {
            expanded.insert(index);
        } else {
            expanded.remove(index);
        }
        toggle->SetIcon(now_open ? QStringLiteral("caret-up") : QStringLiteral("caret-down"));
        toggle->setAccessibleName(now_open ? tr("Collapse") : tr("Expand"));
        for (auto* widget : body) {
            widget->setVisible(now_open);
        }
        FitChain();
    });
}

void ConfigurePostProcessing::FitChain() {
    slots_layout->activate();
    const int wanted = slots_card->sizeHint().height();
    scroll->setFixedHeight(std::min(460, wanted));
    const QPoint center = geometry().center();
    adjustSize();
    if (isVisible()) {
        move(center - rect().center());
    }
}

void ConfigurePostProcessing::RebuildRows() {
    QLayoutItem* item = nullptr;
    while ((item = slots_layout->takeAt(0)) != nullptr) {
        if (item->widget() != nullptr) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
    for (auto* source : sources->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        if (source != preset_combo && source != add_combo) {
            source->deleteLater();
        }
    }

    const bool has_usable =
        std::any_of(VideoCore::GetFxCatalog().begin(), VideoCore::GetFxCatalog().end(),
                    [](const auto& effect) { return effect.Valid(); });
    const auto entries = VideoCore::FxChain::Instance().Entries();
    if (!has_usable) {
        auto* holder = new QWidget;
        holder->setProperty("spotlightTransparent", true);
        auto* holder_layout = new QHBoxLayout(holder);
        holder_layout->setContentsMargins(14, 14, 14, 14);
        auto* banner = new StatusBanner;
        banner->Set(tr("No usable ReShade FX effects were found. Place .fx files in the "
                       "post_shaders folder."),
                    StatusBanner::Tone::Warning);
        holder_layout->addWidget(banner);
        slots_layout->addWidget(holder);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        BuildSlot(static_cast<int>(i), entries[i]);
    }
    const bool shown = !has_usable || !entries.empty();
    chain_heading->setVisible(has_usable && !entries.empty());
    scroll->setVisible(shown);
    FitChain();
}
