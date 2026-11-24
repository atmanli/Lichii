// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace Spotlight {

/// Switches the application to the Spotlight style for the Lichii theme, or back to the platform
/// style. The Spotlight style is Fusion on every platform, so the style sheet draws the same
/// rounded widgets on Windows, Linux and macOS, with transparent popup windows (tooltips, combo
/// box lists, completers) so their rounded corners have no square backing, focus rings only after
/// keyboard navigation, and Spotlight dialog chrome for message boxes, input and progress
/// dialogs. Call it before the style sheet is set.
void ApplyAppStyle(bool spotlight);

} // namespace Spotlight
