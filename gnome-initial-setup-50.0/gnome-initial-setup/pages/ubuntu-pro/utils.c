/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2022-2024 Canonical Ltd.
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

#include "utils.h"

#include <gio/gio.h>
#include <adwaita.h>

gboolean
get_ubuntu_advantage_attached (gboolean *attached)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDBusConnection) bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (bus == NULL)
    {
      g_warning ("Failed to get system bus: %s", error->message);
      return FALSE;
    }

  g_autoptr(GVariant) result = g_dbus_connection_call_sync (bus,
                                                            "com.canonical.UbuntuAdvantage",
                                                            "/com/canonical/UbuntuAdvantage/Manager",
                                                            "org.freedesktop.DBus.Properties",
                                                            "Get",
                                                            g_variant_new ("(ss)",
                                                              "com.canonical.UbuntuAdvantage.Manager",
                                                              "Attached"),
                                                            G_VARIANT_TYPE ("(v)"),
                                                            G_DBUS_CALL_FLAGS_NONE,
                                                            -1,
                                                            NULL,
                                                            &error);
  if (result == NULL)
    {
      g_warning ("Failed to contact Ubuntu Advantage D-Bus service: %s", error->message);
      return FALSE;
    }

  g_autoptr(GVariant) value = NULL;
  g_variant_get (result, "(v)", &value);
  if (!g_variant_is_of_type (value, G_VARIANT_TYPE ("b")))
    {
      g_warning ("Attached property has wrong type");
      return FALSE;
    }
  g_variant_get (value, "b", attached);

  return TRUE;
}

gboolean
is_lts ()
{
  g_autofree gchar *version = g_get_os_info (G_OS_INFO_KEY_VERSION);

  return version && g_strrstr (version, "LTS") != NULL;
}

gboolean
is_ubuntu_pro_supported ()
{
  return is_lts ();
}

/* Routine from Gnome Control Center
 * https://gitlab.gnome.org/GNOME/gnome-control-center/-/commit/a3b8964a */
gboolean
is_dark_theme (GtkWidget *widget)
{
  g_autofree char *theme_name = NULL;

  theme_name = g_strdup (g_getenv ("GTK_THEME"));
  if (theme_name != NULL)
    return g_str_has_suffix (theme_name, "-dark");

  AdwStyleManager *manager = adw_style_manager_get_default ();
  return adw_style_manager_get_dark (manager);
}
