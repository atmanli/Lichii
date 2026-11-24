// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>

#include <QComboBox>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QUrl>
#include <QVBoxLayout>

#include "lichii/spotlight/dialog.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

namespace GraphicsPanel {

using Spotlight::PillButton;
using Spotlight::Color::Text;
using Spotlight::Color::TextSecondary;

inline QLabel* Label(const QString& text, int size, int weight = 500, QColor color = Text()) {
    auto* label = new QLabel(text);
    label->setFont(Spotlight::Font(size, weight));
    label->setTextFormat(Qt::PlainText);
    Spotlight::SetTextColor(label, color);
    return label;
}

inline QFrame* Surface(const QString& fill, const QString& stroke, int radius) {
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("graphicsPanelSurface"));
    frame->setStyleSheet(
        QStringLiteral("QFrame#graphicsPanelSurface { background: %1; border: 1px solid %2; "
                       "border-radius: %3px; }")
            .arg(fill, stroke)
            .arg(radius));
    return frame;
}

inline QLabel* Icon(const QString& name, int size, const QColor& color = Text()) {
    auto* label = new QLabel;
    label->setFixedSize(size, size);
    label->setPixmap(Spotlight::IconPixmap(name, size, color, label->devicePixelRatioF()));
    return label;
}

inline QHBoxLayout* Heading(const QString& title, const QString& subtitle = {},
                            const QString& icon = {}, QDialog* dialog = nullptr) {
    auto* row = new QHBoxLayout;
    row->setSpacing(14);
    if (!icon.isEmpty()) {
        auto* tile = Surface(QStringLiteral("rgba(255,255,255,20)"), QStringLiteral("transparent"),
                             Spotlight::Radius::Tile);
        tile->setFixedSize(44, 44);
        auto* tile_layout = new QHBoxLayout(tile);
        tile_layout->setContentsMargins(0, 0, 0, 0);
        tile_layout->addWidget(Icon(icon, 22), 0, Qt::AlignCenter);
        row->addWidget(tile, 0, Qt::AlignTop);
    }
    auto* stack = new QVBoxLayout;
    stack->setSpacing(4);
    stack->addWidget(Label(title, 22));
    if (!subtitle.isEmpty()) {
        auto* caption = Label(subtitle, 14, 500, TextSecondary());
        caption->setWordWrap(true);
        stack->addWidget(caption);
    }
    row->addLayout(stack, 1);
    if (dialog) {
        auto* close = new Spotlight::CircleButton(QStringLiteral("x"), 32);
        close->setAccessibleName(QDialog::tr("Close"));
        close->setToolTip(close->accessibleName());
        QObject::connect(close, &QAbstractButton::clicked, dialog, &QDialog::reject);
        row->addWidget(close, 0, Qt::AlignTop);
    }
    return row;
}

inline QVBoxLayout* DialogLayout(QDialog* dialog, const QString& title, int card_width,
                                 const QString& subtitle = {}, const QString& icon = {}) {
    Spotlight::AdoptDialogChrome(dialog, false);
    dialog->setAccessibleName(title);
    dialog->resize(card_width + 48, dialog->height());
    auto* root = new QVBoxLayout(dialog);
    root->setContentsMargins(10, 6, 10, 8);
    root->setSpacing(20);
    root->addLayout(Heading(title, subtitle, icon, dialog));
    return root;
}

inline QFrame* Group() {
    return Surface(QStringLiteral("rgba(255,255,255,13)"), QStringLiteral("rgba(255,255,255,20)"),
                   Spotlight::Radius::Panel + 2);
}

inline void Divider(QVBoxLayout* layout) {
    auto* divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background: rgba(255,255,255,18); border: 0;"));
    auto* inset = new QHBoxLayout;
    inset->setContentsMargins(18, 0, 18, 0);
    inset->addWidget(divider);
    layout->addLayout(inset);
}

struct SettingsRow : QHBoxLayout {
    using QHBoxLayout::QHBoxLayout;

    QLabel* title;
    QLabel* detail = nullptr;
};

inline SettingsRow* Row(QVBoxLayout* layout, const QString& title, const QString& detail = {},
                        int left = 18, const QString& icon = {}) {
    auto* row = new QWidget;
    row->setMinimumHeight(54);
    auto* columns = new SettingsRow(row);
    columns->setContentsMargins(left, 10, 16, 10);
    columns->setSpacing(14);
    if (!icon.isEmpty()) {
        columns->addWidget(Icon(icon, 16, TextSecondary()));
    }
    auto* labels = new QVBoxLayout;
    labels->setSpacing(2);
    columns->title = Label(title, 14, 600);
    labels->addWidget(columns->title);
    if (!detail.isEmpty()) {
        columns->detail = Label(detail, 12, 500, TextSecondary());
        columns->detail->setWordWrap(true);
        labels->addWidget(columns->detail);
    }
    columns->addLayout(labels, 1);
    layout->addWidget(row);
    return columns;
}

inline void StyleCombo(QComboBox* combo) {
    combo->setFont(Spotlight::Font(13, 600));
    combo->setFixedHeight(34);
    combo->setStyleSheet(
        QStringLiteral("QComboBox { background: rgba(255,255,255,20); border: 1px solid "
                       "transparent; border-radius: %1px; padding: 0 31px 0 13px; }"
                       "QComboBox:hover { background: rgba(255,255,255,31); }"
                       "QComboBox:focus { border-color: #ff6b95; }"
                       "QComboBox:disabled { color: rgba(255,255,255,115); "
                       "background: rgba(255,255,255,9); }"
                       "QComboBox::drop-down { width: 28px; border: 0; }")
            .arg(Spotlight::Radius::Field));
}

inline QString FieldStyle() {
    return QStringLiteral("background: rgba(255,255,255,15); border: 1px solid "
                          "rgba(255,255,255,31); border-radius: %1px; padding: 0 11px;")
        .arg(Spotlight::Radius::Field);
}

enum class Tone { Info, Ready, Warning, Error };

class Notice final : public QFrame {
public:
    Notice() {
        setObjectName(QStringLiteral("graphicsPanelNotice"));
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(14, 12, 14, 12);
        row->setSpacing(10);
        icon = new QLabel;
        icon->setFixedSize(16, 18);
        row->addWidget(icon, 0, Qt::AlignTop);
        message = Label({}, 13);
        message->setWordWrap(true);
        row->addWidget(message, 1);
    }

    void SetMessage(const QString& text, Tone tone = Tone::Info, const QString& details = {}) {
        QString glyph = QStringLiteral("info");
        QColor tint = Spotlight::Color::TextSecondary();
        QColor ink(255, 255, 255, 184);
        QColor fill(255, 255, 255, 10);
        QColor stroke = Spotlight::Color::StrokeSubtle();
        if (tone == Tone::Ready) {
            glyph = QStringLiteral("check-circle-fill");
            tint = QColor(0x7e, 0xe2, 0xa8);
        } else if (tone == Tone::Warning) {
            glyph = QStringLiteral("warning-circle");
            tint = QColor(0xff, 0xd3, 0x7a);
            ink = QColor(0xff, 0xe7, 0xb3);
        } else if (tone == Tone::Error) {
            glyph = QStringLiteral("warning-circle");
            tint = Spotlight::Color::AccentLight();
            ink = QColor(0xff, 0xc2, 0xd2);
        }
        if (tone != Tone::Info) {
            fill = tint;
            fill.setAlpha(tone == Tone::Error ? 31 : 20);
            stroke = tint;
            stroke.setAlpha(tone == Tone::Error ? 64 : 51);
        }
        const auto css_color = [](const QColor& c) {
            return QStringLiteral("rgba(%1,%2,%3,%4)")
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue())
                .arg(c.alpha());
        };
        if (current_tone != tone) {
            current_tone = tone;
            setStyleSheet(QStringLiteral("QFrame#graphicsPanelNotice { background: %1; "
                                         "border: 1px solid %2; border-radius: %3px; }")
                              .arg(css_color(fill), css_color(stroke))
                              .arg(Spotlight::Radius::Tile));
            icon->setPixmap(Spotlight::IconPixmap(glyph, 16, tint, devicePixelRatioF()));
            Spotlight::SetTextColor(message, ink);
        }
        message->setText(text);
        message->setToolTip(
            details.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    }

private:
    std::optional<Tone> current_tone;
    QLabel* icon;
    QLabel* message;
};

inline void DialogButtons(QVBoxLayout* root, QDialog* dialog, const QString& cancel_text,
                          const QString& accept_text) {
    auto* footer = new QHBoxLayout;
    footer->setSpacing(10);
    footer->addStretch();
    auto* cancel = new PillButton(cancel_text, {}, PillButton::Kind::Control, 34);
    footer->addWidget(cancel);
    QObject::connect(cancel, &QAbstractButton::clicked, dialog, &QDialog::reject);
    if (!accept_text.isEmpty()) {
        auto* accept = new PillButton(accept_text, {}, PillButton::Kind::Accent, 34);
        footer->addWidget(accept);
        QObject::connect(accept, &QAbstractButton::clicked, dialog, &QDialog::accept);
    }
    root->addLayout(footer);
}

inline bool ConfirmDownload(QWidget* parent, const QString& title, const QString& intro,
                            const QString& licence, const QString& cancel_text,
                            const QString& accept_text) {
    QDialog dialog(parent);
    auto* root = DialogLayout(&dialog, title, 546, {}, QStringLiteral("download-simple"));
    root->setSpacing(10);
    auto* notice = new Notice;
    notice->SetMessage(intro);
    root->addWidget(notice);
    auto* terms = new QPlainTextEdit(licence);
    terms->setReadOnly(true);
    terms->setMinimumHeight(180);
    root->addWidget(terms, 1);
    DialogButtons(root, &dialog, cancel_text, accept_text);
    dialog.resize(dialog.width(), dialog.sizeHint().height());
    return dialog.exec() == QDialog::Accepted;
}

class RuntimeProgress final : public QDialog {
public:
    explicit RuntimeProgress(QWidget* parent, const QString& title, const QString& message,
                             const QString& cancel_text = {})
        : QDialog(parent) {
        setWindowModality(Qt::WindowModal);
        auto* root = DialogLayout(this, title, 438, {}, QStringLiteral("download-simple"));
        root->setSpacing(10);
        root->addWidget(Label(message, 13, 600));
        bar = new QProgressBar;
        bar->setRange(0, cancel_text.isEmpty() ? 0 : 1000);
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        root->addWidget(bar);
        if (!cancel_text.isEmpty()) {
            auto* footer = new QHBoxLayout;
            footer->setSpacing(12);
            percent = Label({}, 13, 500, TextSecondary());
            footer->addWidget(percent, 1);
            auto* cancel = new PillButton(cancel_text, {}, PillButton::Kind::Control, 34);
            footer->addWidget(cancel);
            connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
            root->addLayout(footer);
            SetValue(0);
        }
        resize(width(), sizeHint().height());
    }

    void SetValue(int value) {
        bar->setValue(value);
        if (percent) {
            percent->setText(tr("%1%").arg(value / 10));
        }
    }

    bool WasCanceled() const {
        return canceled;
    }

    void reject() override {
        canceled = true;
        QDialog::reject();
    }

private:
    QProgressBar* bar;
    QLabel* percent = nullptr;
    bool canceled = false;
};

inline void ShowResult(QWidget* parent, const QString& title, const QString& text, Tone tone) {
    QDialog dialog(parent);
    auto* root = DialogLayout(&dialog, title, 438, {}, QStringLiteral("download-simple"));
    auto* notice = new Notice;
    notice->SetMessage(text, tone);
    root->addWidget(notice);
    DialogButtons(root, &dialog, QDialog::tr("Close"), {});
    dialog.resize(dialog.width(), dialog.sizeHint().height());
    dialog.exec();
}

class RuntimeCard {
public:
    RuntimeCard(QWidget* panel, const QString& title, const QString& subtitle,
                const QString& icon) {
        auto* outer = new QVBoxLayout(panel);
        outer->setContentsMargins(0, 0, 0, 0);
        auto* card = Surface(QStringLiteral("#15161e"), QStringLiteral("rgba(255,255,255,20)"),
                             Spotlight::Radius::Card + 2);
        outer->addWidget(card);
        auto* root = new QVBoxLayout(card);
        root->setContentsMargins(24, 24, 24, 24);
        root->setSpacing(20);
        root->addLayout(Heading(title, subtitle, icon));
        body = new QVBoxLayout;
        body->setSpacing(10);
        status = new Notice;
        body->addWidget(status);
        root->addLayout(body);
        actions = new QHBoxLayout;
        actions->setContentsMargins(0, 6, 0, 0);
        actions->setSpacing(10);
        root->addLayout(actions);
    }

    PillButton* ProjectLink(const QString& text, const QUrl& url) {
        auto* link =
            new PillButton(text, QStringLiteral("arrow-square-out"), PillButton::Kind::Control, 34);
        QObject::connect(link, &QAbstractButton::clicked, link,
                         [url] { QDesktopServices::openUrl(url); });
        body->addWidget(link, 0, Qt::AlignLeft);
        return link;
    }

    QVBoxLayout* body;
    Notice* status;
    QHBoxLayout* actions;
};

} // namespace GraphicsPanel
