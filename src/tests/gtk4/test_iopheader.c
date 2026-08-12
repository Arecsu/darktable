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

/* Layer B test locking the darkroom module header construction
 * (src/develop/imageop.c dt_iop_gui_set_expander()).
 *
 * Session 6 found the GTK4 port drawing the header buttons in the wrong
 * visual order: the GTK3 code packed the end group with pack_end(), whose
 * *children-list* order differs from the *visual* order, and the flat
 * GTK4 port appended in children-list order.  This test walks the real
 * header box (first_child/next_sibling == visual order in GTK4) and locks
 * the append order the port now uses:
 *
 *   [off | icon | lab | instance | multi | reset | presets]
 *
 * plus the dynamic width-trigger insertion ("hide header buttons" configs
 * other than always/dim/active) that must land between the instance name
 * and the multi-instance button (gtk_box_reorder_child_before), and the
 * show/hide button roundtrip.
 *
 * Headless: widget construction + measure need no display (no window is
 * shown, nothing realizes).  The bootstrap mirrors dt_iop_load_module():
 * enabled/default_enabled stay FALSE -- the app creates every module
 * disabled ("all modules disabled by default") and the header's off
 * toggle-button is only ever set to module->enabled, so with FALSE no
 * "toggled" fires (and _gui_off_callback, which touches module->dev, is
 * never reached).  flags() = 0 keeps the guides widget and the blending
 * UI (both guarded on their IOP_FLAGS_*) out of the picture. */

#include "test_gtk4.h"

#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/imageop.h"
#include "gui/gtk.h"

static gboolean bootstrapped = FALSE;
static dt_iop_module_so_t _so; /* zeroed: so->actions.type = DT_ACTION_TYPE_CATEGORY */

static const char *_module_name(void)
{
  return "test iop";
}

static int _module_flags(void)
{
  return 0; /* no blending UI, no guides widget, no deprecated message */
}

static const char *_module_deprecated_msg(void)
{
  return NULL;
}

/* conf (for the hide_header_buttons config), a ui with the right-center
 * container (dt_ui_container_add_widget appends the expander there) and
 * control (widget_definitions for dt_action_define) -- exactly what
 * dt_iop_gui_set_expander reads; everything else stays zeroed. */
static void _bootstrap(void)
{
  if(bootstrapped) return;
  bootstrapped = TRUE;

  darktable.conf = g_new0(dt_conf_t, 1);
  darktable.conf->table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->override_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->x_confgen = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  dt_pthread_mutex_init(&darktable.conf->mutex, NULL);

  darktable.gui = g_new0(dt_gui_gtk_t, 1);
  darktable.gui->ui = dt_ui_new(gtk_window_new());
  darktable.gui->ui->containers[DT_UI_CONTAINER_PANEL_RIGHT_CENTER] =
    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  darktable.control = g_new0(dt_control_t, 1);
  darktable.control->widget_definitions = g_ptr_array_new_with_free_func(NULL);
}

/* a minimal module, shaped like a freshly loaded real one (see the file
 * comment): enabled/default_enabled FALSE, flags() 0, a plain body widget */
static dt_iop_module_t *_module_new(void)
{
  dt_iop_module_t *module = g_new0(dt_iop_module_t, 1);
  g_strlcpy(module->op, "testiop", sizeof(module->op));
  module->name = _module_name;
  module->flags = _module_flags;
  module->deprecated_msg = _module_deprecated_msg;
  module->so = &_so;
  module->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  return module;
}

static void test_header_order(void)
{
  _bootstrap();
  /* the app default: no width trigger, no _header_size_apply() at
   * construction -- the header is exactly the seven base widgets */
  dt_conf_set_string("darkroom/ui/hide_header_buttons", "always");

  dt_iop_module_t *module = _module_new();
  dt_iop_gui_set_expander(module);

  GtkWidget *header = module->header;
  g_assert_nonnull(header);
  g_assert_true(GTK_IS_BOX(header));

  /* the visual order the GTK3 pack_end/pack_start mix produced:
   * [off | icon | lab | instance | multi | reset | presets] */
  GtkWidget *children[7] = { NULL };
  int n = 0;
  for(GtkWidget *c = gtk_widget_get_first_child(header); c;
      c = gtk_widget_get_next_sibling(c))
  {
    g_assert_cmpint(n, <, 7);
    children[n++] = c;
  }
  g_assert_cmpint(n, ==, 7);

  /* off: the module's enable toggle button, leftmost */
  g_assert_true(children[0] == module->off);
  g_assert_true(GTK_IS_TOGGLE_BUTTON(children[0]));

  /* icon: the CSS-named empty label */
  g_assert_true(GTK_IS_LABEL(children[1]));
  g_assert_cmpstr(gtk_widget_get_name(children[1]), ==, "iop-panel-icon-testiop");

  /* lab: the box whose first child is the module label */
  g_assert_true(GTK_IS_BOX(children[2]));
  g_assert_true(gtk_widget_get_first_child(children[2]) == module->label);

  /* instance name */
  g_assert_true(children[3] == module->instance_name);

  /* the right-side group: multi, reset, presets (GTK3 pack_end visual
   * order, first packed = rightmost) */
  g_assert_true(children[4] == module->multimenu_button);
  g_assert_true(children[5] == module->reset_button);
  g_assert_true(children[6] == module->presets_button);
  for(int i = 4; i < 7; i++)
    g_assert_true(GTK_IS_BUTTON(children[i]));

  /* "always": every child is visible right after construction */
  for(int i = 0; i < 7; i++)
    g_assert_true(gtk_widget_get_visible(children[i]));

  /* the expander landed in the right-center container */
  GtkWidget *container = darktable.gui->ui->containers[DT_UI_CONTAINER_PANEL_RIGHT_CENTER];
  g_assert_true(gtk_widget_get_first_child(container) == module->expander);
  g_assert_true(gtk_widget_get_parent(module->expander) == container);
}

static void test_header_width_trigger(void)
{
  _bootstrap();
  /* a dynamic "hide header buttons" config inserts the width-trigger
   * drawing area (which measures the header width) between the instance
   * name and the multi-instance button; _header_size_apply() then runs
   * once at construction and re-shows the buttons */
  dt_conf_set_string("darkroom/ui/hide_header_buttons", "glide");

  dt_iop_module_t *module = _module_new();
  dt_iop_gui_set_expander(module);

  GtkWidget *header = module->header;
  g_assert_nonnull(header);

  GtkWidget *children[8] = { NULL };
  int n = 0;
  for(GtkWidget *c = gtk_widget_get_first_child(header); c;
      c = gtk_widget_get_next_sibling(c))
  {
    g_assert_cmpint(n, <, 8);
    children[n++] = c;
  }
  g_assert_cmpint(n, ==, 8);

  /* base order preserved: off, icon, lab, instance */
  g_assert_true(children[0] == module->off);
  g_assert_true(children[1] != NULL && GTK_IS_LABEL(children[1]));
  g_assert_true(children[2] == gtk_widget_get_parent(module->label));
  g_assert_true(children[3] == module->instance_name);

  /* the width trigger sits before the multi-instance button */
  g_assert_true(GTK_IS_DRAWING_AREA(children[4]));
  g_assert_true(children[5] == module->multimenu_button);
  g_assert_true(children[6] == module->reset_button);
  g_assert_true(children[7] == module->presets_button);

  /* glide re-shows all four buttons via _header_size_apply() */
  g_assert_true(gtk_widget_get_visible(children[0]));
  for(int i = 4; i < 8; i++)
    g_assert_true(gtk_widget_get_visible(children[i]));
}

static void test_header_show_hide(void)
{
  _bootstrap();
  dt_conf_set_string("darkroom/ui/hide_header_buttons", "always");

  dt_iop_module_t *module = _module_new();
  dt_iop_gui_set_expander(module);

  /* always-hide hides the right-side button group (multi/reset/presets)
   * but leaves the label/icon/instance alone; the off toggle button sits
   * at the head and is not part of the end-group walk (same as GTK3,
   * where the pack_start group was never visited either) */
  dt_iop_show_hide_header_buttons(module, FALSE, TRUE);
  g_assert_true(gtk_widget_get_visible(module->off));
  g_assert_false(gtk_widget_get_visible(module->multimenu_button));
  g_assert_false(gtk_widget_get_visible(module->reset_button));
  g_assert_false(gtk_widget_get_visible(module->presets_button));
  g_assert_true(gtk_widget_get_visible(module->label));
  g_assert_true(gtk_widget_get_visible(module->instance_name));

  /* and back: same children, same order */
  dt_iop_show_hide_header_buttons(module, TRUE, FALSE);
  g_assert_true(gtk_widget_get_visible(module->off));
  g_assert_true(gtk_widget_get_visible(module->multimenu_button));
  g_assert_true(gtk_widget_get_visible(module->reset_button));
  g_assert_true(gtk_widget_get_visible(module->presets_button));

  g_assert_true(gtk_widget_get_first_child(module->header) == module->off);
  GtkWidget *last = gtk_widget_get_last_child(module->header);
  g_assert_true(last == module->presets_button);
}

void dt_test_iopheader_register(void)
{
  g_test_add_func("/gtk4/iopheader/order", test_header_order);
  g_test_add_func("/gtk4/iopheader/width-trigger", test_header_width_trigger);
  g_test_add_func("/gtk4/iopheader/show-hide", test_header_show_hide);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
