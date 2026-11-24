// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>
#include "lichii/spotlight/dialog.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

class InstallDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit InstallDialog(QWidget* parent, const QStringList& files);
    ~InstallDialog() override;

    [[nodiscard]] QStringList GetFiles() const;
    [[nodiscard]] int GetMinimumWidth() const;

private:
    struct Entry {
        QString path;
        qint64 size;
        QCheckBox* check;
    };

    void AddFile(const QString& path);
    void AddMoreFiles();
    void UpdateSummary();

    std::vector<Entry> entries;
    QWidget* file_list;
    QVBoxLayout* rows;
    QLabel* summary;
    QPushButton* install;
};
