// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <functional>
#include <QAbstractButton>
#include <QColor>
#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>
#include <QWidget>
#include "common/settings_enums.h"

class QComboBox;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QLayout;
class QLineEdit;
class QScrollArea;
class QSlider;
class QVBoxLayout;

namespace Settings {
class BasicSetting;
}

namespace VkDeviceInfo {
class Record;
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
inline QColor SidebarBottom() {
    return QColor(0x1e, 0x1b, 0x1f);
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
/// The preset tier Auto picks for a GPU: 0 low-end, 1 mid-range, 2 high-end.
int SuggestedPresetTier(const QString& gpu_name, bool fsr4_capable);
/// The preset Auto picks for a GPU: 0 Low, 1 Mid, 2 High. Without a GPU it is Low.
int SuggestedPresetTier(const VkDeviceInfo::Record* device);
QString PerformancePresetName(int index);

/// A performance preset choice of Settings > General: Auto, Low-end, Mid-range or High-end.
class PresetCard final : public QAbstractButton {
public:
    PresetCard(const QString& title, QString description, QWidget* parent = nullptr);
    void SetSuggestion(QString device, QString tier);
    /// A card for another page that shows this card's state and clicks it.
    PresetCard* Mirror(QWidget* parent);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString description;
    QString suggestion;
    QString suggested_tier;
};

class ToggleSwitch;

/// One section of a category's Advanced view. Each item is a setting key or a source widget's
/// object name; a leading '>' nests the row under the one before it, and "key|Title|Description"
/// replaces the row's wording.
struct AdvancedSection {
    QString title;
    QString hint;
    int column = 0;
    QStringList items;
    /// The chip that scrolls to this section; sections sharing a chip label share one chip.
    QString chip;
};

/// A rounded message line with an icon, tinted by its tone. An empty message hides it.
class StatusBanner final : public QWidget {
public:
    enum class Tone { Info, Success, Warning, Error };
    StatusBanner();
    void Set(const QString& message, Tone tone);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QColor Ink() const;

    QLabel* icon;
    QLabel* text;
    Tone tone = Tone::Info;
};

class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent, bool per_game);

    /// An advanced-only category is listed in the sidebar only while Advanced is on.
    int AddCategory(const QString& title, const QString& subtitle, const QString& icon,
                    const QColor& color, QString page_title = {}, bool advanced_only = false);
    /// The subtitle shown instead of the category's own while Advanced is on.
    void SetAdvancedSubtitle(int category, const QString& text);
    /// A page without a sidebar entry, reached from a row of its parent category.
    /// A wide page hides the sidebar while it is shown.
    int AddSubPage(int parent, const QString& title, const QString& subtitle, bool wide = false);
    /// Builds the category's rows the first time it is shown or searched.
    void SetBuilder(int category, std::function<void()> build);
    /// `action` sits at the right of the section's title, such as an "Add folder" button.
    /// Everyday sections given a column of 0 or 1 share a two-column layout.
    QVBoxLayout* AddSection(int category, const QString& title = {}, bool advanced = false,
                            QWidget* action = nullptr, int column = -1);
    /// Adds the category's hand-built Advanced rows the first time its Advanced view is built.
    void SetAdvancedBuilder(int category, std::function<void()> build);
    void AddBlock(int category, QWidget* widget, QString search_label, bool advanced = false);
    /// Shows an existing settings page as Spotlight rows, built when first needed. The page stays
    /// hidden and keeps its own load and apply logic; the rows edit its controls.
    void AddPage(int category, QWidget* page, const QString& name, bool advanced = true);
    /// Lays the category's Advanced view out in these sections. Rows of its advanced pages that
    /// match no item and are not in `removed` collect under "More".
    void SetAdvancedSections(int category, QList<AdvancedSection> sections,
                             QStringList removed = {});
    /// Adds a hand-built row to the Advanced section that lists `key`.
    QLabel* AddAdvancedRow(int category, const QString& key, const QString& title,
                           const QString& description, QWidget* control);
    void SetCategory(int category);
    void ShowAdvancedPage(int category, QWidget* page);
    void SetFooter(QWidget* widget);
    /// The banner shows only on the given categories and their subpages.
    void SetBanner(QWidget* widget, QList<int> shown_in);
    void SetGame(const QString& title, const QPixmap& icon);

    static QLabel* Text(const QString& text, int size, int weight = 500, bool secondary = false);
    /// A muted line with an info icon, as under a category's Everyday rows.
    static QWidget* Note(const QString& text);
    static QPixmap WholeIcon(const QPixmap& icon, int size, qreal dpr);
    static QPixmap WholeIcon(const QPixmap& icon, QSize size, qreal dpr);
    static QWidget* Badge(const QString& text, const QColor& fill, const QColor& ink);
    static QWidget* Card(const QString& icon = {}, int image_size = 0);
    static QLabel* AddRow(QVBoxLayout* group, const QString& title, const QString& description,
                          const QString& icon, const QColor& color, QWidget* control,
                          const QString& tooltip = {}, const QPixmap& image = {});
    static QAbstractButton* Manage(const QString& text = {});
    /// Indents a row AddRow made under the one before it, with an elbow.
    static void NestRow(QLabel* detail);
    static ConfigurationShared::Widget* Editor(QWidget* page,
                                               const Settings::BasicSetting& setting);
    static QWidget* BindSetting(ConfigurationShared::Widget* source);
    static QWidget* BindFrameGeneration(QWidget* graphics);
    static QComboBox* BindCombo(QComboBox* source, const QString& placeholder = {},
                                std::function<QString(const QComboBox&, int)> closed_label = {});
    static QString ClosedSettingLabel(const QComboBox* source, u32 setting_id, int index);
    static QWidget* BindToggle(QAbstractButton* source);
    static QWidget* BindToggle(QGroupBox* source);
    static QWidget* BindSpin(QWidget* source);
    static QWidget* BindSlider(QSlider* source);
    static QWidget* BindLineEdit(QLineEdit* source);
    static QAbstractButton* BindButton(QAbstractButton* source, const QString& text = {});
    static void WatchOverride(ConfigurationShared::Widget* source, QWidget* context,
                              std::function<void()> update);
    static void SetComboIndex(QComboBox* source, int index);
    /// Puts a "Changed" badge before a per-game control and a reset button after it. Both show
    /// while `is_custom` returns true; call RefreshOverride on the result when it may change.
    static QWidget* WithOverride(QWidget* control, std::function<bool()> is_custom,
                                 std::function<void()> reset);
    static void RefreshOverride(QWidget* combined);

signals:
    void BackRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Block {
        QWidget* widget;
        QString search_label;
        bool advanced;
    };
    struct Page {
        QWidget* source;
        QString name;
        bool advanced;
        QWidget* anchor = nullptr;
    };
    struct Slot {
        int section = 0;
        int order = 0;
        bool nested = false;
        QString title;
        QString description;
    };
    struct Category {
        QString title;
        QString page_title;
        QString subtitle;
        QString icon;
        QColor color;
        QAbstractButton* button = nullptr;
        QWidget* panel = nullptr;
        int parent = -1;
        bool wide = false;
        int basic_count = 0;
        QList<Block> blocks;
        QList<Page> pages;
        std::function<void()> build;
        std::function<void()> build_advanced;
        bool basic_built = false;
        bool advanced_built = false;
        bool advanced_only = false;
        QString advanced_subtitle;
        QHash<QString, QWidget*> chip_groups;
        int advanced_pages = 0;
        QList<AdvancedSection> sections;
        QHash<QString, Slot> slot_map;
        QSet<QString> removed;
        QList<QWidget*> section_widgets;
        QList<QVBoxLayout*> section_rows;
        QWidget* chips = nullptr;
        QWidget* columns = nullptr;
        QWidget* basic_columns = nullptr;
        QLabel* search_heading = nullptr;
        int more_section = -1;
        int advanced_rows = 0;
    };

    void Build(int category, bool with_advanced);
    void BuildPage(int category, Page& page);
    void BuildSections(int category);
    QWidget* AdvancedArea(int category);
    QVBoxLayout* NewSection(int category, const QString& title, const QString& hint, int column,
                            const QString& chip);
    QLabel* PlaceRow(int category, const Slot& slot, const QString& title,
                     const QString& description, const QString& tooltip, QWidget* control);
    bool HasAdvanced(int category) const;
    QString SearchContext(const Category& entry, const QWidget* row, bool advanced) const;
    bool ShowsAdvanced(int category) const;
    void UpdateMode();
    bool RefreshCategory(int category);
    void UpdateAdvanced();
    void UpdateSearch();

    QWidget* navigation;
    QWidget* edges = nullptr;
    QWidget* banner = nullptr;
    QList<int> banner_categories;
    QVBoxLayout* sidebar;
    QVBoxLayout* content;
    QHBoxLayout* header_row;
    QVBoxLayout* main_column;
    QScrollArea* scroll;
    QLineEdit* search;
    QWidget* parent_link;
    QLabel* parent_name;
    QLabel* heading;
    QLabel* subtitle;
    QLabel* empty;
    QLabel* game_icon = nullptr;
    QLabel* game_title = nullptr;
    QAbstractButton* everyday_mode;
    QAbstractButton* advanced_mode;
    QWidget* mode_switch;
    bool show_advanced = false;
    QList<Category> categories;
    int current_category = -1;
    int unfiltered_category = -1;
    bool filtering = false;
    bool building = false;
};

} // namespace Spotlight
