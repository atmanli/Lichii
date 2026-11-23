// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QWidget>

#include "common/common_types.h"
#include "core/file_sys/nup_writer.h"
#include "lichii/spotlight/dialog.h"

class IconSearch;
class QAbstractButton;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;
class QRadioButton;
class QVBoxLayout;

namespace Spotlight {
class PillButton;
} // namespace Spotlight

// Square preview that accepts an image dropped on it or chosen with a click.
class IconDropZone : public QWidget {
    Q_OBJECT
public:
    explicit IconDropZone(int size, QWidget* parent = nullptr);

    void SetIcon(const QPixmap& icon);

signals:
    void FileDropped(const QString& path);
    void Clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QPixmap icon;
    bool dragging = false;
    bool hovered = false;
};

// The square part of a picture to keep, chosen by the user; nothing when cancelled.
std::optional<QRect> ChooseIconCrop(const QImage& image, QWidget* parent);
// The square of a picture as a size x size JPEG, the way the console stores icons.
QByteArray EncodeArchiveIcon(const QImage& source, const QRect& crop, int size = 256);

// The NUP and NX dialogs: Spotlight's dialog card with an icon tile, a title and a subtitle over
// the dimmed window, the dialog's sections and Cancel and action pills.
class ArchiveCardDialog : public Spotlight::Dialog {
    Q_OBJECT

public:
    ArchiveCardDialog(const QString& title, int width, QWidget* parent);

protected:
    // A Phosphor icon in the header tile and the line under the title.
    void SetHeader(const QString& icon, const QString& subtitle);
    // A rounded section with a small title; returns its layout.
    QVBoxLayout* AddSection(const QString& title);
    // A muted explanation line.
    static QLabel* Hint(const QString& text);
    // Cancel and the action pill at the end of the card, after leading when given.
    Spotlight::PillButton* AddButtons(const QString& action, const QString& action_icon,
                                      QWidget* leading = nullptr);
    void keyPressEvent(QKeyEvent* event) override;

    QWidget* card;
    QVBoxLayout* body;
    Spotlight::PillButton* action_button = nullptr;
};

// The icon beside the title of a result or question.
enum class ArchiveTone { None, Success, Error, Warning };

// A rounded card with a message, optional checkboxes and a row of pills, for the questions and
// results of archive tools. exec() returns Accepted for an accent pill; Clicked() names the pill.
class ArchiveMessageDialog : public ArchiveCardDialog {
    Q_OBJECT

public:
    ArchiveMessageDialog(const QString& title, const QString& text, QWidget* parent,
                         ArchiveTone tone = ArchiveTone::None);
    QCheckBox* AddCheckBox(const QString& text, bool checked);
    QAbstractButton* AddButton(const QString& text, const QString& icon, bool accent);
    QAbstractButton* Clicked() const {
        return clicked;
    }

private:
    QVBoxLayout* checks = nullptr;
    QHBoxLayout* buttons = nullptr;
    QAbstractButton* clicked = nullptr;
};

// A rounded card with a message and a pill that closes it, for the results and errors of archive
// tools.
void ArchiveNotice(QWidget* parent, const QString& title, const QString& text,
                   ArchiveTone tone = ArchiveTone::None);

// Something that goes into a new archive: the game, an update or DLC.
struct ArchivePackage {
    QString label;
    QString detail;
    u64 bytes{};
    // Whether the user may leave it out.
    bool optional{};
};

// Convert to a single file: a card per format, what goes into the archive, where it goes and
// whether there is room for it. Picking the other format closes it with OtherFormat.
class ArchiveConvertDialog : public ArchiveCardDialog {
    Q_OBJECT

public:
    enum class Format { Nup, Nx };
    static constexpr int OtherFormat = 2;

    std::filesystem::path Archive() const;
    bool DeleteSources() const;
    std::vector<bool> IncludedPackages() const;

protected:
    ArchiveConvertDialog(Format format, bool offer_other, QWidget* parent);
    // Adds the packages, the From and To paths and the buttons under what the format shows.
    void AddFiles(const QString& packages_title, std::vector<ArchivePackage> packages,
                  const QString& source, std::filesystem::path archive, bool offer_delete,
                  u64 extra_bytes, const QString& action);

private:
    void ChooseTarget();
    void UpdateSpace();

    Format format;
    std::filesystem::path archive;
    std::vector<ArchivePackage> packages;
    // Bytes needed besides the packages.
    u64 extra_bytes{};
    std::vector<QCheckBox*> package_checks;
    QWidget* target = nullptr;
    QLabel* space_label = nullptr;
    QCheckBox* delete_sources = nullptr;
};

// What Convert to NUP shows: the source, the packages it holds and what the NUP gets.
struct NupConvertSummary {
    QString source_name;
    std::filesystem::path archive;
    FileSys::Nup::Metadata detected;
    using Package = ArchivePackage;
    std::vector<Package> packages;
    // Bytes needed besides the packages, such as the overlap of the temporary dump.
    u64 extra_bytes{};
    // A loose dump can be deleted while it is packed.
    bool offer_low_storage{};
    // The NX card can be picked instead.
    bool offer_nx{};
};

class NupConvertDialog : public ArchiveConvertDialog {
    Q_OBJECT

public:
    explicit NupConvertDialog(NupConvertSummary summary, QWidget* parent = nullptr);
    ~NupConvertDialog() override;

    // Only what differs from the detected metadata is an override.
    FileSys::Nup::Metadata Metadata() const;
    QString DisplayName() const;
    QPixmap CurrentIcon() const;

private:
    void ChooseIcon();
    void SetIconFromFile(const QString& path);
    void Recrop();
    void ResetIcon();
    void UpdateIconPreview();

    NupConvertSummary summary;
    QImage icon_source;
    QRect icon_crop;
    QByteArray custom_icon;
    QPixmap current_icon;
    IconDropZone* icon_preview;
    QAbstractButton* recrop;
    QLabel* icon_caption;
    QAbstractButton* reset_icon;
    QLineEdit* name_edit;
    QLineEdit* publisher_edit;
    QRadioButton* auto_version;
    QRadioButton* manual_version;
    QLineEdit* version_edit;
};

// Convert to NX: the related updates and DLC to pack too and where the archive goes.
class NxConvertDialog : public ArchiveConvertDialog {
    Q_OBJECT

public:
    NxConvertDialog(const QString& source, std::vector<ArchivePackage> related,
                    std::filesystem::path archive, bool offer_nup, QWidget* parent);
};

// Edits the title, publisher, version and icon stored in a NUP or NX archive.
class ArchiveEditDialog : public ArchiveCardDialog {
    Q_OBJECT
public:
    enum class Format { Nup, Nx };

    // original_icon: the icon the archive was made with, offered for restoring (NX).
    ArchiveEditDialog(Format format, const std::filesystem::path& archive,
                      FileSys::Nup::Metadata current, std::vector<u8> original_icon = {},
                      QWidget* parent = nullptr);
    ~ArchiveEditDialog() override;

    // nullopt fields keep what is stored. An NX archive takes no version or localized names.
    FileSys::Nup::MetadataEdit Edit() const;
    QString DisplayName() const;
    QPixmap CurrentIcon() const;

private:
    void ChooseIcon();
    void UseImage(const QImage& image, bool crop);
    void SetIconFromFile(const QString& path);
    void FindIcon();
    void OnIconFound(const QByteArray& png, const QString& resolved_title);
    void ClearIcon();
    void RestoreIcon();
    void Recrop();
    void EncodeIcon();
    void UpdateIconPreview();

    Format format;
    FileSys::Nup::Metadata current;
    std::vector<u8> original_icon;
    QImage icon_source;
    QRect icon_crop;
    QByteArray custom_icon;
    bool remove_icon = false;
    QPixmap current_icon;

    IconDropZone* icon_preview;
    QAbstractButton* recrop;
    QLabel* icon_hint;
    QAbstractButton* find_icon;
    QAbstractButton* restore_icon;
    QAbstractButton* clear_icon;
    IconSearch* icon_search;
    QLineEdit* name_edit;
    QLineEdit* publisher_edit;
    QLineEdit* version_edit = nullptr;
    // 256, 512 or 1024 pixels per side (NX).
    int icon_size_index = 0;
    QWidget* localized_panel = nullptr;
    std::vector<std::pair<std::string, QLineEdit*>> localized_edits;
};

// Shown while a conversion or an edit runs: the game, the step, a bar, how much is done and a
// rough time remaining.
class NupProgressDialog : public ArchiveCardDialog {
    Q_OBJECT
public:
    NupProgressDialog(const QString& operation, const QString& title, const QPixmap& icon,
                      QWidget* parent = nullptr);

    // A step of a job with several, such as "Step 1 of 2 · Reading the game". Restarts the
    // time estimate.
    void SetStep(const QString& step);
    void SetProgress(const FileSys::Nup::PackProgress& progress);
    void SetCancelling();
    // Closes the dialog without treating it as a cancel request.
    void Finish();

signals:
    void CancelRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void reject() override;

private:
    QLabel* status;
    QLabel* percent;
    QLabel* details;
    QLabel* remaining;
    QProgressBar* bar;
    Spotlight::PillButton* cancel;
    QString step;
    QElapsedTimer timer;
    bool cancelling = false;
    bool finished = false;
};

// Updates and DLC in an NX archive: a row per entry with a remove button on each update and DLC,
// and buttons that add more. A choice closes it as accepted; Chosen() names it.
class ArchiveContentsDialog : public ArchiveCardDialog {
    Q_OBJECT

public:
    enum class Kind { Base, Update, Dlc };
    struct Entry {
        QString title;
        QString detail;
        Kind kind;
    };
    enum class Choice { None, Remove, AddFolder, AddFiles };

    ArchiveContentsDialog(const QString& archive_name, u64 archive_bytes, const QPixmap& icon,
                          const std::vector<Entry>& entries, bool offer_files, QWidget* parent);
    Choice Chosen() const {
        return choice;
    }
    // The entry Remove was picked for.
    size_t Removed() const {
        return removed;
    }

private:
    Choice choice = Choice::None;
    size_t removed = 0;
};

// Extract to a folder: the game, where its loose files go and whether there is room.
class NupExtractDialog : public ArchiveCardDialog {
    Q_OBJECT

public:
    NupExtractDialog(const std::filesystem::path& archive, const FileSys::Nup::Metadata& probed,
                     QWidget* parent);
    std::filesystem::path Target() const;

private:
    void ChooseTarget();
    void UpdateSpace();

    std::filesystem::path archive;
    std::filesystem::path target;
    u64 needed{};
    QWidget* target_row;
    QLabel* space_label;
};
