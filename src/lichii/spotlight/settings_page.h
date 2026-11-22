// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <QColor>
#include <QList>
#include <QWidget>
#include "common/settings_enums.h"

class QAbstractButton;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QTabWidget;
class QVBoxLayout;

namespace Settings {
class BasicSetting;
}

namespace ConfigurationShared {
class Widget;
}

namespace Spotlight {

namespace SettingsColor {
inline QColor Green() {
    return QColor(0x7e, 0xe2, 0xa8);
}
inline QColor Gold() {
    return QColor(0xff, 0xd3, 0x7a);
}
inline QColor Blue() {
    return QColor(0x8f, 0xb5, 0xff);
}
inline QColor Lavender() {
    return QColor(0xc9, 0xb3, 0xff);
}
inline QColor BackdropTop() {
    return QColor(0x0f, 0x14, 0x20);
}
inline QColor BackdropBottom() {
    return QColor(0x17, 0x16, 0x19);
}
inline QColor Sidebar() {
    return QColor(0x20, 0x21, 0x25);
}
} // namespace SettingsColor

struct PerformancePreset {
    Settings::ResolutionSetup resolution;
    Settings::ScalingFilter filter;
    Settings::AntiAliasing anti_aliasing;
    bool frame_generation;

    bool operator==(const PerformancePreset&) const = default;
};

inline constexpr std::array PerformancePresets{
    PerformancePreset{Settings::ResolutionSetup::Res3_4X, Settings::ScalingFilter::Bilinear,
                      Settings::AntiAliasing::None, false},
    PerformancePreset{Settings::ResolutionSetup::Res1X, Settings::ScalingFilter::ScaleForce,
                      Settings::AntiAliasing::None, false},
    PerformancePreset{Settings::ResolutionSetup::Res2X, Settings::ScalingFilter::Fsr,
                      Settings::AntiAliasing::Smaa, false},
};

int PerformancePresetIndex(const PerformancePreset& values);
QString PerformancePresetName(int index);

class ToggleSwitch;

class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent, bool per_game);

    int AddCategory(const QString& title, const QString& subtitle, const QString& icon,
                    const QColor& color, QString page_title = {});
    QVBoxLayout* AddSection(int category, const QString& title = {});
    void AddBlock(int category, QWidget* widget, QString search_label, bool advanced = false);
    void AddPage(int category, QWidget* page, const QString& name, bool advanced = true);
    void SetCategory(int category);
    void ShowAdvancedPage(int category, QWidget* page);
    void SetFooter(QWidget* widget);
    void SetBanner(QWidget* banner);
    void SetGame(const QString& title, const QPixmap& icon);

    static QLabel* Text(const QString& text, int size, int weight = 500, bool secondary = false);
    static QPixmap WholeIcon(const QPixmap& icon, int size, qreal dpr);
    static QWidget* Badge(const QString& text, const QColor& fill, const QColor& ink);
    static QWidget* Card(const QString& icon = {}, int image_size = 0);
    static QLabel* AddRow(QVBoxLayout* group, const QString& title, const QString& description,
                          const QString& icon, const QColor& color, QWidget* control);
    static QAbstractButton* Manage(const QString& text = {});
    static ConfigurationShared::Widget* Editor(QWidget* page,
                                               const Settings::BasicSetting& setting);
    static QWidget* BindSetting(ConfigurationShared::Widget* source);
    static QWidget* BindFrameGeneration(QWidget* graphics);
    static QComboBox* BindCombo(QComboBox* source, const QString& placeholder = {},
                                std::function<QString(const QComboBox&, int)> closed_label = {});
    static QString ClosedSettingLabel(const QComboBox* source, u32 setting_id, int index);
    static QWidget* BindToggle(QAbstractButton* source);
    static QWidget* BindToggle(QGroupBox* source);
    static void WatchOverride(ConfigurationShared::Widget* source, QWidget* context,
                              std::function<void()> update);
    static void SetComboIndex(QComboBox* source, int index);

signals:
    void BackRequested();

private:
    struct Block {
        QWidget* widget;
        QString search_label;
        bool advanced;
    };
    struct Category {
        QString title;
        QString page_title;
        QString subtitle;
        QAbstractButton* button;
        QWidget* panel;
        QList<Block> blocks;
        QTabWidget* advanced_tabs = nullptr;
        QTabWidget* basic_tabs = nullptr;
    };

    void UpdateSearch();

    QVBoxLayout* sidebar;
    QVBoxLayout* content;
    QVBoxLayout* main_column;
    QScrollArea* scroll;
    QLineEdit* search;
    QLabel* heading;
    QLabel* subtitle;
    QLabel* empty;
    QLabel* game_icon = nullptr;
    QLabel* game_title = nullptr;
    ToggleSwitch* advanced;
    QList<Category> categories;
    int current_category = -1;
    int unfiltered_category = -1;
    bool filtering = false;
};

} // namespace Spotlight
