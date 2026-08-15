/*
    This file is part of darktable,
    Copyright (C) 2015-2021 darktable developers.

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

#include "dtgtk/expander.h"
#include "common/darktable.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include "libs/lib.h"

#include <gtk/gtk.h>

#include <math.h>

/* ------------------------------------------------------------------ */
/* GTK4-only GtkRevealer replacement for module bodies (dtgtk_bodyclip).
 *
 * WHY: stock GtkRevealer un-maps its child when fully collapsed
 * (gtk_revealer_set_position() toggles child_visible with pos), and GTK4
 * drops a widget's cached render node on unmap -- so EVERY module open
 * re-rasterizes the whole body (dozens of bauhaus cairo widgets, pango
 * text, gradients) in ONE frame.  Measured: a 20-25ms first-frame drop at
 * the start of each open/close -- the "stuttery" module animation.  It
 * behaved the same on GTK3, where every animation frame was re-rasterized.
 *
 * dtgtk_bodyclip keeps its child ALWAYS allocated at full natural size
 * (like GtkRevealer does mid-animation) and only animates the clip + a
 * translate transform.  The body therefore stays mapped and its render
 * node survives the collapsed state: opening/closing becomes pure
 * transform updates -- no re-rasterization.  The body's first render
 * happens once at module build time instead of on every click.
 *
 * The rest matches GtkRevealer: same ease-out-cubic pacing, same
 * child-revealed notification (the scroll-to-module machinery connects to
 * it), overflow is clipped while the child is not fully in place, and
 * gtk_widget_do_pick() honors overflow:hidden + the child transform, so
 * input can never reach the translated-away (invisible) body.
 *
 * Known tradeoffs: collapsed bodies stay rendered (modest VRAM), and
 * keyboard focus traversal still sees a collapsed body's focusable widgets
 * (edge case; the stock revealer hid them via child_visible).
 * ------------------------------------------------------------------ */

typedef struct _GtkDarktableBodyClip GtkDarktableBodyClip;
struct _GtkDarktableBodyClip
{
  GtkWidget parent_instance;

  GtkWidget *child;
  guint duration;      /* animation duration in ms */
  double current_pos;  /* 0.0 collapsed .. 1.0 fully revealed */
  double target_pos;
  double source_pos;   /* pos at animation start */
  gint64 start_time;   /* monotonic us */
  guint tick_id;
};

G_DEFINE_TYPE(GtkDarktableBodyClip, dtgtk_bodyclip, GTK_TYPE_WIDGET)
enum
{
  PROP_BODYCLIP_CHILD = 1,
  PROP_BODYCLIP_REVEAL_CHILD,
  PROP_BODYCLIP_CHILD_REVEALED,
  LAST_PROP
};
static GParamSpec *bodyclip_props[LAST_PROP];

static void dtgtk_bodyclip_set_position(GtkDarktableBodyClip *self, double pos)
{
  self->current_pos = pos;

  /* the child is allocated full-size and translated; while not fully in
   * place it must be clipped to this widget's (animated) bounds, both for
   * drawing (gtkwidget.c create_render_node) and for input picking
   * (gtk_widget_do_pick rejects points outside the box when hidden). */
  const gboolean hidden = pos < 1.0;
  if(hidden != (gtk_widget_get_overflow(GTK_WIDGET(self)) == GTK_OVERFLOW_HIDDEN))
    gtk_widget_set_overflow(GTK_WIDGET(self), hidden ? GTK_OVERFLOW_HIDDEN : GTK_OVERFLOW_VISIBLE);

  gtk_widget_queue_resize(GTK_WIDGET(self));

  if(self->current_pos == self->target_pos)
    g_object_notify_by_pspec(G_OBJECT(self), bodyclip_props[PROP_BODYCLIP_CHILD_REVEALED]);
}

static gboolean dtgtk_bodyclip_animate_cb(GtkWidget *widget,
                                          GdkFrameClock *frame_clock,
                                          gpointer user_data)
{
  GtkDarktableBodyClip *self = user_data;
  const gint64 now = g_get_monotonic_time();
  const double t = (double)(now - self->start_time) / (self->duration * 1000.0);

  if(t >= 1.0)
  {
    self->tick_id = 0;
    dtgtk_bodyclip_set_position(self, self->target_pos);
    return G_SOURCE_REMOVE;
  }

  /* ease-out cubic, same curve as GtkProgressTracker (gtkprogresstracker.c) */
  const double p = t - 1.0;
  const double ease = p * p * p + 1.0;
  dtgtk_bodyclip_set_position(self, self->source_pos + ease * (self->target_pos - self->source_pos));
  return G_SOURCE_CONTINUE;
}

static void dtgtk_bodyclip_measure(GtkWidget *widget,
                                   GtkOrientation orientation,
                                   int for_size,
                                   int *minimum,
                                   int *natural,
                                   int *minimum_baseline,
                                   int *natural_baseline)
{
  GtkDarktableBodyClip *self = DTGTK_BODYCLIP(widget);

  if(!self->child || !gtk_widget_get_visible(self->child))
  {
    *minimum = *natural = 0;
    if(minimum_baseline) *minimum_baseline = -1;
    if(natural_baseline) *natural_baseline = -1;
    return;
  }

  /* vertical axis animates (slide-down), horizontal is always full size */
  const double scale = (orientation == GTK_ORIENTATION_VERTICAL) ? self->current_pos : 1.0;
  const double opposite_scale = (orientation == GTK_ORIENTATION_VERTICAL) ? 1.0 : self->current_pos;
  if(scale == 0)
  {
    *minimum = *natural = 0;
    if(minimum_baseline) *minimum_baseline = -1;
    if(natural_baseline) *natural_baseline = -1;
    return;
  }
  else if(opposite_scale == 0)
    for_size = -1;
  else if(for_size >= 0)
    for_size = MIN(G_MAXINT, (int)floor(for_size / opposite_scale));

  gtk_widget_measure(self->child, orientation, for_size, minimum, natural, minimum_baseline, natural_baseline);
  *minimum = (int)ceil(*minimum * scale);
  *natural = (int)ceil(*natural * scale);
}

static void dtgtk_bodyclip_size_allocate(GtkWidget *widget,
                                         int width,
                                         int height,
                                         int baseline)
{
  GtkDarktableBodyClip *self = DTGTK_BODYCLIP(widget);

  if(!self->child || !gtk_widget_get_visible(self->child))
    return;

  if(self->current_pos >= 1.0)
  {
    gtk_widget_allocate(self->child, width, height, baseline, NULL);
    return;
  }

  int min = 0, nat = 0;
  gtk_widget_measure(self->child, GTK_ORIENTATION_VERTICAL, width, &min, &nat, NULL, NULL);
  int child_height = nat;
  const double vscale = self->current_pos;
  if(vscale > 0 && vscale < 1.0)
  {
    /* same min/nat preference as GtkRevealer to avoid rounding drift */
    if(ceil(nat * vscale) == height) child_height = nat;
    else if(ceil(min * vscale) == height) child_height = min;
    else child_height = MIN(G_MAXINT, (int)floor(height / vscale));
  }

  /* ALWAYS allocate the child at full size: unlike GtkRevealer this keeps
   * it mapped, so its render node stays cached across collapses.  The
   * translate slides the body down from the top (slide-down look). */
  GskTransform *transform = gsk_transform_translate(NULL, &GRAPHENE_POINT_INIT(0, height - child_height));
  gtk_widget_allocate(self->child, width, child_height, -1, transform); /* transfer full */
}

static void dtgtk_bodyclip_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
  GtkWidget *child = DTGTK_BODYCLIP(widget)->child;
  if(child)
    gtk_widget_snapshot_child(widget, child, snapshot);
}

static void dtgtk_bodyclip_compute_expand(GtkWidget *widget, gboolean *hexpand, gboolean *vexpand)
{
  GtkWidget *child = DTGTK_BODYCLIP(widget)->child;
  if(child)
  {
    *hexpand = gtk_widget_compute_expand(child, GTK_ORIENTATION_HORIZONTAL);
    *vexpand = gtk_widget_compute_expand(child, GTK_ORIENTATION_VERTICAL);
  }
  else
    *hexpand = *vexpand = FALSE;
}

static GtkSizeRequestMode dtgtk_bodyclip_get_request_mode(GtkWidget *widget)
{
  GtkWidget *child = DTGTK_BODYCLIP(widget)->child;
  return child ? gtk_widget_get_request_mode(child) : GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void dtgtk_bodyclip_unmap(GtkWidget *widget)
{
  GtkDarktableBodyClip *self = DTGTK_BODYCLIP(widget);

  GTK_WIDGET_CLASS(dtgtk_bodyclip_parent_class)->unmap(widget);

  /* finish the animation on hide (view switch), like GtkRevealer */
  if(self->current_pos != self->target_pos)
    dtgtk_bodyclip_set_position(self, self->target_pos);
  if(self->tick_id)
  {
    gtk_widget_remove_tick_callback(widget, self->tick_id);
    self->tick_id = 0;
  }
}

static void dtgtk_bodyclip_dispose(GObject *object)
{
  GtkDarktableBodyClip *self = DTGTK_BODYCLIP(object);
  if(self->tick_id)
  {
    gtk_widget_remove_tick_callback(GTK_WIDGET(self), self->tick_id);
    self->tick_id = 0;
  }
  g_clear_pointer(&self->child, gtk_widget_unparent);
  G_OBJECT_CLASS(dtgtk_bodyclip_parent_class)->dispose(object);
}

static void dtgtk_bodyclip_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
  GtkDarktableBodyClip *self = DTGTK_BODYCLIP(object);
  switch(property_id)
  {
    case PROP_BODYCLIP_CHILD: g_value_set_object(value, self->child); break;
    case PROP_BODYCLIP_REVEAL_CHILD: g_value_set_boolean(value, dtgtk_bodyclip_get_reveal_child(self)); break;
    case PROP_BODYCLIP_CHILD_REVEALED: g_value_set_boolean(value, dtgtk_bodyclip_get_child_revealed(self)); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec); break;
  }
}

static void dtgtk_bodyclip_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
  GtkDarktableBodyClip *self = DTGTK_BODYCLIP(object);
  switch(property_id)
  {
    case PROP_BODYCLIP_CHILD: dtgtk_bodyclip_set_child(self, g_value_get_object(value)); break;
    case PROP_BODYCLIP_REVEAL_CHILD: dtgtk_bodyclip_set_reveal_child(self, g_value_get_boolean(value)); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec); break;
  }
}

static void dtgtk_bodyclip_class_init(GtkDarktableBodyClipClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = dtgtk_bodyclip_dispose;
  object_class->get_property = dtgtk_bodyclip_get_property;
  object_class->set_property = dtgtk_bodyclip_set_property;

  widget_class->unmap = dtgtk_bodyclip_unmap;
  widget_class->size_allocate = dtgtk_bodyclip_size_allocate;
  widget_class->measure = dtgtk_bodyclip_measure;
  widget_class->compute_expand = dtgtk_bodyclip_compute_expand;
  widget_class->get_request_mode = dtgtk_bodyclip_get_request_mode;
  widget_class->snapshot = dtgtk_bodyclip_snapshot;

  bodyclip_props[PROP_BODYCLIP_CHILD] =
    g_param_spec_object("child", NULL, NULL, GTK_TYPE_WIDGET,
                        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  bodyclip_props[PROP_BODYCLIP_REVEAL_CHILD] =
    g_param_spec_boolean("reveal-child", NULL, NULL, FALSE,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  bodyclip_props[PROP_BODYCLIP_CHILD_REVEALED] =
    g_param_spec_boolean("child-revealed", NULL, NULL, FALSE,
                         G_PARAM_READABLE);
  g_object_class_install_properties(object_class, LAST_PROP, bodyclip_props);

  gtk_widget_class_set_css_name(widget_class, "revealer");
  gtk_widget_class_set_accessible_role(widget_class, GTK_ACCESSIBLE_ROLE_GROUP);
}

static void dtgtk_bodyclip_init(GtkDarktableBodyClip *self)
{
  self->current_pos = 0.0;
  self->target_pos = 0.0;
  gtk_widget_set_overflow(GTK_WIDGET(self), GTK_OVERFLOW_HIDDEN);
}

GtkWidget *dtgtk_bodyclip_new(void)
{
  return g_object_new(DTGTK_TYPE_BODYCLIP, NULL);
}

void dtgtk_bodyclip_set_child(GtkDarktableBodyClip *self, GtkWidget *child)
{
  g_return_if_fail(DTGTK_IS_BODYCLIP(self));
  g_return_if_fail(GTK_IS_WIDGET(child));
  g_return_if_fail(self->child == NULL);

  gtk_widget_set_parent(child, GTK_WIDGET(self));
  self->child = child;
}

GtkWidget *dtgtk_bodyclip_get_child(GtkDarktableBodyClip *self)
{
  g_return_val_if_fail(DTGTK_IS_BODYCLIP(self), NULL);
  return self->child;
}

void dtgtk_bodyclip_set_duration(GtkDarktableBodyClip *self, guint duration)
{
  g_return_if_fail(DTGTK_IS_BODYCLIP(self));
  self->duration = duration;
}

void dtgtk_bodyclip_set_reveal_child(GtkDarktableBodyClip *self, gboolean reveal)
{
  g_return_if_fail(DTGTK_IS_BODYCLIP(self));
  reveal = reveal != FALSE;
  const double target = reveal ? 1.0 : 0.0;
  if(self->target_pos == target)
    return;

  self->target_pos = target;
  g_object_notify_by_pspec(G_OBJECT(self), bodyclip_props[PROP_BODYCLIP_REVEAL_CHILD]);

  GtkSettings *settings = gtk_widget_get_settings(GTK_WIDGET(self));
  gboolean enable_animations = TRUE;
  if(settings) g_object_get(settings, "gtk-enable-animations", &enable_animations, NULL);

  if(gtk_widget_get_mapped(GTK_WIDGET(self)) && self->duration != 0 && enable_animations)
  {
    self->source_pos = self->current_pos;
    self->start_time = g_get_monotonic_time();
    if(self->tick_id == 0)
      self->tick_id = gtk_widget_add_tick_callback(GTK_WIDGET(self), dtgtk_bodyclip_animate_cb, self, NULL);
  }
  else
    dtgtk_bodyclip_set_position(self, target);
}

gboolean dtgtk_bodyclip_get_reveal_child(GtkDarktableBodyClip *self)
{
  g_return_val_if_fail(DTGTK_IS_BODYCLIP(self), FALSE);
  return self->target_pos != 0.0;
}

gboolean dtgtk_bodyclip_get_child_revealed(GtkDarktableBodyClip *self)
{
  g_return_val_if_fail(DTGTK_IS_BODYCLIP(self), FALSE);
  /* same semantics as gtk_revealer_get_child_revealed(): during the
   * animation the state lags the target */
  const gboolean animation_finished = (self->target_pos == self->current_pos);
  const gboolean reveal_child = dtgtk_bodyclip_get_reveal_child(self);
  if(animation_finished)
    return reveal_child;
  else
    return !reveal_child;
}

G_DEFINE_TYPE(GtkDarktableExpander, dtgtk_expander, GTK_TYPE_BOX);

static void dtgtk_expander_class_init(GtkDarktableExpanderClass *class)
{
}

GtkWidget *dtgtk_expander_get_frame(GtkDarktableExpander *expander)
{
  g_return_val_if_fail(DTGTK_IS_EXPANDER(expander), NULL);

  return dtgtk_bodyclip_get_child(DTGTK_BODYCLIP(expander->frame));
}

GtkWidget *dtgtk_expander_get_header(GtkDarktableExpander *expander)
{
  g_return_val_if_fail(DTGTK_IS_EXPANDER(expander), NULL);

  return expander->header;
}

GtkWidget *dtgtk_expander_get_header_event_box(GtkDarktableExpander *expander)
{
  g_return_val_if_fail(DTGTK_IS_EXPANDER(expander), NULL);

  return expander->header_evb;
}

GtkWidget *dtgtk_expander_get_body(GtkDarktableExpander *expander)
{
  g_return_val_if_fail(DTGTK_IS_EXPANDER(expander), NULL);

  return expander->body;
}

GtkWidget *dtgtk_expander_get_body_event_box(GtkDarktableExpander *expander)
{
  g_return_val_if_fail(DTGTK_IS_EXPANDER(expander), NULL);

  return expander->body_evb;
}

static GtkWidget *_scroll_widget = NULL;
static GtkWidget *_last_expanded = NULL;
static GtkWidget *_drop_widget = NULL;
static GtkAllocation _start_pos = {0};

#if GTK_CHECK_VERSION(4, 0, 0)
static void _expander_resize(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);
static gboolean _expander_resize_idle(gpointer user_data);
#endif

static void _set_last_expanded(GtkWidget *widget)
{
  if(_last_expanded)
    g_object_remove_weak_pointer(G_OBJECT(_last_expanded), (gpointer *)&_last_expanded);

  _last_expanded = widget;

  if(_last_expanded)
    g_object_add_weak_pointer(G_OBJECT(_last_expanded), (gpointer *)&_last_expanded);
}

void dtgtk_expander_set_expanded(GtkDarktableExpander *expander, gboolean expanded)
{
  g_return_if_fail(DTGTK_IS_EXPANDER(expander));

  expanded = expanded != FALSE;

  /* the first time the state is applied (a freshly created expander being set
   * to its saved state on darkroom entry) it must not animate: expanders are
   * created revealed and immediately collapsed per conf before the panel is
   * laid out. animating that would flash every module open on view switch.
   * only later (user driven) state changes get the revealer transition. */
  const gboolean animate = expander->state_set;
  expander->state_set = TRUE;

  if(expander->expanded != expanded)
  {
    expander->expanded = expanded;

    if(expanded)
    {
      // expose expansion state to CSS so themes can style expanded modules
      dt_gui_add_class(GTK_WIDGET(expander), "dt_module_expanded");
      _set_last_expanded(GTK_WIDGET(expander));
      GtkWidget *sw = gtk_widget_get_ancestor(_last_expanded, GTK_TYPE_SCROLLED_WINDOW);
      if(sw)
      {
        gtk_widget_get_allocation(_last_expanded, &_start_pos);
        _start_pos.x = gtk_adjustment_get_value(gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw)));
      }
    }
    else
      dt_gui_remove_class(GTK_WIDGET(expander), "dt_module_expanded");

    GtkWidget *frame = expander->body;
    if(frame)
    {
      gtk_widget_set_visible(frame, TRUE); // for collapsible sections
      dtgtk_bodyclip_set_duration(DTGTK_BODYCLIP(expander->frame),
                                  animate ? dt_conf_get_int("darkroom/ui/transition_duration") : 0);
      dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(expander->frame), expander->expanded);
    }
  }
  else if(expanded)
  {
    // The expander is already expanded. Still update scroll tracking
    // so that _expander_resize can scroll to this widget. This is
    // needed when navigating from the Quick Access Panel to a module
    // that was already expanded on the destination tab.
    _set_last_expanded(GTK_WIDGET(expander));
    GtkWidget *sw = gtk_widget_get_ancestor(GTK_WIDGET(expander), GTK_TYPE_SCROLLED_WINDOW);
    if(sw)
    {
      gtk_widget_get_allocation(GTK_WIDGET(expander), &_start_pos);
      _start_pos.x = gtk_adjustment_get_value(
        gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw)));
    }
    // Force a size-allocate so _expander_resize fires even if the
    // widget layout has not changed (e.g. module already visible and
    // expanded on the current tab).
    gtk_widget_queue_resize(GTK_WIDGET(expander));
#if GTK_CHECK_VERSION(4, 0, 0)
    // GTK4: no size-allocate to re-trigger _expander_resize; kick it once
    // the pending resize has settled.
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, _expander_resize_idle,
                    g_object_ref(GTK_WIDGET(expander)), g_object_unref);
#endif
  }
}

gboolean dtgtk_expander_get_expanded(GtkDarktableExpander *expander)
{
  g_return_val_if_fail(DTGTK_IS_EXPANDER(expander), FALSE);

  return expander->expanded;
}

#if GTK_CHECK_VERSION(4, 0, 0)
static void _expander_resize(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data);
static gboolean _expander_resize_idle(gpointer user_data);
#endif

static gboolean _expander_scroll(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
  GtkWidget *sw = gtk_widget_get_ancestor(widget, GTK_TYPE_SCROLLED_WINDOW);
  if(!sw) return G_SOURCE_REMOVE;

  GtkAllocation allocation, available;
  gtk_widget_get_allocation(widget, &allocation);
  gtk_widget_get_allocation(sw, &available);

  GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sw));
  gdouble value = gtk_adjustment_get_value(adjustment);

  GtkWidget *header = dtgtk_expander_get_header(DTGTK_EXPANDER(widget));
  const int drop_space = (widget == _drop_widget) && header ? gtk_widget_get_allocated_height(header) : 0;
  allocation.y -= drop_space;

  const gboolean is_iop = !g_strcmp0("iop-expander", gtk_widget_get_name(widget));

  // try not to get dragged upwards if a module above is collapsing
  if(is_iop
     && widget == _last_expanded
     && allocation.y < _start_pos.y)
  {
    const int offset = _start_pos.y - allocation.y - _start_pos.x + value;
    value -= offset;
  }
  // scroll up if more space is needed below
  // if "scroll_to_module" is enabled scroll up or down
  // but don't scroll if not the whole module can be shown
  float prop = 1.0f;
  const gboolean scroll_to_top = !_drop_widget &&
    dt_conf_get_bool(is_iop ? "darkroom/ui/scroll_to_module" : "lighttable/ui/scroll_to_module");

  const int spare = available.height - allocation.height - 2 * drop_space;
  const int from_top = allocation.y - value;
  const int move = MAX(scroll_to_top ? from_top : from_top - MAX(0, MIN(from_top, spare)),
                       - MAX(0, spare - from_top));
  if(move)
  {
    gint64 interval = 0;
    gdk_frame_clock_get_refresh_info(frame_clock, 0, &interval, NULL);
    const int remaining = GPOINTER_TO_INT(user_data) - gdk_frame_clock_get_frame_time(frame_clock);
    prop = (float)interval / MAX(interval, remaining);
    value += prop * move;
  }

  if(is_iop)
  {
    _start_pos = allocation;
    _start_pos.x = value;
  }
  gtk_adjustment_set_value(adjustment, value);

  if(prop != 1.0f) return G_SOURCE_CONTINUE;

  _scroll_widget = NULL;
  return G_SOURCE_REMOVE;
}

static void _expander_resize(GtkWidget *widget, GdkRectangle *allocation, gpointer user_data)
{
  // Already scrolling to this widget
  if(widget == _scroll_widget)
    return;

  // Handle drag-and-drop case
  if(_drop_widget)
  {
    if(widget != _drop_widget)
      return;
  }
  else
  {
    const gboolean is_lib_gui_module = darktable.lib
      && darktable.lib->gui_module
      && darktable.lib->gui_module->expander == widget;
    const gboolean last_target_is_iop = _last_expanded
      && !g_strcmp0("iop-expander", gtk_widget_get_name(_last_expanded));

    if(_last_expanded)
    {
      // When _last_expanded is set (by dtgtk_expander_set_expanded),
      // only allow that specific widget (or lib gui_module) to
      // trigger scroll.  This prevents modules with stale
      // GTK_STATE_FLAG_SELECTED (left over from previous images or
      // sessions) from stealing the scroll target.
      // For IOP targets, do not allow lib gui_module to steal the
      // target on first allocations after image load.
      if(widget != _last_expanded
         && !(is_lib_gui_module && !last_target_is_iop))
        return;

      // Wait until the target widget has a valid layout (positive
      // height means it is mapped and sized).  This handles the case
      // where the module is on a different tab that hasn't been
      // shown yet.
      if(gtk_widget_get_height(widget) <= 0)
        return;

      // Clear _last_expanded so that, once the scroll animation
      // finishes and _scroll_widget becomes NULL, subsequent
      // size-allocate events do not re-trigger scrolling.
      _set_last_expanded(NULL);
    }
    else
    {
      const gboolean height_changed =
        gtk_widget_get_height(widget) != _start_pos.height;

      if(!height_changed)
        return;

      const gboolean frame_selected =
        gtk_widget_get_state_flags(user_data) & GTK_STATE_FLAG_SELECTED;

      if(!frame_selected && !is_lib_gui_module)
        return;
    }
  }

  _scroll_widget = widget;
  GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
  if(clock)
    gtk_widget_add_tick_callback(widget, _expander_scroll,
                                GINT_TO_POINTER(gdk_frame_clock_get_frame_time(clock)
                                + dt_conf_get_int("darkroom/ui/transition_duration") * 1000), NULL);
}

void dtgtk_expander_set_drag_hover(GtkDarktableExpander *expander, gboolean allow, gboolean below, guint time)
{
  GtkWidget *widget = expander ? GTK_WIDGET(expander) : _drop_widget;
  // don't remove drop zone when switching between last expander and empty space to avoid jitter
  static guint last_time;
  if(!widget || (!allow && !below && widget == _drop_widget && time == last_time)) return;

  dt_gui_remove_class(widget, "module_drop_after");
  dt_gui_remove_class(widget, "module_drop_before");

  if(allow || below)
  {
    _drop_widget = widget;
    _set_last_expanded(NULL);
    last_time = time;

    if(!allow)
      gtk_widget_queue_resize(widget);
    else if(below)
      dt_gui_add_class(widget, "module_drop_before");
    else
      dt_gui_add_class(widget, "module_drop_after");
  }
}

// GTK3 DnD (GtkWidget::drag-begin): the expander headers are drag sources
// for module reordering, with a cairo snapshot as drag icon.  GTK4 DnD is a
// different API (GtkDragSource/GdkContentProvider) -- TODO P3; the signal
// connects below are GTK3-only (GTK4 has no widget-level drag signals).
#if !GTK_CHECK_VERSION(4, 0, 0)
static void _expander_drag_begin(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
  GtkAllocation allocation = {0};
  gtk_widget_get_allocation(widget, &allocation);
  // method from https://blog.gtk.org/2017/04/23/drag-and-drop-in-lists/
  cairo_surface_t *surface = dt_cairo_image_surface_create(CAIRO_FORMAT_RGB24, allocation.width, allocation.height);
  cairo_t *cr = cairo_create(surface);

  // hack to render not transparent
  dt_gui_add_class(widget, "module_drag_icon");
  gtk_widget_size_allocate(widget, &allocation);
  gtk_widget_draw(widget, cr);
  dt_gui_remove_class(widget, "module_drag_icon");

  int pointerx, pointery;
  gdk_window_get_device_position(gtk_widget_get_window(widget),
      gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))),
      &pointerx, &pointery, NULL);
  cairo_surface_set_device_offset(surface, -pointerx, -CLAMP(pointery, 0, allocation.height));
  gtk_drag_set_icon_surface(context, surface);

  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  gtk_widget_set_opacity(widget, 0.5);
}

static void _expander_drag_end(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
  dtgtk_expander_set_drag_hover(NULL, FALSE, FALSE, 0);
  _drop_widget = NULL;
  gtk_widget_set_opacity(widget, 1.0);
}
#endif
#if !GTK_CHECK_VERSION(4, 0, 0)
static void _expander_drag_leave(GtkDarktableExpander *widget,
                           GdkDragContext *dc,
                           guint time,
                           gpointer user_data)
{
  dtgtk_expander_set_drag_hover(widget, FALSE, FALSE, time);
}
#endif

static void dtgtk_expander_init(GtkDarktableExpander *expander)
{
}

#if GTK_CHECK_VERSION(4, 0, 0)
/* GTK4: no "size-allocate" signal (and GtkBox's size_allocate vfunc is
 * never called — the box uses an internal layout manager).  The revealer's
 * notify::child-revealed fires when the reveal transition completes (or
 * synchronously for instant reveals), i.e. exactly when the module's size
 * has changed — the equivalent hook for the scroll-to-module logic. */
static void _expander_reveal_changed(GObject *revealer,
                                     GParamSpec *pspec,
                                     gpointer user_data)
{
  GtkWidget *frame = dtgtk_bodyclip_get_child(DTGTK_BODYCLIP(revealer));
  _expander_resize(GTK_WIDGET(user_data), NULL, frame);
}

/* navigation to an already-expanded module: child-revealed never changes,
 * so kick the scroll once the pending resize has settled. */
static gboolean _expander_resize_idle(gpointer user_data)
{
  GtkWidget *expander = user_data;
  GtkWidget *frame = dtgtk_bodyclip_get_child(DTGTK_BODYCLIP(DTGTK_EXPANDER(expander)->frame));
  _expander_resize(expander, NULL, frame);
  return G_SOURCE_REMOVE;
}
#endif

// public functions
GtkWidget *dtgtk_expander_new(GtkWidget *header, GtkWidget *body)
{
  GtkDarktableExpander *expander;

  g_return_val_if_fail(GTK_IS_WIDGET(header), NULL);

  expander
      = g_object_new(dtgtk_expander_get_type(), "orientation", GTK_ORIENTATION_VERTICAL, "spacing", 0, NULL);
  expander->expanded = TRUE;
  dt_gui_add_class(GTK_WIDGET(expander), "dt_module_expanded");
  expander->header = header;
  expander->body = body;

  expander->header_evb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append(GTK_BOX(expander->header_evb), expander->header);
  expander->body_evb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  if(expander->body)
    gtk_box_append(GTK_BOX(expander->body_evb), expander->body);
  GtkWidget *frame = gtk_frame_new(NULL);
  gtk_frame_set_child(GTK_FRAME(frame), expander->body_evb);
  expander->frame = dtgtk_bodyclip_new();
  dtgtk_bodyclip_set_duration(DTGTK_BODYCLIP(expander->frame), 0);
  dtgtk_bodyclip_set_reveal_child(DTGTK_BODYCLIP(expander->frame), TRUE);
  dtgtk_bodyclip_set_child(DTGTK_BODYCLIP(expander->frame), frame);

  dt_gui_box_add(expander, expander->header_evb, expander->frame);

#if !GTK_CHECK_VERSION(4, 0, 0)
  g_signal_connect(expander->header_evb, "drag-begin", G_CALLBACK(_expander_drag_begin), NULL);
  g_signal_connect(expander->header_evb, "drag-end", G_CALLBACK(_expander_drag_end), NULL);
  g_signal_connect(expander, "drag-leave", G_CALLBACK(_expander_drag_leave), NULL);
#endif
#if GTK_CHECK_VERSION(4, 0, 0)
  g_signal_connect(expander->frame, "notify::child-revealed",
                   G_CALLBACK(_expander_reveal_changed), expander);
#else
  g_signal_connect(expander, "size-allocate", G_CALLBACK(_expander_resize), frame);
#endif

  return GTK_WIDGET(expander);
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
