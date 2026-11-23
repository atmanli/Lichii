// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <optional>
#include "frontend_common/data_manager.h"
#include "lichii/spotlight/dialog.h"
#include "qt_common/qt_string_lookup.h"

class QLabel;
class DataWidget;

class DataDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    explicit DataDialog(QWidget* parent = nullptr);
    ~DataDialog();

private:
    void UpdateTotal();

    std::array<DataWidget*, 5> rows{};
};

/// One kind of Lichii data in the storage card: its size, a bar of its share and the buttons
/// that open, export, import and clear it.
class DataWidget : public QWidget {
    Q_OBJECT
public:
    explicit DataWidget(FrontendCommon::DataManager::DataDir data_dir,
                        QtCommon::StringLookup::StringKey tooltip, const QString& exportName,
                        const QString& title, const QString& description, QStringView icon,
                        const QColor& tint, bool divider, QWidget* parent = nullptr);

    /// The size of the data, or nothing while it is being measured.
    [[nodiscard]] std::optional<u64> Size() const;
    /// The total all rows' bars share.
    void SetTotal(u64 total);

signals:
    void SizeChanged();

public slots:
    void clear();
    void open();
    void upload();
    void download();

    void scan();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    FrontendCommon::DataManager::DataDir m_dir;
    const QString m_exportName;
    QColor m_tint;
    bool m_divider;
    QLabel* m_size;
    QWidget* m_bar;
    std::optional<u64> m_bytes;
    u64 m_total = 0;

    std::optional<std::string> selectProfile();
};
