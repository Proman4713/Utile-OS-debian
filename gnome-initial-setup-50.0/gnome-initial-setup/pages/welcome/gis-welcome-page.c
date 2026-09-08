/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2020 Red Hat
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
 *     Matthias Clasen <mclasen@redhat.com>
 */

/* Welcome page {{{1 */

#define PAGE_ID "welcome"

#include "config.h"
#include "welcome-resources.h"
#include "gis-welcome-page.h"
#include "gis-assistant.h"

#include "gis-page-header.h"
#include "gis-webkit.h"


struct _GisWelcomePage
{
  GisPage parent;
};

typedef struct
{
  AdwStatusPage *header;
  GtkWidget *release_notes;
} GisWelcomePagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisWelcomePage, gis_welcome_page, GIS_TYPE_PAGE)

static gboolean
on_link_activated (GisWelcomePage *self,
                   GtkLinkButton  *link_button)
{
  gis_activate_link (NULL,
                     gtk_link_button_get_uri (link_button),
                     GTK_WIDGET (self));

  return TRUE;
}

static void
update_welcome_title (GisWelcomePage *page)
{
  GisWelcomePagePrivate *priv = gis_welcome_page_get_instance_private (page);
  g_autofree char *name = g_get_os_info (G_OS_INFO_KEY_PRETTY_NAME);
  g_autofree char *text = NULL;
  g_autofree char *version = g_get_os_info (G_OS_INFO_KEY_VERSION_ID);
  g_autofree char *release_note_uri = NULL;

  if (!name)
    name = g_strdup ("Ubuntu");

  /* Translators: This is meant to be a warm, engaging welcome message,
   * like greeting somebody at the door. If the exclamation mark is not
   * suitable for this in your language you may replace it. The space
   * before the exclamation mark in this string is a typographical thin
   * space (U200a) to improve the spacing in the title, which you can
   * keep or remove. The %s is getting replaced with the name and version
   * of the OS, e.g. "GNOME 3.38"
   */
  text = g_strdup_printf (_("Welcome to %s !"), name);

  g_object_set (priv->header, "title", text, NULL);

  gtk_widget_set_visible (priv->release_notes, !!version);

  if (!gtk_widget_get_visible (priv->release_notes))
    return;

  release_note_uri = g_strdup_printf (
        "https://www.ubuntu.com/getubuntu/releasenotes?os=ubuntu&ver=%s",
        version);

  gtk_link_button_set_uri (GTK_LINK_BUTTON (priv->release_notes), release_note_uri);
}

static gboolean
transform_dark_to_icon_name (GBinding     *binding,
                             const GValue *from_value,
                             GValue       *to_value,
                             gpointer      user_data)
{
  gboolean is_dark = g_value_get_boolean (from_value);

  if (is_dark) {
    g_value_set_string (to_value, "ubuntu-mascot-dark");
  } else {
    g_value_set_string (to_value, "ubuntu-mascot-light");
  }

  return TRUE;
}

static void
gis_welcome_page_constructed (GObject *object)
{
  g_autoptr(GtkWidget) image = NULL;
  GisWelcomePage *page = GIS_WELCOME_PAGE (object);
  GisWelcomePagePrivate *priv = gis_welcome_page_get_instance_private (page);
  AdwStyleManager *style_manager;

  G_OBJECT_CLASS (gis_welcome_page_parent_class)->constructed (object);

  update_welcome_title (page);

  gis_page_set_complete (GIS_PAGE (page), TRUE);

  if (gis_driver_get_mode (GIS_PAGE (page)->driver) == GIS_DRIVER_MODE_UPGRADE)
    g_object_set (priv->header, "subtitle", NULL, NULL);

  if (gtk_widget_get_visible (priv->release_notes))
    {
      g_signal_connect_object (priv->release_notes, "activate-link",
                               G_CALLBACK (on_link_activated), page,
                               G_CONNECT_SWAPPED);
    }


  style_manager = adw_style_manager_get_default ();

  g_object_bind_property_full (style_manager, "dark",
                               priv->header, "icon-name",
                               G_BINDING_SYNC_CREATE,
                               transform_dark_to_icon_name,
                               NULL,
                               NULL,
                               NULL);
}

static void
start_setup (GtkButton *button, GisWelcomePage *page)
{
  GisAssistant *assistant;

  assistant = GIS_ASSISTANT (gtk_widget_get_ancestor (GTK_WIDGET (page), GIS_TYPE_ASSISTANT));

  gis_assistant_next_page (assistant);
}

static void
gis_welcome_page_class_init (GisWelcomePageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-welcome-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisWelcomePage, header);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisWelcomePage, release_notes);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), start_setup);

  page_class->page_id = PAGE_ID;
  object_class->constructed = gis_welcome_page_constructed;
}

static void
gis_welcome_page_init (GisWelcomePage *page)
{
  g_type_ensure (GIS_TYPE_PAGE_HEADER);
  gtk_widget_init_template (GTK_WIDGET (page));

  gis_add_style_from_resource ("/org/gnome/initial-setup/gis-welcome-page.css");
}

GisPage *
gis_prepare_welcome_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_WELCOME_PAGE,
                       "driver", driver,
                       NULL);
}
