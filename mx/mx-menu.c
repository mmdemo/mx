/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * mx-menu.c: menu class
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:mx-menu
 * @short_description: a menu actor representing a list of user actions
 *
 * #MxMenu displays a list of user actions, defined by a list of
 * #MxAction<!-- -->s. The menu list will appear above all other actors.
 */

#include "mx-menu.h"
#include "mx-label.h"
#include "mx-box-layout.h"
#include "mx-icon.h"
#include "mx-icon-theme.h"
#include "mx-stylable.h"

static void mx_stylable_iface_init (MxStylableIface *iface);

G_DEFINE_TYPE_WITH_CODE (MxMenu, mx_menu, MX_TYPE_FLOATING_WIDGET,
                         G_IMPLEMENT_INTERFACE (MX_TYPE_STYLABLE,
                                                mx_stylable_iface_init))

#define MENU_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), MX_TYPE_MENU, MxMenuPrivate))

typedef struct
{
  MxAction *action;
  MxWidget *box;
} MxMenuChild;

struct _MxMenuPrivate
{
  gint     spacing;
  GArray  *children;
  gboolean transition_out;

  ClutterActor *stage;
  gulong captured_event_handler;
};

enum
{
  ACTION_ACTIVATED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static gboolean mx_menu_captured_event_handler (ClutterActor *actor,
                                                ClutterEvent *event,
                                                ClutterActor *menu);

static void
mx_stylable_iface_init (MxStylableIface *iface)
{
  static gboolean is_initialised = FALSE;

  if (!is_initialised)
    {
      GParamSpec *pspec;

      is_initialised = TRUE;

      pspec = g_param_spec_int ("x-mx-spacing",
                                "Spacing",
                                "Spacing to use between elements inside the "
                                "menu.",
                                0, G_MAXINT, 8,
                                G_PARAM_READWRITE);
      mx_stylable_iface_install_property (iface, MX_TYPE_MENU, pspec);
    }
}

static void
mx_menu_get_property (GObject    *object,
                      guint       property_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mx_menu_set_property (GObject      *object,
                      guint         property_id,
                      const GValue *value,
                      GParamSpec   *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
mx_menu_free_action_at (MxMenu   *menu,
                        gint      index,
                        gboolean  remove_action)
{
  MxMenuPrivate *priv = menu->priv;
  MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                        index);

  clutter_actor_unparent (CLUTTER_ACTOR (child->box));
  g_object_unref (child->action);

  if (remove_action)
    g_array_remove_index (priv->children, index);
}

static void
mx_menu_dispose (GObject *object)
{
  MxMenu *menu = MX_MENU (object);
  MxMenuPrivate *priv = menu->priv;

  if (priv->children)
    {
      gint i;
      for (i = 0; i < priv->children->len; i++)
        mx_menu_free_action_at (menu, i, FALSE);
      g_array_free (priv->children, TRUE);
      priv->children = NULL;
    }


  G_OBJECT_CLASS (mx_menu_parent_class)->dispose (object);
}

static void
mx_menu_finalize (GObject *object)
{
  G_OBJECT_CLASS (mx_menu_parent_class)->finalize (object);
}

static void
mx_menu_get_preferred_width (ClutterActor *actor,
                             gfloat        for_height,
                             gfloat       *min_width_p,
                             gfloat       *natural_width_p)
{
  gint i;
  MxPadding padding;
  gfloat min_width, nat_width;

  MxMenuPrivate *priv = MX_MENU (actor)->priv;

  /* Add padding and the size of the widest child */
  mx_widget_get_padding (MX_WIDGET (actor), &padding);
  min_width = nat_width = 0;
  for (i = 0; i < priv->children->len; i++)
    {
      gfloat child_min_width, child_nat_width;
      MxMenuChild *child;

      child = &g_array_index (priv->children, MxMenuChild, i);

      clutter_actor_get_preferred_width (CLUTTER_ACTOR (child->box),
                                         for_height,
                                         &child_min_width,
                                         &child_nat_width);

      if (child_min_width > min_width)
        min_width = child_min_width;
      if (child_nat_width > nat_width)
        nat_width = child_nat_width;
    }

  if (min_width_p)
    *min_width_p = min_width + padding.left + padding.right;
  if (natural_width_p)
    *natural_width_p = nat_width + padding.left + padding.right;
}

static void
mx_menu_get_preferred_height (ClutterActor *actor,
                              gfloat        for_width,
                              gfloat       *min_height_p,
                              gfloat       *natural_height_p)
{
  gint i;
  MxPadding padding;
  gfloat min_height, nat_height;

  MxMenuPrivate *priv = MX_MENU (actor)->priv;

  /* Add padding and the cumulative height of the children */
  mx_widget_get_padding (MX_WIDGET (actor), &padding);
  min_height = nat_height = padding.top + padding.bottom;
  for (i = 0; i < priv->children->len; i++)
    {
      gfloat child_min_height, child_nat_height;

      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                            i);

      clutter_actor_get_preferred_height (CLUTTER_ACTOR (child->box),
                                          for_width,
                                          &child_min_height,
                                          &child_nat_height);

      min_height += child_min_height + 1;
      nat_height += child_nat_height + 1;
    }

  if (min_height_p)
    *min_height_p = min_height;
  if (natural_height_p)
    *natural_height_p = nat_height;
}

static void
mx_menu_allocate (ClutterActor           *actor,
                  const ClutterActorBox  *box,
                  ClutterAllocationFlags  flags)
{
  gint i;
  MxPadding padding;
  ClutterActorBox child_box;
  MxMenuPrivate *priv = MX_MENU (actor)->priv;

  /* Allocate children */
  mx_widget_get_padding (MX_WIDGET (actor), &padding);
  child_box.x1 = padding.left;
  child_box.y1 = padding.top;
  child_box.x2 = box->x2 - box->x1 - padding.right;
  for (i = 0; i < priv->children->len; i++)
    {
      gfloat natural_height;

      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                            i);

      clutter_actor_get_preferred_height (CLUTTER_ACTOR (child->box),
                                          child_box.x2 - child_box.x1,
                                          NULL,
                                          &natural_height);
      child_box.y2 = child_box.y1 + natural_height;

      clutter_actor_allocate (CLUTTER_ACTOR (child->box), &child_box, flags);

      child_box.y1 = child_box.y2 + 1;
    }

  /* Chain up and allocate background */
  CLUTTER_ACTOR_CLASS (mx_menu_parent_class)->allocate (actor, box, flags);
}

static void
mx_menu_floating_paint (ClutterActor *menu)
{
  gint i;
  MxMenuPrivate *priv = MX_MENU (menu)->priv;

  /* Chain up to get background */
  MX_FLOATING_WIDGET_CLASS (mx_menu_parent_class)->floating_paint (menu);

  /* Paint children */
  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                            i);
      clutter_actor_paint (CLUTTER_ACTOR (child->box));
    }
}

static void
mx_menu_floating_pick (ClutterActor       *menu,
                       const ClutterColor *color)
{
  gint i;
  MxMenuPrivate *priv = MX_MENU (menu)->priv;

  /* chain up to get bounding rectangle */

  MX_FLOATING_WIDGET_CLASS (mx_menu_parent_class)->floating_pick (menu, color);

  /* pick children */
  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild, i);

      if (clutter_actor_should_pick_paint (CLUTTER_ACTOR (child->box)))
        {
          clutter_actor_paint (CLUTTER_ACTOR (child->box));
        }
    }
}

static void
stage_weak_notify (MxMenu       *menu,
                   ClutterStage *stage)
{
  menu->priv->stage = NULL;
}

static void
mx_menu_map (ClutterActor *actor)
{
  gint i;
  MxMenuPrivate *priv = MX_MENU (actor)->priv;

  CLUTTER_ACTOR_CLASS (mx_menu_parent_class)->map (actor);

  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                            i);
      clutter_actor_map (CLUTTER_ACTOR (child->box));
    }

  /* set up a capture so we can close the menu if the user clicks outside it */
  priv->stage = clutter_actor_get_stage (actor);
  g_object_weak_ref (G_OBJECT (priv->stage), (GWeakNotify) stage_weak_notify,
                     actor);

  priv->captured_event_handler =
    g_signal_connect (priv->stage,
                      "captured-event",
                      G_CALLBACK (mx_menu_captured_event_handler),
                      actor);
}

static void
mx_menu_unmap (ClutterActor *actor)
{
  gint i;
  MxMenuPrivate *priv = MX_MENU (actor)->priv;

  CLUTTER_ACTOR_CLASS (mx_menu_parent_class)->unmap (actor);

  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                            i);
      clutter_actor_unmap (CLUTTER_ACTOR (child->box));
    }

  if (priv->stage)
    {
      g_signal_handler_disconnect (priv->stage, priv->captured_event_handler);
      priv->captured_event_handler = 0;

      g_object_weak_unref (G_OBJECT (priv->stage),
                           (GWeakNotify) stage_weak_notify,
                           actor);
      priv->stage = NULL;
    }
}

static gboolean
mx_menu_event (ClutterActor *actor,
               ClutterEvent *event)
{
  /* We swallow mouse events so that they don't fall through to whatever's
   * beneath us.
   */
  switch (event->type)
    {
    case CLUTTER_MOTION:
    case CLUTTER_BUTTON_PRESS:
    case CLUTTER_BUTTON_RELEASE:
    case CLUTTER_SCROLL:
      return TRUE;
    default:
      return FALSE;
    }
}

static gboolean
mx_menu_captured_event_handler (ClutterActor *actor,
                                ClutterEvent *event,
                                ClutterActor *menu)
{
  int i;
  ClutterActor *source;
  MxMenuPrivate *priv = MX_MENU (menu)->priv;

  /* allow the event to continue if it is applied to the menu or any of its
   * children
   */
  source = clutter_event_get_source (event);
  if (source == menu)
    return FALSE;
  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child;

      child = &g_array_index (priv->children, MxMenuChild, i);

      if (source == (ClutterActor*) child->box)
        return FALSE;
    }

  /* hide the menu if the user clicks outside the menu */
  if (event->type == CLUTTER_BUTTON_PRESS)
    {
      if (clutter_actor_get_animation (menu))
        {
          clutter_animation_completed (clutter_actor_get_animation (menu));

          return FALSE;
        }


      clutter_actor_set_reactive (menu, FALSE);
      clutter_actor_animate (menu, CLUTTER_LINEAR, 250,
                             "opacity", (guchar) 0,
                             "signal-swapped::completed", clutter_actor_hide,
                             menu,
                             NULL);
    }


  return TRUE;
}

static void
mx_menu_show (ClutterActor *actor)
{
  ClutterAnimation *animation = NULL;

  /* set reactive and opacity, since these may have been set by the fade-out
   * animation (e.g. from captured_event_handler or button_release_cb) */
  if ((animation = clutter_actor_get_animation (actor)))
    {
      clutter_animation_completed (animation);
    }
  clutter_actor_set_reactive (actor, TRUE);
  clutter_actor_set_opacity (actor, 0xff);

  /* chain up to run show after re-setting properties above */
  CLUTTER_ACTOR_CLASS (mx_menu_parent_class)->show (actor);
}

static void
mx_menu_hide (ClutterActor *actor)
{
  CLUTTER_ACTOR_CLASS (mx_menu_parent_class)->hide (actor);
}

static void
mx_menu_style_changed (MxMenu              *menu,
                       MxStyleChangedFlags  flags)
{
  MxMenuPrivate *priv = menu->priv;
  gint i, spacing;

  mx_stylable_get (MX_STYLABLE (menu),
                   "x-mx-spacing", &spacing,
                   NULL);

  if (priv->spacing != spacing)
    {
      priv->spacing = spacing;
      for (i = 0; i < priv->children->len; i++)
        {
          MxMenuChild *child = &g_array_index (priv->children, MxMenuChild, i);
          mx_box_layout_set_spacing (MX_BOX_LAYOUT (child->box), priv->spacing);
        }
    }

  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child;

      child = &g_array_index (priv->children, MxMenuChild, i);

      mx_stylable_style_changed (MX_STYLABLE (child->box), flags);
    }
}

static void
mx_menu_class_init (MxMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);
  MxFloatingWidgetClass *float_class = MX_FLOATING_WIDGET_CLASS (klass);

  g_type_class_add_private (klass, sizeof (MxMenuPrivate));

  object_class->get_property = mx_menu_get_property;
  object_class->set_property = mx_menu_set_property;
  object_class->dispose = mx_menu_dispose;
  object_class->finalize = mx_menu_finalize;

  actor_class->show = mx_menu_show;
  actor_class->hide = mx_menu_hide;
  actor_class->get_preferred_width = mx_menu_get_preferred_width;
  actor_class->get_preferred_height = mx_menu_get_preferred_height;
  actor_class->allocate = mx_menu_allocate;
  actor_class->map = mx_menu_map;
  actor_class->unmap = mx_menu_unmap;
  actor_class->event = mx_menu_event;

  float_class->floating_paint = mx_menu_floating_paint;
  float_class->floating_pick = mx_menu_floating_pick;

  signals[ACTION_ACTIVATED] =
    g_signal_new ("action-activated",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (MxMenuClass, action_activated),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1, MX_TYPE_ACTION);
}

static void
mx_menu_init (MxMenu *self)
{
  MxMenuPrivate *priv = self->priv = MENU_PRIVATE (self);

  priv->children = g_array_new (FALSE, FALSE, sizeof (MxMenuChild));

  g_object_set (G_OBJECT (self),
                "show-on-set-parent", FALSE,
                NULL);

  g_signal_connect (self, "style-changed", G_CALLBACK (mx_menu_style_changed),
                    NULL);
}

/**
 * mx_menu_new:
 *
 * Create a new #MxMenu
 *
 * Returns: a newly allocated #MxMenu
 */
ClutterActor *
mx_menu_new (void)
{
  return g_object_new (MX_TYPE_MENU, NULL);
}

static void
mx_menu_button_release_cb (MxBoxLayout  *box,
                           ClutterEvent *event,
                           MxAction     *action)
{
  MxMenu *menu;

  menu = MX_MENU (clutter_actor_get_parent (CLUTTER_ACTOR (box)));

  /* set the menu unreactive to prevent other items being hilighted */
  clutter_actor_set_reactive ((ClutterActor*) menu, FALSE);


  g_object_ref (menu);
  g_object_ref (action);

  g_signal_emit (menu, signals[ACTION_ACTIVATED], 0, action);
  g_signal_emit_by_name (action, "activated");

  clutter_actor_animate (CLUTTER_ACTOR (menu), CLUTTER_LINEAR, 250,
                         "opacity", (guchar) 0,
                         "signal-swapped::completed", clutter_actor_hide, menu,
                         NULL);

  g_object_unref (action);
  g_object_unref (menu);

}

/**
 * mx_menu_add_action:
 * @menu: A #MxMenu
 * @action: A #MxAction
 *
 * Append @action to @menu.
 *
 */
void
mx_menu_add_action (MxMenu   *menu,
                    MxAction *action)
{
  MxMenuChild child;
  ClutterActor *label, *icon;

  g_return_if_fail (MX_IS_MENU (menu));
  g_return_if_fail (MX_IS_ACTION (action));

  MxMenuPrivate *priv = menu->priv;

  child.action = g_object_ref_sink (action);
  /* TODO: Connect to notify signals in case action properties change */
  child.box = g_object_new (MX_TYPE_BOX_LAYOUT,
                            "reactive", TRUE,
                            "spacing", priv->spacing,
                            NULL);

  if (mx_action_get_icon (action))
    {
      MxIconTheme *theme = mx_icon_theme_get_default ();

      if (mx_icon_theme_has_icon (theme, mx_action_get_icon (action)))
        {
          icon = mx_icon_new ();
          mx_icon_set_icon_name (MX_ICON (icon), mx_action_get_icon (action));

          clutter_container_add_actor (CLUTTER_CONTAINER (child.box), icon);
          clutter_container_child_set (CLUTTER_CONTAINER (child.box), icon,
                                       "y-fill", FALSE,
                                       NULL);
        }
    }

  label = mx_label_new_with_text (mx_action_get_display_name (action));

  clutter_container_add_actor (CLUTTER_CONTAINER (child.box), label);

  clutter_container_child_set (CLUTTER_CONTAINER (child.box), label,
                               "y-fill", FALSE,
                               NULL);

  g_signal_connect (child.box, "button-release-event",
                    G_CALLBACK (mx_menu_button_release_cb), action);
  clutter_actor_set_parent (CLUTTER_ACTOR (child.box),
                            CLUTTER_ACTOR (menu));

  g_array_append_val (priv->children, child);

  clutter_actor_queue_relayout (CLUTTER_ACTOR (menu));
}

/**
 * mx_menu_remove_action:
 * @menu: A #MxMenu
 * @action: A #MxAction
 *
 * Remove @action from @menu.
 *
 */
void
mx_menu_remove_action (MxMenu   *menu,
                       MxAction *action)
{
  gint i;

  g_return_if_fail (MX_IS_MENU (menu));
  g_return_if_fail (MX_IS_ACTION (action));

  MxMenuPrivate *priv = menu->priv;

  for (i = 0; i < priv->children->len; i++)
    {
      MxMenuChild *child = &g_array_index (priv->children, MxMenuChild,
                                            i);

      if (child->action == action)
        {
          mx_menu_free_action_at (menu, i, TRUE);
          break;
        }
    }
}

/**
 * mx_menu_remove_all:
 * @menu: A #MxMenu
 *
 * Remove all the actions from @menu.
 *
 */
#ifndef MX_DISABLE_DEPRECATED
void
mx_menu_clear (MxMenu *menu)
{
  g_warning ("mx_menu_clear is deprecated."
             " Use mx_menu_remove_all instead.");
  mx_menu_remove_all (menu);
}
#endif
void
mx_menu_remove_all (MxMenu *menu)
{
  gint i;

  g_return_if_fail (MX_IS_MENU (menu));

  MxMenuPrivate *priv = menu->priv;

  if (!priv->children->len)
    return;

  for (i = 0; i < priv->children->len; i++)
    mx_menu_free_action_at (menu, i, FALSE);

  g_array_remove_range (priv->children, 0, priv->children->len);
}

/**
 * mx_menu_show_with_position:
 * @menu: A #MxMenu
 * @x: X position
 * @y: Y position
 *
 * Moves the menu to the specified position and shows it.
 *
 */
void
mx_menu_show_with_position (MxMenu *menu,
                            gfloat  x,
                            gfloat  y)
{
  g_return_if_fail (MX_IS_MENU (menu));

  clutter_actor_set_position (CLUTTER_ACTOR (menu), x, y);
  clutter_actor_show (CLUTTER_ACTOR (menu));
}
