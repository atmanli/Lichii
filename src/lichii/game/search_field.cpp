// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <utility>

#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QToolButton>

#include "lichii/game/search_field.h"
#include "lichii/spotlight/menu.h"
#include "lichii/spotlight/theme.h"
#include "lichii/spotlight/widgets.h"

GameListSearchField::GameListSearchField(QWidget* parent)
    : QLineEdit(parent), keycap(new Spotlight::KeyCap(QStringLiteral("Ctrl F"), this)),
      clear_button(new QToolButton(this)) {
    setFixedHeight(40);
    setMinimumWidth(145);
    setMaximumWidth(300);
    setFont(Spotlight::Font(14));
    auto colors = palette();
    colors.setColor(QPalette::Text, Spotlight::Color::Text());
    colors.setColor(QPalette::PlaceholderText, Spotlight::Color::TextSecondary());
    setPalette(colors);
    setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; "
                                 "color: white; } QToolButton { background: transparent; "
                                 "border: none; padding: 0; }"));
    keycap->setAttribute(Qt::WA_TransparentForMouseEvents);
    clear_button->setFixedSize(26, 26);
    clear_button->setFocusPolicy(Qt::NoFocus);
    clear_button->setIcon(Spotlight::Icon(u"x", Spotlight::Color::TextSecondary(), 16));
    clear_button->setIconSize({16, 16});
    clear_button->setCursor(Qt::PointingHandCursor);
    connect(clear_button, &QToolButton::clicked, this, &QLineEdit::clear);
    connect(this, &QLineEdit::textChanged, this, &GameListSearchField::UpdateAffordances);
    connect(this, &QLineEdit::returnPressed, this, &GameListSearchField::Submitted);
    RetranslateUI();
    UpdateAffordances();
}

QSize GameListSearchField::sizeHint() const {
    return {300, 40};
}

void GameListSearchField::SetShortcutLabel(const QString& label) {
    has_shortcut = !label.isEmpty();
    keycap->SetText(label);
    UpdateAffordances();
}

void GameListSearchField::UpdateAffordances() {
    keycap->adjustSize();
    keycap->move(width() - keycap->width() - 8, (height() - keycap->height()) / 2);
    clear_button->move(width() - clear_button->width() - 8,
                       (height() - clear_button->height()) / 2);
    const bool show_hint = has_shortcut && !hasFocus() && text().isEmpty() && width() >= 170;
    keycap->setVisible(show_hint);
    clear_button->setVisible(!text().isEmpty());
    setTextMargins(38, 0, (show_hint ? keycap->width() : !text().isEmpty() ? 26 : 0) + 10, 0);
    update();
}

bool GameListSearchField::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && !text().isEmpty()) {
        event->accept();
        return true;
    }
    return QLineEdit::event(event);
}

void GameListSearchField::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QLineEdit::changeEvent(event);
}

void GameListSearchField::focusInEvent(QFocusEvent* event) {
    QLineEdit::focusInEvent(event);
    UpdateAffordances();
}

void GameListSearchField::focusOutEvent(QFocusEvent* event) {
    QLineEdit::focusOutEvent(event);
    UpdateAffordances();
}

void GameListSearchField::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && !text().isEmpty()) {
        clear();
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void GameListSearchField::paintEvent(QPaintEvent* event) {
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(hasFocus() ? Spotlight::Color::Accent() : Spotlight::Color::Stroke());
        painter.setBrush(Spotlight::Color::Control());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 20, 20);
        auto ink = Spotlight::Color::Text();
        ink.setAlphaF(0.72f);
        painter.drawPixmap(
            14, 12, Spotlight::IconPixmap(u"magnifying-glass", 16, ink, devicePixelRatioF()));
    }
    QLineEdit::paintEvent(event);
}

void GameListSearchField::resizeEvent(QResizeEvent* event) {
    QLineEdit::resizeEvent(event);
    UpdateAffordances();
}

void GameListSearchField::RetranslateUI() {
    setPlaceholderText(tr("Search"));
    setAccessibleName(tr("Search games"));
    clear_button->setToolTip(tr("Clear search"));
    clear_button->setAccessibleName(tr("Clear search"));
}

void GameListSearchField::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    static constexpr std::array keys{
        std::pair{"edit-undo", QKeySequence::Undo},
        std::pair{"edit-redo", QKeySequence::Redo},
        std::pair{"edit-cut", QKeySequence::Cut},
        std::pair{"edit-copy", QKeySequence::Copy},
        std::pair{"edit-paste", QKeySequence::Paste},
        std::pair{"select-all", QKeySequence::SelectAll},
    };
    for (QAction* action : menu->actions()) {
        for (const auto& [name, key] : keys) {
            if (action->objectName() == QLatin1StringView(name)) {
                action->setText(action->text().section(QLatin1Char('\t'), 0, 0));
                Spotlight::SetHotkeyHint(action, QKeySequence(key), {});
            }
        }
    }
    menu->popup(event->globalPos());
}
