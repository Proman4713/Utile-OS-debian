/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2025 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "gis-focusable-bin.h"

struct _GisFocusableBin
{
  GtkWidget  parent_instance;
  GtkWidget *child;
};

static void gis_focusable_bin_buildable_iface_init (GtkBuildableIface *iface);
void gis_focusable_bin_set_child (GisFocusableBin *bin, GtkWidget *child);
GtkWidget *gis_focusable_bin_get_child (GisFocusableBin *bin);

G_DEFINE_TYPE_WITH_CODE (GisFocusableBin, gis_focusable_bin, GTK_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, gis_focusable_bin_buildable_iface_init))

enum {
  PROP_0,
  PROP_CHILD,
  PROP_LAST,
};
static GParamSpec *props[PROP_LAST];

static void
gis_focusable_bin_compute_expand (GtkWidget *widget,
                                  gboolean  *hexpand,
                                  gboolean  *vexpand)
{
  GisFocusableBin *bin = (GIS_FOCUSABLE_BIN (widget));

  if (bin->child)
    {
      *hexpand = gtk_widget_compute_expand (bin->child, GTK_ORIENTATION_HORIZONTAL);
      *vexpand = gtk_widget_compute_expand (bin->child, GTK_ORIENTATION_VERTICAL);
    }
  else
    {
      *hexpand = FALSE;
      *vexpand = FALSE;
    }
}

static GtkSizeRequestMode
gis_focusable_bin_get_request_mode (GtkWidget *widget)
{
  GisFocusableBin *bin = (GIS_FOCUSABLE_BIN (widget));

  if (bin->child)
    return gtk_widget_get_request_mode (bin->child);
  else
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
gis_focusable_bin_dispose (GObject *object)
{
  GisFocusableBin *bin = (GIS_FOCUSABLE_BIN (object));

  g_clear_pointer (&bin->child, gtk_widget_unparent);

  G_OBJECT_CLASS (gis_focusable_bin_parent_class)->dispose (object);
}

static void
gis_focusable_bin_set_property (GObject         *object,
                                guint            prop_id,
                                const GValue    *value,
                                GParamSpec      *pspec)
{
  GisFocusableBin *bin = (GIS_FOCUSABLE_BIN (object));

  switch (prop_id)
    {
    case PROP_CHILD:
      gis_focusable_bin_set_child (bin, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gis_focusable_bin_get_property (GObject         *object,
                                guint            prop_id,
                                GValue          *value,
                                GParamSpec      *pspec)
{
  GisFocusableBin *bin = (GIS_FOCUSABLE_BIN (object));

  switch (prop_id)
    {
    case PROP_CHILD:
      g_value_set_object (value, bin->child);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static GtkBuildableIface *parent_buildable_iface;

static void
gis_focusable_bin_buildable_add_child (GtkBuildable *buildable,
                                       GtkBuilder   *builder,
                                       GObject      *child,
                                       const char   *type)
{
  if (GTK_IS_WIDGET (child))
    {
      gis_focusable_bin_set_child (GIS_FOCUSABLE_BIN (buildable), GTK_WIDGET (child));
    }
  else
    {
      parent_buildable_iface->add_child (buildable, builder, child, type);
    }
}

static void
gis_focusable_bin_css_init (void)
{
  g_autoptr(GtkCssProvider) provider = NULL;
  GdkDisplay *display;

  if (!gtk_is_initialized ())
    return;

  if (!(display = gdk_display_get_default ()))
    return;

  provider = gtk_css_provider_new ();

  gtk_css_provider_load_from_string (provider,
    "focusablebin:focus:focus-visible {"
    "  outline-color: alpha(var(--accent-bg-color), 0.5);"
    "  outline-width: 2px;"
    "  outline-offset: -2px;"
    "  outline-style: solid;"
    "  border-radius: 10px;"
    "}");
  gtk_style_context_add_provider_for_display (display,
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_FALLBACK);
}

static void
gis_focusable_bin_buildable_iface_init (GtkBuildableIface *iface)
{
  parent_buildable_iface = g_type_interface_peek_parent (iface);

  iface->add_child = gis_focusable_bin_buildable_add_child;
}

static void
gis_focusable_bin_class_init (GisFocusableBinClass *klass)
{
  GtkWidgetClass *widget_class = (GtkWidgetClass*) klass;
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class = G_OBJECT_CLASS (klass);
  widget_class = (GtkWidgetClass*) klass;

  gobject_class->dispose      = gis_focusable_bin_dispose;
  gobject_class->set_property = gis_focusable_bin_set_property;
  gobject_class->get_property = gis_focusable_bin_get_property;

  widget_class->compute_expand = gis_focusable_bin_compute_expand;
  widget_class->get_request_mode = gis_focusable_bin_get_request_mode;

  props[PROP_CHILD] = g_param_spec_object ("child", NULL, NULL,
                                           GTK_TYPE_WIDGET,
                                           G_PARAM_READWRITE|G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (gobject_class, PROP_LAST, props);
  gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_LABEL);
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "focusablebin");
  gis_focusable_bin_css_init ();
}

static void
gis_focusable_bin_init (GisFocusableBin *self)
{
  gtk_widget_set_focusable (GTK_WIDGET (self), TRUE);
  gtk_widget_set_receives_default (GTK_WIDGET (self), TRUE);
}

/**
 * gis_focusable_bin_set_child:
 * @button: a `GisFocusableBin`
 * @child: (nullable): the child widget
 *
 * Sets the child widget of @button.
 *
 * Note that by using this API, you take full responsibility for setting
 * up the proper accessibility label and description information for @button.
 * Most likely, you'll either set the accessibility label or description
 * for @button explicitly, or you'll set a labelled-by or described-by
 * relations from @child to @button.
 */
void
gis_focusable_bin_set_child (GisFocusableBin *bin,
                             GtkWidget *child)
{
  g_return_if_fail (GIS_IS_FOCUSABLE_BIN (bin));

  g_return_if_fail (child == NULL || bin->child == child || gtk_widget_get_parent (child) == NULL);

  if (bin->child == child)
    return;

  g_clear_pointer (&bin->child, gtk_widget_unparent);

  bin->child = child;

  if (bin->child)
    gtk_widget_set_parent (bin->child, GTK_WIDGET (bin));

  g_object_notify_by_pspec (G_OBJECT (bin), props[PROP_CHILD]);
}

/**
 * gis_focusable_bin_get_child:
 * @button: a `GisFocusableBin`
 *
 * Gets the child widget of @button.
 *
 * Returns: (nullable) (transfer none): the child widget of @button
 */
GtkWidget *
gis_focusable_bin_get_child (GisFocusableBin *bin)
{
  g_return_val_if_fail (GIS_IS_FOCUSABLE_BIN (bin), NULL);

  return bin->child;
}

GisFocusableBin *
gis_focusable_bin_new (void)
{
  return g_object_new (GIS_TYPE_FOCUSABLE_BIN, NULL);
}
