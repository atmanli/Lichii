// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QStringList>
#include <QWidget>

// Controls shared by the Spotlight screens. Each paints itself from Spotlight's tokens.
namespace Spotlight {

/// A round icon button: the top bar's +, settings and ⋯, the hero's ⋯.
class CircleButton : public QAbstractButton {
    Q_OBJECT
public:
    explicit CircleButton(QString icon, int diameter = 38, QWidget* parent = nullptr);
    void SetIcon(QString icon);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QString icon_name;
    int diameter;
    bool hovered = false;
};

/// A rounded text button with an optional leading icon.
class PillButton : public QAbstractButton {
    Q_OBJECT
public:
    enum class Kind { Accent, Control, Light };
    explicit PillButton(const QString& text, QString icon = {}, Kind kind = Kind::Control,
                        int height = 36, QWidget* parent = nullptr);
    void SetKind(Kind kind);
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
    bool hovered = false;
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
    void SetCurrentIndex(int index);
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
    int SegmentAt(const QPoint& point) const;

    QStringList options;
    QList<bool> enabled;
    Kind kind;
    int height;
    int current = 0;
    int hovered = -1;
};

/// The on/off switch of settings rows.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
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
