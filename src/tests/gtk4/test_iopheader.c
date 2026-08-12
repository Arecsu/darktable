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
 *   [off | icon | lab | instance | space | multi | reset | presets]
 *
 * where `space` is the invisible hexpanding spacer that reproduces GTK3
 * pack_end()'s structural right-alignment of the end group: it is created
 * in EVERY hide_header_buttons config (always/dim/active and the dynamic
 * glide family alike) and must land between the instance name and the
 * multi-instance button (gtk_box_reorder_child_before).  In dynamic
 * configs it doubles as the width trigger for the glide reveal.
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
#include "develop/develop.h"
#include "develop/imageop.h"
#include "dtgtk/expander.h"
#include "gui/gtk.h"
#include "libs/lib.h"
#include "views/view.h"

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

/* the focus path calls module->version() (dt_get_active_preset_name query)
 * and module->operation_tags_filter() (pixelpipe rebuild gate) */
static int _module_version(void)
{
  return 0;
}

static int _module_operation_tags_filter(void)
{
  return 0;
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
  /* the mapped-window tests draw the dtgtk buttons, which create cairo
   * surfaces scaled by ppd (0 -> 0x0 surface -> cairo assert) */
  darktable.gui->dpi = 96.0;
  darktable.gui->ppd = 1.0;
  darktable.gui->ui->containers[DT_UI_CONTAINER_PANEL_RIGHT_CENTER] =
    gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  darktable.control = g_new0(dt_control_t, 1);
  darktable.control->widget_definitions = g_ptr_array_new_with_free_func(NULL);

  /* toggle tests run the full header-release handler, which reaches
   * dt_iop_request_focus(): it needs a develop/lib/view_manager shell. */
  darktable.develop = g_new0(dt_develop_t, 1);
  darktable.develop->full.pipe = g_new0(dt_dev_pixelpipe_t, 1);
  darktable.develop->preview_pipe = g_new0(dt_dev_pixelpipe_t, 1);
  darktable.develop->preview2.pipe = g_new0(dt_dev_pixelpipe_t, 1);
  darktable.lib = g_new0(dt_lib_t, 1);
  darktable.view_manager = g_new0(dt_view_manager_t, 1);
  darktable.view_manager->guides_toggle = gtk_toggle_button_new();
  /* dt_ui_container_focus_widget / gtk_widget_grab_focus need real targets */
  for(int k = 0; k < DT_UI_CONTAINER_SIZE; k++)
    darktable.gui->ui->containers[k] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  darktable.gui->ui->center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
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
  module->version = _module_version;
  module->operation_tags_filter = _module_operation_tags_filter;
  module->so = &_so;
  module->dev = g_new0(dt_develop_t, 1);
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

  /* the visual order the GTK3 pack_end/pack_start mix produced, plus the
   * right-align spacer: [off | icon | lab | instance | space | multi |
   * reset | presets].  The spacer is created unconditionally now, also in
   * always/dim/active, so the end group hugs the far right in every
   * config (GTK4 has no pack_end; a hexpanding spacer is the idiomatic
   * replacement). */
  GtkWidget *children[8] = { NULL };
  int n = 0;
  for(GtkWidget *c = gtk_widget_get_first_child(header); c;
      c = gtk_widget_get_next_sibling(c))
  {
    g_assert_cmpint(n, <, 8);
    children[n++] = c;
  }
  g_assert_cmpint(n, ==, 8);

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

  /* the right-align spacer: invisible drawing area, hexpanding, between
   * the instance name and the multi-instance button */
  g_assert_true(GTK_IS_DRAWING_AREA(children[4]));
  g_assert_true(gtk_widget_get_hexpand(children[4]));
  g_assert_false(gtk_widget_get_vexpand(children[4]));
  g_assert_true(gtk_widget_get_visible(children[4]));

  /* the right-side group: multi, reset, presets (GTK3 pack_end visual
   * order, first packed = rightmost) */
  g_assert_true(children[5] == module->multimenu_button);
  g_assert_true(children[6] == module->reset_button);
  g_assert_true(children[7] == module->presets_button);
  for(int i = 5; i < 8; i++)
    g_assert_true(GTK_IS_BUTTON(children[i]));

  /* "always": every child is visible right after construction */
  for(int i = 0; i < 8; i++)
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

static void test_header_release_buttons_no_toggle(void);

void dt_test_iopheader_register(void)
{
  g_test_add_func("/gtk4/iopheader/order", test_header_order);
  g_test_add_func("/gtk4/iopheader/width-trigger", test_header_width_trigger);
  g_test_add_func("/gtk4/iopheader/show-hide", test_header_show_hide);
  g_test_add_func("/gtk4/iopheader/release-over-button", test_header_release_buttons_no_toggle);
}

/* ---- header click handling (Session 17) ----
 *
 * The header release handler must ignore releases over its child buttons
 * (off / multi / reset / presets) and toggle the module only for clicks on
 * the header itself.  gtk_widget_pick() interprets the released (x, y) in
 * the header's OWN coordinate space -- the same space the real
 * GtkGestureClick::released signal uses -- so the tests feed coordinates
 * computed via gtk_widget_compute_bounds(), exactly like the app does. */

static void test_header_release_buttons_no_toggle(void);

static void _pump_allocated(GtkWidget *widget)
{
  for(int i = 0; i < 200 && gtk_widget_get_allocated_width(widget) <= 0; i++)
  {
    while(g_main_context_iteration(NULL, FALSE));
    g_usleep(10000);
  }
  g_assert_cmpint(gtk_widget_get_allocated_width(widget), >, 0);
}

/* the GtkGestureClick dt_iop_gui_set_expander() attaches to the header
 * event box (button=0, released connected directly) */
static GtkGestureClick *_header_gesture(dt_iop_module_t *module)
{
  GtkWidget *hevb = dtgtk_expander_get_header_event_box(DTGTK_EXPANDER(module->expander));
  GListModel *controllers = gtk_widget_observe_controllers(hevb);
  GtkGestureClick *found = NULL;
  for(guint i = 0; i < g_list_model_get_n_items(controllers); i++)
  {
    GtkEventController *c = g_list_model_get_item(controllers, i);
    if(GTK_IS_GESTURE_CLICK(c))
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

/* the expander must be mapped for gtk_widget_pick() to find the buttons:
 * host the right-center container box in a window and pump until the
 * header has an allocation */
static GtkWidget *_mapped_header_window(dt_iop_module_t *module)
{
  GtkWidget *win = gtk_window_new();
  GtkWidget *box = darktable.gui->ui->containers[DT_UI_CONTAINER_PANEL_RIGHT_CENTER];
  g_assert_true(gtk_widget_get_parent(box) == NULL);
  gtk_window_set_child(GTK_WINDOW(win), box);
  gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
  gtk_widget_show(win);
  _pump_allocated(module->header);
  return win;
}

/* a header release over the header background would toggle the module --
 * the positive side is locked in test_libheader.c (the lib header shares
 * the release-handler pattern and needs no develop shell); here we lock
 * the negative side for all four iop header buttons.  If the guard is
 * broken the handler proceeds into the dev-dependent toggle and the
 * missing darktable.db trips the SQL assert -- the test then fails. */
static void test_header_release_buttons_no_toggle(void)
{
  _bootstrap();
  dt_conf_set_string("darkroom/ui/hide_header_buttons", "always");
  dt_test_require_display();

  dt_iop_module_t *module = _module_new();
  dt_iop_gui_set_expander(module);
  GtkWidget *win = _mapped_header_window(module);
  GtkWidget *hevb = dtgtk_expander_get_header_event_box(DTGTK_EXPANDER(module->expander));
  GtkGestureClick *gesture = _header_gesture(module);

  /* a release over any header button must be ignored by the header's
   * toggle (the button's own gesture handles it) -- otherwise one click
   * would toggle the module AND activate the button */
  GtkWidget *buttons[] = { module->off, module->multimenu_button,
                           module->reset_button, module->presets_button };
  for(guint i = 0; i < G_N_ELEMENTS(buttons); i++)
  {
    gdouble x, y;
    _center_inside(hevb, buttons[i], &x, &y);
    g_signal_emit_by_name(gesture, "released", 1, x, y);
    g_assert_false(module->expanded);
    /* the expander starts expanded; a release over a button must leave it */
    g_assert_true(dtgtk_expander_get_expanded(DTGTK_EXPANDER(module->expander)));
  }

  gtk_window_destroy(GTK_WINDOW(win));
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
