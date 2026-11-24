// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

#include "lichii/spotlight/dialog.h"

class QAbstractButton;

class MigrationDialog : public Spotlight::Dialog {
    Q_OBJECT
public:
    MigrationDialog(QWidget* parent = nullptr);

    virtual ~MigrationDialog();

    /// The line under the title.
    void setText(const QString& text);
    /// A labelled field, such as the profile to import from.
    void addField(const QString& label, QWidget* field);
    /// A radio button or check box with a muted line that explains it.
    void addOption(QAbstractButton* option, const QString& description);
    void addBox(QWidget* box);
    /// Reject buttons sit on the left as plain text; the accept button is the accent on the right.
    QAbstractButton* addButton(const QString& text, const bool reject = false);

    QAbstractButton* clickedButton() const;

private:
    QVBoxLayout* m_boxes;
    QHBoxLayout* m_buttons;

    QAbstractButton* m_clickedButton{};
};
