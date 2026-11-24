// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "lichii/spotlight/dialog.h"

class QLineEdit;

namespace Ui {
class ConfigureTas;
}

class ConfigureTasDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit ConfigureTasDialog(QWidget* parent);
    ~ConfigureTasDialog() override;

    /// Save all button configurations to settings file
    void ApplyConfiguration();

private:
    enum class DirectoryTarget {
        TAS,
    };

    void LoadConfiguration();

    void SetDirectory(DirectoryTarget target, QLineEdit* edit);

    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    void HandleApplyButtonClicked();

    std::unique_ptr<Ui::ConfigureTas> ui;
};
