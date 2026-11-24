// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lichii/game/game_grid.h"

class GameCarousel final : public GameGrid {
public:
    explicit GameCarousel(QWidget* parent = nullptr);
};
