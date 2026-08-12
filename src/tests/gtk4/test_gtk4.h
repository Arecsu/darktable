/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>

/* ---- registrars, one per area file (see test_gtk4.c) ---- */
void dt_test_harness_register(void);
void dt_test_menu_register(void);
void dt_test_container_register(void);
void dt_test_controller_register(void);
void dt_test_bauhaus_register(void);
void dt_test_iopheader_register(void);
void dt_test_gesture_register(void);

/* g_test_skip() when the process has no display.  Widget construction and
 * controller/gesture signal emission work without one, but realize/show/
 * size only work on a real session (see agent-docs/TEST.md). */
void dt_test_require_display(void);

/* ---- GTK4 children-walk helpers (visual order = first_child chain) ---- */
GtkWidget *dt_test_box_child(GtkWidget *box, int pos);
int dt_test_box_count(GtkWidget *box);
