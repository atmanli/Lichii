// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QKeySequence>
#include <QStringList>
#include <QVariantAnimation>
#include <QWidget>

// Controls shared by the Spotlight screens. Each paints itself from Spotlight's tokens.
namespace Spotlight {

/// Durations in ms shared by Spotlight transitions; every one eases out.
namespace Motion {
inline constexpr int Hover = 120;
inline constexpr int Slide = 180;
inline constexpr int Fade = 160;
} // namespace Motion

/// A shortcut as Spotlight's key caps spell it: "Ctrl G" rather than "Ctrl+G".
QString KeyText(const QKeySequence& sequence);

/// Colours a label's text. The Lichii style sheet's QWidget colour overrides palettes.
void SetTextColor(QWidget* label, const QColor& color);

/// The colour t of the way from one colour to another, alpha included.
QColor Mix(const QColor& from, const QColor& to, qreal t);

/// Fades a shown widget in, such as a page that just replaced another.
void FadeIn(QWidget* widget, int duration = Motion::Fade);
/// Fades a top-level window in every time it is shown.
void FadeInOnShow(QWidget* window, int duration = Motion::Fade);
/// Whether the user moved focus to `widget` with the keyboard, so it shows a focus ring.
bool HasKeyboardFocus(const QWidget* widget);

/// A 0..1 value that follows the pointer entering and leaving a control.
class HoverFade {
public:
    explicit HoverFade(QWidget* owner);
    void SetHovered(bool hovered);
    qreal Value() const;

private:
    QVariantAnimation animation;
};

/// A round icon button: the top bar's +, settings and ⋯, the hero's ⋯.
class CircleButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit CircleButton(QString icon, int diameter = 38, QWidget* parent = nullptr);
    void SetIcon(QString icon);
    /// Draws the icon in `tint` on a fill tinted with it, such as Color::Destructive() for clear.
    void SetTint(const QColor& tint);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QString icon_name;
    QColor tint;
    int diameter;
    HoverFade hover{this};
};

/// A rounded text button with an optional leading icon.
class PillButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Kind { Accent, Control, Light };
    explicit PillButton(const QString& text, QString icon = {}, Kind kind = Kind::Control,
                        int height = 36, QWidget* parent = nullptr);
    void SetKind(Kind kind);
    void SetIcon(QString icon);
    void SetForegroundColor(const QColor& color);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QString icon_name;
    Kind kind;
    QColor foreground;
    int height;
    HoverFade hover{this};
};

/// Mutually exclusive options in one pill: All / Switch 2 / Switch 1, Auto / Low / Mid / High.
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    /// Light marks the selection white (top bar); Accent marks it pink (settings).
    enum class Kind { Light, Accent };
    explicit SegmentedControl(QStringList options, Kind kind = Kind::Accent, int height = 40,
                              QWidget* parent = nullptr);
    int CurrentIndex() const { return current; }
    /// -1 selects no option.
    void SetCurrentIndex(int index);
    /// Selects no option, for a state none of the options describes.
    void ClearSelection();
    void SetOptions(QStringList options);
    /// Greys out an option that the current game or hardware does not support.
    void SetOptionEnabled(int index, bool enabled);
    QSize sizeHint() const override;

signals:
    void CurrentChanged(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QRect SegmentRect(int index) const;
    QRectF SelectionRect() const;
    int SegmentAt(const QPoint& point) const;

    QStringList options;
    QList<bool> enabled;
    Kind kind;
    int height;
    int current = 0;
    int hovered = -1;
    QVariantAnimation slide;
};

/// The on/off switch of settings rows.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void nextCheckState() override;

private:
    QVariantAnimation knob;
};

/// A keyboard shortcut chip such as "Ctrl F".
class KeyCap : public QWidget {
    Q_OBJECT
public:
    explicit KeyCap(QString text, QWidget* parent = nullptr);
    void SetText(QString text);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QString text;
};

} // namespace Spotlight
