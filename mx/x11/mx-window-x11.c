/*
 * mx-window-x11.c: A top-level window (X11 backend)
 *
 * Copyright 2010 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * Boston, MA 02111-1307, USA.
 *
 * Written by: Thomas Wood <thomas.wood@intel.com>
 *             Chris Lord <chris@linux.intel.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mx-window.h"
#include "mx-toolbar.h"
#include "mx-focus-manager.h"
#include "mx-private.h"
#include "mx-marshal.h"

#include <clutter/x11/clutter-x11.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/cursorfont.h>

G_DEFINE_TYPE (MxWindow, mx_window, MX_TYPE_WINDOW_BASE)

#define WINDOW_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MX_TYPE_WINDOW, MxWindowPrivate))

struct _MxWindowPrivate
{
  guint is_resizing   : 1;
  guint has_mapped    : 1;
  guint width_set     : 1;
  guint height_set    : 1;
  guint icon_changed  : 1;

  gint  is_moving;

  gfloat     last_width;
  gfloat     last_height;
  gfloat     natural_width;
  gfloat     natural_height;

  gint  drag_x_start;
  gint  drag_y_start;
  gint  drag_win_x_start;
  gint  drag_win_y_start;
  guint drag_width_start;
  guint drag_height_start;
};

enum
{
  PROP_STYLE = 1,
  PROP_STYLE_CLASS,
  PROP_STYLE_PSEUDO_CLASS,
  PROP_HAS_TOOLBAR,
  PROP_TOOLBAR,
  PROP_SMALL_SCREEN,
  PROP_ICON_NAME,
  PROP_ICON_COGL_TEXTURE,
  PROP_CLUTTER_STAGE,
  PROP_CHILD
};

enum
{
  DESTROY,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static gboolean
mx_window_get_has_border (MxWindow *self)
{
  return (mx_window_get_has_toolbar (self) &&
          !(mx_window_get_small_screen (self) ||
            clutter_stage_get_fullscreen (mx_window_get_clutter_stage (self))));
}

static void
mx_window_get_size (MxWindow *self,
                    gfloat   *width_p,
                    gfloat   *height_p,
                    gfloat   *pref_width_p,
                    gfloat   *pref_height_p)
{
  gboolean has_border;
  gfloat width, pref_width;
  ClutterActor *child, *toolbar;

  ClutterStage *stage = mx_window_get_clutter_stage (self);

  pref_width = width = 0;

  if (!stage)
    return;

  has_border = mx_window_get_has_border (self);

  child = mx_window_get_child (self);
  toolbar = (ClutterActor *)mx_window_get_toolbar (self);

  if (toolbar)
    clutter_actor_get_preferred_width (toolbar,
                                       -1,
                                       &width,
                                       &pref_width);

  if (child)
    {
      gfloat child_min_width, child_pref_width;
      clutter_actor_get_preferred_width (child,
                                         -1,
                                         &child_min_width,
                                         &child_pref_width);
      if (child_min_width > width)
        width = child_min_width;
      if (child_pref_width > pref_width)
        pref_width = child_pref_width;
    }

  if (width_p)
    *width_p = width + (has_border ? 2 : 0);
  if (pref_width_p)
    *pref_width_p = pref_width + (has_border ? 2 : 0);

  if (!height_p && !pref_height_p)
    return;

  if (height_p)
    *height_p = 0;
  if (pref_height_p)
    *pref_height_p = 0;

  if (toolbar)
    clutter_actor_get_preferred_height (toolbar,
                                        width,
                                        height_p,
                                        pref_height_p);

  if (child)
    {
      gfloat child_min_height, child_pref_height;
      clutter_actor_get_preferred_height (child,
                                          width,
                                          &child_min_height,
                                          &child_pref_height);
      if (height_p)
        *height_p += child_min_height;
      if (pref_height_p)
        *pref_height_p += child_pref_height;
    }

  if (has_border)
    {
      if (height_p)
        *height_p += 2;
      if (pref_height_p)
        *pref_height_p += 2;
    }
}

typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long inputMode;
    unsigned long status;
} PropMotifWmHints;

static void
mx_window_set_wm_hints (MxWindow *window)
{
  const gchar *icon_name;
  CoglHandle texture;
  Display *dpy;
  Window win;

  static Atom motif_wm_hints_atom = None;
  static Atom net_wm_icon = None;

  MxWindowPrivate *priv = window->priv;
  ClutterStage *stage = mx_window_get_clutter_stage (window);

  if (!stage)
    return;

  if (!CLUTTER_ACTOR_IS_MAPPED (stage))
    return;

  dpy = clutter_x11_get_default_display ();
  win = clutter_x11_get_stage_window (stage);

  if (win == None)
    return;

  if (!motif_wm_hints_atom)
    motif_wm_hints_atom = XInternAtom (dpy, "_MOTIF_WM_HINTS", False);

  /* Remove/add the window decorations */
  if (motif_wm_hints_atom)
    {
      PropMotifWmHints new_hints = {0,};
      PropMotifWmHints *hints;

      hints = &new_hints;

      hints->flags = 0x2;
      hints->functions = 0x0;
      hints->decorations = mx_window_get_has_toolbar (window) ? 0x0 : 0x1;

      XChangeProperty (dpy, win, motif_wm_hints_atom, motif_wm_hints_atom,
                       32, PropModeReplace, (guchar*) hints,
                       sizeof(PropMotifWmHints)/ sizeof (long));
    }

  if (!net_wm_icon)
    net_wm_icon = XInternAtom (dpy, "_NET_WM_ICON", False);

  /* Set the window icon */
  if (!priv->icon_changed)
    return;

  priv->icon_changed = FALSE;

  icon_name = mx_window_get_icon_name (window);
  if (!icon_name) icon_name = g_get_prgname ();

  texture = _mx_window_get_icon_cogl_texture (window);

  if ((icon_name || texture) && net_wm_icon)
    {
      guint width, height;
      guchar *data;
      gint size;

      /* Lookup icon for program name if there's no texture set */
      if (!texture)
        {
          texture = mx_icon_theme_lookup (mx_icon_theme_get_default (),
                                          icon_name, 32);
          if (!texture)
            {
              /* Remove the window icon */
              clutter_x11_trap_x_errors ();
              XDeleteProperty (dpy, win, net_wm_icon);
              clutter_x11_untrap_x_errors ();

              return;
            }
        }

      /* Get window icon size */
      width = cogl_texture_get_width (texture);
      height = cogl_texture_get_height (texture);
      size = cogl_texture_get_data (texture,
                                    COGL_PIXEL_FORMAT_BGRA_8888,
                                    width * 4,
                                    NULL);
      if (!size)
        {
          g_warning ("Unable to get texture data in "
                     "correct format for window icon");
          cogl_handle_unref (texture);
          return;
        }

      data = g_malloc (size + (sizeof (gulong) * 2));
      ((gulong *)data)[0] = width;
      ((gulong *)data)[1] = height;

      /* Get the window icon */
      if (cogl_texture_get_data (texture,
                                 COGL_PIXEL_FORMAT_BGRA_8888,
                                 width * 4,
                                 data + (sizeof (gulong) * 2)) == size)
        {
          /* Set the property */
          XChangeProperty (dpy, win, net_wm_icon, XA_CARDINAL,
                           32, PropModeReplace, data,
                           (width * height) + 2);
        }
      else
        g_warning ("Size mismatch when retrieving texture data "
                   "for window icon");

      cogl_handle_unref (texture);
      g_free (data);
    }
}

static void
mx_window_mapped_notify_cb (ClutterActor *actor,
                            GParamSpec   *pspec,
                            MxWindow     *window)
{
  if (CLUTTER_ACTOR_IS_MAPPED (actor))
    mx_window_set_wm_hints (window);
}

static void
mx_window_allocation_changed_cb (ClutterActor           *actor,
                                 ClutterActorBox        *box,
                                 ClutterAllocationFlags  flags,
                                 MxWindow               *window)
{
  gfloat width, height, stage_width, stage_height;

  MxWindowPrivate *priv = window->priv;

  actor = clutter_actor_get_stage (actor);
  clutter_actor_get_size (actor, &stage_width, &stage_height);

  /* Don't mess with the window size when we're full-screen, or you
   * get odd race conditions (and you never want to do it anyway)
   */
  if (clutter_stage_get_fullscreen (CLUTTER_STAGE (actor)))
    return;

  if (!priv->has_mapped)
    {
      Window win;
      Display *dpy;

      dpy = clutter_x11_get_default_display ();
      win = clutter_x11_get_stage_window (CLUTTER_STAGE (actor));

      if (!win)
        return;

      priv->has_mapped = TRUE;

      if (mx_window_get_small_screen (window))
        {
          XRRScreenResources *res;

          res = XRRGetScreenResourcesCurrent (dpy, win);

          XMoveResizeWindow (dpy, win,
                             0, 0,
                             res->modes[res->nmode].width,
                             res->modes[res->nmode].height);

          XRRFreeScreenResources (res);
        }
      else
        {
          /* Set the initial size of the window - if the user has set
           * a dimension, it will be used, otherwise the preferred size
           * will be used.
           */
          mx_window_get_size (window, NULL, NULL, &width, &height);

          if (priv->width_set)
            width = priv->natural_width + 2;
          if (priv->height_set)
            height = priv->natural_height + 2;

          XResizeWindow (dpy, win, width, height);
          stage_width = width;
          stage_height = height;
        }
    }
  else
    {
      /* Update minimum size */
      mx_window_get_size (window, &width, &height, NULL, NULL);
      if (width < 1.0)
        width = 1.0;
      if (height < 1.0)
        height = 1.0;
      clutter_stage_set_minimum_size (CLUTTER_STAGE (actor),
                                      (guint)width,
                                      (guint)height);
    }
}

static gboolean
mx_window_button_press_event_cb (ClutterActor       *actor,
                                 ClutterButtonEvent *event,
                                 MxWindow           *window)
{
  unsigned int width, height, border_width, depth, mask;
  Window win, root, child;
  int x, y, win_x, win_y;
  MxWindowPrivate *priv;
  Display *dpy;

  priv = window->priv;

  /* Bail out early in no-toolbar, small-screen or fullscreen mode */
  if (!mx_window_get_has_border (window))
    return FALSE;

  /* We're already moving/resizing */
  if (priv->is_moving != -1)
    return FALSE;

  /* We only care about the first mouse button */
  if (event->button != 1)
    return FALSE;

  priv->is_moving = clutter_input_device_get_device_id (event->device);

  win = clutter_x11_get_stage_window (CLUTTER_STAGE (actor));
  dpy = clutter_x11_get_default_display ();

  if (win == None)
    return FALSE;

  /* Get the initial width/height */
  XGetGeometry (dpy, win, &root, &x, &y, &width, &height,
                &border_width, &depth);

  priv->drag_win_x_start = x;
  priv->drag_win_y_start = y;
  priv->drag_width_start = width;
  priv->drag_height_start = height;

  /* Get the initial cursor position */
  XQueryPointer (dpy, root, &root, &child, &x, &y, &win_x, &win_y, &mask);

  priv->drag_x_start = x;
  priv->drag_y_start = y;

  /* Disable motion events on other actors */
  clutter_set_motion_events_enabled (FALSE);

  /* Grab the mouse so that we receive the release if the cursor
   * goes off-stage.
   */
  clutter_grab_pointer_for_device (actor, priv->is_moving);

  return TRUE;
}

static void
mx_window_button_release (MxWindow *window)
{
  MxWindowPrivate *priv = window->priv;

  if (priv->is_moving != -1)
    {
      clutter_ungrab_pointer_for_device (priv->is_moving);
      clutter_set_motion_events_enabled (TRUE);
      priv->is_moving = -1;
    }
}

static gboolean
mx_window_button_release_event_cb (ClutterActor       *actor,
                                   ClutterButtonEvent *event,
                                   MxWindow           *window)
{
  MxWindowPrivate *priv = window->priv;

  if ((clutter_input_device_get_device_id (event->device) == priv->is_moving) &&
      (event->button == 1))
    {
      mx_window_button_release (window);
      return TRUE;
    }

  return FALSE;
}

static gboolean
mx_window_captured_event_cb (ClutterActor *actor,
                             ClutterEvent *event,
                             MxWindow     *window)
{
  MxWindowPrivate *priv = window->priv;
  ClutterActor *resize_grip = _mx_window_get_resize_grip (window);

  switch (event->type)
    {
    case CLUTTER_MOTION:
      /* Check if we're over the resize handle */
      if ((priv->is_moving == -1) && mx_window_get_has_border (window) &&
          clutter_stage_get_user_resizable (CLUTTER_STAGE (actor)) &&
          resize_grip)
        {
          gint x, y;
          Window win;
          Display *dpy;
          gfloat height, width, rwidth, rheight;

          static Cursor csoutheast = 0;
          ClutterMotionEvent *mevent = &event->motion;

          win = clutter_x11_get_stage_window (CLUTTER_STAGE (actor));
          dpy = clutter_x11_get_default_display ();

          if (win == None)
            return FALSE;

          clutter_actor_get_size (actor, &width, &height);

          x = mevent->x;
          y = mevent->y;

          /* Create the resize cursor */
          if (!csoutheast)
            csoutheast = XCreateFontCursor (dpy, XC_bottom_right_corner);

          clutter_actor_get_size (resize_grip, &rwidth, &rheight);

          /* Set the cursor if necessary */
          if (x > width - rwidth && y > height - rheight)
            {
              if (!priv->is_resizing)
                {
                  XDefineCursor (dpy, win, csoutheast);
                  priv->is_resizing = TRUE;
                }
              return TRUE;
            }
          else
            {
              if (priv->is_resizing)
                {
                  XUndefineCursor (dpy, win);
                  priv->is_resizing = FALSE;
                }
              return FALSE;
            }
        }
      return FALSE;

    case CLUTTER_BUTTON_PRESS:
      /* We want resizing to happen even if there are active widgets
       * underneath the resize-handle.
       */
      if (priv->is_resizing)
        return mx_window_button_press_event_cb (actor, &event->button, window);
      else
        return FALSE;

    default:
      return FALSE;
    }
}

static gboolean
mx_window_motion_event_cb (ClutterActor       *actor,
                           ClutterMotionEvent *event,
                           MxWindow           *window)
{
  gint offsetx, offsety;
  gint x, y, winx, winy;
  guint mask;
  MxWindowPrivate *priv;
  Window win, root_win, root, child;
  Display *dpy;
  gfloat height, width;

  priv = window->priv;

  /* Ignore motion events when we don't have a border, or if they're not from
   * our grabbed device.
   */
  if (!mx_window_get_has_border (window) ||
      (clutter_input_device_get_device_id (event->device) != priv->is_moving))
    return FALSE;

  /* Check if the mouse button is still down - if the user releases the
   * mouse button while outside of the stage (which can happen), we don't
   * get the release event.
   */
  if (!(event->modifier_state & CLUTTER_BUTTON1_MASK))
    {
      mx_window_button_release (window);
      return TRUE;
    }

  win = clutter_x11_get_stage_window (CLUTTER_STAGE (actor));
  dpy = clutter_x11_get_default_display ();

  if (win == None)
    return FALSE;

  clutter_actor_get_size (actor, &width, &height);

  x = event->x;
  y = event->y;

  /* Move/resize the window if we're dragging */
  offsetx = priv->drag_x_start;
  offsety = priv->drag_y_start;

  root_win = clutter_x11_get_root_window ();
  XQueryPointer (dpy, root_win, &root, &child, &x, &y, &winx, &winy, &mask);

  if (priv->is_resizing)
    {
      XRRScreenResources *res;
      gfloat min_width, min_height;

      mx_window_get_size (window, &min_width, &min_height, NULL, NULL);

      x = MAX (priv->drag_width_start + (x - priv->drag_x_start), min_width);
      y = MAX (priv->drag_height_start + (y - priv->drag_y_start), min_height);

      res = XRRGetScreenResourcesCurrent (dpy, win);
      width = res->modes[res->nmode].width;
      height = res->modes[res->nmode].height;
      XRRFreeScreenResources (res);

      width = MIN (x, width - priv->drag_win_x_start);
      height = MIN (y, height - priv->drag_win_y_start);

      clutter_actor_set_size (actor, width, height);
    }
  else
    XMoveWindow (dpy, win,
                 MAX (0, priv->drag_win_x_start + x - offsetx),
                 MAX (0, priv->drag_win_y_start + y - offsety));

  return TRUE;
}

static void
mx_window_realize_cb (ClutterActor *actor,
                      MxWindow     *window)
{
  gboolean width_set, height_set;

  MxWindowPrivate *priv = window->priv;

  /* See if the user has set a size on the window to use on initial map */
  g_object_get (G_OBJECT (actor),
                "natural-width", &priv->natural_width,
                "natural-width-set", &width_set,
                "natural-height", &priv->natural_height,
                "natural-height-set", &height_set,
                NULL);

  priv->width_set = width_set;
  priv->height_set = height_set;
}

static void
mx_window_fullscreen_set_cb (ClutterStage *stage,
                             GParamSpec   *pspec,
                             MxWindow     *self)
{
  MxWindowPrivate *priv = self->priv;

  /* If we're in small-screen mode, make sure the size gets reset
   * correctly.
   */
  if (!clutter_stage_get_fullscreen (stage) &&
      mx_window_get_small_screen (self))
    priv->has_mapped = FALSE;
}

static void
mx_window_notify_small_screen_cb (MxWindow   *self,
                                  GParamSpec *pspec)
{
  ClutterActor *resize_grip;
  gboolean small_screen;
  ClutterStage *stage;
  Display *dpy;
  Window win;

  MxWindowPrivate *priv = self->priv;

  stage = mx_window_get_clutter_stage (self);
  if (!stage)
    return;

  win = clutter_x11_get_stage_window (stage);
  dpy = clutter_x11_get_default_display ();

  /* If there's no window, we're not mapped yet - we'll resize
   * on map.
   */
  if (win == None)
    return;

  small_screen = mx_window_get_small_screen (self);
  resize_grip = _mx_window_get_resize_grip (self);

  /* In case we were in the middle of a move/resize */
  if (priv->is_moving != -1)
    {
      clutter_ungrab_pointer_for_device (priv->is_moving);
      clutter_set_motion_events_enabled (TRUE);
      priv->is_moving = -1;
      if (priv->is_resizing)
        {
          XUndefineCursor (dpy, win);
          priv->is_resizing = FALSE;
        }
    }

  if (small_screen)
    {
      if (!clutter_stage_get_fullscreen (stage))
        {
          int width, height;
          XRRScreenResources *res;

          clutter_actor_get_size (CLUTTER_ACTOR (stage),
                                  &priv->last_width,
                                  &priv->last_height);

          /* Move/size ourselves to the size of the screen. We could
           * also set ourselves as not resizable, but a WM that respects
           * our small-screen mode won't give the user controls to
           * modify the window, and if it does, just let them.
           */
          res = XRRGetScreenResourcesCurrent (dpy, win);
          width = res->modes[res->nmode].width;
          height = res->modes[res->nmode].height;
          XRRFreeScreenResources (res);

          XMoveResizeWindow (dpy, win, 0, 0, width, height);
        }

      if (resize_grip)
        clutter_actor_hide (resize_grip);
    }
  else
    {
      /* If we started off in small-screen mode, our last size won't
       * be known, so use the preferred size.
       */
      if (!priv->last_width && !priv->last_height)
        mx_window_get_size (self, NULL, NULL,
                            &priv->last_width, &priv->last_height);

      clutter_actor_set_size (CLUTTER_ACTOR (stage),
                              priv->last_width,
                              priv->last_height);

      if (resize_grip && mx_window_get_has_toolbar (self) &&
          clutter_stage_get_user_resizable (stage))
        {
          ClutterActor *child = mx_window_get_child (self);

          clutter_actor_show (resize_grip);
          if (child)
            clutter_actor_raise (resize_grip, child);
        }
    }
}

static void
mx_window_notify_icon_changed_cb (MxWindow *window)
{
  window->priv->icon_changed = TRUE;
  mx_window_set_wm_hints (window);
}

static void
mx_window_constructed (GObject *object)
{
  ClutterStage *stage;
  MxWindow *window = MX_WINDOW (object);

  G_OBJECT_CLASS (mx_window_parent_class)->constructed (object);

  stage = mx_window_get_clutter_stage (window);

  g_signal_connect (stage, "notify::mapped",
                    G_CALLBACK (mx_window_mapped_notify_cb), object);
  g_signal_connect (stage, "allocation-changed",
                    G_CALLBACK (mx_window_allocation_changed_cb), object);
  g_signal_connect (mx_window_get_toolbar (window), "allocation-changed",
                    G_CALLBACK (mx_window_allocation_changed_cb), object);
  g_signal_connect (stage, "notify::fullscreen-set",
                    G_CALLBACK (mx_window_fullscreen_set_cb), object);
  g_signal_connect (stage, "realize",
                    G_CALLBACK (mx_window_realize_cb), object);
  g_signal_connect (stage, "button-press-event",
                    G_CALLBACK (mx_window_button_press_event_cb), object);
  g_signal_connect (stage, "button-release-event",
                    G_CALLBACK (mx_window_button_release_event_cb), object);
  g_signal_connect (stage, "captured-event",
                    G_CALLBACK (mx_window_captured_event_cb), object);
  g_signal_connect (stage, "motion-event",
                    G_CALLBACK (mx_window_motion_event_cb), object);

  g_signal_connect (object, "notify::small-screen",
                    G_CALLBACK (mx_window_notify_small_screen_cb), NULL);
  g_signal_connect (object, "notify::icon-name",
                    G_CALLBACK (mx_window_notify_icon_changed_cb), NULL);
  g_signal_connect (object, "notify::icon-cogl-texture",
                    G_CALLBACK (mx_window_notify_icon_changed_cb), NULL);
}

static void
mx_window_class_init (MxWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MxWindowPrivate));

  object_class->constructed = mx_window_constructed;

  signals[DESTROY] = g_signal_new ("destroy",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   G_STRUCT_OFFSET (MxWindowClass, destroy),
                                   NULL, NULL,
                                   _mx_marshal_VOID__VOID,
                                   G_TYPE_NONE, 0);
}

static void
mx_window_init (MxWindow *self)
{
  MxWindowPrivate *priv = self->priv = WINDOW_PRIVATE (self);

  priv->is_moving = -1;
  priv->icon_changed = TRUE;
}

void
mx_window_get_window_position (MxWindow *window, gint *x, gint *y)
{
  unsigned int width, height, border_width, depth;
  MxWindowPrivate *priv;
  Window win, root_win;
  ClutterStage *stage;
  int win_x, win_y;
  Display *dpy;

  g_return_if_fail (MX_IS_WINDOW (window));

  priv = window->priv;
  stage = mx_window_get_clutter_stage (window);

  if (!stage)
    return;

  if (clutter_stage_get_fullscreen (stage) ||
      mx_window_get_small_screen (window))
    {
      if (x)
        *x = 0;
      if (y)
        *y = 0;
      return;
    }

  win = clutter_x11_get_stage_window (stage);
  dpy = clutter_x11_get_default_display ();

  XGetGeometry (dpy, win,
                &root_win,
                &win_x, &win_y,
                &width, &height,
                &border_width,
                &depth);

  if (x)
    *x = win_x;
  if (y)
    *y = win_y;
}

void
mx_window_set_window_position (MxWindow *window, gint x, gint y)
{
  Window win;
  Display *dpy;
  ClutterStage *stage;
  MxWindowPrivate *priv;

  g_return_if_fail (MX_IS_WINDOW (window));

  priv = window->priv;

  stage = mx_window_get_clutter_stage (window);

  if (!stage)
    return;

  /* Don't try to move a full-screen/small-screen window */
  if (clutter_stage_get_fullscreen (stage) ||
      mx_window_get_small_screen (window))
    return;

  win = clutter_x11_get_stage_window (stage);
  dpy = clutter_x11_get_default_display ();

  XMoveWindow (dpy, win, x, y);
}

