/*
    This file is part of darktable,
    Copyright (C) 2024 darktable developers.

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

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* The style menu hierarchy is built as a popover menu (see the shared
 * dt_gui_menu_* layer in gui/gtk.h): items are GtkButtons, submenus are
 * popovers pointed at their triggering item.  Used by export.c,
 * print_settings.c and darkroom.c to pick a style by name. */

typedef struct {
  gchar *name;
  gpointer user_data;
} dt_stylemenu_data_t;

typedef void dtgtk_menuitem_activate_callback_fn(GtkWidget *menuitem,
                                                 const dt_stylemenu_data_t *menu_data);
typedef void dtgtk_menuitem_button_callback_fn(GtkGestureSingle *gesture,
                                               gint n_press,
                                               gdouble x,
                                               gdouble y,
                                               const dt_stylemenu_data_t *menu_data);

/* builds the full style hierarchy (group|sub|preset paths) as a popover
 * menu and returns it; NULL when there are no styles and @allow_none is
 * FALSE.  The caller pops it up with dt_gui_menu_popup(). */
GtkWidget *dtgtk_build_style_menu_hierarchy(gboolean allow_none,
                                            dtgtk_menuitem_activate_callback_fn *activate_callback,
                                            dtgtk_menuitem_button_callback_fn *button_callback,
                                            gpointer user_data);

G_END_DECLS

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
