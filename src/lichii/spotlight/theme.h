// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPixmap>
#include <QStringView>

class QPainter;

// Design tokens and assets of the Spotlight launcher (Paper: Lichii Launcher Redesign, 02 · Spotlight).
// Widgets that paint themselves read these; the Lichii stylesheet uses the same values.
namespace Spotlight {

namespace Color {
inline QColor Window() { return QColor(0x0b, 0x0c, 0x10); }
inline QColor WindowBottom() { return QColor(0x0d, 0x0e, 0x14); }
inline QColor Scrim() { return QColor(0x09, 0x0a, 0x10); }
inline QColor Panel() { return QColor(0x12, 0x13, 0x1a); }
inline QColor PanelDeep() { return QColor(0x0f, 0x10, 0x16); }
inline QColor Popover() { return QColor(0x14, 0x16, 0x20, 0xf2); }
inline QColor Menu() { return QColor(0x0e, 0x0f, 0x14, 0xf0); }
inline QColor Accent() { return QColor(0xff, 0x6b, 0x95); }
inline QColor AccentSoft() { return QColor(0xff, 0x6b, 0x95, 0x24); }
inline QColor AccentLight() { return QColor(0xff, 0x8f, 0xae); }
inline QColor OnAccent() { return QColor(0x2a, 0x0a, 0x14); }
inline QColor OnLight() { return QColor(0x19, 0x1a, 0x22); }
inline QColor Text() { return QColor(0xff, 0xff, 0xff); }
inline QColor TextSecondary() { return QColor(0xff, 0xff, 0xff, 0x9e); }
inline QColor TextMuted() { return QColor(0xff, 0xff, 0xff, 0x8c); }
inline QColor TextFaint() { return QColor(0xff, 0xff, 0xff, 0x73); }
inline QColor Control() { return QColor(0xff, 0xff, 0xff, 0x1f); }
inline QColor ControlHover() { return QColor(0xff, 0xff, 0xff, 0x2e); }
inline QColor Fill() { return QColor(0xff, 0xff, 0xff, 0x14); }
inline QColor FillFaint() { return QColor(0xff, 0xff, 0xff, 0x0d); }
inline QColor Stroke() { return QColor(0xff, 0xff, 0xff, 0x2e); }
inline QColor StrokeSubtle() { return QColor(0xff, 0xff, 0xff, 0x14); }
inline QColor Success() { return QColor(0x3f, 0xd1, 0x8b); }
inline QColor Warning() { return QColor(0xff, 0xc8, 0x57); }
inline QColor Info() { return QColor(0x8f, 0xb5, 0xff); }
inline QColor Hud() { return QColor(0x08, 0x09, 0x0d, 0xb8); }
inline QColor HudSolid() { return QColor(0x08, 0x09, 0x0d, 0xdb); }
} // namespace Color

namespace Radius {
inline constexpr int Pill = 20;
inline constexpr int CircleButton = 19;
inline constexpr int Card = 16;
inline constexpr int Tile = 14;
inline constexpr int Row = 13;
inline constexpr int KeyCap = 7;
inline constexpr int InGamePanel = 22;
} // namespace Radius

/// Registers the bundled Lexend and Cal Sans faces. Call once before any widget is created.
void LoadFonts();

/// Lexend, the interface face. Sizes are in pixels, as in the design.
QFont Font(int pixel_size, int weight = 500);

/// Cal Sans, the display face for the wordmark and page titles.
QFont DisplayFont(int pixel_size);

/// A Phosphor icon from :/spotlight/icons, tinted with color, at size device-independent pixels.
/// Names are Phosphor's, such as "gear-six" or "play-fill".
QPixmap IconPixmap(QStringView name, int size, const QColor& color, qreal device_pixel_ratio);

/// The same icon for actions and buttons, with a dimmed variant for the disabled state.
QIcon Icon(QStringView name, const QColor& color = Color::Text(), int size = 20);

void PaintWholeIcon(QPainter& painter, const QRectF& rect, const QPixmap& icon, qreal radius);

} // namespace Spotlight
