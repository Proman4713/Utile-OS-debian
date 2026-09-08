/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* -*- encoding: utf8 -*- */
/*
 * Copyright (C) 2019 Purism SPC
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
 *
 * Written by:
 *     Adrien Plazas <kekun.plazas@laposte.net>
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gis-page-header.h"
#include "gis-focusable-bin.h"
#include "gis-webkit.h"

enum {
  PROP_0,
  PROP_TITLE,
  PROP_SUBTITLE,
  PROP_ICON_NAME,
  PROP_PAINTABLE,
  PROP_HEIGHT_REQUEST,
  PROP_SHOW_ICON,
  PROP_PIXEL_SIZE,
  PROP_ACCESSIBLE_TITLE,
  PROP_LAST,
};

static GParamSpec *obj_props[PROP_LAST];

struct _GisPageHeader
{
  GtkBox parent;

  GtkWidget *box;
  GtkWidget *icon;
  GtkWidget *subtitle;
  GtkWidget *title;

  gint pixel_size;
  guint height_request;
  gboolean show_icon;
};

G_DEFINE_TYPE (GisPageHeader, gis_page_header, GTK_TYPE_BOX)

static gboolean
is_valid_string (const gchar *s)
{
  return s != NULL && g_strcmp0 (s, "") != 0;
}

static void
update_box_visibility (GisPageHeader *header)
{
  gtk_widget_set_visible (header->box, gtk_widget_get_visible (header->subtitle) ||
                                       gtk_widget_get_visible (header->title));
}

static gboolean
gis_page_header_grab_focus (GtkWidget *widget)
{
  GisPageHeader *header = GIS_PAGE_HEADER (widget);
  gboolean retval = gtk_widget_grab_focus (GTK_WIDGET (header->box));

  /* Ensure that when grab_focus is called on the header (which happens when
   * switching between pages), the "focus ring" is hidden.
   */
  gtk_window_set_focus_visible (GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (header))), FALSE);
  return retval;
}

static void
gis_page_header_init (GisPageHeader *header)
{
  gtk_widget_init_template (GTK_WIDGET (header));

  g_signal_connect_swapped (header->subtitle, "notify::visible",
                            G_CALLBACK(update_box_visibility), header);
  g_signal_connect_swapped (header->title, "notify::visible",
                            G_CALLBACK(update_box_visibility), header);
}

static void
update_icon_properties (GisPageHeader *header)
{
  if (!header->icon)
    return;

  g_object_set (G_OBJECT (header->icon),
                "pixel-size", header->pixel_size,
                "height-request", header->height_request,
                "visible", header->show_icon,
                NULL);
}

static void
gis_page_header_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  GisPageHeader *header = GIS_PAGE_HEADER (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      g_value_set_string (value, gtk_label_get_label (GTK_LABEL (header->title)));
      break;

    case PROP_SUBTITLE:
      g_value_set_string (value, gtk_label_get_label (GTK_LABEL (header->subtitle)));
      break;

    case PROP_ICON_NAME:
      g_object_get_property (G_OBJECT (header->icon), "icon-name", value);
      break;

    case PROP_PAINTABLE:
      g_object_get_property (G_OBJECT (header->icon), "paintable", value);
      break;

    case PROP_HEIGHT_REQUEST:
      g_value_set_uint (value, header->height_request);
      break;

    case PROP_PIXEL_SIZE:
      g_value_set_int (value, header->pixel_size);
      break;

    case PROP_SHOW_ICON:
      g_value_set_boolean (value, header->show_icon);
      break;

    case PROP_ACCESSIBLE_TITLE:
      g_value_set_boolean (value, gtk_widget_get_can_focus (header->box));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gis_page_header_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  GisPageHeader *header = GIS_PAGE_HEADER (object);

  switch (prop_id)
    {
    case PROP_TITLE:
      gtk_label_set_label (GTK_LABEL (header->title), g_value_get_string (value));
      gtk_widget_set_visible (header->title, is_valid_string (g_value_get_string (value)));
      break;

    case PROP_SUBTITLE:
      gtk_label_set_markup (GTK_LABEL (header->subtitle), g_value_get_string (value));
      gtk_widget_set_visible (header->subtitle, is_valid_string (g_value_get_string (value)));
      break;

    case PROP_ICON_NAME:
      g_object_set_property (G_OBJECT (header->icon), "icon-name", value);
      update_icon_properties (header);
      break;

    case PROP_PAINTABLE:
      g_object_set_property (G_OBJECT (header->icon), "paintable", value);
      update_icon_properties (header);
      break;

    case PROP_HEIGHT_REQUEST:
      header->height_request = g_value_get_uint (value);
      update_icon_properties (header);
      break;

    case PROP_PIXEL_SIZE:
      header->pixel_size = g_value_get_int (value);
      update_icon_properties (header);
      break;

    case PROP_SHOW_ICON:
      header->show_icon = g_value_get_boolean (value);
      update_icon_properties (header);
      break;

    case PROP_ACCESSIBLE_TITLE:
      gtk_widget_set_can_focus (header->box, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gis_page_header_class_init (GisPageHeaderClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  g_type_ensure (GIS_TYPE_FOCUSABLE_BIN);
  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-page-header.ui");

  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisPageHeader, box);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisPageHeader, icon);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisPageHeader, subtitle);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisPageHeader, title);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gis_activate_link);

  gobject_class->get_property = gis_page_header_get_property;
  gobject_class->set_property = gis_page_header_set_property;

  obj_props[PROP_TITLE] =
    g_param_spec_string ("title",
                         "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_SUBTITLE] =
    g_param_spec_string ("subtitle",
                         "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_ICON_NAME] =
    g_param_spec_string ("icon-name",
                         "", "",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_PAINTABLE] =
    g_param_spec_object ("paintable",
                         "", "",
                         GDK_TYPE_PAINTABLE,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  obj_props[PROP_HEIGHT_REQUEST] =
    g_param_spec_uint ("height-request",
                       "", "",
                       0, G_MAXUINT, 0,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  obj_props[PROP_PIXEL_SIZE] =
    g_param_spec_int ("pixel-size",
                      "", "",
                      0, G_MAXINT, 280, /* 280 matches the default image size */
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  obj_props[PROP_SHOW_ICON] =
    g_param_spec_boolean ("show-icon",
                          "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  obj_props[PROP_ACCESSIBLE_TITLE] =
    g_param_spec_boolean ("use-accessible-title",
                          "", "",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (gobject_class, PROP_LAST, obj_props);

  widget_class->grab_focus = gis_page_header_grab_focus;
}
