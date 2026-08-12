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

/* Layer B test locking the lighttable module header click handling
 * (src/libs/lib.c dt_lib_gui_get_expander() and the header/arrow
 * handlers).
 *
 * Session 17: clicking a module's arrow toggle opened AND closed the module
 * on one click ("opens and closes very fast").  Root cause: the arrow's
 * click gesture claims the press sequence, which CANCELS the parent header
 * gesture; dt_gui_connect_click()'s cancel bridge re-emitted "released"
 * with RAW SURFACE coordinates, and the release handler's
 * gtk_widget_pick() (which expects header-LOCAL coordinates) missed the
 * arrow button, so the header toggled a second time.
 *
 * These tests lock the contract the fix depends on, with real allocations
 * (display-gated): a header release over a button must NOT toggle the
 * module, a header release over the background MUST toggle, and the
 * arrow's own gesture must toggle on press -- together these make one
 * arrow click produce exactly one toggle. */

#include "test_gtk4.h"

#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "dtgtk/expander.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "views/view.h"

static gboolean bootstrapped = FALSE;

static const char *_lib_name(dt_lib_module_t *self)
{
  (void)self;
  return "test lib";
}

static uint32_t _lib_container(dt_lib_module_t *self)
{
  (void)self;
  return DT_UI_CONTAINER_PANEL_LEFT_CENTER;
}

static gboolean _lib_expandable(dt_lib_module_t *self)
{
  (void)self;
  return TRUE;
}

/* conf, a ui with the left-center container, a lib shell (gui_module),
 * a view manager (dt_lib_gui_set_expanded stores the state under
 * views/<current>/<name>/expanded) and control -- exactly what
 * dt_lib_gui_get_expander() and the toggle path read. */
static void _bootstrap(void)
{
  if(bootstrapped) return;
  bootstrapped = TRUE;

  darktable.conf = g_new0(dt_conf_t, 1);
  darktable.conf->table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->override_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->x_confgen = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  dt_pthread_mutex_init(&darktable.conf->mutex, NULL);
  /* plain toggle: no single-module collapsing */
  dt_conf_set_bool("lighttable/ui/single_module", FALSE);

  darktable.gui = g_new0(dt_gui_gtk_t, 1);
  darktable.gui->dpi = 96.0;
  darktable.gui->ppd = 1.0;
  darktable.gui->ui = dt_ui_new(gtk_window_new());
  darktable.gui->ui->containers[DT_UI_CONTAINER_PANEL_LEFT_CENTER] =
    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  /* the arrow-press core grabs focus on the center */
  darktable.gui->ui->center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  darktable.lib = g_new0(dt_lib_t, 1);
  darktable.lib->plugins = NULL;

  darktable.view_manager = g_new0(dt_view_manager_t, 1);
  darktable.view_manager->current_view = g_new0(dt_view_t, 1);
  g_strlcpy(darktable.view_manager->current_view->module_name, "lighttable",
            sizeof(darktable.view_manager->current_view->module_name));

  darktable.control = g_new0(dt_control_t, 1);
  darktable.control->widget_definitions = g_ptr_array_new_with_free_func(NULL);
}

/* the suite runs tests in-process, so globals leak between tests: the
 * panel/view tests assert a pristine startup state (view_manager == NULL),
 * so undo what this area's tests set up */
static void _bootstrap_down(void)
{
  darktable.view_manager = NULL;
  darktable.lib = NULL;
  bootstrapped = FALSE; /* next test re-bootstraps */
}

/* a minimal expandable lib module, shaped like a freshly loaded one */
static dt_lib_module_t *_module_new(void)
{
  dt_lib_module_t *module = g_new0(dt_lib_module_t, 1);
  g_strlcpy(module->plugin_name, "testlib", sizeof(module->plugin_name));
  module->name = _lib_name;
  module->container = _lib_container;
  module->expandable = _lib_expandable;
  module->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  return module;
}

/* pump the main loop until `widget` has an allocation (realize+allocate
 * happen a frame or two after show, and only on a real display) */
static void _pump_allocated(GtkWidget *widget)
{
  for(int i = 0; i < 200 && gtk_widget_get_allocated_width(widget) <= 0; i++)
  {
    while(g_main_context_iteration(NULL, FALSE));
    g_usleep(10000);
  }
  g_assert_cmpint(gtk_widget_get_allocated_width(widget), >, 0);
}

/* first GtkGestureClick on `widget` whose button is 0 -- that is the
 * dt_gui_connect_click() gesture (dtgtk/GtkButton internal gestures use
 * GDK_BUTTON_PRIMARY) */
static GtkGestureClick *_dt_click_gesture(GtkWidget *widget)
{
  GListModel *controllers = gtk_widget_observe_controllers(widget);
  GtkGestureClick *found = NULL;
  for(guint i = 0; i < g_list_model_get_n_items(controllers); i++)
  {
    GtkEventController *c = g_list_model_get_item(controllers, i);
    if(GTK_IS_GESTURE_CLICK(c) && gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(c)) == 0)
    {
      found = GTK_GESTURE_CLICK(c);
      break;
    }
  }
  g_assert_nonnull(found);
  return found;
}

/* center of `child` inside `parent` (parent-local coordinates) */
static void _center_inside(GtkWidget *parent, GtkWidget *child, gdouble *x, gdouble *y)
{
  graphene_rect_t r;
  g_assert_true(gtk_widget_compute_bounds(child, parent, &r));
  g_assert_cmpfloat(r.size.width, >, 0);
  g_assert_cmpfloat(r.size.height, >, 0);
  *x = r.origin.x + r.size.width / 2.0;
  *y = r.origin.y + r.size.height / 2.0;
}

/* build the header, host it in a mapped window, return (window, header evb) */
static GtkWidget *_mapped_header(dt_lib_module_t *module, GtkWidget **header_evb_out)
{
  GtkWidget *expander = dt_lib_gui_get_expander(module);
  g_assert_nonnull(expander);
  g_assert_nonnull(module->expander);

  GtkWidget *win = gtk_window_new();
  /* a fresh container box per test: the previous test destroyed its window
   * (and the box with it), and reusing the stale ui container box would
   * leave a dangling pointer */
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(box), expander);
  gtk_window_set_child(GTK_WINDOW(win), box);
  gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
  gtk_widget_show(win);
  _pump_allocated(dtgtk_expander_get_header(DTGTK_EXPANDER(module->expander)));

  *header_evb_out = dtgtk_expander_get_header_event_box(DTGTK_EXPANDER(module->expander));
  return win;
}

static gboolean _lib_expanded(dt_lib_module_t *module)
{
  return dtgtk_expander_get_expanded(DTGTK_EXPANDER(module->expander));
}

/* a header release over the arrow (or the presets button) must NOT toggle:
 * the header gesture sees the whole header, and its pick-based button
 * exclusion is what stops the arrow's claimed sequence from double-toggling
 * (the Session 17 bug) */
static void test_header_release_over_button_no_toggle(void)
{
  _bootstrap();
  dt_test_require_display();

  dt_lib_module_t *module = _module_new();
  GtkWidget *hevb = NULL;
  GtkWidget *win = _mapped_header(module, &hevb);
  GtkGestureClick *gesture = _dt_click_gesture(hevb);

  g_assert_true(_lib_expanded(module)); /* expanders start expanded */
  gdouble x, y;

  _center_inside(hevb, module->arrow, &x, &y);
  g_signal_emit_by_name(gesture, "released", 1, x, y);
  g_assert_true(_lib_expanded(module));

  _center_inside(hevb, module->presets_button, &x, &y);
  g_signal_emit_by_name(gesture, "released", 1, x, y);
  g_assert_true(_lib_expanded(module));

  _center_inside(hevb, module->reset_button, &x, &y);
  g_signal_emit_by_name(gesture, "released", 1, x, y);
  g_assert_true(_lib_expanded(module));

  _bootstrap_down();
  gtk_window_destroy(GTK_WINDOW(win));
}

/* a header release over the background reaches the toggle logic; with a
 * real event it carries button=PRIMARY and toggles exactly once (locked via
 * the core below, which is what the real release forwards to) */
static void test_header_release_over_background_toggles(void)
{
  _bootstrap();
  dt_test_require_display();

  dt_lib_module_t *module = _module_new();
  GtkWidget *hevb = NULL;
  GtkWidget *win = _mapped_header(module, &hevb);
  GtkGestureClick *gesture = _dt_click_gesture(hevb);

  g_assert_true(_lib_expanded(module));
  gdouble x, y;
  /* the middle of the header is the title/preset-label area, not a button;
   * the synthetic release carries no current button, so the handler's
   * forward is a no-op -- but the pick must NOT have excluded the point
   * (no early return), which the core call with the real button value
   * then proves toggles */
  _center_inside(hevb, dtgtk_expander_get_header(DTGTK_EXPANDER(module->expander)), &x, &y);
  g_signal_emit_by_name(gesture, "released", 1, x, y);
  g_assert_true(_lib_expanded(module));

  dt_lib_plugin_arrow_button_press(GDK_BUTTON_PRIMARY, 0, 1, module);
  g_assert_false(_lib_expanded(module));
  dt_lib_plugin_arrow_button_press(GDK_BUTTON_PRIMARY, 0, 1, module);
  g_assert_true(_lib_expanded(module));

  _bootstrap_down();
  gtk_window_destroy(GTK_WINDOW(win));
}

/* the arrow's click toggles the module.  Synthetic gesture emission cannot
 * populate gtk_gesture_single_get_current_button() (it reads 0), so the
 * toggle is driven through the plain-signature core the gesture wrapper
 * calls -- the same split as the bauhaus popup handlers (Session 11). */
static void test_arrow_press_toggles(void)
{
  _bootstrap();
  dt_test_require_display();

  dt_lib_module_t *module = _module_new();
  GtkWidget *hevb = NULL;
  GtkWidget *win = _mapped_header(module, &hevb);

  g_assert_true(_lib_expanded(module));
  dt_lib_plugin_arrow_button_press(GDK_BUTTON_PRIMARY, 0, 1, module);
  g_assert_false(_lib_expanded(module));
  dt_lib_plugin_arrow_button_press(GDK_BUTTON_PRIMARY, 0, 1, module);
  g_assert_true(_lib_expanded(module));

  _bootstrap_down();
  gtk_window_destroy(GTK_WINDOW(win));
}

/* the cancel bridge re-emits "released" with widget-local coordinates
 * (gtk_gesture_get_point), so a cancelled header press -- which is exactly
 * what an arrow click produces -- still respects the button exclusion.
 * Without points the re-emission falls back to (0,0), the header's top-left
 * corner, which sits on the arrow; the guard must keep that from toggling. */
static void test_header_cancel_no_toggle(void)
{
  _bootstrap();
  dt_test_require_display();

  dt_lib_module_t *module = _module_new();
  GtkWidget *hevb = NULL;
  GtkWidget *win = _mapped_header(module, &hevb);
  GtkGestureClick *gesture = _dt_click_gesture(hevb);

  g_assert_true(_lib_expanded(module));
  g_signal_emit_by_name(gesture, "cancel", NULL);
  g_assert_true(_lib_expanded(module));

  _bootstrap_down();
  gtk_window_destroy(GTK_WINDOW(win));
}

void dt_test_libheader_register(void)
{
  g_test_add_func("/gtk4/libheader/release-over-button-no-toggle", test_header_release_over_button_no_toggle);
  g_test_add_func("/gtk4/libheader/release-over-background-toggles", test_header_release_over_background_toggles);
  g_test_add_func("/gtk4/libheader/arrow-press-toggles", test_arrow_press_toggles);
  g_test_add_func("/gtk4/libheader/cancel-no-toggle", test_header_cancel_no_toggle);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
