// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLabel>

namespace Spotlight {

/// An inline status box under a form: a tinted icon and wrapped text in a rounded card, such as
/// "Testing iiDB access…" or "Check or replace your iiDB API key." The text is the label's own.
class Notice final : public QLabel {
    Q_OBJECT
public:
    enum class Tone { Neutral, Success, Error, Warning };

    explicit Notice(QWidget* parent = nullptr);

    void SetTone(Tone tone);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Tone tone = Tone::Neutral;
};

} // namespace Spotlight
