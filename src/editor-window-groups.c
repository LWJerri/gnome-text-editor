/* editor-window-groups.c
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Tab groups
 *
 * Every window owns a list of EditorTabGroup and each EditorPage may point
 * at one of them. Tabs of a collapsed group are moved into a second tab view
 * (hidden_tab_view) which is never shown. That keeps the pages, their
 * documents, and their drafts alive while they are out of the tab bar.
 *
 * Groups are shown as colored "chips" at the start of the tab bar. Clicking
 * a chip collapses or expands the group, right-clicking it shows a menu.
 */

#define G_LOG_DOMAIN "editor-window-groups"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application.h"
#include "editor-page-private.h"
#include "editor-session-private.h"
#include "editor-tab-group-private.h"
#include "editor-window-private.h"

static void editor_window_groups_queue_update (EditorWindow *self);

static GPtrArray *
get_view_pages (AdwTabView     *view,
                EditorTabGroup *group,
                gboolean        any_group)
{
  GPtrArray *ar = g_ptr_array_new ();
  guint n_pages = adw_tab_view_get_n_pages (view);

  for (guint i = 0; i < n_pages; i++)
    {
      AdwTabPage *tab_page = adw_tab_view_get_nth_page (view, i);
      EditorPage *page = EDITOR_PAGE (adw_tab_page_get_child (tab_page));

      if (any_group || _editor_page_get_tab_group (page) == group)
        g_ptr_array_add (ar, page);
    }

  return ar;
}

static guint
count_group_pages (AdwTabView     *view,
                   EditorTabGroup *group)
{
  g_autoptr(GPtrArray) pages = get_view_pages (view, group, FALSE);

  return pages->len;
}

static void
move_page (EditorWindow *self,
           EditorPage   *page,
           AdwTabView   *from,
           AdwTabView   *to)
{
  AdwTabPage *tab_page;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (from != to);

  if (!(tab_page = adw_tab_view_get_page (from, GTK_WIDGET (page))))
    return;

  _editor_page_begin_move (page);
  adw_tab_view_transfer_page (from, tab_page, to, adw_tab_view_get_n_pages (to));
  _editor_page_end_move (page);
}

static char *
dup_display_name (EditorTabGroup *group)
{
  const char *name = _editor_tab_group_get_name (group);

  if (name != NULL)
    return g_strdup (name);

  /* translators: %s is a color name, used for tab groups without a name */
  return g_strdup_printf (_("%s Group"),
                          _editor_tab_group_get_color_label (_editor_tab_group_get_color (group)));
}

/* Menu labels use "_" for mnemonics, so double them in user text */
static char *
dup_menu_label (EditorTabGroup *group)
{
  g_autofree char *name = dup_display_name (group);
  g_auto(GStrv) parts = g_strsplit (name, "_", -1);

  return g_strjoinv ("__", parts);
}

static void
mark_dirty (void)
{
  _editor_session_mark_dirty (EDITOR_SESSION_DEFAULT);
}

gboolean
_editor_window_page_is_hidden (EditorWindow *self,
                               EditorPage   *page)
{
  g_return_val_if_fail (EDITOR_IS_WINDOW (self), FALSE);
  g_return_val_if_fail (EDITOR_IS_PAGE (page), FALSE);

  return self->hidden_tab_view != NULL &&
         gtk_widget_get_ancestor (GTK_WIDGET (page), ADW_TYPE_TAB_VIEW) == GTK_WIDGET (self->hidden_tab_view);
}

/**
 * _editor_window_find_tab_group:
 * @self: an #EditorWindow
 * @id: (nullable): the group id
 *
 * Returns: (transfer none) (nullable): the #EditorTabGroup or %NULL
 */
EditorTabGroup *
_editor_window_find_tab_group (EditorWindow *self,
                               const char   *id)
{
  guint n_items;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), NULL);

  if (id == NULL)
    return NULL;

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i);

      if (g_strcmp0 (_editor_tab_group_get_id (group), id) == 0)
        return group;
    }

  return NULL;
}

void
_editor_window_set_tab_group_collapsed (EditorWindow   *self,
                                        EditorTabGroup *group,
                                        gboolean        collapsed)
{
  g_autoptr(GPtrArray) pages = NULL;
  AdwTabView *from;
  AdwTabView *to;

  g_return_if_fail (EDITOR_IS_WINDOW (self));
  g_return_if_fail (EDITOR_IS_TAB_GROUP (group));

  from = collapsed ? self->tab_view : self->hidden_tab_view;
  to = collapsed ? self->hidden_tab_view : self->tab_view;

  /* Update state first so that attach handlers see the final state */
  _editor_tab_group_set_collapsed (group, collapsed);

  pages = get_view_pages (from, group, FALSE);

  for (guint i = 0; i < pages->len; i++)
    move_page (self, g_ptr_array_index (pages, i), from, to);

  editor_window_groups_queue_update (self);
  mark_dirty ();
}

/**
 * _editor_window_reveal_page:
 * @self: an #EditorWindow
 * @page: an #EditorPage
 *
 * Expands the collapsed tab group containing @page so that the page
 * is part of the tab bar again.
 */
void
_editor_window_reveal_page (EditorWindow *self,
                            EditorPage   *page)
{
  EditorTabGroup *group;

  g_return_if_fail (EDITOR_IS_WINDOW (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  if (!_editor_window_page_is_hidden (self, page))
    return;

  if ((group = _editor_page_get_tab_group (page)))
    _editor_window_set_tab_group_collapsed (self, group, FALSE);

  /* Pages without a group should never be hidden, but be safe */
  if (_editor_window_page_is_hidden (self, page))
    move_page (self, page, self->hidden_tab_view, self->tab_view);
}

static void
add_page_to_group (EditorWindow   *self,
                   EditorPage     *page,
                   EditorTabGroup *group)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (EDITOR_IS_TAB_GROUP (group));

  _editor_page_set_tab_group (page, group);

  if (_editor_tab_group_get_collapsed (group))
    {
      /* Adding a tab to a collapsed group tucks it away with the others */
      if (!_editor_window_page_is_hidden (self, page))
        move_page (self, page, self->tab_view, self->hidden_tab_view);
    }
  else if (!_editor_window_page_is_hidden (self, page))
    {
      AdwTabPage *tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (page));
      guint n_pages = adw_tab_view_get_n_pages (self->tab_view);
      int current = adw_tab_view_get_page_position (self->tab_view, tab_page);
      int last = -1;

      /* Keep the tabs of a group next to each other */
      for (guint i = 0; i < n_pages; i++)
        {
          AdwTabPage *other = adw_tab_view_get_nth_page (self->tab_view, i);
          EditorPage *other_page = EDITOR_PAGE (adw_tab_page_get_child (other));

          if (other_page != page && _editor_page_get_tab_group (other_page) == group)
            last = i;
        }

      if (last >= 0 && !adw_tab_page_get_pinned (tab_page))
        adw_tab_view_reorder_page (self->tab_view,
                                   tab_page,
                                   current > last ? last + 1 : last);
    }

  editor_window_groups_queue_update (self);
  mark_dirty ();
}

static const char *
get_next_color (EditorWindow *self)
{
  const char * const *colors = _editor_tab_group_get_colors ();
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));

  for (guint c = 0; colors[c]; c++)
    {
      gboolean used = FALSE;

      for (guint i = 0; i < n_items && !used; i++)
        {
          g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i);

          used = g_str_equal (_editor_tab_group_get_color (group), colors[c]);
        }

      if (!used)
        return colors[c];
    }

  return colors[n_items % g_strv_length ((char **)colors)];
}

/*
 * Keeps the hidden tab view in sync with the collapsed state of groups,
 * removes groups without tabs, and refreshes the chips and menus.
 */
static void
editor_window_groups_sync (EditorWindow *self)
{
  g_autoptr(GPtrArray) hidden = NULL;
  gboolean changed = FALSE;
  guint n_items;

  g_assert (EDITOR_IS_WINDOW (self));

  /* Hidden pages must belong to a collapsed group of this window */
  hidden = get_view_pages (self->hidden_tab_view, NULL, TRUE);
  for (guint i = 0; i < hidden->len; i++)
    {
      EditorPage *page = g_ptr_array_index (hidden, i);
      EditorTabGroup *group = _editor_page_get_tab_group (page);

      if (group == NULL ||
          !_editor_tab_group_get_collapsed (group) ||
          _editor_window_find_tab_group (self, _editor_tab_group_get_id (group)) != group)
        {
          move_page (self, page, self->hidden_tab_view, self->tab_view);
          changed = TRUE;
        }
    }

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));

  for (guint i = n_items; i > 0; i--)
    {
      g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i - 1);
      guint n_visible = count_group_pages (self->tab_view, group);
      guint n_hidden = count_group_pages (self->hidden_tab_view, group);

      if (n_visible + n_hidden == 0)
        {
          /* Like in web browsers, a group goes away with its last tab */
          g_list_store_remove (self->tab_groups, i - 1);
          changed = TRUE;
        }
      else if (_editor_tab_group_get_collapsed (group) && n_visible > 0)
        {
          /* A tab was added to the bar (such as by drag-and-drop) */
          _editor_window_set_tab_group_collapsed (self, group, FALSE);
          changed = TRUE;
        }
    }

  if (changed)
    mark_dirty ();
}

static void
editor_window_groups_update_actions (EditorWindow *self)
{
  EditorPage *page = self->visible_page;
  gboolean has_page = page != NULL;

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.tab-group-new", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.tab-group-add-page", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.tab-group-remove-page",
                                 has_page && _editor_page_get_tab_group (page) != NULL);
}

static GMenuItem *
create_group_menu_item (const char *label,
                        const char *action,
                        GVariant   *target)
{
  GMenuItem *item = g_menu_item_new (label, NULL);

  g_menu_item_set_action_and_target_value (item, action, target);

  return item;
}

static GMenuModel *
create_group_menu (EditorWindow   *self,
                   EditorTabGroup *group)
{
  const char * const *colors = _editor_tab_group_get_colors ();
  const char *id = _editor_tab_group_get_id (group);
  GMenu *menu = g_menu_new ();
  g_autoptr(GMenu) section1 = g_menu_new ();
  g_autoptr(GMenu) section2 = g_menu_new ();
  g_autoptr(GMenu) section3 = g_menu_new ();
  g_autoptr(GMenu) section4 = g_menu_new ();
  g_autoptr(GMenu) color_menu = g_menu_new ();

#define APPEND(section, label, action, target) \
  G_STMT_START { \
    g_autoptr(GMenuItem) _item = create_group_menu_item (label, action, target); \
    g_menu_append_item (section, _item); \
  } G_STMT_END

  APPEND (section1, _("_New Tab in Group"), "win.tab-group-new-tab", g_variant_new_string (id));

  APPEND (section2, _("_Rename Group…"), "win.tab-group-rename", g_variant_new_string (id));
  for (guint i = 0; colors[i]; i++)
    APPEND (color_menu,
            _editor_tab_group_get_color_label (colors[i]),
            "win.tab-group-set-color",
            g_variant_new ("(ss)", id, colors[i]));
  g_menu_append_submenu (section2, _("_Color"), G_MENU_MODEL (color_menu));

  APPEND (section3,
          _editor_tab_group_get_collapsed (group) ? _("_Expand Group") : _("_Collapse Group"),
          "win.tab-group-toggle",
          g_variant_new_string (id));
  APPEND (section3, _("Show _Only This Group"), "win.tab-group-show-only", g_variant_new_string (id));
  APPEND (section3, _("Expand _All Groups"), "win.tab-group-expand-all", NULL);

  APPEND (section4, _("_Ungroup"), "win.tab-group-ungroup", g_variant_new_string (id));

#undef APPEND

  g_menu_append_section (menu, NULL, G_MENU_MODEL (section1));
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section2));
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section3));
  g_menu_append_section (menu, NULL, G_MENU_MODEL (section4));

  return G_MENU_MODEL (menu);
}

static void
popup_group_menu (EditorWindow *self,
                  GtkWidget    *chip)
{
  g_autoptr(GMenuModel) menu = NULL;
  EditorTabGroup *group;
  graphene_rect_t bounds;
  GdkRectangle rect;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (GTK_IS_WIDGET (chip));

  if (!(group = g_object_get_data (G_OBJECT (chip), "EDITOR_TAB_GROUP")))
    return;

  if (!gtk_widget_compute_bounds (chip, GTK_WIDGET (self->tab_groups_box), &bounds))
    return;

  rect.x = bounds.origin.x;
  rect.y = bounds.origin.y;
  rect.width = bounds.size.width;
  rect.height = bounds.size.height;

  menu = create_group_menu (self, group);
  gtk_popover_menu_set_menu_model (GTK_POPOVER_MENU (self->tab_group_popover), menu);
  gtk_popover_set_pointing_to (GTK_POPOVER (self->tab_group_popover), &rect);
  gtk_popover_popup (GTK_POPOVER (self->tab_group_popover));
}

static void
chip_secondary_pressed_cb (GtkGestureClick *gesture,
                           int              n_press,
                           double           x,
                           double           y,
                           EditorWindow    *self)
{
  GtkWidget *chip = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  popup_group_menu (self, chip);
}

static void
chip_long_pressed_cb (GtkGestureLongPress *gesture,
                      double               x,
                      double               y,
                      EditorWindow        *self)
{
  GtkWidget *chip = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));

  gtk_gesture_set_state (GTK_GESTURE (gesture), GTK_EVENT_SEQUENCE_CLAIMED);
  popup_group_menu (self, chip);
}

static GtkWidget *
create_chip (EditorWindow   *self,
             EditorTabGroup *group)
{
  g_autofree char *display_name = dup_display_name (group);
  g_autofree char *tooltip = NULL;
  const char *name = _editor_tab_group_get_name (group);
  gboolean collapsed = _editor_tab_group_get_collapsed (group);
  guint n_pages;
  GtkGesture *gesture;
  GtkWidget *button;
  GtkWidget *box;
  GtkWidget *label;

  n_pages = count_group_pages (self->tab_view, group) +
            count_group_pages (self->hidden_tab_view, group);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  label = gtk_label_new (name);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 20);
  gtk_widget_set_visible (label, name != NULL);
  gtk_box_append (GTK_BOX (box), label);

  if (collapsed)
    {
      g_autofree char *count = g_strdup_printf ("%u", n_pages);
      GtkWidget *count_label = gtk_label_new (count);

      gtk_widget_add_css_class (count_label, "count");
      gtk_widget_add_css_class (count_label, "numeric");
      gtk_box_append (GTK_BOX (box), count_label);
    }

  button = g_object_new (GTK_TYPE_BUTTON,
                         "child", box,
                         "valign", GTK_ALIGN_CENTER,
                         "action-name", "win.tab-group-toggle",
                         "action-target", g_variant_new_string (_editor_tab_group_get_id (group)),
                         NULL);
  gtk_widget_add_css_class (button, "tab-group-chip");
  gtk_widget_add_css_class (button, _editor_tab_group_get_color (group));
  if (collapsed)
    gtk_widget_add_css_class (button, "collapsed");

  if (collapsed)
    tooltip = g_strdup_printf (ngettext ("%s: %u hidden tab\nClick to show, right-click for options",
                                         "%s: %u hidden tabs\nClick to show, right-click for options",
                                         n_pages),
                               display_name, n_pages);
  else
    tooltip = g_strdup_printf (ngettext ("%s: %u tab\nClick to collapse, right-click for options",
                                         "%s: %u tabs\nClick to collapse, right-click for options",
                                         n_pages),
                               display_name, n_pages);
  gtk_widget_set_tooltip_text (button, tooltip);

  gtk_accessible_update_property (GTK_ACCESSIBLE (button),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, display_name,
                                  -1);
  gtk_accessible_update_state (GTK_ACCESSIBLE (button),
                               GTK_ACCESSIBLE_STATE_EXPANDED, !collapsed,
                               -1);

  g_object_set_data_full (G_OBJECT (button),
                          "EDITOR_TAB_GROUP",
                          g_object_ref (group),
                          g_object_unref);

  gesture = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect_object (gesture, "pressed", G_CALLBACK (chip_secondary_pressed_cb), self, 0);
  gtk_widget_add_controller (button, GTK_EVENT_CONTROLLER (gesture));

  gesture = gtk_gesture_long_press_new ();
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (gesture), TRUE);
  g_signal_connect_object (gesture, "pressed", G_CALLBACK (chip_long_pressed_cb), self, 0);
  gtk_widget_add_controller (button, GTK_EVENT_CONTROLLER (gesture));

  return button;
}

static void
editor_window_groups_rebuild (EditorWindow *self)
{
  GtkWidget *child;
  guint n_items;
  guint n_hidden;

  g_assert (EDITOR_IS_WINDOW (self));

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));
  n_hidden = adw_tab_view_get_n_pages (self->hidden_tab_view);

  /* Chips at the start of the tab bar */
  child = gtk_widget_get_first_child (GTK_WIDGET (self->tab_groups_box));
  while (child != NULL)
    {
      GtkWidget *next = gtk_widget_get_next_sibling (child);

      if (child != self->tab_group_popover)
        gtk_box_remove (self->tab_groups_box, child);

      child = next;
    }

  /* Groups of the "Add Tab to Group" menu of tabs */
  g_menu_remove_all (self->tab_groups_menu);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i);
      g_autofree char *label = dup_menu_label (group);
      g_autoptr(GMenuItem) item = NULL;

      gtk_box_append (self->tab_groups_box, create_chip (self, group));

      item = create_group_menu_item (label,
                                     "win.tab-group-add-page",
                                     g_variant_new_string (_editor_tab_group_get_id (group)));
      g_menu_append_item (self->tab_groups_menu, item);
    }

  gtk_widget_set_visible (GTK_WIDGET (self->tab_groups_box), n_items > 0);

  /* Keep the tab bar around so collapsed groups can be expanded again */
  adw_tab_bar_set_autohide (self->tab_bar, n_items == 0);

  if (n_hidden > 0)
    {
      adw_status_page_set_title (ADW_STATUS_PAGE (self->empty),
                                 _("All Tabs Are in Collapsed Groups"));
      adw_status_page_set_description (ADW_STATUS_PAGE (self->empty),
                                       _("Click a group above to show its tabs"));
    }
  else
    {
      adw_status_page_set_title (ADW_STATUS_PAGE (self->empty),
                                 _("Open a Document"));
      adw_status_page_set_description (ADW_STATUS_PAGE (self->empty),
                                       _("Create a new document (Ctrl+N), or open an existing one (Ctrl+O)"));
    }

  if (self->visible_page == NULL)
    gtk_window_set_title (GTK_WINDOW (self), _(PACKAGE_NAME));

  editor_window_groups_update_actions (self);
}

static gboolean
editor_window_groups_update_cb (gpointer user_data)
{
  EditorWindow *self = user_data;

  g_assert (EDITOR_IS_WINDOW (self));

  self->update_tab_groups_source = 0;

  if (self->tab_groups_disposed)
    return G_SOURCE_REMOVE;

  editor_window_groups_sync (self);
  editor_window_groups_rebuild (self);

  return G_SOURCE_REMOVE;
}

static void
editor_window_groups_queue_update (EditorWindow *self)
{
  g_assert (EDITOR_IS_WINDOW (self));

  if (self->tab_groups_disposed || self->update_tab_groups_source != 0)
    return;

  self->update_tab_groups_source = g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
                                                    editor_window_groups_update_cb,
                                                    g_object_ref (self),
                                                    g_object_unref);
}

static void
on_page_attached_cb (EditorWindow *self,
                     AdwTabPage   *tab_page,
                     int           position,
                     AdwTabView   *view)
{
  EditorPage *page;
  EditorTabGroup *group;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (tab_page));

  page = EDITOR_PAGE (adw_tab_page_get_child (tab_page));

  /* A tab dragged in from another window brings its group along */
  if ((group = _editor_page_get_tab_group (page)))
    {
      const char *id = _editor_tab_group_get_id (group);
      EditorTabGroup *local = _editor_window_find_tab_group (self, id);

      if (local == NULL)
        {
          g_autoptr(EditorTabGroup) copy = _editor_tab_group_new (id,
                                                                  _editor_tab_group_get_name (group),
                                                                  _editor_tab_group_get_color (group));

          g_list_store_append (self->tab_groups, copy);
          local = copy;
          mark_dirty ();
        }

      _editor_page_set_tab_group (page, local);
    }

  editor_window_groups_queue_update (self);
}

static void
on_page_detached_cb (EditorWindow *self,
                     AdwTabPage   *tab_page,
                     int           position,
                     AdwTabView   *view)
{
  g_assert (EDITOR_IS_WINDOW (self));

  editor_window_groups_queue_update (self);
}

static void
update_tab_icon_cb (EditorPage *page,
                    GParamSpec *pspec,
                    AdwTabPage *tab_page)
{
  static GIcon *modified_icon;
  EditorTabGroup *group;
  gboolean modified;
  GIcon *icon = NULL;

  g_assert (EDITOR_IS_PAGE (page));
  g_assert (ADW_IS_TAB_PAGE (tab_page));

  group = _editor_page_get_tab_group (page);
  modified = editor_page_get_is_modified (page);

  if (group != NULL)
    {
      icon = _editor_tab_group_get_icon (_editor_tab_group_get_color (group), modified);
    }
  else if (modified)
    {
      if (modified_icon == NULL)
        modified_icon = g_themed_icon_new ("document-modified-symbolic");
      icon = modified_icon;
    }

  adw_tab_page_set_icon (tab_page, icon);
}

void
_editor_window_groups_track_page (EditorWindow *self,
                                  EditorPage   *page,
                                  AdwTabPage   *tab_page)
{
  g_return_if_fail (EDITOR_IS_WINDOW (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));
  g_return_if_fail (ADW_IS_TAB_PAGE (tab_page));

  /* The tab page is kept when moving between windows or into the
   * hidden view, so tie the handlers to it rather than the window.
   */
  g_signal_connect_object (page,
                           "notify::is-modified",
                           G_CALLBACK (update_tab_icon_cb),
                           tab_page,
                           0);
  g_signal_connect_object (page,
                           "notify::tab-group",
                           G_CALLBACK (update_tab_icon_cb),
                           tab_page,
                           0);
  update_tab_icon_cb (page, NULL, tab_page);
}

/**
 * _editor_window_save_tab_groups:
 * @self: an #EditorWindow
 *
 * Returns: (transfer floating) (nullable): a #GVariant of type `aa{sv}`,
 *   or %NULL if the window has no tab groups.
 */
GVariant *
_editor_window_save_tab_groups (EditorWindow *self)
{
  GVariantBuilder builder;
  guint n_items;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), NULL);

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));

  if (n_items == 0)
    return NULL;

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i);

      g_variant_builder_add_value (&builder, _editor_tab_group_to_variant (group));
    }

  return g_variant_builder_end (&builder);
}

/**
 * _editor_window_load_tab_groups:
 * @self: an #EditorWindow
 * @groups: a #GVariant of type `aa{sv}`
 *
 * Adds the groups saved with _editor_window_save_tab_groups(). The groups
 * are added expanded so that pages can be added to the tab bar. Call
 * _editor_window_collapse_tab_groups() with the result once the pages
 * have been restored.
 *
 * Returns: (transfer full): the ids of groups which were collapsed
 */
char **
_editor_window_load_tab_groups (EditorWindow *self,
                                GVariant     *groups)
{
  g_autoptr(GStrvBuilder) collapsed = g_strv_builder_new ();
  GVariantIter iter;
  GVariant *value;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), NULL);
  g_return_val_if_fail (groups != NULL, NULL);
  g_return_val_if_fail (g_variant_is_of_type (groups, G_VARIANT_TYPE ("aa{sv}")), NULL);

  g_variant_iter_init (&iter, groups);
  while (g_variant_iter_loop (&iter, "@a{sv}", &value))
    {
      g_autoptr(EditorTabGroup) group = _editor_tab_group_new_from_variant (value);

      if (group == NULL ||
          _editor_window_find_tab_group (self, _editor_tab_group_get_id (group)))
        continue;

      if (_editor_tab_group_get_collapsed (group))
        g_strv_builder_add (collapsed, _editor_tab_group_get_id (group));

      _editor_tab_group_set_collapsed (group, FALSE);
      g_list_store_append (self->tab_groups, group);
    }

  editor_window_groups_queue_update (self);

  return g_strv_builder_end (collapsed);
}

void
_editor_window_collapse_tab_groups (EditorWindow       *self,
                                    const char * const *ids)
{
  g_return_if_fail (EDITOR_IS_WINDOW (self));

  if (ids == NULL)
    return;

  for (guint i = 0; ids[i]; i++)
    {
      EditorTabGroup *group = _editor_window_find_tab_group (self, ids[i]);

      if (group != NULL)
        _editor_window_set_tab_group_collapsed (self, group, TRUE);
    }
}

typedef struct
{
  EditorWindow   *self;
  EditorPage     *page;
  EditorTabGroup *group;
  GtkEditable    *entry;
} NameRequest;

static void
name_request_free (gpointer data)
{
  NameRequest *req = data;

  g_clear_object (&req->self);
  g_clear_object (&req->page);
  g_clear_object (&req->group);
  g_free (req);
}

static void
name_dialog_response_cb (AdwAlertDialog *dialog,
                         const char     *response,
                         NameRequest    *req)
{
  g_autofree char *name = NULL;

  g_assert (ADW_IS_ALERT_DIALOG (dialog));
  g_assert (req != NULL);

  if (g_strcmp0 (response, "ok") != 0)
    return;

  name = g_strstrip (g_strdup (gtk_editable_get_text (req->entry)));

  if (req->group != NULL)
    {
      /* Renaming an existing group */
      _editor_tab_group_set_name (req->group, name);
    }
  else if (req->page != NULL && _editor_page_get_window (req->page) == req->self)
    {
      g_autoptr(EditorTabGroup) group = NULL;

      group = _editor_tab_group_new (NULL, name, get_next_color (req->self));
      g_list_store_append (req->self->tab_groups, group);
      add_page_to_group (req->self, req->page, group);
    }

  editor_window_groups_queue_update (req->self);
  mark_dirty ();
}

static void
name_dialog_closed_cb (AdwDialog   *dialog,
                       NameRequest *req)
{
  g_assert (ADW_IS_DIALOG (dialog));
  g_assert (req != NULL);

  /* The menu that opened the dialog is gone, so focus would otherwise
   * land on the first button in the header bar.
   */
  if (!req->self->tab_groups_disposed && req->self->visible_page != NULL)
    editor_page_grab_focus (req->self->visible_page);
}

static void
present_name_dialog (EditorWindow   *self,
                     EditorPage     *page,
                     EditorTabGroup *group)
{
  NameRequest *req;
  AdwDialog *dialog;
  GtkWidget *entry;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (!page || EDITOR_IS_PAGE (page));
  g_assert (!group || EDITOR_IS_TAB_GROUP (group));

  entry = g_object_new (GTK_TYPE_ENTRY,
                        "activates-default", TRUE,
                        "placeholder-text", _("Group name"),
                        NULL);
  if (group != NULL && _editor_tab_group_get_name (group) != NULL)
    gtk_editable_set_text (GTK_EDITABLE (entry), _editor_tab_group_get_name (group));

  dialog = adw_alert_dialog_new (group ? _("Rename Tab Group") : _("New Tab Group"), NULL);
  adw_alert_dialog_set_extra_child (ADW_ALERT_DIALOG (dialog), entry);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", _("_Cancel"),
                                  "ok", group ? _("_Rename") : _("C_reate"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "ok", ADW_RESPONSE_SUGGESTED);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "ok");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "cancel");

  req = g_new0 (NameRequest, 1);
  req->self = g_object_ref (self);
  req->page = page ? g_object_ref (page) : NULL;
  req->group = group ? g_object_ref (group) : NULL;
  req->entry = GTK_EDITABLE (entry);

  g_object_set_data_full (G_OBJECT (dialog), "EDITOR_NAME_REQUEST", req, name_request_free);
  g_signal_connect (dialog, "response", G_CALLBACK (name_dialog_response_cb), req);
  g_signal_connect (dialog, "closed", G_CALLBACK (name_dialog_closed_cb), req);

  adw_dialog_set_focus (dialog, entry);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

static EditorTabGroup *
group_from_param (EditorWindow *self,
                  GVariant     *param)
{
  if (param == NULL || !g_variant_is_of_type (param, G_VARIANT_TYPE_STRING))
    return NULL;

  return _editor_window_find_tab_group (self, g_variant_get_string (param, NULL));
}

static void
tab_group_new_action (GtkWidget  *widget,
                      const char *action_name,
                      GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);

  if (self->visible_page != NULL)
    present_name_dialog (self, self->visible_page, NULL);
}

static void
tab_group_add_page_action_cb (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  EditorTabGroup *group;

  if (self->visible_page != NULL && (group = group_from_param (self, param)))
    add_page_to_group (self, self->visible_page, group);
}

static void
tab_group_remove_page_action (GtkWidget  *widget,
                              const char *action_name,
                              GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);

  if (self->visible_page == NULL)
    return;

  _editor_page_set_tab_group (self->visible_page, NULL);

  editor_window_groups_queue_update (self);
  mark_dirty ();
}

static void
tab_group_toggle_action (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  EditorTabGroup *group;

  if ((group = group_from_param (self, param)))
    _editor_window_set_tab_group_collapsed (self, group, !_editor_tab_group_get_collapsed (group));
}

static void
tab_group_new_tab_action (GtkWidget  *widget,
                          const char *action_name,
                          GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  EditorTabGroup *group;
  EditorPage *page;

  if (!(group = group_from_param (self, param)))
    return;

  if (_editor_tab_group_get_collapsed (group))
    _editor_window_set_tab_group_collapsed (self, group, FALSE);

  page = editor_session_add_draft (EDITOR_SESSION_DEFAULT, self);
  add_page_to_group (self, page, group);
  _editor_page_raise (page);
}

static void
tab_group_rename_action (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  EditorTabGroup *group;

  if ((group = group_from_param (self, param)))
    present_name_dialog (self, NULL, group);
}

static void
tab_group_set_color_action (GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  EditorTabGroup *group;
  const char *id;
  const char *color;

  g_variant_get (param, "(&s&s)", &id, &color);

  if ((group = _editor_window_find_tab_group (self, id)))
    {
      _editor_tab_group_set_color (group, color);
      editor_window_groups_queue_update (self);
      mark_dirty ();
    }
}

static void
tab_group_show_only_action (GtkWidget  *widget,
                            const char *action_name,
                            GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  EditorTabGroup *target;
  guint n_items;

  if (!(target = group_from_param (self, param)))
    return;

  /* Expand first so the window always has a tab to select */
  _editor_window_set_tab_group_collapsed (self, target, FALSE);

  n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));
  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i);

      if (group != target)
        _editor_window_set_tab_group_collapsed (self, group, TRUE);
    }
}

static void
tab_group_expand_all_action (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  guint n_items = g_list_model_get_n_items (G_LIST_MODEL (self->tab_groups));

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(EditorTabGroup) group = g_list_model_get_item (G_LIST_MODEL (self->tab_groups), i);

      _editor_window_set_tab_group_collapsed (self, group, FALSE);
    }
}

static void
tab_group_ungroup_action (GtkWidget  *widget,
                          const char *action_name,
                          GVariant   *param)
{
  EditorWindow *self = EDITOR_WINDOW (widget);
  g_autoptr(EditorTabGroup) group = NULL;
  g_autoptr(GPtrArray) pages = NULL;
  EditorTabGroup *found;
  guint position;

  if (!(found = group_from_param (self, param)))
    return;

  group = g_object_ref (found);

  /* Bring the tabs back into the tab bar before forgetting the group */
  _editor_window_set_tab_group_collapsed (self, group, FALSE);

  pages = get_view_pages (self->tab_view, group, FALSE);
  for (guint i = 0; i < pages->len; i++)
    _editor_page_set_tab_group (g_ptr_array_index (pages, i), NULL);

  if (g_list_store_find (self->tab_groups, group, &position))
    g_list_store_remove (self->tab_groups, position);

  editor_window_groups_queue_update (self);
  mark_dirty ();
}

void
_editor_window_class_groups_init (EditorWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_install_action (widget_class, "win.tab-group-new", NULL, tab_group_new_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-add-page", "s", tab_group_add_page_action_cb);
  gtk_widget_class_install_action (widget_class, "win.tab-group-remove-page", NULL, tab_group_remove_page_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-toggle", "s", tab_group_toggle_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-new-tab", "s", tab_group_new_tab_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-rename", "s", tab_group_rename_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-set-color", "(ss)", tab_group_set_color_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-show-only", "s", tab_group_show_only_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-expand-all", NULL, tab_group_expand_all_action);
  gtk_widget_class_install_action (widget_class, "win.tab-group-ungroup", "s", tab_group_ungroup_action);
}

void
_editor_window_groups_init (EditorWindow *self)
{
  g_return_if_fail (EDITOR_IS_WINDOW (self));

  self->tab_groups = g_list_store_new (EDITOR_TYPE_TAB_GROUP);

  /* The hidden view is never shown, so it must not steal shortcuts */
  adw_tab_view_set_shortcuts (self->hidden_tab_view, ADW_TAB_VIEW_SHORTCUT_NONE);

  self->tab_group_popover = gtk_popover_menu_new_from_model (NULL);
  gtk_widget_set_parent (self->tab_group_popover, GTK_WIDGET (self->tab_groups_box));

  g_signal_connect_object (self->tab_view,
                           "page-attached",
                           G_CALLBACK (on_page_attached_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->tab_view,
                           "page-detached",
                           G_CALLBACK (on_page_detached_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->hidden_tab_view,
                           "page-attached",
                           G_CALLBACK (on_page_detached_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->hidden_tab_view,
                           "page-detached",
                           G_CALLBACK (on_page_detached_cb),
                           self,
                           G_CONNECT_SWAPPED);

  editor_window_groups_rebuild (self);
}

void
_editor_window_groups_update_actions (EditorWindow *self)
{
  g_return_if_fail (EDITOR_IS_WINDOW (self));

  editor_window_groups_update_actions (self);
}

void
_editor_window_groups_dispose (EditorWindow *self)
{
  g_return_if_fail (EDITOR_IS_WINDOW (self));

  self->tab_groups_disposed = TRUE;

  /* Tab views detach their pages when destroyed, ignore that */
  if (self->tab_view != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->tab_view, G_CALLBACK (on_page_attached_cb), self);
      g_signal_handlers_disconnect_by_func (self->tab_view, G_CALLBACK (on_page_detached_cb), self);
    }

  if (self->hidden_tab_view != NULL)
    g_signal_handlers_disconnect_by_func (self->hidden_tab_view, G_CALLBACK (on_page_detached_cb), self);

  g_clear_handle_id (&self->update_tab_groups_source, g_source_remove);
  g_clear_pointer (&self->tab_group_popover, gtk_widget_unparent);
}
