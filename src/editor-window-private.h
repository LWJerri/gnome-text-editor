/* editor-window-private.h
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include <adwaita.h>

#include "editor-fullscreen-box.h"
#include "editor-open-popover-private.h"
#include "editor-page-private.h"
#include "editor-statusbar-private.h"
#include "editor-tab-group-private.h"
#include "editor-window.h"

G_BEGIN_DECLS

struct _EditorWindow
{
  GtkApplicationWindow  parent_instance;

  /* Cancellable which cancels when window closes */
  GCancellable         *cancellable;

  /* Template Widgets */
  GtkWidget            *empty;
  AdwTabView           *tab_view;
  AdwTabBar            *tab_bar;
  AdwTabView           *hidden_tab_view;
  GtkBox               *tab_groups_box;
  GMenu                *tab_groups_menu;
  GtkLabel             *title;
  GtkLabel             *subtitle;
  GtkLabel             *is_modified;
  GtkImage             *indicator;
  EditorOpenPopover    *open_menu_popover;
  GtkStack             *stack;
  GtkMenuButton        *open_menu_button;
  GtkMenuButton        *primary_menu;
  GtkMenuButton        *options_menu;
  GtkMenuButton        *export_menu;
  GtkWidget            *zoom_label;
  GMenu                *options_menu_model;
  EditorStatusbar      *statusbar;
  EditorFullscreenBox  *fullscreen_box;

  /* Borrowed References */
  EditorPage           *visible_page;
  EditorPage           *removing_page;

  /* Owned References */
  GBindingGroup        *page_bindings;
  GSignalGroup         *page_signals;
  GSettings            *settings;
  GArray               *closed_items;
  GListStore           *tab_groups;
  GtkWidget            *tab_group_popover;

  /* Used to update "Document Type: Markdown" */
  GMenuModel           *doc_type_menu;
  guint                 doc_type_index;

  guint                 inhibit_cookie;
  guint                 update_tab_groups_source;
  gboolean              tab_groups_disposed;
};


void          _editor_window_class_actions_init   (EditorWindowClass *klass);
void          _editor_window_actions_init         (EditorWindow      *self);
void          _editor_window_actions_update       (EditorWindow      *self,
                                                   EditorPage        *page);
void          _editor_window_dnd_init             (EditorWindow      *self);
EditorWindow *_editor_window_new                  (void);
GList        *_editor_window_get_pages            (EditorWindow      *self);
GList        *_editor_window_get_visible_pages    (EditorWindow      *self);
void          _editor_window_add_page             (EditorWindow      *self,
                                                   EditorPage        *page);
void          _editor_window_remove_page          (EditorWindow      *self,
                                                   EditorPage        *page);
void          _editor_window_focus_search         (EditorWindow      *self);
gboolean      _editor_window_request_close_page   (EditorWindow      *self,
                                                   EditorPage        *page);
gboolean      _editor_window_request_close_pages  (EditorWindow      *self,
                                                   GList             *pages,
                                                   gboolean           close_saved);
GCancellable *_editor_window_get_cancellable      (EditorWindow      *self);

/* editor-window-groups.c */
void            _editor_window_class_groups_init       (EditorWindowClass  *klass);
void            _editor_window_groups_init             (EditorWindow       *self);
void            _editor_window_groups_dispose          (EditorWindow       *self);
void            _editor_window_groups_update_actions   (EditorWindow       *self);
void            _editor_window_groups_track_page       (EditorWindow       *self,
                                                        EditorPage         *page,
                                                        AdwTabPage         *tab_page);
gboolean        _editor_window_page_is_hidden          (EditorWindow       *self,
                                                        EditorPage         *page);
void            _editor_window_reveal_page             (EditorWindow       *self,
                                                        EditorPage         *page);
EditorTabGroup *_editor_window_find_tab_group          (EditorWindow       *self,
                                                        const char         *id);
void            _editor_window_set_tab_group_collapsed (EditorWindow       *self,
                                                        EditorTabGroup     *group,
                                                        gboolean            collapsed);
GVariant       *_editor_window_save_tab_groups         (EditorWindow       *self);
char          **_editor_window_load_tab_groups         (EditorWindow       *self,
                                                        GVariant           *groups);
void            _editor_window_collapse_tab_groups     (EditorWindow       *self,
                                                        const char * const *ids);

G_END_DECLS
