#include "search_dropdown.h"
#include "common/darktable.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include <gdk/gdkkeysyms.h>
#include <pango/pangocairo.h>

#define MAX_POPUP_HEIGHT 500
#define LINE_HEIGHT 22
#define INNER_PAD 8

/* ──────────────────────────── entry data ──────────────────────────── */

typedef struct dt_search_entry_t
{
  char *name;
  char *description;
  gpointer data;
  gboolean is_section;
  gboolean visible;
} dt_search_entry_t;

/* ────────────────────────── widget instance ───────────────────────── */

typedef struct dt_search_dropdown_data_t
{
  GtkWidget *widget;       /* the event-box parent */
  GtkWidget *value_label;  /* shows current selection text */
  GtkWidget *label_widget; /* the static label ("film stock") */

  dt_search_entry_t *entries;
  int n_entries, cap;

  void (*changed)(GtkWidget *, gpointer);
  gpointer changed_data;

  /* popup */
  GtkWidget *popup;
  GtkWidget *search_entry;
  GtkWidget *desc_stack;
  GtkWidget *desc_sep;
  GtkWidget *list_da;
  GtkWidget *toggle;
  GtkWidget *scrolled;
  int active;
  int hovered;
  int preview;   /* -1 = no preview, use active; set during keyboard nav */

  gboolean desc_visible;
  char config_key[128];
  double click_x, click_y;  /* last click coordinates for popup anchor */
  dt_search_dropdown_width_mode_t width_mode;
} dt_search_dropdown_data_t;

static inline dt_search_dropdown_data_t *_data(GtkWidget *w)
{
  return (dt_search_dropdown_data_t *)g_object_get_data(G_OBJECT(w), "search-dd-data");
}

static gboolean _entry_has_visible_children(dt_search_dropdown_data_t *d, int si)
{
  if(!d->entries[si].is_section) return FALSE;
  for(int i = si + 1; i < d->n_entries && !d->entries[i].is_section; i++)
    if(d->entries[i].visible) return TRUE;
  return FALSE;
}

/* ──────────────── Pango rendering helpers ──────────────── */

static void _pango_draw(GtkStyleContext *context, cairo_t *cr, const char *text,
                        float x, float y, float max_w,
                        gboolean right_aligned, gboolean is_markup)
{
  PangoLayout *layout = pango_cairo_create_layout(cr);
  if(max_w > 0)
  {
    pango_layout_set_width(layout, (int)(PANGO_SCALE * max_w + 0.5f));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  }
  PangoFontDescription *font_desc = NULL;
  gtk_style_context_get(context, gtk_style_context_get_state(context),
                        "font", &font_desc, NULL);
  if(font_desc)
  {
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free(font_desc);
  }
  pango_cairo_context_set_resolution(pango_layout_get_context(layout), darktable.gui->dpi);

  if(is_markup)
    pango_layout_set_markup(layout, text, -1);
  else
    pango_layout_set_text(layout, text, -1);

  pango_cairo_update_layout(cr, layout);
  if(right_aligned)
  {
    int w;
    pango_layout_get_size(layout, &w, NULL);
    cairo_move_to(cr, x - w / (double)PANGO_SCALE, y);
  }
  else
    cairo_move_to(cr, x, y);
  pango_cairo_show_layout(cr, layout);
  g_object_unref(layout);
}

static void _set_color_from_rgba(cairo_t *cr, const GdkRGBA *c)
{
  cairo_set_source_rgba(cr, c->red, c->green, c->blue, c->alpha);
}

/* ──────────────── count visible items ──────────────── */

static int _n_visible(dt_search_dropdown_data_t *d)
{
  int n = 0;
  for(int i = 0; i < d->n_entries; i++)
  {
    if(!d->entries[i].visible) continue;
    if(d->entries[i].is_section && !_entry_has_visible_children(d, i)) continue;
    n++;
  }
  return n;
}

/* ──────────────── list drawing area ──────────────── */

static void _resolve_row_colors(GtkStyleContext *ctx, GdkRGBA *normal,
                                 GdkRGBA *hover, GdkRGBA *selected, GdkRGBA *insensitive)
{
  gtk_style_context_get_color(ctx, GTK_STATE_FLAG_NORMAL, normal);
  gtk_style_context_get_color(ctx, GTK_STATE_FLAG_INSENSITIVE, insensitive);

  /* Try named darktable theme colors; fall back to lightening the normal color */
  GdkRGBA c;
  if(gtk_style_context_lookup_color(ctx, "bauhaus_fg_hover", &c)) *hover = c;
  else {
    *hover = *normal;
    hover->red   = MIN(hover->red   * 1.25f, 1.0f);
    hover->green = MIN(hover->green * 1.25f, 1.0f);
    hover->blue  = MIN(hover->blue  * 1.25f, 1.0f);
  }
  if(gtk_style_context_lookup_color(ctx, "bauhaus_fg_selected", &c)) *selected = c;
  else {
    *selected = *normal;
    selected->red   = MIN(selected->red   * 1.08f, 1.0f);
    selected->green = MIN(selected->green * 1.08f, 1.0f);
    selected->blue  = MIN(selected->blue  * 1.08f, 1.0f);
  }
}

static gboolean _list_draw(GtkWidget *da, cairo_t *cr, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  GtkAllocation alloc;
  gtk_widget_get_allocation(da, &alloc);
  GtkStyleContext *context = gtk_widget_get_style_context(da);
  gtk_render_background(context, cr, 0, 0, alloc.width, alloc.height);

  GdkRGBA c_normal, c_hover, c_selected, c_insensitive;
  _resolve_row_colors(context, &c_normal, &c_hover, &c_selected, &c_insensitive);

  int y = 0;
  gboolean first_visible = TRUE;
  for(int i = 0; i < d->n_entries; i++)
  {
    dt_search_entry_t *e = &d->entries[i];
    if(!e->visible) continue;
    if(e->is_section && !_entry_has_visible_children(d, i)) continue;

    if(e->is_section)
    {
      if(!first_visible) y += 4;
      first_visible = FALSE;
      gchar *upper = g_utf8_strup(e->name, -1);
      gchar *markup = g_markup_printf_escaped("<span size=\"85%%\" letter_spacing=\"512\">%s</span>", upper);
      g_free(upper);
      GdkRGBA sc = { 0 };
      sc.red   = (c_normal.red   + c_insensitive.red)   / 2;
      sc.green = (c_normal.green + c_insensitive.green) / 2;
      sc.blue  = (c_normal.blue  + c_insensitive.blue)  / 2;
      sc.alpha = (c_normal.alpha + c_insensitive.alpha) / 2;
      _set_color_from_rgba(cr, &sc);
      _pango_draw(context, cr, markup, INNER_PAD, y,
                  alloc.width - INNER_PAD * 2, FALSE, TRUE);
      g_free(markup);
    }
    else
    {
      first_visible = FALSE;
      const gboolean is_hovered = (i == d->hovered);
      const gboolean is_active  = (i == d->active);
      GdkRGBA row_color;
      GtkStateFlags flags = GTK_STATE_FLAG_NORMAL;

      if(is_hovered && is_active) { row_color = c_hover;    flags |= GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_SELECTED; }
      else if(is_hovered)         { row_color = c_hover;    flags |= GTK_STATE_FLAG_PRELIGHT; }
      else if(is_active)          { row_color = c_selected; flags |= GTK_STATE_FLAG_SELECTED; }
      else                          row_color = c_normal;

      if(is_hovered || is_active)
      {
        gtk_style_context_save(context);
        gtk_style_context_set_state(context, flags);
        gtk_render_background(context, cr, 0, y, alloc.width, LINE_HEIGHT);
        gtk_style_context_restore(context);
      }

      _set_color_from_rgba(cr, &row_color);
      _pango_draw(context, cr, e->name, INNER_PAD, y,
                  alloc.width - INNER_PAD * 2, FALSE, FALSE);
    }
    y += LINE_HEIGHT;
  }
  return FALSE;
}



/* ──────────────── description stack helper ──────────────── */

static void _switch_desc(dt_search_dropdown_data_t *d)
{
  if(!d->desc_stack || !d->desc_visible) return;
  char n[16] = "_none";
  /* fallback to preview (keyboard nav) then active (committed) when nothing hovered */
  const int hidx = d->hovered >= 0 ? d->hovered
                : (d->preview >= 0   ? d->preview
                   : d->active);
  if(hidx >= 0)
  {
    char buf[16];
    snprintf(buf, sizeof(buf), "_e%d", hidx);
    if(gtk_stack_get_child_by_name(GTK_STACK(d->desc_stack), buf))
      memcpy(n, buf, sizeof(n));
  }
  gtk_stack_set_visible_child_name(GTK_STACK(d->desc_stack), n);
}

/* ──────────────── mouse tracking ──────────────── */

static int _entry_at_y(dt_search_dropdown_data_t *d, double abs_y)
{
  int idx = (int)(abs_y / LINE_HEIGHT);
  int nv = 0;
  for(int i = 0; i < d->n_entries; i++)
  {
    if(!d->entries[i].visible) continue;
    if(d->entries[i].is_section && !_entry_has_visible_children(d, i)) continue;
    if(nv == idx) return d->entries[i].is_section ? -1 : i;
    nv++;
  }
  return -1;
}

static gboolean _list_motion(GtkWidget *da, GdkEventMotion *event, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  int target = _entry_at_y(d, event->y);
  if(target != d->hovered)
  {
    d->hovered = target;
    gtk_widget_queue_draw(d->list_da);
    if(d->desc_stack) _switch_desc(d);
  }
  return FALSE;
}

static gboolean _list_button(GtkWidget *da, GdkEventButton *event, gpointer user_data)
{
  if(event->button != GDK_BUTTON_PRIMARY) return FALSE;
  dt_search_dropdown_data_t *d = user_data;
  if(d->hovered >= 0 && d->hovered < d->n_entries && !d->entries[d->hovered].is_section)
  {
    d->active = d->hovered;
    d->preview = -1;
    gtk_label_set_text(GTK_LABEL(d->value_label), d->entries[d->active].name);
    if(d->changed) d->changed(d->widget, d->changed_data);
    gtk_popover_popdown(GTK_POPOVER(d->popup));
  }
  return FALSE;
}

/* ──────────────── search filtering ──────────────── */

static void _filter_entries(dt_search_dropdown_data_t *d)
{
  const char *query = gtk_entry_get_text(GTK_ENTRY(d->search_entry));
  if(!query || !query[0])
  {
    for(int i = 0; i < d->n_entries; i++) d->entries[i].visible = TRUE;
    return;
  }
  gchar *qfold = g_utf8_casefold(query, -1);
  for(int i = 0; i < d->n_entries; i++)
  {
    if(d->entries[i].is_section) continue;
    gchar *nf = g_utf8_casefold(d->entries[i].name, -1);
    d->entries[i].visible = (strstr(nf, qfold) != NULL);
    g_free(nf);
  }
  if(d->preview >= 0 && !d->entries[d->preview].visible) d->preview = -1;
  if(d->hovered >= 0 && !d->entries[d->hovered].visible)
  {
    d->hovered = d->preview >= 0 ? d->preview : -1;
    for(int i = 0; i < d->n_entries; i++)
      if(d->entries[i].visible && !d->entries[i].is_section) { d->hovered = i; break; }
  }

  /* also mark sections visible if they have any visible children */
  for(int i = 0; i < d->n_entries; i++)
    if(d->entries[i].is_section)
      d->entries[i].visible = _entry_has_visible_children(d, i);

  g_free(qfold);
}

/* ──────────────── search changed ──────────────── */

static void _search_changed(GtkEditable *editable, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  _filter_entries(d);
  GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(d->scrolled));
  gtk_adjustment_set_value(vadj, 0);
  int nv = MAX(_n_visible(d), 1);
  gtk_widget_set_size_request(d->list_da, -1, nv * LINE_HEIGHT);
  gtk_widget_queue_draw(d->list_da);
}

/* ──────────────── description toggle ──────────────── */

static void _toggle_desc(GtkToggleButton *btn, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  d->desc_visible = gtk_toggle_button_get_active(btn);
  if(d->config_key[0])
    dt_conf_set_bool(d->config_key, d->desc_visible);
  gtk_widget_set_visible(d->desc_sep, d->desc_visible);
  gtk_widget_set_visible(d->desc_stack, d->desc_visible);
  if(d->desc_visible && d->desc_stack) _switch_desc(d);
  /* return focus to search entry so arrow keys keep working */
  gtk_widget_grab_focus(d->search_entry);
}

/* ──────────────── scroll helpers ──────────────── */

static int _visible_row(dt_search_dropdown_data_t *d, int idx)
{
  int vy = 0;
  for(int i = 0; i < idx; i++)
  {
    if(!d->entries[i].visible) continue;
    if(d->entries[i].is_section && !_entry_has_visible_children(d, i)) continue;
    vy++;
  }
  return vy;
}

static void _scroll_to_visible(dt_search_dropdown_data_t *d, int idx)
{
  GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(d->scrolled));
  double target = _visible_row(d, idx) * LINE_HEIGHT;
  double view_h = gtk_adjustment_get_page_size(vadj);
  double upper = gtk_adjustment_get_upper(vadj);
  /* center the item in the viewport, clamped to valid scroll range */
  double center = target - (view_h / 2) + (LINE_HEIGHT / 2);
  center = CLAMP(center, 0, MAX(0, upper - view_h));
  gtk_adjustment_set_value(vadj, center);
}

static gboolean _scroll_idle(gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  if(d->popup && d->active >= 0) _scroll_to_visible(d, d->active);
  return FALSE;
}

static void _scroll_on_size_allocate(GtkWidget *w, GtkAllocation *a, gpointer user_data)
{
  g_signal_handlers_disconnect_by_func(w, (gpointer)_scroll_on_size_allocate, user_data);
  g_idle_add(_scroll_idle, user_data);
}

/* ──────────────── keyboard navigation ──────────────── */

static gboolean _search_key_press(GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  int step = 0;

  switch(event->keyval)
  {
    case GDK_KEY_Up:
    case GDK_KEY_KP_Up:
      step = -1;
      break;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
      step = 1;
      break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      if(d->hovered >= 0 && d->hovered < d->n_entries && !d->entries[d->hovered].is_section)
      {
        if(d->preview >= 0)
        {
          d->active = d->preview;
          d->preview = -1;
          gtk_label_set_text(GTK_LABEL(d->value_label), d->entries[d->active].name);
        }
        else if(d->hovered != d->active)
        {
          d->active = d->hovered;
          gtk_label_set_text(GTK_LABEL(d->value_label), d->entries[d->active].name);
          if(d->changed) d->changed(d->widget, d->changed_data);
        }
        gtk_popover_popdown(GTK_POPOVER(d->popup));
      }
      return TRUE;
    case GDK_KEY_Escape:
      if(d->preview >= 0)
      {
        d->preview = -1;
        if(d->changed) d->changed(d->widget, d->changed_data);
      }
      gtk_popover_popdown(GTK_POPOVER(d->popup));
      return TRUE;
    default:
      return FALSE;
  }

  if(step)
  {
    int cur = d->hovered;
    if(cur < 0) cur = d->active;
    if(cur < 0) { cur = 0; step = 1; }

    int nv = d->n_entries;
    for(int s = 0; s < nv; s++)
    {
      int i = (cur + step * (s + 1) + nv) % nv;
      if(!d->entries[i].visible) continue;
      if(d->entries[i].is_section) continue;
      d->hovered = i;
      break;
    }

    /* live preview: fires callback but doesn't change label or active */
    if(d->hovered >= 0 && d->entries[d->hovered].is_section == FALSE)
    {
      d->preview = d->hovered;
      if(d->changed) d->changed(d->widget, d->changed_data);
    }

    gtk_widget_queue_draw(d->list_da);
    if(d->desc_stack) _switch_desc(d);

    /* scroll to make hovered visible; wrap-around snaps to top/bottom */
    const gboolean wrapped = (step > 0 && d->hovered < cur) || (step < 0 && d->hovered > cur);
    if(wrapped)
    {
      GtkAdjustment *vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(d->scrolled));
      gtk_adjustment_set_value(vadj, step > 0 ? 0
        : gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj));
    }
    else
      _scroll_to_visible(d, d->hovered);

    return TRUE;
  }
  return FALSE;
}

/* ──────────────── custom separator draw ──────────────── */

static gboolean _sep_draw(GtkWidget *da, cairo_t *cr, gpointer user_data)
{
  GtkAllocation a;
  gtk_widget_get_allocation(da, &a);
  GtkStyleContext *ctx = gtk_widget_get_style_context(da);
  gtk_render_background(ctx, cr, 0, 0, a.width, a.height);
  GdkRGBA c;
  gtk_style_context_get_color(ctx, GTK_STATE_FLAG_INSENSITIVE, &c);
  c.alpha *= 0.3;
  gdk_cairo_set_source_rgba(cr, &c);
  cairo_set_line_width(cr, 1.0);
  cairo_move_to(cr, 0, 0.5);
  cairo_line_to(cr, a.width, 0.5);
  cairo_stroke(cr);
  return FALSE;
}

/* ──────────────── widget hover state (bauhaus-style) ──────────────── */

static void _set_label_color_state(GtkWidget *lbl, GtkStyleContext *ctx,
                                   GtkStateFlags state)
{
  if(!lbl) return;
  GdkRGBA c;
  gtk_style_context_get_color(ctx, state, &c);
  gchar hex[8];
  g_snprintf(hex, sizeof(hex), "#%02x%02x%02x",
    (int)(c.red*255), (int)(c.green*255), (int)(c.blue*255));
  const gchar *text = gtk_label_get_text(GTK_LABEL(lbl));
  if(text)
  {
    gchar *escaped = g_markup_escape_text(text, -1);
    gchar *markup = g_strdup_printf("<span foreground='%s'>%s</span>", hex, escaped);
    gtk_label_set_markup(GTK_LABEL(lbl), markup);
    g_free(markup);
    g_free(escaped);
  }
}

static void _apply_labels_for_state(dt_search_dropdown_data_t *d, GtkStateFlags state)
{
  GtkStyleContext *ctx = gtk_widget_get_style_context(d->widget);
  _set_label_color_state(d->label_widget, ctx, state);
  _set_label_color_state(d->value_label, ctx, state);
}

static gboolean _widget_enter(GtkWidget *w, GdkEventCrossing *event, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  gtk_widget_set_state_flags(w, GTK_STATE_FLAG_PRELIGHT, FALSE);
  _apply_labels_for_state(d, GTK_STATE_FLAG_PRELIGHT);
  return FALSE;
}

static gboolean _widget_leave(GtkWidget *w, GdkEventCrossing *event, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  gtk_widget_unset_state_flags(w, GTK_STATE_FLAG_PRELIGHT);
  _apply_labels_for_state(d, GTK_STATE_FLAG_NORMAL);
  return FALSE;
}

/* ──────────────── popup (GtkPopover) ──────────────── */

static void _popover_closed(GtkPopover *popover, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  /* revert preview on close without commit (Escape already cleared it;
     this catches click-outside dismiss) */
  if(d->preview >= 0)
  {
    d->preview = -1;
    if(d->changed) d->changed(d->widget, d->changed_data);
  }
  d->popup = NULL;

  /* restore normal state */
  gtk_widget_unset_state_flags(d->widget, GTK_STATE_FLAG_ACTIVE | GTK_STATE_FLAG_PRELIGHT);
  _apply_labels_for_state(d, GTK_STATE_FLAG_NORMAL);
}

static void _popup_build(dt_search_dropdown_data_t *d)
{
  if(d->popup) return;

  d->popup = gtk_popover_new(d->widget);
  gtk_popover_set_position(GTK_POPOVER(d->popup), GTK_POS_BOTTOM);
  gtk_popover_set_modal(GTK_POPOVER(d->popup), TRUE);
  gtk_popover_set_constrain_to(GTK_POPOVER(d->popup), GTK_POPOVER_CONSTRAINT_NONE);
  if(d->click_x >= 0 && d->click_y >= 0)
  {
    GdkRectangle rect = { (int)d->click_x, (int)d->click_y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(d->popup), &rect);
  }
  g_signal_connect(G_OBJECT(d->popup), "closed", G_CALLBACK(_popover_closed), d);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  /* ── top bar: toggle + search ── */
  GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

  d->search_entry = gtk_entry_new();
  gtk_entry_set_alignment(GTK_ENTRY(d->search_entry), 0.0);
  gtk_entry_set_placeholder_text(GTK_ENTRY(d->search_entry), "filter…");
  g_signal_connect(G_OBJECT(d->search_entry), "changed", G_CALLBACK(_search_changed), d);
  g_signal_connect(G_OBJECT(d->search_entry), "key-press-event", G_CALLBACK(_search_key_press), d);
  gtk_widget_set_margin_start(d->search_entry, INNER_PAD / 2);
  gtk_box_pack_start(GTK_BOX(top), d->search_entry, TRUE, TRUE, 0);

  d->toggle = gtk_toggle_button_new_with_label("i");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->toggle), d->desc_visible);
  g_signal_connect(G_OBJECT(d->toggle), "toggled", G_CALLBACK(_toggle_desc), d);
  gtk_widget_set_size_request(d->toggle, 28, -1);
  gtk_widget_set_margin_end(d->toggle, INNER_PAD / 2);
  {
    GtkCssProvider *tp = gtk_css_provider_new();
    gtk_css_provider_load_from_data(tp,
      "button.toggle:checked {"
      "  background: alpha(@theme_fg_color, 0.15);"
      "  border-color: alpha(@theme_fg_color, 0.15);"
      "}", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(d->toggle),
                                   GTK_STYLE_PROVIDER(tp),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(tp);
  }
  gtk_box_pack_end(GTK_BOX(top), d->toggle, FALSE, FALSE, 0);

  gtk_widget_set_margin_top(top, INNER_PAD / 2);
  gtk_widget_set_margin_bottom(top, INNER_PAD / 2);
  gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);

  /* ── scrollable item list ── */
  d->scrolled = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(d->scrolled), MAX_POPUP_HEIGHT);
  gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(d->scrolled), FALSE);
  gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(d->scrolled), TRUE);
  gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(d->scrolled), FALSE);
  gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(d->scrolled), FALSE);
  {
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
      "overshoot, undershoot {"
      "  background: transparent;"
      "  background-image: none;"
      "  border: none;"
      "  box-shadow: none;"
      "}", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(d->scrolled),
                                   GTK_STYLE_PROVIDER(p),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER + 2);
    g_object_unref(p);
  }

  d->list_da = gtk_drawing_area_new();
  gtk_style_context_add_class(gtk_widget_get_style_context(d->list_da), "view");
  g_signal_connect(G_OBJECT(d->list_da), "draw", G_CALLBACK(_list_draw), d);
  g_signal_connect(G_OBJECT(d->list_da), "motion-notify-event", G_CALLBACK(_list_motion), d);
  g_signal_connect(G_OBJECT(d->list_da), "button-press-event", G_CALLBACK(_list_button), d);
  gtk_widget_add_events(d->list_da, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK);

  gtk_container_add(GTK_CONTAINER(d->scrolled), d->list_da);
  gtk_box_pack_start(GTK_BOX(vbox), d->scrolled, TRUE, TRUE, 0);

  /* ── sticky description footer (GtkStack with vhomogeneous ──
     all children share the tallest page's height, like CSS grid overlay) ── */
  d->desc_stack = gtk_stack_new();
  gtk_stack_set_vhomogeneous(GTK_STACK(d->desc_stack), TRUE);
  gtk_stack_set_hhomogeneous(GTK_STACK(d->desc_stack), FALSE);
  gtk_stack_set_transition_type(GTK_STACK(d->desc_stack), GTK_STACK_TRANSITION_TYPE_NONE);
  for(int i = 0; i < d->n_entries; i++)
  {
    if(d->entries[i].is_section) continue;

    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "view");
    gtk_widget_set_margin_top(content, INNER_PAD);

    /* name (muted, not bold) */
    GtkWidget *name_lbl = gtk_label_new(d->entries[i].name);
    gtk_widget_set_opacity(name_lbl, 0.65);
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0);
    gtk_widget_set_margin_start(name_lbl, INNER_PAD);
    gtk_widget_set_margin_end(name_lbl, INNER_PAD);
    gtk_widget_set_margin_bottom(name_lbl, INNER_PAD / 2);
    gtk_box_pack_start(GTK_BOX(content), name_lbl, FALSE, FALSE, 0);

    /* description (wraps to multiple lines) */
    if(d->entries[i].description)
    {
      GtkWidget *desc_lbl = gtk_label_new(d->entries[i].description);
      gtk_label_set_xalign(GTK_LABEL(desc_lbl), 0.0);
      gtk_label_set_line_wrap(GTK_LABEL(desc_lbl), TRUE);
      gtk_widget_set_margin_start(desc_lbl, INNER_PAD);
      gtk_widget_set_margin_end(desc_lbl, INNER_PAD);
      gtk_widget_set_margin_bottom(desc_lbl, INNER_PAD);
      gtk_box_pack_start(GTK_BOX(content), desc_lbl, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(page), content, TRUE, TRUE, 0);

    char child_name[16];
    snprintf(child_name, sizeof(child_name), "_e%d", i);
    gtk_stack_add_titled(GTK_STACK(d->desc_stack), page, child_name, d->entries[i].name);
  }

  /* empty fallback page for when nothing is hovered */
  {
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(content), "view");
    gtk_widget_set_margin_top(content, INNER_PAD);
    gtk_box_pack_start(GTK_BOX(page), content, TRUE, TRUE, 0);
    gtk_stack_add_titled(GTK_STACK(d->desc_stack), page, "_none", NULL);
  }

  /* custom separator (GtkSeparator can't be reliably stretched full-width) */
  d->desc_sep = gtk_drawing_area_new();
  gtk_widget_set_size_request(d->desc_sep, -1, 2);
  g_signal_connect(G_OBJECT(d->desc_sep), "draw", G_CALLBACK(_sep_draw), NULL);
  gtk_box_pack_start(GTK_BOX(vbox), d->desc_sep, FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(vbox), d->desc_stack, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(d->popup), vbox);

  /* measure the widest content and set a minimum popup width */
  int popup_w = 280;
  int desc_h = 20;
  {
    GtkStyleContext *lctx = gtk_widget_get_style_context(d->list_da);
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(surf);
    PangoLayout *layout = pango_cairo_create_layout(cr);

    PangoFontDescription *font_desc = NULL;
    gtk_style_context_get(lctx, gtk_style_context_get_state(lctx), "font", &font_desc, NULL);
    if(font_desc)
    {
      pango_layout_set_font_description(layout, font_desc);
      pango_font_description_free(font_desc);
    }
    pango_cairo_context_set_resolution(pango_layout_get_context(layout), darktable.gui->dpi);

    int max_w = 200;
    for(int i = 0; i < d->n_entries; i++)
    {
      if(d->entries[i].is_section) continue;

      pango_layout_set_text(layout, d->entries[i].name, -1);
      int w;
      pango_layout_get_pixel_size(layout, &w, NULL);
      if(w > max_w) max_w = w;

      if(d->width_mode == DT_SEARCH_DROPDOWN_WIDTH_LIST_AND_DESC
         && d->entries[i].description)
      {
        pango_layout_set_text(layout, d->entries[i].description, -1);
        pango_layout_get_pixel_size(layout, &w, NULL);
        if(w > max_w) max_w = w;
      }

      /* measure tallest description page height for fixed popup size */
      int eh = 2; /* separator */
      gchar *nm = g_markup_printf_escaped("<b>%s</b>", d->entries[i].name);
      pango_layout_set_markup(layout, nm, -1);
      g_free(nm);
      pango_layout_set_width(layout, (popup_w - INNER_PAD * 2) * PANGO_SCALE);
      int nh;
      pango_layout_get_pixel_size(layout, NULL, &nh);
      eh += nh + INNER_PAD;

      if(d->entries[i].description)
      {
        pango_layout_set_text(layout, d->entries[i].description, -1);
        int dh;
        pango_layout_get_pixel_size(layout, NULL, &dh);
        eh += dh + INNER_PAD;
      }
      desc_h = MAX(desc_h, eh);
    }
    desc_h = MAX(desc_h, 20);

    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    popup_w = max_w + INNER_PAD * 2 + 4;
    popup_w = CLAMP(popup_w, 280, 480);
  }

  int nv = MAX(_n_visible(d), 1);
  /* count extra section margins (4px before non-first section titles) */
  int section_margins = 0;
  {
    gboolean first = TRUE;
    for(int i = 0; i < d->n_entries; i++)
    {
      if(!d->entries[i].visible) continue;
      if(d->entries[i].is_section && !_entry_has_visible_children(d, i)) continue;
      if(d->entries[i].is_section && !first) section_margins += 4;
      first = FALSE;
    }
  }
  int list_h = nv * LINE_HEIGHT + section_margins;
  int fixed_h = 28 + MIN(list_h + desc_h, MAX_POPUP_HEIGHT);

  gtk_widget_set_size_request(d->list_da, -1, list_h);
  gtk_widget_set_size_request(vbox, popup_w, fixed_h);

  gtk_widget_show_all(vbox);
  if(d->desc_visible)
    _switch_desc(d);
  else
  {
    gtk_widget_set_visible(d->desc_sep, FALSE);
    gtk_widget_set_visible(d->desc_stack, FALSE);
  }
}

/* ──────────────── open / close ──────────────── */

static void _open_popup(dt_search_dropdown_data_t *d)
{
  if(d->popup) { gtk_widget_destroy(d->popup); }

  d->hovered = d->active;
  d->preview = -1;
  for(int i = 0; i < d->n_entries; i++) d->entries[i].visible = TRUE;
  for(int i = 0; i < d->n_entries; i++)
    if(d->entries[i].is_section)
      d->entries[i].visible = _entry_has_visible_children(d, i);

  _popup_build(d);
  if(!d->popup) return;

  gtk_popover_popup(GTK_POPOVER(d->popup));

  /* scroll to make active item visible (after layout, via size-allocate) */
  g_signal_connect(G_OBJECT(d->scrolled), "size-allocate",
    G_CALLBACK(_scroll_on_size_allocate), d);

  /* active+prelight while popup is open — same appearance as hover */
  gtk_widget_set_state_flags(d->widget, GTK_STATE_FLAG_ACTIVE | GTK_STATE_FLAG_PRELIGHT, FALSE);
  _apply_labels_for_state(d, GTK_STATE_FLAG_PRELIGHT);
}

/* ──────────────── widget drawing (bauhaus-style quad) ──────────────── */

static gboolean _widget_draw_after(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);

  /* draw V-shaped chevron in the right margin space, matching bauhaus style */
  const double r = LINE_HEIGHT * 0.13;
  const double x = alloc.width - 22;
  const double y = alloc.height * 0.5 + 1;

  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context(widget);
  gtk_style_context_get_color(context, gtk_widget_get_state_flags(widget), &color);

  cairo_save(cr);
  cairo_set_line_width(cr, 1.0);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
  cairo_move_to(cr, x, y - r);
  cairo_rel_line_to(cr, r, r);
  cairo_rel_line_to(cr, r, -r);
  cairo_stroke(cr);
  cairo_restore(cr);
  return FALSE;
}

/* ──────────────── widget-level handlers ──────────────── */

static void _widget_destroy(GtkWidget *w, gpointer user_data)
{
  dt_search_dropdown_data_t *d = user_data;
  for(int i = 0; i < d->n_entries; i++)
  {
    g_free(d->entries[i].name);
    g_free(d->entries[i].description);
  }
  g_free(d->entries);
  g_free(d);
}

static gboolean _widget_button(GtkWidget *w, GdkEventButton *event, gpointer user_data)
{
  if(event->button == GDK_BUTTON_PRIMARY)
  {
    dt_search_dropdown_data_t *d = user_data;
    d->click_x = event->x;
    d->click_y = event->y;
    _open_popup(d);
    return TRUE;
  }
  return FALSE;
}

/* ──────────────── public API ──────────────── */

GtkWidget *dt_search_dropdown_new(const char *label)
{
  dt_search_dropdown_data_t *d = calloc(1, sizeof(*d));
  d->active = -1;
  d->hovered = -1;
  d->desc_visible = TRUE;
  d->click_x = d->click_y = -1;
  d->width_mode = DT_SEARCH_DROPDOWN_WIDTH_LIST;

  GtkWidget *eb = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(eb), TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(eb), "dt_bauhaus");
  gtk_widget_set_margin_top(eb, 2);
  gtk_widget_set_margin_bottom(eb, 2);
  gtk_widget_add_events(eb, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
  g_signal_connect(G_OBJECT(eb), "enter-notify-event", G_CALLBACK(_widget_enter), d);
  g_signal_connect(G_OBJECT(eb), "leave-notify-event", G_CALLBACK(_widget_leave), d);
  g_signal_connect(G_OBJECT(eb), "button-press-event", G_CALLBACK(_widget_button), d);
  {
    /* subtle hover background matching bauhaus style */
    GtkCssProvider *hp = gtk_css_provider_new();
    gtk_css_provider_load_from_data(hp,
      ".dt_bauhaus:hover { background-color: alpha(@fg_color, 0.08); }\n"
      ".dt_bauhaus:active { color: shade(@fg_color, 1.2); }", -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(eb),
                                   GTK_STYLE_PROVIDER(hp),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(hp);
  }

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_margin_end(box, 36);
  gtk_container_add(GTK_CONTAINER(eb), box);

  if(label)
  {
    d->label_widget = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(d->label_widget), label);
    gtk_box_pack_start(GTK_BOX(box), d->label_widget, FALSE, FALSE, 0);
  }

  /* value label, right-aligned; quad drawn in the 30px right margin */
  d->value_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(d->value_label), 1.0);
  gtk_box_pack_end(GTK_BOX(box), d->value_label, TRUE, TRUE, 0);

  d->widget = eb;
  g_signal_connect_after(G_OBJECT(eb), "draw", G_CALLBACK(_widget_draw_after), d);
  g_object_set_data(G_OBJECT(eb), "search-dd-data", d);
  g_signal_connect(G_OBJECT(eb), "destroy", G_CALLBACK(_widget_destroy), d);
  return eb;
}

void dt_search_dropdown_set_width_strategy(GtkWidget *widget, dt_search_dropdown_width_mode_t mode)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  d->width_mode = mode;
}

static inline void _ensure_cap(dt_search_dropdown_data_t *d)
{
  if(d->n_entries >= d->cap)
  {
    d->cap = d->cap ? d->cap * 2 : 32;
    d->entries = realloc(d->entries, d->cap * sizeof(dt_search_entry_t));
  }
}

void dt_search_dropdown_add_section(GtkWidget *widget, const char *label)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  _ensure_cap(d);
  dt_search_entry_t *e = &d->entries[d->n_entries++];
  memset(e, 0, sizeof(*e));
  e->name = g_strdup(label);
  e->is_section = TRUE;
  e->visible = TRUE;
}

void dt_search_dropdown_add_entry(GtkWidget *widget, const char *name,
                                  const char *description, gpointer data)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  _ensure_cap(d);
  dt_search_entry_t *e = &d->entries[d->n_entries++];
  memset(e, 0, sizeof(*e));
  e->name = g_strdup(name);
  e->description = description ? g_strdup(description) : NULL;
  e->data = data;
  e->visible = TRUE;

  if(d->active < 0)
  {
    d->active = d->n_entries - 1;
    gtk_label_set_text(GTK_LABEL(d->value_label), e->name);
  }
}

void dt_search_dropdown_clear(GtkWidget *widget)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  for(int i = 0; i < d->n_entries; i++)
  {
    g_free(d->entries[i].name);
    g_free(d->entries[i].description);
  }
  d->n_entries = 0;
  d->active = -1;
  gtk_label_set_text(GTK_LABEL(d->value_label), "");
}

void dt_search_dropdown_set(GtkWidget *widget, int idx)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d || idx < 0 || idx >= d->n_entries) return;
  d->active = idx;
  gtk_label_set_text(GTK_LABEL(d->value_label), d->entries[idx].name);
}

void dt_search_dropdown_set_by_data(GtkWidget *widget, gpointer data)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  for(int i = 0; i < d->n_entries; i++)
  {
    if(d->entries[i].is_section) continue;
    if(d->entries[i].data == data)
    {
      d->active = i;
      gtk_label_set_text(GTK_LABEL(d->value_label), d->entries[i].name);
      return;
    }
  }
}

int dt_search_dropdown_get(GtkWidget *widget)
{
  dt_search_dropdown_data_t *d = _data(widget);
  return d ? d->active : -1;
}

gpointer dt_search_dropdown_get_data(GtkWidget *widget)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return NULL;
  int idx = d->preview >= 0 ? d->preview : d->active;
  if(idx < 0) return NULL;
  return d->entries[idx].data;
}

void dt_search_dropdown_set_changed_callback(GtkWidget *widget,
    void (*cb)(GtkWidget *, gpointer), gpointer user_data)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  d->changed = cb;
  d->changed_data = user_data;
}

void dt_search_dropdown_set_config_key(GtkWidget *widget, const char *key)
{
  dt_search_dropdown_data_t *d = _data(widget);
  if(!d) return;
  g_strlcpy(d->config_key, key ? key : "", sizeof(d->config_key));
  if(d->config_key[0])
  {
    if(dt_conf_key_exists(d->config_key))
      d->desc_visible = dt_conf_get_bool(d->config_key);
  }
}
