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

#include "test_gtk4.h"

#include <locale.h>

int main(int argc, char **argv)
{
  setlocale(LC_ALL, "");

  g_test_init(&argc, &argv, NULL);

  /* Survives without a display (display = NULL): widget construction and
   * controller/gesture signal emission still work; realize/show/size do
   * not (those tests call dt_test_require_display()).  This box reaches
   * the user's Wayland session, so gtk_init_check() also succeeds here. */
  if(!gtk_init_check())
    g_error("gtk_init_check() failed: no usable GTK backend");

  dt_test_harness_register();
  dt_test_menu_register();
  dt_test_container_register();
  dt_test_controller_register();
  dt_test_bauhaus_register();
  dt_test_iopheader_register();
  dt_test_gesture_register();

  return g_test_run();
}

/* ---- shared helpers ---- */

void dt_test_require_display(void)
{
  if(!gdk_display_get_default())
    g_test_skip("no display: realize/show/size not possible");
}

GtkWidget *dt_test_box_child(GtkWidget *box, int pos)
{
  GtkWidget *child = gtk_widget_get_first_child(box);
  for(int i = 0; child && i < pos; i++)
    child = gtk_widget_get_next_sibling(child);
  return child;
}

int dt_test_box_count(GtkWidget *box)
{
  int n = 0;
  for(GtkWidget *child = gtk_widget_get_first_child(box); child;
      child = gtk_widget_get_next_sibling(child))
    n++;
  return n;
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
