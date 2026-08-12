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

/* dt_gui_menu_* popover-menu layer (src/gui/gtk.c): construction, GTK3
 * pack/insert order semantics, UTF-8 collation, item types, submenu
 * attachment and the one-shot close chain.  All headless: widget
 * construction + "clicked"/"toggled" emission need no display, and the
 * close chain is driven through the visible FLAG only (nothing maps, so
 * nothing flashes on the user's screen). */

#include "test_gtk4.h"

#include "gui/gtk.h"

typedef struct close_state_t
{
  int closed;
  GtkWidget *menu; /* weak pointer to detect finalization */
} close_state_t;

static void on_menu_closed(GtkWidget *menu, gpointer data)
{
  close_state_t *s = data;
  s->closed++;
  (void)menu;
}

/* the one-shot menu unparents itself on an idle (deferred past the
 * popdown's cascade), so tests pump the main loop before asserting */
static void pump_main_loop(void)
{
  while(g_main_context_pending(NULL))
    g_main_context_iteration(NULL, FALSE);
}

static gchar *collect_labels(GtkWidget *menu)
{
  GtkWidget *box = gtk_popover_get_child(GTK_POPOVER(menu));
  GString *s = g_string_new(NULL);
  for(GtkWidget *child = gtk_widget_get_first_child(box); child;
      child = gtk_widget_get_next_sibling(child))
  {
    const gchar *label = dt_gui_menu_item_get_label(child);
    if(s->len) g_string_append_c(s, ',');
    g_string_append(s, label ? label : "(no-label)");
  }
  return g_string_free(s, FALSE);
}

static int cmp_collate(const void *a, const void *b)
{
  return g_utf8_collate(*(const char *const *)a, *(const char *const *)b);
}

static void test_menu_append_order(void)
{
  GtkWidget *menu = dt_gui_menu_new();
  dt_gui_menu_append(menu, dt_gui_menu_item_new("a"));
  dt_gui_menu_append(menu, dt_gui_menu_item_new("b"));
  dt_gui_menu_append(menu, dt_gui_menu_item_new("c"));

  gchar *labels = collect_labels(menu);
  g_assert_cmpstr(labels, ==, "a,b,c");
  g_free(labels);
}

static void test_menu_insert_positions(void)
{
  GtkWidget *menu = dt_gui_menu_new();
  dt_gui_menu_append(menu, dt_gui_menu_item_new("a"));
  dt_gui_menu_append(menu, dt_gui_menu_item_new("c"));

  /* middle insert at a 0-based box index */
  dt_gui_menu_insert(menu, dt_gui_menu_item_new("b"), 1);
  gchar *labels = collect_labels(menu);
  g_assert_cmpstr(labels, ==, "a,b,c");
  g_free(labels);

  /* pos <= 0 prepends, pos >= count appends */
  dt_gui_menu_insert(menu, dt_gui_menu_item_new("0"), 0);
  dt_gui_menu_insert(menu, dt_gui_menu_item_new("z"), 99);
  labels = collect_labels(menu);
  g_assert_cmpstr(labels, ==, "0,a,b,c,z");
  g_free(labels);

  /* prepend alias */
  GtkWidget *m2 = dt_gui_menu_new();
  dt_gui_menu_prepend(m2, dt_gui_menu_item_new("first"));
  dt_gui_menu_prepend(m2, dt_gui_menu_item_new("zeroth"));
  labels = collect_labels(m2);
  g_assert_cmpstr(labels, ==, "zeroth,first");
  g_free(labels);
}

static void test_menu_insert_sorted(void)
{
  /* names whose order is unambiguous in any UTF-8 locale (single lowercase
   * letters): guards against a broken collation context */
  GtkWidget *menu = dt_gui_menu_new();
  const char *names[] = { "z", "m", "a", "d" };
  for(size_t i = 0; i < G_N_ELEMENTS(names); i++)
    dt_gui_menu_insert_sorted(menu, dt_gui_menu_item_new(names[i]), names[i]);
  gchar *labels = collect_labels(menu);
  g_assert_cmpstr(labels, ==, "a,d,m,z");
  g_free(labels);

  /* full set: the walk must reproduce g_utf8_collate order exactly,
   * incrementally (insert one at a time, verify after each) */
  GtkWidget *m2 = dt_gui_menu_new();
  const char *mixed[] = { "delta", "Bravo", "äpfel", "Alpha", "charlie", "10x" };
  const size_t n = G_N_ELEMENTS(mixed);
  for(size_t i = 0; i < n; i++)
  {
    dt_gui_menu_insert_sorted(m2, dt_gui_menu_item_new(mixed[i]), mixed[i]);
    /* expected = the first i+1 names sorted with the same comparator */
    const char *sorted[G_N_ELEMENTS(mixed)];
    for(size_t j = 0; j <= i; j++)
      sorted[j] = mixed[j];
    qsort(sorted, i + 1, sizeof(char *), cmp_collate);
    GtkWidget *box = gtk_popover_get_child(GTK_POPOVER(m2));
    GtkWidget *child = gtk_widget_get_first_child(box);
    for(size_t j = 0; j <= i; j++)
    {
      g_assert_true(child != NULL);
      g_assert_cmpstr(dt_gui_menu_item_get_label(child), ==, sorted[j]);
      child = gtk_widget_get_next_sibling(child);
    }
    g_assert_true(child == NULL);
  }
}

static void test_menu_separator(void)
{
  GtkWidget *menu = dt_gui_menu_new();
  GtkWidget *sep = dt_gui_menu_separator_new();
  g_assert_true(GTK_IS_SEPARATOR(sep));
  dt_gui_menu_append(menu, sep);
  dt_gui_menu_append(menu, dt_gui_menu_item_new("x"));
  g_assert_cmpint(dt_test_box_count(gtk_popover_get_child(GTK_POPOVER(menu))), ==, 2);
}

typedef struct toggle_state_t
{
  int toggled;
} toggle_state_t;

static void on_toggled(GtkWidget *widget, gpointer data)
{
  toggle_state_t *s = data;
  s->toggled++;
  (void)widget;
}

static void test_menu_check_item_toggle(void)
{
  GtkWidget *item = dt_gui_menu_check_item_new("tog");
  g_assert_true(GTK_IS_CHECK_BUTTON(item));
  g_assert_false(dt_gui_menu_item_get_active(item));

  dt_gui_menu_item_set_active(item, TRUE);
  g_assert_true(dt_gui_menu_item_get_active(item));
  g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(item)));

  dt_gui_menu_item_set_active(item, FALSE);
  g_assert_false(dt_gui_menu_item_get_active(item));

  /* "toggled" fires on programmatic activation too (that is what the
   * masks/modulegroups check-item handlers rely on) */
  toggle_state_t t = { 0 };
  g_signal_connect(item, "toggled", G_CALLBACK(on_toggled), &t);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(item), TRUE);
  g_assert_cmpint(t.toggled, ==, 1);
}

static void test_menu_radio_items(void)
{
  GSList *group = NULL;
  GtkWidget *r1 = dt_gui_menu_radio_item_new(&group, "r1");
  GtkWidget *r2 = dt_gui_menu_radio_item_new(&group, "r2");
  g_assert_nonnull(group);
  g_slist_free(group);

  dt_gui_menu_item_set_active(r1, TRUE);
  g_assert_true(dt_gui_menu_item_get_active(r1));
  g_assert_false(dt_gui_menu_item_get_active(r2));

  /* exclusive: activating r2 releases r1 */
  dt_gui_menu_item_set_active(r2, TRUE);
  g_assert_false(dt_gui_menu_item_get_active(r1));
  g_assert_true(dt_gui_menu_item_get_active(r2));
}

static void test_menu_markup_item(void)
{
  GtkWidget *item = dt_gui_menu_item_new_markup("<b>bold</b>");
  g_assert_true(GTK_IS_BUTTON(item));
  GtkWidget *lab = gtk_button_get_child(GTK_BUTTON(item));
  g_assert_true(GTK_IS_LABEL(lab));
  g_assert_true(gtk_label_get_use_markup(GTK_LABEL(lab)));

  dt_gui_menu_item_set_label_markup(item, "<i>ital</i>");
  g_assert_cmpstr(gtk_label_get_label(GTK_LABEL(lab)), ==, "<i>ital</i>");
}

static void test_menu_submenu_attach(void)
{
  GtkWidget *root = dt_gui_menu_new();
  GtkWidget *sub_item = dt_gui_menu_item_new("sub");
  GtkWidget *sub = dt_gui_menu_new();
  dt_gui_menu_item_set_submenu(sub_item, sub);
  dt_gui_menu_append(root, dt_gui_menu_item_new("top"));
  dt_gui_menu_append(root, sub_item);

  /* the submenu popover is parented into the root's box so it dies with
   * the menu and can be pointed at its item: the box holds the two menu
   * rows plus the submenu popover itself */
  GtkWidget *box = gtk_popover_get_child(GTK_POPOVER(root));
  g_assert_true(gtk_widget_get_parent(sub) == box);
  g_assert_true(dt_gui_menu_item_get_submenu(sub_item) == sub);
  g_assert_cmpint(dt_test_box_count(box), ==, 3);
  g_assert_true(dt_test_box_child(box, 2) == sub);

  /* closing the root pops the submenu down: dt_gui_menu_insert connected
   * the popdown hook on the root's "closed" at attach time.  Drive the
   * chain by emitting "closed" on the root -- the submenu is not visible
   * (nothing maps, nothing flashes on screen), so the hook's popdown is a
   * no-op, but the hook wiring and the root's own closed handling are
   * still exercised */
  close_state_t root_state = { 0, NULL };
  close_state_t sub_state = { 0, NULL };
  g_signal_connect(root, "closed", G_CALLBACK(on_menu_closed), &root_state);
  g_signal_connect(sub, "closed", G_CALLBACK(on_menu_closed), &sub_state);

  g_signal_emit_by_name(root, "closed", NULL);
  g_assert_cmpint(root_state.closed, ==, 1);
  g_assert_cmpint(sub_state.closed, ==, 0); /* invisible: popdown no-op */
}

/* menus anchor to panels/buttons/boxes in the app; a bare GtkWindow works
 * as an unrealized stand-in here: the popover's parent chain then reaches a
 * toplevel, so the popup's visible flag maps nothing (the window is never
 * shown) and nothing flashes on the user's screen.  A plain box would
 * make the popover try to realize without a toplevel (Gtk warning). */
static GtkWidget *new_anchor(void)
{
  return gtk_window_new();
}

static void test_menu_item_click_closes(void)
{
  /* one-shot semantics: activating a plain item closes (pops down) the
   * root menu; the "closed" handler unparents it and the tree dies */
  GtkWidget *anchor = new_anchor();
  GtkWidget *menu = dt_gui_menu_new();
  GtkWidget *item = dt_gui_menu_item_new("click me");
  dt_gui_menu_append(menu, item);

  close_state_t state = { 0, menu };
  g_object_add_weak_pointer(G_OBJECT(menu), (gpointer *)&state.menu);
  g_signal_connect(menu, "closed", G_CALLBACK(on_menu_closed), &state);

  dt_gui_menu_popup(menu, anchor);
  g_assert_true(gtk_widget_get_parent(menu) == anchor);
  g_assert_true(gtk_widget_get_visible(menu));

  /* virtually visible: the anchor is unrealized, so the visible flag is
   * set without mapping anything to the user's screen.  A real click
   * would emit "clicked" through the press/release machinery; emit it
   * directly (gtk_widget_activate is a no-op until the button is
   * realized and then adds a timeout -- not a click simulation) */
  g_signal_emit_by_name(item, "clicked", NULL);
  pump_main_loop();

  g_assert_cmpint(state.closed, ==, 1);
  /* finalized by the deferred one-shot teardown: the weak pointer is
   * NULL, which also proves it was unparented (no leak) */
  g_assert_true(state.menu == NULL);
  if(state.menu)
    g_assert_true(gtk_widget_get_parent(menu) == NULL);

  gtk_widget_destroy(anchor);
}

static void test_menu_check_click_keeps_open(void)
{
  /* GTK3 parity: check/radio items toggle without dismissing the menu
   * (multi-select popups like the module-list need it) */
  GtkWidget *anchor = new_anchor();
  GtkWidget *menu = dt_gui_menu_new();
  GtkWidget *check = dt_gui_menu_check_item_new("tog");
  dt_gui_menu_append(menu, check);

  close_state_t state = { 0, NULL };
  g_signal_connect(menu, "closed", G_CALLBACK(on_menu_closed), &state);

  dt_gui_menu_popup(menu, anchor);
  /* check items toggle via their "toggled" signal (they have no "clicked"
   * in GTK4); clicking a menu check row is exactly this */
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);

  g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(check)));
  g_assert_cmpint(state.closed, ==, 0);            /* menu still open */
  g_assert_true(gtk_widget_get_parent(menu) == anchor);

  /* teardown: closing the kept-open menu finalizes it (deferred unparent);
   * the window can then be destroyed without a leftover child.  The weak
   * pointer (not the raw pointer) proves the finalize. */
  gpointer wp = menu;
  g_object_add_weak_pointer(G_OBJECT(menu), &wp);
  dt_gui_menu_close(menu);
  pump_main_loop();
  g_assert_true(wp == NULL);
  gtk_widget_destroy(anchor);
}

static void test_menu_close_and_reopen(void)
{
  /* explicit close via dt_gui_menu_close; a fresh popup must not leave the
   * previous menu tree alive (the one-shot lifecycle) */
  GtkWidget *anchor = new_anchor();

  GtkWidget *menu = dt_gui_menu_new();
  dt_gui_menu_append(menu, dt_gui_menu_item_new("x"));
  gpointer wp = menu;
  g_object_add_weak_pointer(G_OBJECT(menu), &wp);
  dt_gui_menu_popup(menu, anchor);
  dt_gui_menu_close(menu);
  pump_main_loop();
  g_assert_true(wp == NULL); /* closed menu finalized, not just hidden */
  GtkWidget *menu2 = dt_gui_menu_new();
  dt_gui_menu_append(menu2, dt_gui_menu_item_new("y"));
  gpointer wp2 = menu2;
  g_object_add_weak_pointer(G_OBJECT(menu2), &wp2);
  dt_gui_menu_popup(menu2, anchor);
  g_assert_true(gtk_widget_get_parent(menu2) == anchor);
  dt_gui_menu_close(menu2);
  pump_main_loop();
  g_assert_true(wp2 == NULL);
  if(wp2)
    g_assert_true(gtk_widget_get_parent(menu2) == NULL);

  gtk_widget_destroy(anchor);
}

void dt_test_menu_register(void)
{
  g_test_add_func("/gtk4/menu/append-order", test_menu_append_order);
  g_test_add_func("/gtk4/menu/insert-positions", test_menu_insert_positions);
  g_test_add_func("/gtk4/menu/insert-sorted", test_menu_insert_sorted);
  g_test_add_func("/gtk4/menu/separator", test_menu_separator);
  g_test_add_func("/gtk4/menu/check-item-toggle", test_menu_check_item_toggle);
  g_test_add_func("/gtk4/menu/radio-items", test_menu_radio_items);
  g_test_add_func("/gtk4/menu/markup-item", test_menu_markup_item);
  g_test_add_func("/gtk4/menu/submenu-attach", test_menu_submenu_attach);
  g_test_add_func("/gtk4/menu/item-click-closes", test_menu_item_click_closes);
  g_test_add_func("/gtk4/menu/check-click-keeps-open", test_menu_check_click_keeps_open);
  g_test_add_func("/gtk4/menu/close-and-reopen", test_menu_close_and_reopen);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
