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

/* Gesture-claim wiring (PR #21787 audit leftovers A2.10/A2.11, Session 6):
 * the capture-phase "pressed"-time claim (dt_gui_gesture_claim_pressed),
 * the any-button (button=0) requirement that kept right/middle clicks
 * alive after GtkGestureMultiPress became GtkGestureClick, the
 * DT_ACTION_GESTURE_KEY shortcut seam, and the cancel → released
 * re-emission.  All headless: widget construction + signal emission need
 * no display (see agent-docs/TEST.md).  Gesture *sequence state* (what
 * gtk_gesture_single_get_current_sequence() reports) is not populated by
 * synthetic emission, so the claim itself is asserted as a safe no-op --
 * the regression locked here is the wiring around it.
 *
 * togglebutton-capture-claim locks the REAL widget topology the port
 * produces today (verified against /tmp/gtk4 sources + a controller-dump
 * probe, Session 13): BOTH capture-phase GtkGestureClicks are on the
 * BUTTON, ours FIRST (gtk_widget_add_controller PREPENDS), the button's
 * internal one GDK_BUTTON_PRIMARY-only.  The claim's suppression of the
 * button's own "clicked" does NOT work with this ordering (the claim
 * cannot reach a same-widget gesture that hasn't processed the press
 * yet) -- the fix (attach the claim gesture to the button's CHILD in
 * capture phase, so the button's gesture has the point when the claim
 * cancels it) must UPDATE these assertions. */

#include "test_gtk4.h"

#include "common/darktable.h"
#include "control/conf.h"
#include "control/control.h"
#include "develop/imageop.h"
#include "develop/imageop_gui.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"

typedef struct click_data_t
{
  int pressed, released;
} click_data_t;

static void _on_pressed(GtkGestureSingle *gesture, int n_press, double x, double y, click_data_t *d)
{
  d->pressed++;
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
}

static void _on_released(GtkGestureSingle *gesture, int n_press, double x, double y, click_data_t *d)
{
  d->released++;
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
}

/* ---- A2.10: the togglebutton site (dt_iop_togglebutton_new) ---- */

static gboolean bootstrapped = FALSE;
static dt_iop_module_so_t _so; /* zeroed static: so->actions is the action root */

static void _bootstrap(void)
{
  if(bootstrapped) return;
  bootstrapped = TRUE;

  darktable.conf = g_new0(dt_conf_t, 1);
  darktable.conf->table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->override_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  darktable.conf->x_confgen = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
  dt_pthread_mutex_init(&darktable.conf->mutex, NULL);

  /* dt_action_define_iop() registers the widget action: the defs array
   * (for the DT_ACTION_TYPE_WIDGET numbering) and the shortcuts sequence
   * (dt_action_define_fallback() inserts the toggle's three fallback
   * shortcuts into it -- dt_accel_init() creates it exactly so) */
  darktable.control = g_new0(dt_control_t, 1);
  darktable.control->widget_definitions = g_ptr_array_new_with_free_func(NULL);
  darktable.control->shortcuts = g_sequence_new(g_free);
}

static dt_iop_module_t *_module_new(void)
{
  dt_iop_module_t *module = g_new0(dt_iop_module_t, 1);
  g_strlcpy(module->op, "testgest", sizeof(module->op));
  module->so = &_so;
  return module;
}

static int _toggle_presses = 0;

static void _toggle_pressed(GtkGestureSingle *gesture, int n_press, double x, double y, gpointer user_data)
{
  _toggle_presses++;
  g_assert_true(user_data != NULL);
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
}

static void test_togglebutton_capture_claim(void)
{
  _bootstrap();
  _toggle_presses = 0;

  dt_iop_module_t *module = _module_new();
  GtkWidget *w = dt_iop_togglebutton_new(module, NULL, "toggle test", NULL,
                                         G_CALLBACK(_toggle_pressed), FALSE, 0, 0, NULL, NULL);
  g_assert_nonnull(w);

  /* The button carries TWO CAPTURE-phase GtkGestureClick controllers:
   * GtkButton's own (created in gtk_button_init, GDK_BUTTON_PRIMARY-only)
   * and ours (added afterwards, any button).  Both on the BUTTON, both
   * CAPTURE -- the "bubble-phase internal gesture" the A2.10 comment was
   * written against is a GTK3 memory.  gtk_widget_add_controller()
   * PREPENDS, so our gesture sits BEFORE the button's in the controller
   * list and dispatches FIRST (Session 13: this is exactly why the
   * claim-at-pressed cannot suppress the button's own "clicked" -- the
   * claim reaches only gestures that already have the sequence's point,
   * and the button's gesture processes the press after ours). */
  GtkGesture *ours = NULL;
  GtkGesture *button_gesture = NULL;
  guint ours_index = G_MAXUINT, button_index = G_MAXUINT;
  GListModel *controllers = gtk_widget_observe_controllers(w);
  const guint n = g_list_model_get_n_items(controllers);
  for(guint i = 0; i < n; i++)
  {
    gpointer c = g_list_model_get_item(controllers, i);
    if(GTK_IS_GESTURE_CLICK(c)
       && gtk_event_controller_get_propagation_phase(GTK_EVENT_CONTROLLER(c))
            == GTK_PHASE_CAPTURE)
    {
      if(gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(c)) == 0)
      {
        ours = c; /* keep the item ref for the emission below */
        ours_index = i;
      }
      else
      {
        button_gesture = c; /* keep the item ref */
        button_index = i;
      }
      continue; /* refs kept: unref below */
    }
    g_object_unref(c);
  }
  g_object_unref(controllers);

  g_assert_nonnull(ours);
  g_assert_nonnull(button_gesture);
  /* ours dispatches BEFORE the button's internal gesture (prepend order).
   * Locked here because it is the current-broken-state's defining detail:
   * with this ordering the claim cannot reach the button's gesture (no
   * point yet), so GtkButton still emits "clicked" and toggles behind
   * darktable's callback.  The fix moves our gesture onto the button's
   * CHILD (capture phase), where the button's gesture has already
   * processed the press when the claim cancels it -- then this order
   * assertion inverts (ours lives on a different widget entirely). */
  g_assert_cmpuint(ours_index, <, button_index);
  /* ours listens to ANY button (the old button-press-event handlers
   * reacted to every button; GtkGestureSingle's default primary-button
   * filter would silently drop right/middle clicks) and is stored under
   * DT_ACTION_GESTURE_KEY so shortcut activation routes through it
   * (_action_process_toggle) */
  g_assert_cmpuint(gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(ours)), ==, 0);
  g_assert_cmpuint(gtk_gesture_single_get_button(GTK_GESTURE_SINGLE(button_gesture)),
                   ==, GDK_BUTTON_PRIMARY);
  g_assert_true(g_object_get_data(G_OBJECT(w), DT_ACTION_GESTURE_KEY) == ours);

  /* synthetic "pressed": the claim handler runs first (no current
   * sequence under synthetic emission -> safe no-op), then the module
   * callback; the data wiring (user_data == module) is asserted in the
   * callback */
  g_signal_emit_by_name(G_OBJECT(ours), "pressed", 1, (gdouble)3.0, (gdouble)4.0, NULL);
  g_assert_cmpint(_toggle_presses, ==, 1);

  /* the widget action was registered on the module (dt_action_define_iop
   * appended the referral to widget_list) */
  g_assert_nonnull(module->widget_list);
  const dt_action_target_t *referral = module->widget_list->data;
  g_assert_true((GtkWidget *)referral->target == w);

  g_object_unref(ours);
  g_object_unref(button_gesture);
  gtk_widget_destroy(w);
  g_slist_free_full(module->widget_list, g_free);
  g_free(module);
}

static void test_claim_pressed_null_sequence(void)
{
  /* the claim handler on a bare gesture with no current sequence (the
   * synthetic-emission case, and the "forwarded"/programmatic paths):
   * must run and do nothing -- no critical, no state change */
  click_data_t d = { 0, 0 };
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkGestureSingle *g = dt_gui_connect_click(w, _on_pressed, _on_released, &d);
  g_signal_emit_by_name(G_OBJECT(g), "pressed", 1, (gdouble)1.0, (gdouble)2.0, NULL);
  g_assert_cmpint(d.pressed, ==, 1);
  g_assert_cmpint(d.released, ==, 0);
  gtk_widget_destroy(w);
}

static void test_click_secondary_key(void)
{
  /* the right-click idiom helper: button filter set to the secondary
   * button (the handler needs no button check) and the gesture stored
   * under DT_ACTION_GESTURE_KEY for the right-click shortcut effects */
  click_data_t d = { 0, 0 };
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkGestureSingle *g = dt_gui_connect_click_secondary(w, _on_pressed, _on_released, &d);
  g_assert_cmpuint(gtk_gesture_single_get_button(g), ==, GDK_BUTTON_SECONDARY);
  g_assert_true(g_object_get_data(G_OBJECT(w), DT_ACTION_GESTURE_KEY) == (gpointer)g);

  g_signal_emit_by_name(G_OBJECT(g), "pressed", 1, (gdouble)5.0, (gdouble)6.0, NULL);
  g_assert_cmpint(d.pressed, ==, 1);
  gtk_widget_destroy(w);
}

/* ---- A2.11: cancel re-emits released with the last event's position ---- */

static void test_cancel_reemits_released(void)
{
  /* dt_gui_connect_click wires "cancel" so a grabbed gesture that is
   * cancelled still delivers the release (handlers that clean up
   * button-held state on "released" must not miss it).  Under synthetic
   * emission there is no last event, so the re-emission falls back to the
   * gesture's top-left corner (0,0) instead of never firing -- and the
   * wiring is what this locks. */
  click_data_t d = { 0, 0 };
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkGestureSingle *g = dt_gui_connect_click(w, _on_pressed, _on_released, &d);

  g_signal_emit_by_name(G_OBJECT(g), "cancel", NULL);
  g_assert_cmpint(d.released, ==, 1); /* exactly one re-emission */
  g_assert_cmpint(d.pressed, ==, 0);  /* cancel is not a press */

  /* a normal release still delivers afterwards */
  g_signal_emit_by_name(G_OBJECT(g), "released", 1, (gdouble)2.0, (gdouble)3.0, NULL);
  g_assert_cmpint(d.released, ==, 2);
  gtk_widget_destroy(w);
}

void dt_test_gesture_register(void)
{
  g_test_add_func("/gtk4/gesture/claim-pressed-null-sequence", test_claim_pressed_null_sequence);
  g_test_add_func("/gtk4/gesture/togglebutton-capture-claim", test_togglebutton_capture_claim);
  g_test_add_func("/gtk4/gesture/click-secondary-key", test_click_secondary_key);
  g_test_add_func("/gtk4/gesture/cancel-reemits-released", test_cancel_reemits_released);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
