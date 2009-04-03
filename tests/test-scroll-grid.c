#include <stdio.h>
#include <stdlib.h>

#include <clutter/clutter.h>
#include <nbtk/nbtk.h>

void
stage_size_notify_cb (ClutterActor *stage,
                      GParamSpec *pspec,
                      ClutterActor *table)
{
  guint width, height;

  clutter_actor_get_size (stage, &width, &height);
  clutter_actor_set_size (table, width, height);
}

static void
swap_orientation (ClutterActor *button,
                  NbtkGrid     *grid)
{
  nbtk_grid_set_column_major (grid,
                              !nbtk_grid_get_column_major (grid));
}

int
main (int argc, char *argv[])
{
  NbtkWidget *scroll, *grid;
  ClutterActor *stage;
  int i;

  clutter_init (&argc, &argv);

  nbtk_style_load_from_file (nbtk_style_get_default (),
                             "style/default.css", NULL);

  stage = clutter_stage_get_default ();
  clutter_actor_set_size (stage, 640, 480);
  clutter_stage_set_user_resizable (CLUTTER_STAGE (stage), TRUE);

  scroll = (NbtkWidget *) nbtk_scroll_view_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (stage),
                               CLUTTER_ACTOR (scroll));
  clutter_actor_set_size (CLUTTER_ACTOR (scroll), 640, 480);

  grid = nbtk_grid_new ();
  clutter_container_add_actor (CLUTTER_CONTAINER (scroll),
                               CLUTTER_ACTOR (grid));

  for (i = 1; i <= 200; i++)
    {
      NbtkWidget *button;
      gchar *label;

      label = g_strdup_printf ("Button %d", i);

      button = nbtk_button_new_with_label (label);
      clutter_container_add_actor (CLUTTER_CONTAINER (grid),
                                   CLUTTER_ACTOR (button));
      if (i == 1)
        g_signal_connect (button,
                          "clicked",
                          G_CALLBACK (swap_orientation),
                          grid);

      g_free (label);
    }

  g_signal_connect (stage, "notify::width",
                    G_CALLBACK (stage_size_notify_cb), scroll);
  g_signal_connect (stage, "notify::height",
                    G_CALLBACK (stage_size_notify_cb), scroll);
  clutter_actor_show (stage);
  clutter_main ();

  return EXIT_SUCCESS;
}