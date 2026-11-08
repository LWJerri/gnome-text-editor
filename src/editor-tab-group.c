/* editor-tab-group.c
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

#define G_LOG_DOMAIN "editor-tab-group"

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "editor-tab-group-private.h"

/* A named, colored group of tabs within a window. The group itself only
 * holds its metadata; pages point at the group they belong to.
 */
struct _EditorTabGroup
{
  GObject   parent_instance;
  char     *id;
  char     *name;
  char     *color;
  guint     collapsed : 1;
};

typedef struct
{
  const char *id;
  const char *label;
  guint32     rgb;
} TabGroupColor;

/* Same values as the libadwaita accent colors, which are all
 * readable with white text on top of them.
 */
static const TabGroupColor tab_group_colors[] = {
  { "blue",   N_("Blue"),   0x3584e4 },
  { "teal",   N_("Teal"),   0x2190a4 },
  { "green",  N_("Green"),  0x3a944a },
  { "yellow", N_("Yellow"), 0xc88800 },
  { "orange", N_("Orange"), 0xed5b00 },
  { "red",    N_("Red"),    0xe62d42 },
  { "pink",   N_("Pink"),   0xd56199 },
  { "purple", N_("Purple"), 0x9141ac },
  { "slate",  N_("Slate"),  0x6f8396 },
};

enum {
  PROP_0,
  PROP_ID,
  PROP_NAME,
  PROP_COLOR,
  PROP_COLLAPSED,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorTabGroup, editor_tab_group, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static const TabGroupColor *
find_color (const char *color)
{
  if (color != NULL)
    {
      for (guint i = 0; i < G_N_ELEMENTS (tab_group_colors); i++)
        {
          if (g_str_equal (tab_group_colors[i].id, color))
            return &tab_group_colors[i];
        }
    }

  return NULL;
}

static void
editor_tab_group_finalize (GObject *object)
{
  EditorTabGroup *self = (EditorTabGroup *)object;

  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->color, g_free);

  G_OBJECT_CLASS (editor_tab_group_parent_class)->finalize (object);
}

static void
editor_tab_group_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EditorTabGroup *self = EDITOR_TAB_GROUP (object);

  switch (prop_id)
    {
    case PROP_ID:
      g_value_set_string (value, self->id);
      break;

    case PROP_NAME:
      g_value_set_string (value, self->name);
      break;

    case PROP_COLOR:
      g_value_set_string (value, self->color);
      break;

    case PROP_COLLAPSED:
      g_value_set_boolean (value, self->collapsed);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_tab_group_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EditorTabGroup *self = EDITOR_TAB_GROUP (object);

  switch (prop_id)
    {
    case PROP_ID:
      self->id = g_value_dup_string (value);
      break;

    case PROP_NAME:
      _editor_tab_group_set_name (self, g_value_get_string (value));
      break;

    case PROP_COLOR:
      _editor_tab_group_set_color (self, g_value_get_string (value));
      break;

    case PROP_COLLAPSED:
      _editor_tab_group_set_collapsed (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_tab_group_constructed (GObject *object)
{
  EditorTabGroup *self = (EditorTabGroup *)object;

  G_OBJECT_CLASS (editor_tab_group_parent_class)->constructed (object);

  if (self->id == NULL || self->id[0] == 0)
    {
      g_free (self->id);
      self->id = g_uuid_string_random ();
    }

  if (self->color == NULL)
    self->color = g_strdup (tab_group_colors[0].id);
}

static void
editor_tab_group_class_init (EditorTabGroupClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_tab_group_constructed;
  object_class->finalize = editor_tab_group_finalize;
  object_class->get_property = editor_tab_group_get_property;
  object_class->set_property = editor_tab_group_set_property;

  properties [PROP_ID] =
    g_param_spec_string ("id", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_COLOR] =
    g_param_spec_string ("color", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_COLLAPSED] =
    g_param_spec_boolean ("collapsed", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_tab_group_init (EditorTabGroup *self)
{
}

/**
 * _editor_tab_group_new:
 * @id: (nullable): the group id, or %NULL to generate one
 * @name: (nullable): the display name
 * @color: (nullable): a color id from _editor_tab_group_get_colors()
 *
 * Returns: (transfer full): a new #EditorTabGroup
 */
EditorTabGroup *
_editor_tab_group_new (const char *id,
                       const char *name,
                       const char *color)
{
  return g_object_new (EDITOR_TYPE_TAB_GROUP,
                       "id", id,
                       "name", name,
                       "color", color,
                       NULL);
}

/**
 * _editor_tab_group_new_from_variant:
 * @variant: a #GVariant of type `a{sv}`
 *
 * Creates a group from state saved with _editor_tab_group_to_variant().
 *
 * Returns: (transfer full) (nullable): a new #EditorTabGroup or %NULL
 *   if @variant is not valid group state.
 */
EditorTabGroup *
_editor_tab_group_new_from_variant (GVariant *variant)
{
  EditorTabGroup *self;
  const char *id;
  const char *name;
  const char *color;
  gboolean collapsed;

  g_return_val_if_fail (variant != NULL, NULL);

  if (!g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT))
    return NULL;

  if (!g_variant_lookup (variant, "id", "&s", &id) || id[0] == 0)
    return NULL;

  if (!g_variant_lookup (variant, "name", "&s", &name))
    name = NULL;

  if (!g_variant_lookup (variant, "color", "&s", &color))
    color = NULL;

  if (!g_variant_lookup (variant, "collapsed", "b", &collapsed))
    collapsed = FALSE;

  self = _editor_tab_group_new (id, name, color);
  self->collapsed = !!collapsed;

  return self;
}

/**
 * _editor_tab_group_to_variant:
 * @self: an #EditorTabGroup
 *
 * Returns: (transfer floating): a #GVariant of type `a{sv}`
 */
GVariant *
_editor_tab_group_to_variant (EditorTabGroup *self)
{
  GVariantDict dict;

  g_return_val_if_fail (EDITOR_IS_TAB_GROUP (self), NULL);

  g_variant_dict_init (&dict, NULL);
  g_variant_dict_insert (&dict, "id", "s", self->id);
  g_variant_dict_insert (&dict, "name", "s", self->name ? self->name : "");
  g_variant_dict_insert (&dict, "color", "s", self->color);
  g_variant_dict_insert (&dict, "collapsed", "b", (gboolean)self->collapsed);

  return g_variant_dict_end (&dict);
}

const char *
_editor_tab_group_get_id (EditorTabGroup *self)
{
  g_return_val_if_fail (EDITOR_IS_TAB_GROUP (self), NULL);

  return self->id;
}

const char *
_editor_tab_group_get_name (EditorTabGroup *self)
{
  g_return_val_if_fail (EDITOR_IS_TAB_GROUP (self), NULL);

  return self->name;
}

void
_editor_tab_group_set_name (EditorTabGroup *self,
                            const char     *name)
{
  g_return_if_fail (EDITOR_IS_TAB_GROUP (self));

  if (name != NULL && name[0] == 0)
    name = NULL;

  if (g_set_str (&self->name, name))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_NAME]);
}

const char *
_editor_tab_group_get_color (EditorTabGroup *self)
{
  g_return_val_if_fail (EDITOR_IS_TAB_GROUP (self), NULL);

  return self->color;
}

void
_editor_tab_group_set_color (EditorTabGroup *self,
                             const char     *color)
{
  const TabGroupColor *info;

  g_return_if_fail (EDITOR_IS_TAB_GROUP (self));

  /* Unknown colors (such as from a newer version) fall back to the default */
  if (!(info = find_color (color)))
    info = &tab_group_colors[0];

  if (g_set_str (&self->color, info->id))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_COLOR]);
}

gboolean
_editor_tab_group_get_collapsed (EditorTabGroup *self)
{
  g_return_val_if_fail (EDITOR_IS_TAB_GROUP (self), FALSE);

  return self->collapsed;
}

void
_editor_tab_group_set_collapsed (EditorTabGroup *self,
                                 gboolean        collapsed)
{
  g_return_if_fail (EDITOR_IS_TAB_GROUP (self));

  collapsed = !!collapsed;

  if (self->collapsed != collapsed)
    {
      self->collapsed = collapsed;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_COLLAPSED]);
    }
}

/**
 * _editor_tab_group_get_colors:
 *
 * Returns: (transfer none): a %NULL-terminated array of color ids
 */
const char * const *
_editor_tab_group_get_colors (void)
{
  static const char *ids[G_N_ELEMENTS (tab_group_colors) + 1];

  if (ids[0] == NULL)
    {
      for (guint i = 0; i < G_N_ELEMENTS (tab_group_colors); i++)
        ids[i] = tab_group_colors[i].id;
    }

  return ids;
}

const char *
_editor_tab_group_get_color_label (const char *color)
{
  const TabGroupColor *info;

  if (!(info = find_color (color)))
    info = &tab_group_colors[0];

  return g_dgettext (GETTEXT_PACKAGE, info->label);
}

#define ICON_SIZE  16
#define ICON_SCALE 3

static GIcon *
create_icon (guint32  rgb,
             gboolean modified)
{
  const int px = ICON_SIZE * ICON_SCALE;
  g_autoptr(GBytes) bytes = NULL;
  cairo_surface_t *surface;
  GdkTexture *texture;
  cairo_t *cr;
  int stride;

  surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, px, px);
  cr = cairo_create (surface);

  cairo_scale (cr, ICON_SCALE, ICON_SCALE);
  cairo_set_source_rgb (cr,
                        ((rgb >> 16) & 0xff) / 255.,
                        ((rgb >> 8) & 0xff) / 255.,
                        (rgb & 0xff) / 255.);

  /* A ring in the group color, with a dot inside if there are changes */
  cairo_set_line_width (cr, 2);
  cairo_arc (cr, ICON_SIZE / 2., ICON_SIZE / 2., 5.5, 0, 2 * G_PI);
  cairo_stroke (cr);

  if (modified)
    {
      cairo_arc (cr, ICON_SIZE / 2., ICON_SIZE / 2., 2.5, 0, 2 * G_PI);
      cairo_fill (cr);
    }

  cairo_destroy (cr);
  cairo_surface_flush (surface);

  stride = cairo_image_surface_get_stride (surface);
  bytes = g_bytes_new (cairo_image_surface_get_data (surface), (gsize)stride * px);
  texture = gdk_memory_texture_new (px, px, GDK_MEMORY_DEFAULT, bytes, stride);

  cairo_surface_destroy (surface);

  return G_ICON (texture);
}

/**
 * _editor_tab_group_get_icon:
 * @color: a color id
 * @modified: if the tab has unsaved changes
 *
 * Gets the icon shown on tabs which belong to a group of @color.
 *
 * Returns: (transfer none): a #GIcon
 */
GIcon *
_editor_tab_group_get_icon (const char *color,
                            gboolean    modified)
{
  static GIcon *icons[G_N_ELEMENTS (tab_group_colors)][2];
  const TabGroupColor *info;
  guint index;

  if (!(info = find_color (color)))
    info = &tab_group_colors[0];

  index = info - tab_group_colors;
  modified = !!modified;

  if (icons[index][modified] == NULL)
    icons[index][modified] = create_icon (info->rgb, modified);

  return icons[index][modified];
}
