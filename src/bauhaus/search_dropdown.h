#ifndef DT_SEARCH_DROPDOWN_H
#define DT_SEARCH_DROPDOWN_H

#include <gtk/gtk.h>

typedef enum
{
  DT_SEARCH_DROPDOWN_WIDTH_LIST = 0,
  DT_SEARCH_DROPDOWN_WIDTH_LIST_AND_DESC = 1
} dt_search_dropdown_width_mode_t;

GtkWidget *dt_search_dropdown_new(const char *label);
void dt_search_dropdown_set_width_strategy(GtkWidget *widget, dt_search_dropdown_width_mode_t mode);
void dt_search_dropdown_add_section(GtkWidget *widget, const char *label);
void dt_search_dropdown_add_entry(GtkWidget *widget, const char *name,
                                  const char *description, gpointer data);
void dt_search_dropdown_clear(GtkWidget *widget);
void dt_search_dropdown_set(GtkWidget *widget, int idx);
void dt_search_dropdown_set_by_data(GtkWidget *widget, gpointer data);
int dt_search_dropdown_get(GtkWidget *widget);
gpointer dt_search_dropdown_get_data(GtkWidget *widget);
void dt_search_dropdown_set_changed_callback(GtkWidget *widget,
    void (*cb)(GtkWidget *, gpointer), gpointer user_data);
void dt_search_dropdown_set_config_key(GtkWidget *widget, const char *key);

#endif
