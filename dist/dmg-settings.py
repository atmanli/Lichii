# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# dmgbuild settings for the macOS release disk image: lichii.app with an arrow to Applications.
# dmgbuild -s dist/dmg-settings.py -D app=<path to lichii.app> Lichii <output.dmg>

import os.path

app = defines["app"]  # noqa: F821 (set by dmgbuild)
app_name = os.path.basename(app)

format = "UDZO"
files = [app]
symlinks = {"Applications": "/Applications"}
icon = os.path.join(app, "Contents", "Resources", "lichii.icns")

background = "builtin-arrow"
window_rect = ((200, 120), (640, 280))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
show_icon_preview = False
arrange_by = None
icon_size = 128
text_size = 13
icon_locations = {app_name: (140, 120), "Applications": (500, 120)}
