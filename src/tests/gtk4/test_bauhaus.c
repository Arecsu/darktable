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

/* Layer B test for the bauhaus slider + popup machinery (src/bauhaus/bauhaus.c).
 *
 * Bauhaus widgets need darktable.bauhaus, which dt_bauhaus_init() builds;
 * that in turn needs three slices of darktable state: conf (for
 * bauhaus/condensed and friends), a ui with a main window (theme load,
 * popup transient) and control (actions_focus).  This bootstraps exactly
 * those three — NOT full dt_init() (no library, no plugins, no develop) —
 * and drives the popup through the public handler cores
 * dt_bauhaus_popup_{motion,button_press,button_release}(): synthetic
 * signal emission cannot populate gtk_gesture_single_get_current_button()
 * or the controller's current event, so the cores are the testable
 * surface (see agent-docs/TEST.md).
 *
 * Display-dependent: realize/show/size only work on the Wayland session
 * (see agent-docs/TEST.md); the popup window briefly appears on screen.
 *
 * Slider geometry facts the expected values below rely on (all from
 * bauhaus.c, locked by this test): the slider popup is a square (height ==
 * width); with the button held the whole popup is inside the tolerance
 * band, and a point near the top maps linearly — value = CLAMP(x, 0, 1)
 * quantized to the slider's digits — with x = (ex - padding.left) /
 * (width - quad_width - 4*INNER_PADDING).  INNER_PADDING is 4 because the
 * bootstrap pins bauhaus/condensed to FALSE. */

#include "test_gtk4.h"

#include "bauhaus/bauhaus.h"
#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "gui/gtk.h"

static GtkWidget *win = NULL;
static GtkWidget *slider = NULL;
static gboolean bootstrapped = FALSE;

static void _popup_close(void);

/* pump the main loop until the widget has an allocation (realize+allocate
 * happen a frame or two after show, and only on a real display; on Wayland
 * the first layout waits for the compositor's frame callback, so give the
 * loop some real time) */
static void _pump_allocated(GtkWidget *widget)
{
  for(int i = 0; i < 200 && gtk_widget_get_allocated_width(widget) <= 0; i++)
  {
    g_main_context_iteration(NULL, FALSE);
    g_usleep(10000);
  }
  if(gtk_widget_get_allocated_width(widget) <= 0)
    g_error("_pump_allocated: %s never allocated (w=%d h=%d mapped=%d visible=%d; "
            "win w=%d h=%d visible=%d; popup visible=%d; current=%p)",
            G_OBJECT_TYPE_NAME(widget), gtk_widget_get_allocated_width(widget),
            gtk_widget_get_allocated_height(widget), gtk_widget_get_mapped(widget),
            gtk_widget_get_visible(widget),
            gtk_widget_get_allocated_width(win), gtk_widget_get_allocated_height(win),
            gtk_widget_get_visible(win),
            gtk_widget_get_visible(darktable.bauhaus->popup.window),
            (void *)darktable.bauhaus->current);
  g_assert_cmpint(gtk_widget_get_allocated_width(widget), >, 0);
}

/* the conf/gui/ui/control state dt_bauhaus_init() reads; everything else
 * in darktable stays zeroed */
static void _bootstrap(void)
{
  dt_test_require_display();
  if(bootstrapped) return;
  bootstrapped = TRUE;

  darktable.conf = g_new0(dt_conf_t, 1);
  darktable.conf->table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->override_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->x_confgen = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  dt_pthread_mutex_init(&darktable.conf->mutex, NULL);
  /* deterministic metrics: INNER_PADDING = 4, quad_width = line_height */
  dt_conf_set_bool("bauhaus/condensed", FALSE);

  darktable.gui = g_new0(dt_gui_gtk_t, 1);
  win = gtk_window_new();
  darktable.gui->ui = dt_ui_new(win);
  darktable.gui->dpi = 96.0;
  darktable.gui->dpi_factor = 1.0;
  darktable.gui->ppd = 1.0;
  darktable.gui->scroll_mask = GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK;

  darktable.control = g_new0(dt_control_t, 1);
  darktable.control->widget_definitions = g_ptr_array_new_with_free_func(NULL);

  dt_bauhaus_init();
  g_assert_nonnull(darktable.bauhaus);
  g_assert_true(GTK_IS_WINDOW(darktable.bauhaus->popup.window));
}

/* a bare linear slider 0..1 default 0.5, no module attached */
static void _slider_new(void)
{
  _popup_close(); /* a previous test may have left the popup open; closing
                     first keeps bh->current valid while the old slider is
                     still alive */
  /* a fresh window per test: replacing the child of an already-shown window
   * left the new child unallocated (no relayout frame); creating the window
   * anew each time mirrors the plain show+pump pattern that always works */
  if(win) gtk_window_destroy(GTK_WINDOW(win));
  win = gtk_window_new();
  g_free(darktable.gui->ui);
  darktable.gui->ui = dt_ui_new(win);

  dt_bauhaus_widget_t *w = DT_BAUHAUS_WIDGET(g_object_new(DT_BAUHAUS_WIDGET_TYPE, NULL));
  slider = dt_bauhaus_slider_from_widget(w, NULL, 0.0f, 1.0f, 0.1f, 0.5f, 3, 1);
  g_assert_true(GTK_IS_WIDGET(slider));
  gtk_window_set_child(GTK_WINDOW(win), slider);
  gtk_widget_set_hexpand(slider, TRUE);
  gtk_widget_set_vexpand(slider, TRUE);
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 100);
  gtk_widget_queue_resize(win); /* replacing the child on a mapped window
                                   does not always queue a relayout */
  gtk_widget_show(win);
  _pump_allocated(slider);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, 0.5f);
}

static void _popup_open(void)
{
  dt_bauhaus_popup_t *pop = &darktable.bauhaus->popup;
  dt_bauhaus_widget_show_popup(slider);
  g_assert_true(darktable.bauhaus->current == DT_BAUHAUS_WIDGET(slider));
  g_assert_true(gtk_widget_get_visible(pop->window));
  _pump_allocated(pop->area);
  g_assert_cmpint(gtk_widget_get_allocated_width(pop->area), >, 0);
}

/* close an open popup through the reject path (restores the anchor value,
 * hides the window, clears current).  Tests must never end with the popup
 * open: a later slider destruction would leave bh->current dangling. */
static void _popup_close(void)
{
  dt_bauhaus_t *bh = darktable.bauhaus;
  if(!bh->current) return;
  dt_bauhaus_popup_motion(10000.0, 10000.0, 0);
  g_assert_null(bh->current);
}

/* expected value for a press/drag at (ex, ey) in the popup's top linear
 * band (see the geometry facts in the file comment).  The handler truncates
 * the coordinates to gint (root_x - allocation.x) before the linear math,
 * so the expectation must too. */
static float _expected_value(float ex, float ey)
{
  dt_bauhaus_t *bh = darktable.bauhaus;
  const GtkBorder *pad = &bh->popup.padding;
  const float width = gtk_widget_get_allocated_width(bh->popup.area) - pad->left - pad->right;
  const float quad = bh->quad_width + 4.0f * 4.0f; /* INNER_PADDING = 4 */
  const gint ex_i = (gint)ex;
  const gint ey_i = (gint)ey;
  (void)ey_i;
  const float x = (ex_i - pad->left) / (width - quad);
  return roundf(1000.0f * CLAMP(x, 0.0f, 1.0f)) / 1000.0f;
}

static void _on_value_changed(GtkWidget *widget, gpointer data)
{
  int *count = data;
  (*count)++;
  (void)widget;
}

static void test_slider_values(void)
{
  _bootstrap();
  _slider_new();

  int changed = 0;
  g_signal_connect(slider, "value-changed", G_CALLBACK(_on_value_changed), &changed);

  /* set clamps to [hard_min, hard_max] and quantizes to the digits */
  dt_bauhaus_slider_set(slider, 2.0f);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, 1.0f);
  dt_bauhaus_slider_set(slider, -1.0f);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, 0.0f);
  dt_bauhaus_slider_set(slider, 0.3333f);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, 0.333f);
  g_assert_cmpint(changed, >, 0);

  /* reset restores the default */
  dt_bauhaus_slider_set(slider, 0.1f);
  dt_bauhaus_widget_reset(slider);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, 0.5f);
}

static void test_popup_open(void)
{
  _bootstrap();
  _slider_new();
  dt_bauhaus_slider_set(slider, 0.5f);
  _popup_open();

  dt_bauhaus_t *bh = darktable.bauhaus;
  g_assert_cmpfloat(bh->popup.oldpos, ==, 0.5f); /* drag anchor = current pos */
  g_assert_false(bh->change_active);
  _popup_close();
}

static void test_popup_drag(void)
{
  _bootstrap();
  _slider_new();
  dt_bauhaus_slider_set(slider, 0.5f);
  _popup_open();

  dt_bauhaus_t *bh = darktable.bauhaus;
  const GtkBorder *pad = &bh->popup.padding;
  const float width = gtk_widget_get_allocated_width(bh->popup.area) - pad->left - pad->right;
  const float quad = bh->quad_width + 16.0f;
  const float ey = pad->top + 4.0f; /* top linear band, inside the BUTTON1 tolerance */

  /* primary press at 30% of the usable width -> exact linear mapping */
  const float ex1 = pad->left + 0.3f * (width - quad);
  dt_bauhaus_popup_button_press(GDK_BUTTON_PRIMARY, 0, ex1, ey, 0);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, _expected_value(ex1, ey));
  g_assert_true(bh->change_active);
  g_assert_true(gtk_widget_get_visible(bh->popup.window));

  /* drag to 70% -> value follows */
  const float ex2 = pad->left + 0.7f * (width - quad);
  dt_bauhaus_popup_motion(ex2, ey, GDK_BUTTON1_MASK);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, _expected_value(ex2, ey));
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), >, _expected_value(ex1, ey));

  /* release closes the popup */
  dt_bauhaus_popup_button_release(GDK_BUTTON_PRIMARY);
  g_assert_false(gtk_widget_get_visible(bh->popup.window));
  g_assert_null(bh->current);
}

static void test_popup_reject(void)
{
  _bootstrap();
  _slider_new();
  dt_bauhaus_t *bh = darktable.bauhaus;
  dt_bauhaus_slider_set(slider, 0.5f);
  _popup_open();
  const float oldpos = bh->popup.oldpos;

  const GtkBorder *pad = &bh->popup.padding;
  const float width = gtk_widget_get_allocated_width(bh->popup.area) - pad->left - pad->right;
  const float quad = bh->quad_width + 16.0f;
  const float ex = pad->left + 0.2f * (width - quad);
  const float ey = pad->top + 4.0f;

  /* drag to a new value first */
  dt_bauhaus_popup_button_press(GDK_BUTTON_PRIMARY, 0, ex, ey, 0);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, _expected_value(ex, ey));
  g_assert_true(gtk_widget_get_visible(bh->popup.window));

  /* a buttonless motion far outside the popup rejects: value restored to
   * the open-time anchor, popup hidden */
  dt_bauhaus_popup_motion(10000.0, 10000.0, 0);
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, oldpos);
  g_assert_false(gtk_widget_get_visible(bh->popup.window));
  g_assert_null(bh->current);

  /* reopening and pressing a non-primary button rejects too */
  _popup_open();
  dt_bauhaus_popup_button_press(GDK_BUTTON_SECONDARY, 0, ex, ey, 0);
  g_assert_false(gtk_widget_get_visible(bh->popup.window));
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, oldpos);
}

static void test_popup_motion_wiring(void)
{
  _bootstrap();
  _slider_new();
  dt_bauhaus_slider_set(slider, 0.5f);
  _popup_open();

  dt_bauhaus_t *bh = darktable.bauhaus;
  g_assert_cmpfloat(bh->mouse_x, ==, 0.0f);

  /* hover at the popup's middle row: inside the buttonless (50 px)
   * tolerance band */
  const float ex = 150.0f;
  const float ey = bh->popup.position.height;
  const float before = dt_bauhaus_slider_get(slider);

  /* the popup window's motion controller must route into the core (the
   * handler falls back to the signal's coordinates when there is no
   * current event, which is exactly the synthetic-emission case).  The
   * darktable controller is added after GTK's own internal hover
   * controller, so emitting on the first motion controller found would
   * hit the wrong one: emit on each motion controller until ours (the
   * one that tracks the popup's mouse position) reacts. */
  gboolean hit = FALSE;
  GListModel *controllers = gtk_widget_observe_controllers(bh->popup.window);
  const guint n = g_list_model_get_n_items(controllers);
  for(guint i = 0; i < n && !hit; i++)
  {
    GtkEventController *candidate = g_list_model_get_item(controllers, i);
    if(GTK_IS_EVENT_CONTROLLER_MOTION(candidate))
    {
      g_signal_emit_by_name(candidate, "motion", (gdouble)ex, (gdouble)ey, NULL);
      hit = bh->mouse_x != 0.0f;
    }
    g_object_unref(candidate);
  }
  g_assert_true(hit);

  g_assert_cmpfloat(bh->mouse_x, ==, ex - bh->popup.padding.left);
  /* buttonless hover must not change the value */
  g_assert_cmpfloat(dt_bauhaus_slider_get(slider), ==, before);
  _popup_close();
}

void dt_test_bauhaus_register(void)
{
  g_test_add_func("/gtk4/bauhaus/slider-values", test_slider_values);
  g_test_add_func("/gtk4/bauhaus/popup-open", test_popup_open);
  g_test_add_func("/gtk4/bauhaus/popup-drag", test_popup_drag);
  g_test_add_func("/gtk4/bauhaus/popup-reject", test_popup_reject);
  g_test_add_func("/gtk4/bauhaus/popup-motion-wiring", test_popup_motion_wiring);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
