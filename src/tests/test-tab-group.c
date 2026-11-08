/* test-tab-group.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <gtk/gtk.h>

#include "editor-tab-group-private.h"

static void
test_round_trip (void)
{
  g_autoptr(EditorTabGroup) group = _editor_tab_group_new ("abc", "Notes", "green");
  g_autoptr(EditorTabGroup) copy = NULL;
  g_autoptr(GVariant) state = NULL;

  _editor_tab_group_set_collapsed (group, TRUE);

  state = g_variant_ref_sink (_editor_tab_group_to_variant (group));
  g_assert_true (g_variant_is_of_type (state, G_VARIANT_TYPE_VARDICT));

  copy = _editor_tab_group_new_from_variant (state);
  g_assert_nonnull (copy);
  g_assert_cmpstr (_editor_tab_group_get_id (copy), ==, "abc");
  g_assert_cmpstr (_editor_tab_group_get_name (copy), ==, "Notes");
  g_assert_cmpstr (_editor_tab_group_get_color (copy), ==, "green");
  g_assert_true (_editor_tab_group_get_collapsed (copy));
}

static void
test_unnamed (void)
{
  g_autoptr(EditorTabGroup) group = _editor_tab_group_new (NULL, "", NULL);
  g_autoptr(EditorTabGroup) copy = NULL;
  g_autoptr(GVariant) state = NULL;

  /* An id is generated and a default color is used */
  g_assert_nonnull (_editor_tab_group_get_id (group));
  g_assert_cmpstr (_editor_tab_group_get_id (group), !=, "");
  g_assert_null (_editor_tab_group_get_name (group));
  g_assert_cmpstr (_editor_tab_group_get_color (group), ==, _editor_tab_group_get_colors ()[0]);

  state = g_variant_ref_sink (_editor_tab_group_to_variant (group));
  copy = _editor_tab_group_new_from_variant (state);
  g_assert_cmpstr (_editor_tab_group_get_id (copy), ==, _editor_tab_group_get_id (group));
  g_assert_null (_editor_tab_group_get_name (copy));
  g_assert_false (_editor_tab_group_get_collapsed (copy));
}

static void
test_invalid_state (void)
{
  g_autoptr(GVariant) no_id = g_variant_ref_sink (g_variant_new_parsed ("{'name': <'x'>}"));
  g_autoptr(GVariant) empty_id = g_variant_ref_sink (g_variant_new_parsed ("{'id': <''>}"));
  g_autoptr(GVariant) wrong_type = g_variant_ref_sink (g_variant_new_parsed ("{'id': <1>}"));
  g_autoptr(GVariant) not_dict = g_variant_ref_sink (g_variant_new_string ("abc"));
  g_autoptr(GVariant) bad_color = g_variant_ref_sink (g_variant_new_parsed ("{'id': <'a'>, 'color': <'chartreuse'>}"));
  g_autoptr(EditorTabGroup) group = NULL;

  g_assert_null (_editor_tab_group_new_from_variant (no_id));
  g_assert_null (_editor_tab_group_new_from_variant (empty_id));
  g_assert_null (_editor_tab_group_new_from_variant (wrong_type));
  g_assert_null (_editor_tab_group_new_from_variant (not_dict));

  /* Unknown colors fall back to the default */
  group = _editor_tab_group_new_from_variant (bad_color);
  g_assert_nonnull (group);
  g_assert_cmpstr (_editor_tab_group_get_color (group), ==, _editor_tab_group_get_colors ()[0]);
}

static void
count_notify (guint *count)
{
  (*count)++;
}

static void
test_notify_once (void)
{
  g_autoptr(EditorTabGroup) group = _editor_tab_group_new ("a", "One", "blue");
  guint count = 0;

  g_signal_connect_swapped (group, "notify", G_CALLBACK (count_notify), &count);

  /* Setting the same value again must not notify */
  _editor_tab_group_set_name (group, "One");
  _editor_tab_group_set_color (group, "blue");
  _editor_tab_group_set_collapsed (group, FALSE);
  g_assert_cmpuint (count, ==, 0);

  _editor_tab_group_set_name (group, "Two");
  _editor_tab_group_set_color (group, "red");
  _editor_tab_group_set_collapsed (group, TRUE);
  g_assert_cmpuint (count, ==, 3);
}

static void
test_icons (void)
{
  const char * const *colors = _editor_tab_group_get_colors ();

  g_assert_cmpuint (g_strv_length ((char **)colors), ==, 9);

  for (guint i = 0; colors[i]; i++)
    {
      GIcon *plain = _editor_tab_group_get_icon (colors[i], FALSE);
      GIcon *modified = _editor_tab_group_get_icon (colors[i], TRUE);

      g_assert_true (GDK_IS_TEXTURE (plain));
      g_assert_true (GDK_IS_TEXTURE (modified));
      g_assert_true (plain != modified);

      /* Icons are cached */
      g_assert_true (plain == _editor_tab_group_get_icon (colors[i], FALSE));
      g_assert_nonnull (_editor_tab_group_get_color_label (colors[i]));
    }
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/TabGroup/round-trip", test_round_trip);
  g_test_add_func ("/TabGroup/unnamed", test_unnamed);
  g_test_add_func ("/TabGroup/invalid-state", test_invalid_state);
  g_test_add_func ("/TabGroup/notify-once", test_notify_once);
  g_test_add_func ("/TabGroup/icons", test_icons);

  return g_test_run ();
}
