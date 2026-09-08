/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Canonical Ltd.
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

/* Get more apps page {{{1 */

#define PAGE_ID "apps"

#include "config.h"
#include "gis-apps-page.h"
#include "apps-resources.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

struct _GisAppsPage {
  GisPage parent;

  GtkWidget *apps_image;
  GtkWidget *app_center;
  GtkWidget *header;
};

G_DEFINE_TYPE (GisAppsPage, gis_apps_page, GIS_TYPE_PAGE);

static void
gis_apps_page_constructed (GObject *object)
{
  GisAppsPage *page = GIS_APPS_PAGE (object);
  g_autoptr(GtkWidget) image = NULL;

  G_OBJECT_CLASS (gis_apps_page_parent_class)->constructed (object);

  gis_page_set_skippable (GIS_PAGE (page), TRUE);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
}

static void
page_shown (GisPage *page)
{
  GisAppsPage *self = GIS_APPS_PAGE (page);
  gtk_widget_grab_focus (self->header);
}

static void
open_software (GtkButton      *button,
               const gchar    *uri,
               GisAppsPage *page)
{
  g_autofree gchar *command = NULL;
  g_autoptr(GAppInfo) info = NULL;
  g_autoptr(GError) error = NULL;

  g_autofree gchar *storecmd = NULL;

  storecmd = g_find_program_in_path ("snap-store");
  if (storecmd == NULL)
    storecmd = g_find_program_in_path ("snap-store.ubuntu-software");
  if (storecmd == NULL)
    storecmd = g_find_program_in_path ("gnome-software");
  if (storecmd == NULL) {
    g_warning ("Failed to find snap-store or gnome-software");
    return;
  }

  info = g_app_info_create_from_commandline (storecmd, NULL, G_APP_INFO_CREATE_NONE, &error);
  if (info == NULL) {
     g_warning ("Failed to get launch information from gnome-software: %s", error->message);
     return;
  }
  if (!g_app_info_launch (info, NULL, NULL, &error)) {
     g_warning ("Failed to launch gnome-software: %s", error->message);
     return;
  }
}

static void
gis_apps_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Ready to go"));
}


static void
gis_apps_page_class_init (GisAppsPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-apps-page.ui");
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), open_software);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppsPage, app_center);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppsPage, header);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_apps_page_locale_changed;
  page_class->shown = page_shown;
  object_class->constructed = gis_apps_page_constructed;
}

static void
gis_apps_page_init (GisAppsPage *page)
{
  g_resources_register (apps_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));

}

GisPage *
gis_prepare_apps_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_APPS_PAGE,
                       "driver", driver,
                       NULL);
}
