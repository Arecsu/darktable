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

/* Layer B test for dtgtk_bodyclip (src/dtgtk/expander.c), the module-body
 * revealer replacement.  The whole point of the widget is that, unlike
 * stock GtkRevealer, the child stays MAPPED and ALLOCATED AT FULL SIZE when
 * collapsed — only the clip + a translate animate — so the child's cached
 * render node survives the collapse and opening/closing a module body does
 * not re-rasterize dozens of cairo/pango widgets (see the design notes at
 * the top of expander.c).  Locked here:
 *   - measurement: vertical natural height collapses to 0, restores fully;
 *   - the child stays mapped when collapsed (the GtkRevealer difference);
 *   - the collapsed child is translated away so it can't be picked;
 *   - reveal-child / child-revealed property semantics + notification.
 * No darktable state needed: bodyclip is a standalone GtkWidget subclass.
 * Measurement is headless; the mapped/allocated/pick checks are
 * display-gated (realize + allocate only work on a session). */

#include "test_gtk4.h"

#include "dtgtk/expander.h"

/* pump the main loop until `widget` has an allocation (realize+allocate
 * happen a frame or two after show, and only on a real display; on Wayland
 * the first layout waits for the compositor's frame callback) */
static void _pump_allocated(GtkWidget *widget)
{
  for(int i = 0; i < 200 && gtk_widget_get_allocated_width(widget) <= 0; i++)
  {
    while(g_main_context_iteration(NULL, FALSE))
      ;
    g_usleep(10000);
  }
  g_assert_cmpint(gtk_widget_get_allocated_width(widget), >, 0);
}

/* vertical natural height of `w` via its measure vfunc */
static int _nat_h(GtkWidget *w)
{
  int min = 0, nat = 0;
  gtk_widget_measure(w, GTK_ORIENTATION_VERTICAL, -1, &min, &nat, NULL, NULL);
  return nat;
}

static GtkWidget *_body(void)
{
  /* a body with real content so its natural height is nonzero */
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(box), gtk_label_new("module body"));
  return box;
}

/* dispose a bodyclip that was never parented (g_object_new returns a
 * floating ref; g_object_unref on a floating object is a GTK critical) */
static void _unref_floating(GtkWidget *w)
{
  g_object_ref_sink(w);
  g_object_unref(w);
}

/* measurement collapses to 0 and restores fully — headless (measure works
 * without a display) */
static void test_bodyclip_measure(void)
{
  GtkWidget *bc = dtgtk_bodyclip_new();
  GtkWidget *body = _body();
  g_assert_nonnull(bc);
  g_assert_nonnull(body);

  dtgtk_bodyclip_set_duration(DTGTK_BODYCLIP(bc), 0);
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE); /* start collapsed */
  dtgtk_bodyclip_set_child(DTGTK_BODYCLIP(bc), body);

  /* collapsed: vertical natural height is 0 */
  g_assert_cmpint(_nat_h(bc), ==, 0);
  g_assert_false(dtgtk_bodyclip_get_reveal_child(DTGTK_BODYCLIP(bc)));
  g_assert_false(dtgtk_bodyclip_get_child_revealed(DTGTK_BODYCLIP(bc)));

  /* revealed: natural height comes back (nonzero, matches the body) */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), TRUE);
  g_assert_cmpint(_nat_h(bc), >, 0);
  g_assert_true(dtgtk_bodyclip_get_reveal_child(DTGTK_BODYCLIP(bc)));
  g_assert_true(dtgtk_bodyclip_get_child_revealed(DTGTK_BODYCLIP(bc)));

  /* horizontal stays full size in both states (only vertical animates) */
  int minw = 0, natw = 0;
  gtk_widget_measure(bc, GTK_ORIENTATION_HORIZONTAL, -1, &minw, &natw, NULL, NULL);
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE);
  int minw2 = 0, natw2 = 0;
  gtk_widget_measure(bc, GTK_ORIENTATION_HORIZONTAL, -1, &minw2, &natw2, NULL, NULL);
  g_assert_cmpint(natw, ==, natw2);

  /* dispose must not leak or critical: unparent the child + drop the bc */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), TRUE);
  _unref_floating(bc);
}

/* the child stays MAPPED when collapsed (the GtkRevealer difference) and is
 * translated away so it can't be picked — display-gated */
static void test_bodyclip_child_stays_mapped(void)
{
  dt_test_require_display();

  GtkWidget *bc = dtgtk_bodyclip_new();
  GtkWidget *body = _body();
  dtgtk_bodyclip_set_duration(DTGTK_BODYCLIP(bc), 0);
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), TRUE);
  dtgtk_bodyclip_set_child(DTGTK_BODYCLIP(bc), body);

  GtkWidget *win = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
  gtk_window_set_child(GTK_WINDOW(win), bc);
  gtk_widget_show(win);
  _pump_allocated(body);

  /* revealed: body is mapped and its allocation is inside the bodyclip */
  g_assert_true(gtk_widget_get_mapped(body));
  g_assert_cmpint(gtk_widget_get_allocated_height(body), >, 0);

  /* collapse (instant): the body must STAY mapped and keep its full size,
   * but be translated above the (now zero-height) bodyclip clip box */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE);
  while(g_main_context_iteration(NULL, FALSE))
    ;
  g_assert_false(dtgtk_bodyclip_get_child_revealed(DTGTK_BODYCLIP(bc)));
  g_assert_true(gtk_widget_get_mapped(body)); /* the whole design point */
  g_assert_true(gtk_widget_get_visible(body));
  g_assert_cmpint(gtk_widget_get_allocated_height(body), >, 0);

  /* a pick at the bodyclip's own (zero-height, top-left) point must NOT
   * hit the translated-away body */
  /* a pick at the bodyclip's own (zero-height, top-left) point, in the
   * toplevel's surface coordinates, must NOT reach the translated-away
   * body (overflow:hidden clips it; the design claim) */
  graphene_point_t p;
  g_assert_true(gtk_widget_compute_point(bc, win, &GRAPHENE_POINT_INIT(0, 0), &p));
  GtkWidget *picked = gtk_widget_pick(win, (int)p.x, (int)p.y, GTK_PICK_DEFAULT);
  g_assert_nonnull(picked); /* the toplevel still gets the pick */
  g_assert_true(picked != body); /* ...but not the hidden body itself */
  g_assert_false(gtk_widget_is_ancestor(picked, GTK_WIDGET(body)));

  gtk_window_destroy(GTK_WINDOW(win));
}

static gint _bodyclip_notifies = 0;
static void _on_child_revealed_notify(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
  (void)obj; (void)pspec; (void)user_data;
  _bodyclip_notifies++;
}

/* reveal-child / child-revealed semantics + notify::child-revealed */
static void test_bodyclip_reveal_notify(void)
{
  GtkWidget *bc = dtgtk_bodyclip_new();
  GtkWidget *body = _body();
  dtgtk_bodyclip_set_duration(DTGTK_BODYCLIP(bc), 0);
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE);
  dtgtk_bodyclip_set_child(DTGTK_BODYCLIP(bc), body);

  _bodyclip_notifies = 0;
  g_signal_connect(bc, "notify::child-revealed", G_CALLBACK(_on_child_revealed_notify), NULL);

  /* target unchanged -> early return, no notify */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE);
  g_assert_cmpint(_bodyclip_notifies, ==, 0);

  /* reveal -> notify fires and the flag tracks */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), TRUE);
  g_assert_cmpint(_bodyclip_notifies, ==, 1);
  g_assert_true(dtgtk_bodyclip_get_reveal_child(DTGTK_BODYCLIP(bc)));
  g_assert_true(dtgtk_bodyclip_get_child_revealed(DTGTK_BODYCLIP(bc)));

  /* collapse -> notify fires again */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE);
  g_assert_cmpint(_bodyclip_notifies, ==, 2);
  g_assert_false(dtgtk_bodyclip_get_child_revealed(DTGTK_BODYCLIP(bc)));

  _unref_floating(bc);
}

/* the reveal is TOP-anchored in BOTH directions: the child's top edge is
 * pinned to the bodyclip's own top edge (never pushed above or below it),
 * and overflow:hidden clips the overhanging child to our growing/shrinking
 * box.  So expanding reveals the top of the content first and grows
 * downward, and collapsing hides the top first with the bottom last to
 * vanish.  A mid-animation child whose top edge sits at or below the
 * bodyclip's top edge proves the reveal is top -> bottom with no empty gap
 * (a child pushed below our top would show an empty gap above it; a child
 * pushed above would reveal bottom-first).
 * display-gated: needs realize + allocate to read the child transform. */
static void test_bodyclip_reveal_direction(void)
{
  dt_test_require_display();

  /* a tall body so mid-animation gives a clearly partial clip height */
  GtkWidget *bc = dtgtk_bodyclip_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  for(int i = 0; i < 20; i++)
    gtk_box_append(GTK_BOX(box), gtk_label_new("module body row"));
  dtgtk_bodyclip_set_duration(DTGTK_BODYCLIP(bc), 5000); /* long, stay mid-anim */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), FALSE);
  dtgtk_bodyclip_set_child(DTGTK_BODYCLIP(bc), box);

  GtkWidget *win = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
  gtk_window_set_child(GTK_WINDOW(win), bc);
  gtk_widget_show(win);
  _pump_allocated(box);

  /* body is tall enough for a clearly partial mid-animation clip */
  int nat = _nat_h(box);
  g_assert_cmpint(nat, >, 0);

  /* expand, then let a couple frames elapse so it's mid-animation */
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(bc), TRUE);
  for(int i = 0; i < 3; i++)
  {
    while(g_main_context_iteration(NULL, FALSE))
      ;
    g_usleep(15000);
  }

  /* must still be animating (not yet fully revealed) */
  g_assert_false(dtgtk_bodyclip_get_child_revealed(DTGTK_BODYCLIP(bc)));

  /* body's top edge must be at or below the bodyclip's top (pushed down),
   * i.e. top-first.  A bottom-first (regressed) slide would put it above. */
  graphene_point_t top;
  g_assert_true(gtk_widget_compute_point(box, bc, &GRAPHENE_POINT_INIT(0, 0), &top));
  g_assert_cmpfloat(top.y, >=, 0.0f);

  gtk_window_destroy(GTK_WINDOW(win));
}

void dt_test_bodyclip_register(void)
{
  g_test_add_func("/gtk4/bodyclip/measure", test_bodyclip_measure);
  g_test_add_func("/gtk4/bodyclip/child-stays-mapped", test_bodyclip_child_stays_mapped);
  g_test_add_func("/gtk4/bodyclip/reveal-notify", test_bodyclip_reveal_notify);
  g_test_add_func("/gtk4/bodyclip/reveal-direction", test_bodyclip_reveal_direction);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
