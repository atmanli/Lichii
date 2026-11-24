// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <type_traits>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include "lichii/recommended_settings_dialog.h"
#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"
#include "qt_common/config/shared_translation.h"

namespace {

struct ChangeRow {
    QString name;
    QString previous;
    QString current;
};

// One row per change, worded as the settings dialog words them.
std::vector<ChangeRow> Describe(QWidget* parent,
                                const std::vector<Settings::RecommendedSettingChange>& changes) {
    const auto titles = ConfigurationShared::InitializeTranslations(parent);
    const auto enums = ConfigurationShared::ComboboxEnumeration(parent);
    const QString on = RecommendedSettingsDialog::tr("On");
    const QString off = RecommendedSettingsDialog::tr("Off");

    const auto title_of = [&](const Settings::BasicSetting& setting) {
        const auto it = titles->find(setting.Id());
        return it == titles->end() ? QString{} : it->second.first;
    };
    const auto text_of = [&](const Settings::BasicSetting& setting, const std::string& value) {
        if (setting.TypeId() == "bool") {
            return value == "true" ? on : off;
        }
        if (const auto it = enums->find(setting.EnumIndex()); it != enums->end()) {
            for (const auto& [id, text] : it->second) {
                if (std::to_string(id) == value) {
                    return text;
                }
            }
        }
        QString text = QString::fromStdString(value);
        if (setting.Specialization() & Settings::Specialization::Percentage) {
            text.append(QLatin1Char{'%'});
        }
        return text;
    };

    std::vector<ChangeRow> rows;
    for (const auto& change : changes) {
        QString title = title_of(*change.setting);
        if (title.isEmpty()) {
            // A checkbox paired with a value is named by the value's setting.
            for (const auto& [key, other] : Settings::values.linkage.by_key) {
                if (other->PairedSetting() == change.setting) {
                    title = title_of(*other);
                }
            }
        }
        if (title.isEmpty()) {
            title = QString::fromStdString(change.setting->GetLabel());
        }
        if (title.endsWith(QLatin1Char{':'})) {
            title.chop(1);
        }
        if (!change.game.empty()) {
            title = RecommendedSettingsDialog::tr("%1 \u00b7 %2", "%1 is the game, %2 the setting")
                        .arg(QString::fromStdString(change.game), title);
        }
        rows.push_back({title, text_of(*change.setting, change.previous),
                        text_of(*change.setting, change.current)});
    }
    return rows;
}

// What ApplyRecommendedSettings would change in the global values, without changing them.
std::vector<Settings::RecommendedSettingChange> Preview() {
    const auto text = [](const auto& value) -> std::string {
        using Type = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Type, bool>) {
            return value ? "true" : "false";
        } else if constexpr (std::is_enum_v<Type>) {
            return std::to_string(std::underlying_type_t<Type>(value));
        } else {
            return std::to_string(value);
        }
    };
    std::vector<Settings::RecommendedSettingChange> changes;
    Settings::ForEachRecommendedSetting(Settings::values, [&](auto& setting, const auto& value) {
        if (setting.GetValue(true) != value) {
            changes.push_back({&setting, setting.ToStringGlobal(), text(value), {}});
        }
    });
    return changes;
}

class Divider : public QWidget {
public:
    Divider() {
        setFixedHeight(1);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter(this).fillRect(rect(), Spotlight::Color::Divider());
    }
};

// The rounded panel of changes: each row names a setting and its previous and new value. More
// rows than fit scroll inside it.
class ChangeList : public QWidget {
public:
    explicit ChangeList(const std::vector<ChangeRow>& rows) {
        constexpr int RowHeight = 44;
        constexpr int VisibleRows = 6;

        auto* content = new QWidget;
        auto* column = new QVBoxLayout(content);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        const QIcon arrow = Spotlight::Icon(u"arrow-right", Spotlight::Color::TextMuted(), 14);
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (i != 0) {
                auto* divider = new Divider;
                auto* inset = new QHBoxLayout;
                inset->setContentsMargins(18, 0, 18, 0);
                inset->addWidget(divider);
                column->addLayout(inset);
            }
            auto* row = new QHBoxLayout;
            row->setContentsMargins(16, 0, 16, 0);
            row->setSpacing(10);
            auto* name = new QLabel(rows[i].name);
            name->setFont(Spotlight::Font(14, 500));
            name->setWordWrap(true);
            Spotlight::SetTextColor(name, Spotlight::Color::Text());
            auto* previous = new QLabel(rows[i].previous);
            previous->setFont(Spotlight::Font(13, 400));
            Spotlight::SetTextColor(previous, Spotlight::Color::TextMuted());
            auto* to = new QLabel;
            to->setPixmap(arrow.pixmap(14, 14));
            auto* current = new QLabel(rows[i].current);
            current->setFont(Spotlight::Font(13, 600));
            Spotlight::SetTextColor(current, Spotlight::Color::Success());
            row->addWidget(name, 1);
            row->addWidget(previous);
            row->addWidget(to);
            row->addWidget(current);
            auto* holder = new QWidget;
            holder->setMinimumHeight(RowHeight);
            holder->setLayout(row);
            column->addWidget(holder);
        }

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(1, 1, 1, 1);
        if (rows.size() <= VisibleRows) {
            layout->addWidget(content);
            return;
        }
        auto* scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        scroll->viewport()->setAutoFillBackground(false);
        content->setAutoFillBackground(false);
        scroll->setFixedHeight(VisibleRows * (RowHeight + 1) - 1 + RowHeight / 2);
        layout->addWidget(scroll);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Spotlight::Color::StrokeSubtle(), 1));
        painter.setBrush(Spotlight::Color::FillFaint());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                Spotlight::Radius::Inset, Spotlight::Radius::Inset);
    }
};

QLabel* Footer(const QString& text) {
    auto* footer = new QLabel(text);
    footer->setFont(Spotlight::Font(12, 400));
    footer->setWordWrap(true);
    Spotlight::SetTextColor(footer, Spotlight::Color::TextSecondary());
    return footer;
}

// A card with the changes, a footer and the buttons; returns the button the user clicked.
QAbstractButton* ShowChanges(Spotlight::Dialog& dialog, QStringView icon, const QColor& tint,
                             const QString& subtitle,
                             const std::vector<Settings::RecommendedSettingChange>& changes,
                             const QString& footer, QDialogButtonBox* buttons) {
    dialog.SetIcon(icon, tint);
    dialog.SetSubtitle(subtitle);
    dialog.SetCardWidth(560);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    if (!changes.empty()) {
        layout->addWidget(new ChangeList(Describe(&dialog, changes)));
    }
    layout->addWidget(Footer(footer));
    buttons->setContentsMargins(0, 6, 0, 0);
    layout->addWidget(buttons);
    for (auto* button : buttons->buttons()) {
        if (auto* push = qobject_cast<QPushButton*>(button); push && push->isDefault()) {
            push->setFocus();
        }
    }

    QAbstractButton* clicked = nullptr;
    QObject::connect(buttons, &QDialogButtonBox::clicked, &dialog, [&](QAbstractButton* button) {
        clicked = button;
        dialog.done(buttons->buttonRole(button) == QDialogButtonBox::AcceptRole
                        ? QDialog::Accepted
                        : QDialog::Rejected);
    });
    dialog.exec();
    return clicked;
}

} // namespace

bool RecommendedSettingsDialog::Ask(QWidget* parent) {
    Spotlight::Dialog dialog(parent);
    dialog.setWindowTitle(tr("Recommended settings"));
    auto* buttons = new QDialogButtonBox;
    auto* const use = buttons->addButton(tr("Use recommended"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Not now"), QDialogButtonBox::RejectRole);
    use->setDefault(true);
    return ShowChanges(dialog, u"magic-wand", Spotlight::Color::Purple(),
                       tr("They improve performance for most games. You can go back any time."),
                       Preview(),
                       tr("Games with their own value for one of these settings use the "
                          "recommended value too; other per-game settings do not change. Your "
                          "previous settings are saved: Help \u203a Restore My Previous Settings "
                          "brings them back."),
                       buttons) == use;
}

void RecommendedSettingsDialog::ShowApplied(
    QWidget* parent, const std::vector<Settings::RecommendedSettingChange>& changes) {
    if (changes.empty()) {
        QMessageBox::information(
            parent, tr("Recommended settings"),
            tr("Your settings already match the recommended settings. Nothing changed."));
        return;
    }
    Spotlight::Dialog dialog(parent);
    dialog.setWindowTitle(tr("Recommended settings"));
    ShowChanges(dialog, u"check-circle", Spotlight::Color::Success(),
                tr("Lichii now uses the recommended settings, in every game."), changes,
                tr("To go back, use Help \u203a Restore My Previous Settings."),
                new QDialogButtonBox(QDialogButtonBox::Ok));
}

void RecommendedSettingsDialog::ShowRestored(
    QWidget* parent, const std::vector<Settings::RecommendedSettingChange>& changes) {
    if (changes.empty()) {
        QMessageBox::information(
            parent, tr("Previous settings"),
            tr("Your settings already match your previous settings. Nothing changed."));
        return;
    }
    Spotlight::Dialog dialog(parent);
    dialog.setWindowTitle(tr("Previous settings"));
    ShowChanges(dialog, u"arrow-counter-clockwise", Spotlight::Color::Info(),
                tr("Lichii uses your previous settings again."), changes,
                tr("To use the recommended settings again, use Help \u203a Apply Recommended "
                   "Settings."),
                new QDialogButtonBox(QDialogButtonBox::Ok));
}
