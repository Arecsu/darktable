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

/* View-manager input path (src/views/view.c): the main window is created
 * by dt_gui_gtk_init() before darktable.view_manager exists (dt_init
 * creates it later), so any click/scroll/gesture/close received while the
 * window is up during startup used to crash in vm->current_view /
 * vm->proxy.  The handlers must be no-ops while vm is NULL.
 * Locked here: every input-reachable entry point called with vm == NULL.
 * Headless: these are pure guard checks, no darktable state needed. */

#include "test_gtk4.h"

#include "common/darktable.h"
#include "gui/gtk.h"
#include "views/view.h"

static void test_view_manager_null_vm(void)
{
  /* startup: view_manager is NULL until dt_init creates it; every entry
   * point reachable from window input must be a safe no-op then */

  /* click / release */
  g_assert_cmpint(dt_view_manager_button_pressed(NULL, 1.0, 2.0, 1.0, 1,
                                                 GDK_BUTTON_PRESS, 0), ==, 0);
  g_assert_cmpint(dt_view_manager_button_released(NULL, 1.0, 2.0, 1, 0), ==, 0);

  /* scroll / gestures */
  dt_view_manager_scrolled(NULL, 0.0, 0.0, 1, 0); /* must not crash */
  g_assert_false(dt_view_manager_gesture_pan(NULL, 0.0, 0.0, 1.0, 1.0, 0));
  g_assert_false(dt_view_manager_gesture_pinch(NULL, 0.0, 0.0, 1.0, 1.0, 0, 1.0, 0));

  /* pointer tracking */
  dt_view_manager_mouse_moved(NULL, 0.0, 0.0, 1.0, 1); /* must not crash */
  dt_view_manager_mouse_enter(NULL);                   /* must not crash */
  dt_view_manager_mouse_leave(NULL);                   /* must not crash */
  dt_view_manager_reset(NULL);                         /* must not crash */

  /* close-request path: _gui_quit_callback -> preview_state */
  g_assert_false(dt_view_lighttable_preview_state(NULL));
  dt_view_lighttable_set_preview_state(NULL, FALSE, FALSE, FALSE,
                                       DT_LIGHTTABLE_CULLING_RESTRICTION_AUTO);

  /* view lookup / switching (double-click -> dt_ctl_switch_mode) */
  g_assert_null(dt_view_manager_get_current_view(NULL));
  g_assert_false(dt_view_manager_switch(NULL, "lighttable"));

  /* draw / name (window is drawn while the view manager is still NULL;
   * the background paint needs darktable.gui->colors, which exists in the
   * app long before the view manager does) */
  g_assert_cmpstr(dt_view_manager_name(NULL), ==, "");
  dt_gui_gtk_t gui;
  memset(&gui, 0, sizeof(gui));
  darktable.gui = &gui;
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 16, 16);
  cairo_t *cr = cairo_create(surf);
  dt_view_manager_expose(NULL, cr, 16, 16, 0, 0); /* must not crash */
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
  darktable.gui = NULL;
}

void dt_test_view_register(void)
{
  g_test_add_func("/gtk4/view/manager-null-vm", test_view_manager_null_vm);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
