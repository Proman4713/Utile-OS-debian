/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2026 Canonical Ltd.
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

#define PAGE_ID "appearance"

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include "config.h"
#include "appearance-resources.h"
#include "gis-appearance-page.h"
#include "gis-page-header.h"

#include "cc-background-preview.h"

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <adwaita.h>
#include <gdesktop-enums.h>

#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define INTERFACE_PATH_ID "org.gnome.desktop.interface"
#define INTERFACE_ACCENT_COLOR_KEY "accent-color"
#define INTERFACE_COLOR_SCHEME_KEY "color-scheme"
#define INTERFACE_CURSOR_THEME_KEY "cursor-theme"
#define INTERFACE_GTK_THEME_KEY "gtk-theme"
#define INTERFACE_ICON_THEME_KEY "icon-theme"

#define DEFAULT_ACCENT_COLOR "default"

struct _GisAppearancePage
{
  GisPage parent;

  GSettings *interface_settings;
  GDBusProxy *shell_proxy;
  GCancellable *cancellable;

  GtkWidget *header;
  GtkWidget *accent_box;
  GtkWidget *oled_label;
  GtkToggleButton *default_toggle;
  GtkToggleButton *dark_toggle;
};

G_DEFINE_TYPE (GisAppearancePage, gis_appearance_page, GIS_TYPE_PAGE);

static const char *
get_color_tooltip (GDesktopAccentColor color)
{
  switch (color)
    {
    case G_DESKTOP_ACCENT_COLOR_BLUE:
    /* TRANSLATORS: An accent color. */
      return _("Blue");
    case G_DESKTOP_ACCENT_COLOR_TEAL:
    /* TRANSLATORS: An accent color. */
      return _("Teal");
    case G_DESKTOP_ACCENT_COLOR_GREEN:
    /* TRANSLATORS: An accent color. */
      return _("Green");
    case G_DESKTOP_ACCENT_COLOR_YELLOW:
    /* TRANSLATORS: An accent color. */
      return _("Yellow");
    case G_DESKTOP_ACCENT_COLOR_ORANGE:
    /* TRANSLATORS: An accent color. */
      return _("Orange");
    case G_DESKTOP_ACCENT_COLOR_RED:
    /* TRANSLATORS: An accent color. */
      return _("Red");
    case G_DESKTOP_ACCENT_COLOR_PINK:
    /* TRANSLATORS: An accent color. */
      return _("Pink");
    case G_DESKTOP_ACCENT_COLOR_PURPLE:
    /* TRANSLATORS: An accent color. */
      return _("Purple");
    case G_DESKTOP_ACCENT_COLOR_SLATE:
    /* TRANSLATORS: An accent color. */
      return _("Slate");
    case G_DESKTOP_ACCENT_COLOR_BROWN:
    /* TRANSLATORS: An accent color. */
      return _("Warty Brown");
    default:
      g_assert_not_reached ();
    }
}

static const char *
get_untranslated_color (GDesktopAccentColor color)
{
  switch (color)
    {
    case G_DESKTOP_ACCENT_COLOR_BLUE:
      return "blue";
    case G_DESKTOP_ACCENT_COLOR_TEAL:
      return "teal";
    case G_DESKTOP_ACCENT_COLOR_GREEN:
      return "green";
    case G_DESKTOP_ACCENT_COLOR_YELLOW:
      return "yellow";
    case G_DESKTOP_ACCENT_COLOR_ORANGE:
      return "orange";
    case G_DESKTOP_ACCENT_COLOR_RED:
      return "red";
    case G_DESKTOP_ACCENT_COLOR_PINK:
      return "pink";
    case G_DESKTOP_ACCENT_COLOR_PURPLE:
      return "purple";
    case G_DESKTOP_ACCENT_COLOR_SLATE:
      return "slate";
    case G_DESKTOP_ACCENT_COLOR_BROWN:
      return "brown";
    default:
      g_assert_not_reached ();
    }
}

static void
got_shell_proxy_cb (GObject      *source,
                    GAsyncResult *res,
                    gpointer      data)
{
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GError) error = NULL;
  GisAppearancePage *self = data;

  if (!(proxy = g_dbus_proxy_new_for_bus_finish (res, &error)))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Error creating Shell proxy: %s", error->message);

      return;
    }

  self->shell_proxy = g_steal_pointer (&proxy);
}

static void
transition_screen (GisAppearancePage *self)
{
  if (!self->shell_proxy)
    return;

  g_dbus_proxy_call (self->shell_proxy,
                     "ScreenTransition",
                     NULL,
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     self->cancellable,
                     NULL, NULL);
}

static void
update_yaru_accent_settings (GisAppearancePage *self)
{
  g_autoptr(GString) gtk_theme = NULL;
  g_autoptr(GString) icon_theme = NULL;
  g_autoptr(GString) gedit_theme = NULL;
  g_autofree char *current_gtk_theme = NULL;
  g_autofree char *current_icon_theme = NULL;
  g_autofree char *yaru_accent = NULL;
  GDesktopColorScheme scheme;

  g_object_get (G_OBJECT (adw_style_manager_get_default ()), "yaru-accent",
                &yaru_accent, NULL);
  if (yaru_accent == NULL)
    return;

  gtk_theme = g_string_new ("Yaru");
  icon_theme = g_string_new ("Yaru");
  gedit_theme = g_string_new ("Yaru");

  if (!g_str_equal (yaru_accent, DEFAULT_ACCENT_COLOR))
    {
      g_string_append_printf (gtk_theme, "-%s", yaru_accent);
      g_string_append_printf (icon_theme, "-%s", yaru_accent);
    }

  scheme = g_settings_get_enum (self->interface_settings, INTERFACE_COLOR_SCHEME_KEY);
  if (scheme == G_DESKTOP_COLOR_SCHEME_PREFER_DARK)
    {
      g_string_append (gtk_theme, "-dark");
      g_string_append (icon_theme, "-dark");
      g_string_append (gedit_theme, "-dark");
    }

  current_gtk_theme = g_settings_get_string (self->interface_settings, INTERFACE_GTK_THEME_KEY);
  if (g_strcmp0 (current_gtk_theme, gtk_theme->str) != 0)
    g_settings_set_string (self->interface_settings, INTERFACE_GTK_THEME_KEY, gtk_theme->str);

  current_icon_theme = g_settings_get_string (self->interface_settings, INTERFACE_ICON_THEME_KEY);

  if (!current_icon_theme ||
      g_str_has_prefix (current_icon_theme, "Yaru"))
    g_settings_set_string (self->interface_settings, INTERFACE_ICON_THEME_KEY, icon_theme->str);
}

static void
on_accent_color_toggled_cb (GisAppearancePage *self,
                            GtkToggleButton   *toggle)
{
  GDesktopAccentColor accent_color_from_key;
  GDesktopAccentColor accent_color = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (toggle), "accent-color"));

  if (!gtk_toggle_button_get_active (toggle))
    return;

  accent_color_from_key = g_settings_get_enum (self->interface_settings,
                                                INTERFACE_ACCENT_COLOR_KEY);

  if (accent_color == accent_color_from_key)
    return;

  transition_screen (self);

  g_settings_set_enum (self->interface_settings,
                       INTERFACE_ACCENT_COLOR_KEY,
                       accent_color);

  update_yaru_accent_settings (self);
}

static void
setup_accent_color_toggles (GisAppearancePage *self)
{
  GDesktopAccentColor accent_color = g_settings_get_enum (self->interface_settings, INTERFACE_ACCENT_COLOR_KEY);
  GDesktopAccentColor i;

  for (i = G_DESKTOP_ACCENT_COLOR_BLUE;
       i <= G_DESKTOP_ACCENT_COLOR_SLATE || i == G_DESKTOP_ACCENT_COLOR_BROWN; i++)
    {
      GtkWidget *button = GTK_WIDGET (gtk_toggle_button_new ());
      GtkToggleButton *grouping_button = GTK_TOGGLE_BUTTON (gtk_widget_get_first_child (self->accent_box));

      gtk_widget_set_tooltip_text (button, get_color_tooltip (i));
      gtk_widget_add_css_class (button, "accent-button");
      gtk_widget_add_css_class (button, get_untranslated_color (i));
      g_object_set_data (G_OBJECT (button), "accent-color", GINT_TO_POINTER (i));
      g_signal_connect_object (button, "toggled",
                               G_CALLBACK (on_accent_color_toggled_cb),
                               self,
                               G_CONNECT_SWAPPED);

      if (grouping_button != NULL)
        gtk_toggle_button_set_group (GTK_TOGGLE_BUTTON (button), grouping_button);

      if (i == accent_color)
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

      gtk_box_append (GTK_BOX (self->accent_box), button);

      if (i == G_DESKTOP_ACCENT_COLOR_SLATE)
        i = G_DESKTOP_ACCENT_COLOR_BROWN-1;
    }
}

static void
load_custom_css (GisAppearancePage *self)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/initial-setup/preview.css");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


static void
on_color_scheme_toggle_active_cb (GisAppearancePage *page)
{
  GDesktopColorScheme color_scheme;

  transition_screen (page);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (page->dark_toggle)))
    color_scheme = G_DESKTOP_COLOR_SCHEME_PREFER_DARK;
  else
    color_scheme = G_DESKTOP_COLOR_SCHEME_DEFAULT;

  g_settings_set_enum (page->interface_settings,
                       INTERFACE_COLOR_SCHEME_KEY,
                       color_scheme);

  update_yaru_accent_settings (page);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (drmModeRes, drmModeFreeResources);
G_DEFINE_AUTO_CLEANUP_FREE_FUNC (drmModePropertyPtr, drmModeFreeProperty, NULL);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (drmModeConnector, drmModeFreeConnector);

gboolean
has_oled_panel (void)
{
  int i, j, k;
  char path[48];

  /* Iterate over /dev/dri/card* devices */
  for (i = 0; i < 16; i++)
    {
      g_autofd int fd = -1;
      g_autoptr (drmModeRes) resources = NULL;

      snprintf (path, sizeof (path), "/dev/dri/card%d", i);
      fd = open (path, O_RDWR | O_CLOEXEC);
      if (fd < 0)
        continue;

      resources = drmModeGetResources (fd);
      if (!resources)
        continue;

      for (j = 0; j < resources->count_connectors; j++)
        {
          g_autoptr (drmModeConnector) connector = NULL;

          connector = drmModeGetConnector (fd, resources->connectors[j]);
          if (!connector)
            continue;

          if (connector->connection == DRM_MODE_CONNECTED)
            {
              for (k = 0; k < connector->count_props; k++)
                {
                  g_auto(drmModePropertyPtr) prop = NULL;

                  prop = drmModeGetProperty (fd, connector->props[k]);
                  if (!prop)
                    continue;

                  if (g_str_equal (prop->name, "panel_type"))
                    {
                      if (connector->prop_values[k] == 1)
                        return TRUE;
                    }
                }
            }
        }
    }

  return FALSE;
}

static void
check_has_oled_page_thread (GTask *task,
                            gpointer source_object,
                            gpointer task_data,
                            GCancellable *cancellable)
{
  g_task_return_boolean (task, has_oled_panel ());
}

static void
on_oled_check_done (GObject      *source,
                    GAsyncResult *res,
                    gpointer      data)
{
  GisAppearancePage *page = GIS_APPEARANCE_PAGE (source);
  g_autoptr (GError) error = NULL;
  gboolean has_oled;

  has_oled = g_task_propagate_boolean (G_TASK (res), &error);
  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Error checking for OLED panel: %s", error->message);

      return;
    }

  gtk_widget_set_visible (page->oled_label, has_oled);
}

static void
gis_appearance_page_shown (GisPage *gis_page)
{
  GisAppearancePage *page = GIS_APPEARANCE_PAGE (gis_page);
  gtk_widget_grab_focus (GTK_WIDGET (page->header));
}

static void
gis_appearance_page_constructed (GObject *object)
{
  GisAppearancePage *page = GIS_APPEARANCE_PAGE (object);
  GDesktopColorScheme color_scheme;
  g_autoptr (GTask) task = NULL;

  G_OBJECT_CLASS (gis_appearance_page_parent_class)->constructed (object);

  page->interface_settings = g_settings_new (INTERFACE_PATH_ID);

  load_custom_css (page);
  setup_accent_color_toggles (page);

  color_scheme = g_settings_get_enum (page->interface_settings,
                                      INTERFACE_COLOR_SCHEME_KEY);

  if (color_scheme == G_DESKTOP_COLOR_SCHEME_PREFER_DARK)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->dark_toggle), TRUE);
    }
  else
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (page->default_toggle), TRUE);
    }

  page->cancellable = g_cancellable_new ();
  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SESSION,
                            G_DBUS_PROXY_FLAGS_NONE,
                            NULL,
                            "org.gnome.Shell",
                            "/org/gnome/Shell",
                            "org.gnome.Shell",
                            page->cancellable,
                            got_shell_proxy_cb,
                            page);

  g_signal_connect_object (adw_style_manager_get_default (),
                           "notify::yaru-accent",
                           G_CALLBACK (update_yaru_accent_settings),
                           page,
                           G_CONNECT_SWAPPED);

  task = g_task_new (page, NULL, on_oled_check_done, NULL);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_run_in_thread (task, check_has_oled_page_thread);

  gis_page_set_complete (GIS_PAGE (page), TRUE);
}

static void
gis_appearance_page_dispose (GObject *object)
{
  GisAppearancePage *page = GIS_APPEARANCE_PAGE (object);

  g_cancellable_cancel (page->cancellable);

  g_clear_object (&page->cancellable);
  g_clear_object (&page->interface_settings);
  g_clear_object (&page->shell_proxy);

  G_OBJECT_CLASS (gis_appearance_page_parent_class)->dispose (object);
}

static void
gis_appearance_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Appearance"));
}

static void
gis_appearance_page_class_init (GisAppearancePageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass),
                                               "/org/gnome/initial-setup/gis-appearance-page.ui");

  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppearancePage, header);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppearancePage, accent_box);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppearancePage, default_toggle);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppearancePage, dark_toggle);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisAppearancePage, oled_label);

  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_color_scheme_toggle_active_cb);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_appearance_page_locale_changed;
  page_class->shown = gis_appearance_page_shown;
  object_class->constructed = gis_appearance_page_constructed;
  object_class->dispose = gis_appearance_page_dispose;
}

static void
gis_appearance_page_init (GisAppearancePage *page)
{
  g_resources_register (appearance_get_resource ());
  g_type_ensure (GIS_TYPE_PAGE_HEADER);
  g_type_ensure (CC_TYPE_BACKGROUND_PREVIEW);

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_appearance_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_APPEARANCE_PAGE,
                       "driver", driver,
                       NULL);
}

