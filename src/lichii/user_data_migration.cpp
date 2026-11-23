// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTranslator>
#include "common/fs/path_util.h"
#include "common/fs/profile_migration.h"
#include "common/fs/symlink.h"
#include "frontend_common/profile_import.h"
#include "qt_common/qt_string_lookup.h"
#include "user_data_migration.h"
#include "lichii/migration_dialog.h"

// Needs to be included at the end due to https://bugreports.qt.io/browse/QTBUG-73263
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QEventLoop>
#include <QGuiApplication>
#include <QProgressDialog>
#include <QRadioButton>
#include <QThread>

UserDataMigrator::UserDataMigrator(QMainWindow* main_window) {
    // NOTE: Logging is not initialized yet, do not produce logs here.

    if (FrontendCommon::NeedsLegacyProfileImport()) {
        namespace Migration = Common::FS::ProfileMigration;
        const auto state_dir = FrontendCommon::ProfileImportStateDirectory();
        if (Migration::GetState(state_dir) == Migration::State::Pending) {
            const auto answer = QMessageBox::question(
                main_window, QObject::tr("Recover unfinished import"),
                QObject::tr("An earlier import did not finish. Recover the previous profile "
                            "and retry? No source or backup data will be deleted."),
                QMessageBox::Retry | QMessageBox::Close, QMessageBox::Retry);
            if (answer != QMessageBox::Retry) {
                throw std::runtime_error(
                    "Import recovery was postponed. Relaunch Lichii to retry.");
            }
            Migration::Recover(state_dir);
        }
        ShowMigrationPrompt(main_window);
    }
}

void UserDataMigrator::ShowMigrationPrompt(QMainWindow* main_window) {
    namespace fs = std::filesystem;
    using namespace QtCommon::StringLookup;

    MigrationDialog migration_prompt;
    migration_prompt.setWindowTitle(QObject::tr("Bring your data along"));

    std::vector<Emulator> found{};

    for (const Emulator& emu : legacy_emus)
        if (fs::is_directory(Common::FS::GetLegacyPath(emu.e_user_dir)))
            found.emplace_back(emu);

    if (found.empty()) {
        return;
    }

    migration_prompt.setText(QObject::tr("We found another emulator on this PC. Import its saves, "
                                         "games and settings?"));

    auto* source = new QComboBox(&migration_prompt);
    for (const Emulator& emu : found) {
        source->addItem(QStringLiteral("%1 \u00b7 %2")
                            .arg(emu.name(), QString::fromStdString(
                                                 Common::FS::GetLegacyPathString(emu.e_user_dir))));
    }
    migration_prompt.addField(QObject::tr("From"), source);

    // mutually exclusive
    QButtonGroup* group = new QButtonGroup(&migration_prompt);
    const auto radio = [&](const QString& text, const QString& description, StringKey tooltip,
                           bool checked, MigrationWorker::MigrationStrategy strategy) {
        auto* button = new QRadioButton(text, &migration_prompt);
        button->setToolTip(Lookup(tooltip));
        button->setChecked(checked);
        group->addButton(button, static_cast<int>(strategy));
        migration_prompt.addOption(button, description);
        return button;
    };
    auto* keep_old = radio(QObject::tr("Copy"),
                           QObject::tr("Keeps the old folder as it is. Uses the most space."),
                           MigrationTooltipKeepOld, true, MigrationWorker::MigrationStrategy::Copy);
    auto* clear_old =
        radio(QObject::tr("Move"), QObject::tr("Moves the data and keeps a backup."),
              MigrationTooltipClearOld, false, MigrationWorker::MigrationStrategy::Move);
    auto* link_old =
        radio(QObject::tr("Share the folder"), QObject::tr("Both apps use the same folder."),
              MigrationTooltipLinkOld, false, MigrationWorker::MigrationStrategy::Link);

    auto* clear_shaders = new QCheckBox(
        QObject::tr("Clear the shader cache (recommended when coming from Yuzu or Suyu)"),
        &migration_prompt);
    clear_shaders->setToolTip(Lookup(MigrationTooltipClearShader));
    migration_prompt.addBox(clear_shaders);

    const auto update_cache_default = [&] {
        const Emulator& emu = found.at(source->currentIndex());
        clear_shaders->setChecked(emu.e_user_dir != Common::FS::LegacyEdenDir);
        clear_shaders->setEnabled(!link_old->isChecked());
        // A profile folder that is a link (e.g. a junction to another drive) can be copied or
        // linked, but Move would rename the real folder behind the link, so it is not offered.
        bool linked = false;
        for (const auto dir : {emu.e_user_dir, emu.e_config_dir, emu.e_cache_dir}) {
            linked = linked || Common::FS::IsSymlink(Common::FS::GetLegacyPath(dir));
        }
        if (linked && clear_old->isChecked()) {
            keep_old->setChecked(true);
        }
        clear_old->setEnabled(!linked);
    };
    QObject::connect(source, &QComboBox::currentIndexChanged, &migration_prompt,
                     update_cache_default);
    QObject::connect(link_old, &QRadioButton::toggled, &migration_prompt,
                     [=](bool linked) { clear_shaders->setEnabled(!linked); });
    update_cache_default();
    auto* fresh = migration_prompt.addButton(QObject::tr("Start fresh"), true);
    migration_prompt.addButton(QObject::tr("Import"));

    migration_prompt.exec();

    QAbstractButton* button = migration_prompt.clickedButton();

    if (button == nullptr) {
        throw std::runtime_error("Import was postponed. Relaunch Lichii to choose a profile.");
    }
    if (button == fresh) {
        Common::FS::ProfileMigration::MarkFresh(FrontendCommon::ProfileImportStateDirectory());
        return ShowMigrationCancelledMessage(main_window);
    }

    MigrationWorker::MigrationStrategy strategy =
        static_cast<MigrationWorker::MigrationStrategy>(group->checkedId());

    selected_emu = found.at(source->currentIndex());

    MigrateUserData(main_window, clear_shaders->isEnabled() && clear_shaders->isChecked(),
                    strategy);
    if (!migrated) {
        throw std::runtime_error("Import failed. Relaunch Lichii to retry or recover.");
    }
}

void UserDataMigrator::ShowMigrationCancelledMessage(QMainWindow* main_window) {
    QMessageBox::information(main_window, QObject::tr("Migration"),
                             QObject::tr("Lichii will use its own profile. Your old emulator "
                                         "data has not been changed."),
                             QMessageBox::Ok);
}

void UserDataMigrator::MigrateUserData(QMainWindow* main_window, const bool clear_shader_cache,
                                       const MigrationWorker::MigrationStrategy strategy) {
    // Create a dialog to let the user know it's migrating
    QProgressDialog* progress = new QProgressDialog(main_window);
    progress->setWindowTitle(QObject::tr("Migrating"));
    progress->setLabelText(QObject::tr("Migrating, this may take a while..."));
    progress->setRange(0, 0);
    progress->setCancelButton(nullptr);
    progress->setWindowFlag(Qt::WindowCloseButtonHint, false);
    progress->setWindowModality(Qt::WindowModality::ApplicationModal);

    QThread* thread = new QThread(main_window);
    MigrationWorker* worker = new MigrationWorker(selected_emu, clear_shader_cache, strategy);
    worker->moveToThread(thread);

    thread->connect(thread, &QThread::started, worker, &MigrationWorker::process);

    // The worker can't be stopped, so wait for it rather than for the dialog, and show the dialog
    // again if Esc closes it before the worker is done.
    QEventLoop loop;
    bool worker_done = false;
    QString failure;
    QObject::connect(progress, &QDialog::rejected, progress, [progress, &worker_done] {
        if (!worker_done) {
            progress->show();
        }
    });

    thread->connect(
        worker, &MigrationWorker::finished, progress,
        [=, this, &loop, &worker_done](const QString& success_text, const std::string& path) {
            worker_done = true;
            progress->close();
            QMessageBox::information(main_window, QObject::tr("Migration"), success_text,
                                     QMessageBox::Ok);

            migrated = true;
            thread->quit();
            loop.quit();
        });

    thread->connect(worker, &MigrationWorker::finished, worker, &QObject::deleteLater);
    thread->connect(worker, &MigrationWorker::error, progress,
                    [=, &loop, &worker_done, &failure](const QString& message) {
                        worker_done = true;
                        failure = message;
                        progress->close();
                        thread->quit();
                        loop.quit();
                    });
    thread->connect(worker, &MigrationWorker::error, worker, &QObject::deleteLater);
    thread->connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
    progress->show();
    loop.exec();
    thread->wait();
    progress->deleteLater();
    if (!failure.isEmpty()) {
        throw std::runtime_error(failure.toStdString());
    }
}
