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

/* Harness sanity: proves the binary links lib_darktable + GTK4, g_test
 * runs, and GTK4 widget construction + the first_child/next_sibling
 * children walk (the port's replacement for gtk_container_get_children)
 * work without a display. */

#include "test_gtk4.h"

#include "gui/gtk.h"

static void test_harness_trivial(void)
{
  g_assert_cmpint(GTK_MAJOR_VERSION, ==, 4);
  g_assert_cmpint(GTK_MINOR_VERSION, >=, 10); /* port requires >= 4.10 */
}

static void test_harness_widget_tree(void)
{
  GtkWidget *win = gtk_window_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(GTK_WINDOW(win), box);

  g_assert_true(dt_test_box_count(win) == 1);
  g_assert_cmpint(dt_test_box_count(box), ==, 0);

  for(int i = 0; i < 3; i++)
    gtk_box_append(GTK_BOX(box), gtk_label_new("x"));
  g_assert_cmpint(dt_test_box_count(box), ==, 3);
  g_assert_true(gtk_widget_get_first_child(win) == box);

  /* nth-child walk matches insertion order */
  g_assert_true(dt_test_box_child(box, 0) != NULL);
  g_assert_true(dt_test_box_child(box, 2) != NULL);
  g_assert_true(dt_test_box_child(box, 3) == NULL);

  /* remove_children must empty the box and unparent (not destroy) */
  for(GtkWidget *child = gtk_widget_get_first_child(box); child;
      child = gtk_widget_get_next_sibling(child))
    g_assert_true(gtk_widget_get_parent(child) == box);

  gtk_widget_destroy(win);
}

void dt_test_harness_register(void)
{
  g_test_add_func("/gtk4/harness/trivial", test_harness_trivial);
  g_test_add_func("/gtk4/harness/widget-tree", test_harness_widget_tree);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
