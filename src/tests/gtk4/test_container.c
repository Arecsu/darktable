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

/* dt_gui_container_* helpers (src/gui/gtk.c): the GTK4 first_child/
 * next_sibling children walks that replace gtk_container_get_children,
 * and dt_gui_widget_reparent() (the detach-and-reuse idiom).  Headless:
 * pure widget-tree manipulation. */

#include "test_gtk4.h"

#include "gui/gtk.h"

static void test_container_helpers(void)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  g_assert_false(dt_gui_container_has_children(box));
  g_assert_cmpint(dt_gui_container_num_children(box), ==, 0);
  g_assert_true(dt_gui_container_first_child(box) == NULL);
  g_assert_true(dt_gui_container_nth_child(box, 0) == NULL);

  GtkWidget *kids[4];
  for(int i = 0; i < 4; i++)
  {
    kids[i] = gtk_label_new("k");
    gtk_box_append(GTK_BOX(box), kids[i]);
  }

  g_assert_true(dt_gui_container_has_children(box));
  g_assert_cmpint(dt_gui_container_num_children(box), ==, 4);
  g_assert_true(dt_gui_container_first_child(box) == kids[0]);
  g_assert_true(dt_gui_container_nth_child(box, 0) == kids[0]);
  g_assert_true(dt_gui_container_nth_child(box, 2) == kids[2]);
  g_assert_true(dt_gui_container_nth_child(box, 3) == kids[3]);
  g_assert_true(dt_gui_container_nth_child(box, 4) == NULL);
  g_assert_true(dt_gui_container_nth_child(box, -1) == NULL);

  /* remove_children unparents without destroying: the widgets survive
   * when the caller holds its own refs (the container's ref is released,
   * GTK3 gtk_container_remove parity).  The four labels appended above are
   * still in the box; add the test's own refs before removing. */
  gpointer wp[4];
  for(int i = 0; i < 4; i++)
  {
    g_object_ref(kids[i]); /* the test owns a ref, like a reuser would */
    wp[i] = kids[i];
    g_object_add_weak_pointer(G_OBJECT(kids[i]), &wp[i]);
  }
  dt_gui_container_remove_children(box);
  g_assert_cmpint(dt_gui_container_num_children(box), ==, 0);
  for(int i = 0; i < 4; i++)
  {
    g_assert_true(wp[i] != NULL); /* still alive, just detached */
    g_assert_true(gtk_widget_get_parent(kids[i]) == NULL);
    g_object_unref(kids[i]);
  }
}

static void test_container_destroy_children(void)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *kids[3];
  gpointer wp[3];
  for(int i = 0; i < 3; i++)
  {
    kids[i] = gtk_label_new("k");
    gtk_box_append(GTK_BOX(box), kids[i]);
    wp[i] = kids[i];
    g_object_add_weak_pointer(G_OBJECT(kids[i]), &wp[i]);
  }
  dt_gui_container_destroy_children(box);
  g_assert_cmpint(dt_gui_container_num_children(box), ==, 0);
  for(int i = 0; i < 3; i++)
    g_assert_true(wp[i] == NULL); /* destroyed */
}

static void test_container_reparent(void)
{
  GtkWidget *box1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *box2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *kid = gtk_label_new("k");

  gtk_box_append(GTK_BOX(box1), kid);
  g_assert_true(gtk_widget_get_parent(kid) == box1);

  dt_gui_widget_reparent(kid, box2);
  g_assert_true(gtk_widget_get_parent(kid) == box2);
  g_assert_cmpint(dt_gui_container_num_children(box1), ==, 0);
  g_assert_cmpint(dt_gui_container_num_children(box2), ==, 1);

  /* the moved widget stays alive through the detach (no finalize) */
  gpointer wp = kid;
  g_object_add_weak_pointer(G_OBJECT(kid), &wp);
  dt_gui_widget_reparent(kid, box1);
  g_assert_true(wp != NULL);
  g_assert_true(gtk_widget_get_parent(kid) == box1);
}

void dt_test_container_register(void)
{
  g_test_add_func("/gtk4/container/helpers", test_container_helpers);
  g_test_add_func("/gtk4/container/destroy-children", test_container_destroy_children);
  g_test_add_func("/gtk4/container/reparent", test_container_reparent);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
