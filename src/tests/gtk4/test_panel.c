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

/* Panel-toggle path (src/gui/gtk.c): clicking a window border while the
 * splash screen is still up used to segfault on macOS.  The view manager
 * does not exist yet during startup, so _panels_get_view_path() returns
 * NULL and the panel code passed that NULL into the conf API, which hit
 * g_hash_table_lookup(NULL) -> g_str_hash(NULL).  Homebrew's glib is
 * built with G_DISABLE_CHECKS, so glib's own key!=NULL guard is compiled
 * out (upstream relied on it).  Locked here:
 *   - NULL-key safety of the conf API (the mechanism), and
 *   - dt_ui_panel_show(..., write=TRUE) with an uninitialized view
 *     manager (the exact crash site: gtk.c dt_ui_panel_show).
 * Headless: no display, no dt_init(), no full darktable state needed —
 * the tests build the minimal ui/gui/conf contexts themselves. */

#include "test_gtk4.h"

#include "common/darktable.h"
#include "control/conf.h"
#include "gui/gtk.h"

/* minimal conf context: real hash tables + mutex, empty of entries.  The
 * bauhaus/iopheader/gesture fixtures also build a conf and never tear it
 * down, and the whole suite runs in ONE process, so start from a fresh
 * empty conf regardless of what came before (tear down first, don't
 * assert NULL). */
static void _test_conf_down(void);
static void _test_conf_up(void)
{
  if(darktable.conf)
    _test_conf_down();
  darktable.conf = g_malloc0(sizeof(dt_conf_t));
  dt_pthread_mutex_init(&darktable.conf->mutex, NULL);
  darktable.conf->table = g_hash_table_new(g_str_hash, g_str_equal);
  darktable.conf->x_confgen = g_hash_table_new(g_str_hash, g_str_equal);
  darktable.conf->override_entries = g_hash_table_new(g_str_hash, g_str_equal);
}

static void _test_conf_down(void)
{
  g_hash_table_destroy(darktable.conf->table);
  g_hash_table_destroy(darktable.conf->x_confgen);
  g_hash_table_destroy(darktable.conf->override_entries);
  dt_pthread_mutex_destroy(&darktable.conf->mutex);
  g_free(darktable.conf);
  darktable.conf = NULL;
}

/* the crash mechanism: conf lookups with a NULL name must behave like a
 * missing key, never touch g_hash_table with a NULL key. */
static void test_conf_null_key_safety(void)
{
  _test_conf_up();

  g_assert_cmpint(dt_conf_get_int(NULL), ==, 0);
  g_assert_false(dt_conf_get_bool(NULL));
  g_assert_cmpfloat(dt_conf_get_float(NULL), ==, 0.0f);
  g_assert_nonnull(dt_conf_get_string(NULL)); /* empty value, not NULL */
  g_assert_cmpstr(dt_conf_get_string(NULL), ==, "");
  g_assert_nonnull(dt_conf_get_string_const(NULL));
  g_assert_false(dt_conf_key_exists(NULL));
  g_assert_false(dt_confgen_value_exists(NULL, DT_DEFAULT));
  g_assert_null(dt_confgen_get(NULL, DT_DEFAULT));
  g_assert_false(dt_conf_is_equal(NULL, "x"));

  /* setters with a NULL name must be no-ops, not write garbage */
  dt_conf_set_int(NULL, 42);
  dt_conf_set_bool(NULL, TRUE);
  dt_conf_set_string(NULL, "x");
  dt_conf_remove_key(NULL);
  g_assert_cmpint(g_hash_table_size(darktable.conf->table), ==, 0);

  _test_conf_down();
}

/* the exact crash site from the macOS report: border click while the
 * splash is up -> _panel_toggle -> dt_ui_panel_show(..., write=TRUE)
 * with darktable.view_manager == NULL. */
static void test_panel_show_during_startup(void)
{
  _test_conf_up();

  /* startup state: view manager not created yet — this is what made
   * _panels_get_view_path() return NULL */
  g_assert_null(darktable.view_manager);

  /* minimal ui; the port made dt_ui_t public so tests can build one */
  dt_ui_t ui;
  memset(&ui, 0, sizeof(ui));
  for(int k = 0; k < DT_UI_PANEL_SIZE; k++)
    ui.panels[k] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  /* minimal gui: dt_ui_panel_show() queue_draw()s the border widgets */
  dt_gui_gtk_t gui;
  memset(&gui, 0, sizeof(gui));
  gui.ui = &ui;
  gui.widgets.left_border = gtk_drawing_area_new();
  gui.widgets.right_border = gtk_drawing_area_new();
  gui.widgets.top_border = gtk_drawing_area_new();
  gui.widgets.bottom_border = gtk_drawing_area_new();
  darktable.gui = &gui;

  /* show + write (the crashing combination) on several panels */
  dt_ui_panel_show(&ui, DT_UI_PANEL_LEFT, TRUE, TRUE);
  dt_ui_panel_show(&ui, DT_UI_PANEL_LEFT, FALSE, TRUE);
  dt_ui_panel_show(&ui, DT_UI_PANEL_TOP, TRUE, TRUE);
  dt_ui_panel_show(&ui, DT_UI_PANEL_TOP, FALSE, TRUE);

  /* no view -> no panel config keys: nothing may have been written */
  g_assert_cmpint(g_hash_table_size(darktable.conf->table), ==, 0);
  g_assert_cmpint(g_hash_table_size(darktable.conf->x_confgen), ==, 0);

  darktable.gui = NULL;
  _test_conf_down();
}

void dt_test_panel_register(void)
{
  g_test_add_func("/gtk4/panel/conf-null-key-safety", test_conf_null_key_safety);
  g_test_add_func("/gtk4/panel/show-during-startup", test_panel_show_during_startup);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
