/*
    This file is part of darktable,
    Copyright (C) 2010-2026 darktable developers.

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
#include "common/gdk_event_utils.h"

#include "gui/gtk.h"
#include "dtgtk/resetlabel.h"

G_DEFINE_TYPE(GtkDarktableResetLabel, dtgtk_reset_label, GTK_TYPE_WIDGET);

static void _reset_label_callback(GtkGestureSingle *gesture,
                                   gint n_press,
                                   gdouble x,
                                   gdouble y,
                                   gpointer user_data);

static void _reset_label_measure(GtkWidget *widget,
                                 GtkOrientation orientation,
                                 int for_size,
                                 int *minimum,
                                 int *natural,
                                 int *minimum_baseline,
                                 int *natural_baseline)
{
  GtkWidget *child = gtk_widget_get_first_child(widget);
  if(child)
    gtk_widget_measure(child, orientation, for_size, minimum, natural,
                       minimum_baseline, natural_baseline);
  else
  {
    *minimum = 0;
    *natural = 0;
    if(minimum_baseline) *minimum_baseline = -1;
    if(natural_baseline) *natural_baseline = -1;
  }
}

static void _reset_label_size_allocate(GtkWidget *widget,
                                       int width,
                                       int height,
                                       int baseline)
{
  GtkWidget *child = gtk_widget_get_first_child(widget);
  if(child)
    gtk_widget_allocate(child, width, height, baseline, NULL);
}

static void dtgtk_reset_label_dispose(GObject *gobject)
{
  GtkDarktableResetLabel *label = DTGTK_RESET_LABEL(gobject);

  // GTK4: this class derives from GTK_TYPE_WIDGET (not a container), which
  // does not unparent its children in dispose; without this the child label
  // is still parented when we finalize ("Finalizing ... but it still has
  // children left").
  if(label->lb)
  {
    gtk_widget_unparent(GTK_WIDGET(label->lb));
    label->lb = NULL;
  }

  G_OBJECT_CLASS(dtgtk_reset_label_parent_class)->dispose(gobject);
}

static void dtgtk_reset_label_class_init(GtkDarktableResetLabelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  widget_class->measure = _reset_label_measure;
  widget_class->size_allocate = _reset_label_size_allocate;

  G_OBJECT_CLASS(klass)->dispose = dtgtk_reset_label_dispose;
}

static void dtgtk_reset_label_init(GtkDarktableResetLabel *label)
{
  label->lb = GTK_LABEL(gtk_label_new(NULL));
  gtk_widget_set_halign(GTK_WIDGET(label->lb), GTK_ALIGN_START);
  gtk_label_set_ellipsize(GTK_LABEL(label->lb), PANGO_ELLIPSIZE_END);
  gtk_widget_set_parent(GTK_WIDGET(label->lb), GTK_WIDGET(label));
  gtk_widget_set_tooltip_text(GTK_WIDGET(label), _("double-click to reset"));
  dt_gui_connect_click(label, _reset_label_callback, NULL, NULL);
}

static void _reset_label_callback(GtkGestureSingle *gesture,
                                   gint n_press,
                                   gdouble x,
                                   gdouble y,
                                   gpointer user_data)
{
  if(n_press >= 2)
  {
    GtkDarktableResetLabel *label = DTGTK_RESET_LABEL(dt_gui_get_widget(gesture));
    memcpy(((char *)label->module->params) + label->offset,
           ((char *)label->module->default_params) + label->offset, label->size);
    dt_iop_gui_update(label->module);
    dt_dev_add_history_item(darktable.develop, label->module, FALSE);
  }
}

// public functions
GtkWidget *dtgtk_reset_label_new(const gchar *text, dt_iop_module_t *module, void *param, int param_size)
{
  GtkDarktableResetLabel *label;
  label = g_object_new(dtgtk_reset_label_get_type(), NULL);
  label->module = module;
  label->offset = param - (void *)module->params;
  label->size = param_size;

  if(label->offset < 0 || label->offset + label->size > module->params_size)
  {
    label->offset = param - (void *)module->default_params;
    if(label->offset < 0 || label->offset + label->size > module->params_size)
        dt_print(DT_DEBUG_ALWAYS, "[dtgtk_reset_label_new] reference outside %s params", module->so->op);
  }

  gtk_label_set_text(GTK_LABEL(label->lb), text);

  return (GtkWidget *)label;
}

void dtgtk_reset_label_set_text(GtkDarktableResetLabel *label, const gchar *str)
{
  gtk_label_set_text(GTK_LABEL(label->lb), str);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
