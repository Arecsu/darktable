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

/* Controller/gesture wiring (src/gui/gtk.c): the port's riskiest surface.
 * No input synthesis exists in GTK4, so we drive the HANDLERS by emitting
 * the controller/gesture signals in-process with synthetic values (see
 * agent-docs/TEST.md).  Headless: signal emission needs no display.
 *
 * Locked here: the scroll-proxy gboolean return (the ::scroll signal is
 * G_TYPE_BOOLEAN; a void handler connected raw returns garbage — Session 6
 * moved the proxies to return `handled`) and the per-controller discrete
 * step accumulation (A2.5). */

#include "test_gtk4.h"

#include "gui/gtk.h"

typedef struct scroll_data_t
{
  int calls;
  double last_dx, last_dy;
  gboolean ok; /* set by handlers; catches garbage-pointer writes */
} scroll_data_t;

static void on_scroll(GtkEventControllerScroll *controller,
                      gdouble dx,
                      gdouble dy,
                      scroll_data_t *s)
{
  s->calls++;
  s->last_dx = dx;
  s->last_dy = dy;
  (void)controller;
}

static void test_scroll_proxy_dispatch(void)
{
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  scroll_data_t s = { 0, 0, 0, TRUE };

  GtkEventController *c = dt_gui_connect_scroll(
      w, GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES, on_scroll, &s);
  g_assert_true(GTK_IS_EVENT_CONTROLLER(c));

  /* synthetic emission has no current event on the controller, which is
   * exactly the "forwarded" path dt_gui_forward_scroll() uses: the proxy
   * skips the event checks and passes the deltas straight through */
  gboolean ret = FALSE;
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)5.0, &ret);
  g_assert_true(ret); /* the marshaller must see the proxy's TRUE */
  g_assert_cmpint(s.calls, ==, 1);
  g_assert_cmpfloat(s.last_dx, ==, 0.0);
  g_assert_cmpfloat(s.last_dy, ==, 5.0);

  /* zero deltas are not a scroll: handler skipped, FALSE returned */
  ret = TRUE;
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)0.0, &ret);
  g_assert_false(ret);
  g_assert_cmpint(s.calls, ==, 1);
}

static void test_scroll_discrete_accumulation(void)
{
  /* A2.5: discrete proxies accumulate per-controller remainders and only
   * emit whole unit steps; a real wheel notch arrives as a unit, a smooth
   * trackpad event as a fraction that must NOT be forwarded until it adds
   * up (state lives on the controller, not in a file static) */
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  scroll_data_t s = { 0, 0, 0, TRUE };

  GtkEventController *c = dt_gui_connect_scroll(
      w, GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE,
      on_scroll, &s);

  /* 0.4 + 0.4 = 0.8 < 1: no step yet */
  gboolean ret = FALSE;
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)0.4, &ret);
  g_assert_false(ret);
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)0.4, &ret);
  g_assert_false(ret);
  g_assert_cmpint(s.calls, ==, 0);

  /* 0.8 + 0.4 = 1.2: one step, 0.2 remainder */
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)0.4, &ret);
  g_assert_true(ret);
  g_assert_cmpint(s.calls, ==, 1);
  g_assert_cmpfloat(s.last_dx, ==, 0.0);
  g_assert_cmpfloat(s.last_dy, ==, 1.0);

  /* a whole notch passes through as a unit immediately */
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)1.0, &ret);
  g_assert_true(ret);
  g_assert_cmpint(s.calls, ==, 2);
  g_assert_cmpfloat(s.last_dy, ==, 1.0);

  /* the remainder carries over between emissions (per-controller state) */
  g_signal_emit_by_name(c, "scroll", (gdouble)0.0, (gdouble)0.9, &ret);
  g_assert_true(ret); /* 0.2 + 0.9 = 1.1 -> one step */
  g_assert_cmpint(s.calls, ==, 3);
  g_assert_cmpfloat(s.last_dy, ==, 1.0);

  /* a second controller accumulates independently */
  scroll_data_t s2 = { 0, 0, 0, TRUE };
  GtkEventController *c2 = dt_gui_connect_scroll(
      w, GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE,
      on_scroll, &s2);
  g_signal_emit_by_name(c2, "scroll", (gdouble)0.0, (gdouble)0.4, &ret);
  g_assert_false(ret);
  g_assert_cmpint(s.calls, ==, 3); /* first controller untouched */
}

typedef struct click_data_t
{
  int pressed, released;
  int n_press;
  double x, y;
} click_data_t;

static void on_gesture_pressed(GtkGestureSingle *gesture,
                               int n_press,
                               double x,
                               double y,
                               click_data_t *d)
{
  d->pressed++;
  d->n_press = n_press;
  d->x = x;
  d->y = y;
  (void)gesture;
}

static void on_gesture_released(GtkGestureSingle *gesture,
                                int n_press,
                                double x,
                                double y,
                                click_data_t *d)
{
  d->released++;
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
}

static void test_click_gesture_wiring(void)
{
  click_data_t d = { 0, 0, 0, 0, 0 };
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  GtkGestureSingle *g = dt_gui_connect_click(w, on_gesture_pressed, on_gesture_released, &d);
  (void)g;

  g_signal_emit_by_name(G_OBJECT(g), "pressed", 1, (gdouble)3.0, (gdouble)4.0, NULL);
  g_assert_cmpint(d.pressed, ==, 1);
  g_assert_cmpint(d.n_press, ==, 1);
  g_assert_cmpfloat(d.x, ==, 3.0);
  g_assert_cmpfloat(d.y, ==, 4.0);

  g_signal_emit_by_name(G_OBJECT(g), "released", 1, (gdouble)3.0, (gdouble)4.0, NULL);
  g_assert_cmpint(d.released, ==, 1);
}

void dt_test_controller_register(void)
{
  g_test_add_func("/gtk4/controller/scroll-proxy-dispatch", test_scroll_proxy_dispatch);
  g_test_add_func("/gtk4/controller/scroll-discrete-accumulation",
                  test_scroll_discrete_accumulation);
  g_test_add_func("/gtk4/controller/click-gesture-wiring", test_click_gesture_wiring);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
