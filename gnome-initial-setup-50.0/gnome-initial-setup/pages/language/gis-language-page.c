/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2012 Red Hat
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
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 *     Michael Wood <michael.g.wood@intel.com>
 *
 * Based on gnome-control-center cc-region-panel.c
 */

/* Language page {{{1 */

#define PAGE_ID "language"

#define GNOME_SYSTEM_LOCALE_DIR "org.gnome.system.locale"
#define REGION_KEY "region"

#include "config.h"
#include "language-resources.h"
#include "gis-welcome-widget.h"
#include "cc-language-chooser.h"
#include "gis-language-page.h"

#include <act/act-user-manager.h>
#include <polkit/polkit.h>
#include <locale.h>
#include <gtk/gtk.h>

struct _GisLanguagePagePrivate
{
  GtkWidget *logo;
  GtkWidget *welcome_widget;
  GtkWidget *language_chooser;

  const gchar *new_locale_id;
};
typedef struct _GisLanguagePagePrivate GisLanguagePagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisLanguagePage, gis_language_page, GIS_TYPE_PAGE);

static void
language_changed (CcLanguageChooser  *chooser,
                  GParamSpec         *pspec,
                  GisLanguagePage    *page)
{
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);
  GisDriver *driver;
  GSettings *region_settings;

  priv->new_locale_id = cc_language_chooser_get_language (chooser);
  driver = GIS_PAGE (page)->driver;

  gis_driver_set_user_language (driver, priv->new_locale_id, TRUE);

  /* Ensure we won't override the selected language for format strings */
  region_settings = g_settings_new (GNOME_SYSTEM_LOCALE_DIR);
  g_settings_reset (region_settings, REGION_KEY);
  g_object_unref (region_settings);

  gis_welcome_widget_show_locale (GIS_WELCOME_WIDGET (priv->welcome_widget),
                                  priv->new_locale_id);
}

static void
update_distro_logo (GisLanguagePage *page)
{
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);
  g_autofree char *id = g_get_os_info (G_OS_INFO_KEY_ID);
  gsize i;

  static const struct {
    const char *id;
    const char *logo;
  } id_to_logo[] = {
    { "debian",                         "emblem-debian" },
    { "fedora",                         "fedora-logo-icon" },
    { "ubuntu",                         "distributor-logo" },
    { "openSUSE Tumbleweed",            "opensuse-logo-icon" },
    { "openSUSE Leap",                  "opensuse-logo-icon" },
    { "SLED",                           "suse-logo-icon" },
    { "SLES",                           "suse-logo-icon" },
  };

  for (i = 0; i < G_N_ELEMENTS (id_to_logo); i++)
    {
      if (g_strcmp0 (id, id_to_logo[i].id) == 0)
        {
          g_object_set (priv->logo, "icon-name", id_to_logo[i].logo, NULL);
          break;
        }
    }
}

static void
language_confirmed (CcLanguageChooser *chooser,
                    GisLanguagePage   *page)
{
  gis_assistant_next_page (gis_driver_get_assistant (GIS_PAGE (page)->driver));
}

static void
gis_language_page_constructed (GObject *object)
{
  GisLanguagePage *page = GIS_LANGUAGE_PAGE (object);
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (page);

  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);

  G_OBJECT_CLASS (gis_language_page_parent_class)->constructed (object);

  update_distro_logo (page);

  g_signal_connect (priv->language_chooser, "notify::language",
                    G_CALLBACK (language_changed), page);
  g_signal_connect (priv->language_chooser, "confirm",
                    G_CALLBACK (language_confirmed), page);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (page), TRUE);
}

static void
gis_language_page_shown (GisPage *page)
{
  GisLanguagePagePrivate *priv = gis_language_page_get_instance_private (GIS_LANGUAGE_PAGE (page));
  gtk_widget_grab_focus (GTK_WIDGET (priv->language_chooser));
}

static void
gis_language_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Welcome"));
}

static void
gis_language_page_dispose (GObject *object)
{
  G_OBJECT_CLASS (gis_language_page_parent_class)->dispose (object);
}

static void
gis_language_page_class_init (GisLanguagePageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-language-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, welcome_widget);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, language_chooser);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisLanguagePage, logo);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_language_page_locale_changed;
  page_class->shown = gis_language_page_shown;
  object_class->constructed = gis_language_page_constructed;
  object_class->dispose = gis_language_page_dispose;
}

static void
gis_language_page_init (GisLanguagePage *page)
{
  g_type_ensure (GIS_TYPE_WELCOME_WIDGET);
  g_type_ensure (CC_TYPE_LANGUAGE_CHOOSER);

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_language_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_LANGUAGE_PAGE,
                       "driver", driver,
                       NULL);
}
