// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLineEdit>

class QToolButton;

namespace Spotlight {
class KeyCap;
}

class GameListSearchField final : public QLineEdit {
    Q_OBJECT

public:
    explicit GameListSearchField(QWidget* parent = nullptr);
    QSize sizeHint() const override;
    void SetShortcutLabel(const QString& label);

signals:
    void Submitted();

protected:
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void UpdateAffordances();
    void RetranslateUI();

    Spotlight::KeyCap* keycap;
    QToolButton* clear_button;
    bool has_shortcut = true;
};
