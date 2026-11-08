/* editor-tab-group-private.h
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

#include <gio/gio.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_TAB_GROUP (editor_tab_group_get_type())

G_DECLARE_FINAL_TYPE (EditorTabGroup, editor_tab_group, EDITOR, TAB_GROUP, GObject)

EditorTabGroup     *_editor_tab_group_new              (const char      *id,
                                                        const char      *name,
                                                        const char      *color);
EditorTabGroup     *_editor_tab_group_new_from_variant (GVariant        *variant);
GVariant           *_editor_tab_group_to_variant       (EditorTabGroup  *self);
const char         *_editor_tab_group_get_id           (EditorTabGroup  *self);
const char         *_editor_tab_group_get_name         (EditorTabGroup  *self);
void                _editor_tab_group_set_name         (EditorTabGroup  *self,
                                                        const char      *name);
const char         *_editor_tab_group_get_color        (EditorTabGroup  *self);
void                _editor_tab_group_set_color        (EditorTabGroup  *self,
                                                        const char      *color);
gboolean            _editor_tab_group_get_collapsed    (EditorTabGroup  *self);
void                _editor_tab_group_set_collapsed    (EditorTabGroup  *self,
                                                        gboolean         collapsed);
const char * const *_editor_tab_group_get_colors       (void);
const char         *_editor_tab_group_get_color_label  (const char      *color);
GIcon              *_editor_tab_group_get_icon         (const char      *color,
                                                        gboolean         modified);

G_END_DECLS
