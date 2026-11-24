// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QDialog>
#include <QStringView>

class QMessageBox;

namespace Spotlight {

/// A dialog in Spotlight's floating card (Paper: dialog card, radius 22, padding 24) with a soft
/// shadow over a dimmed window, a header (an optional icon tile, the window title, an optional
/// subtitle and a close button) and the dialog's own layout below it; the style sheet turns its
/// QDialogButtonBox into pill buttons. Derive from it instead of QDialog to adopt it: setupUi(this)
/// and layouts set on the dialog work unchanged and sit inside the card. The card moves with the
/// pointer anywhere outside its controls.
class Dialog : public QDialog {
    Q_OBJECT
public:
    explicit Dialog(QWidget* parent = nullptr, Qt::WindowFlags flags = {});

    /// Content chrome draws only the card: the contents bring their own header and buttons.
    enum class Chrome { Content };
    Dialog(QWidget* parent, Chrome chrome);

    /// A muted line under the title, such as what the dialog does. Empty hides it.
    void SetSubtitle(const QString& subtitle);

    /// A Phosphor icon, such as "download-simple", in a tile before the title, drawn in `tint`.
    /// An empty name hides the tile.
    void SetIcon(QStringView name, const QColor& tint);

    /// Fixes the width of the card, shadow excluded, to the width Paper draws it, such as 600.
    void SetCardWidth(int width);

    /// Hides the title, for a dialog whose contents start with their own heading. The close button
    /// stays.
    void SetTitleVisible(bool visible);

    /// Hides the close button, for a card that only its own buttons may close, such as progress.
    void SetCloseVisible(bool visible);
};

/// Rounds the corners of a top-level window that draws its own frame (Radius::Tile) while it is
/// neither maximized nor full screen, for platforms whose window manager leaves it square.
void RoundWindowCorners(QWidget* window);

/// Shows a message box with the success icon (a green check) instead of its information icon.
void MarkSuccess(QMessageBox* box);

/// Puts a dialog that cannot derive from Dialog in the same card, without a close button when
/// `close_button` is false. Call it before the dialog is first shown; a dialog whose window
/// already exists keeps its frame. The Spotlight style does this for every QMessageBox,
/// QInputDialog and QProgressDialog; message boxes get Paper's message layout, with the icon
/// beside the title and text.
void AdoptDialogChrome(QDialog* dialog, bool close_button = true);

} // namespace Spotlight
