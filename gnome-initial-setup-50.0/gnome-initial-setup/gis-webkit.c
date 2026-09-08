/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2015, 2024 Red Hat Inc.
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

#include "config.h"

#include "gis-driver.h"
#include "gis-webkit.h"

#include <glib/gi18n.h>
#include <adwaita.h>
#include <gnome-qr-gtk/gnome-qr-widget.h>

#ifdef HAVE_WEBKITGTK
#include <webkit/webkit.h>
#endif

gboolean
ignore_uri_activation (GtkLabel    *label,
                       const gchar *uri,
                       GtkWidget   *any_widget)
{
  return TRUE;
}

static gboolean
gis_show_qr_code_dialog (const gchar *uri,
                         GtkWidget   *parent,
                         GtkLabel    *maybe_label)
{
  GtkWidget *headerbar;
  AdwDialog *dialog;
  GtkWidget *vbox;
  GtkWidget *qr_box;
  GtkWidget *qr_widget;
  GtkWidget *uri_label;

  dialog = adw_dialog_new ();
  adw_dialog_set_can_close (dialog, TRUE);
  adw_dialog_set_title (dialog, _("Scan the QR Code…"));

  qr_widget = g_object_new (GNOME_TYPE_QR_WIDGET,
                            "text", uri,
                            "focusable", TRUE,
                            "size", 300,
                            "margin-top", 30,
                            "margin-start", 30,
                            "margin-end", 30,
                            NULL);

  qr_box = g_object_new (GTK_TYPE_BOX,
                         "orientation", GTK_ORIENTATION_VERTICAL,
                         "spacing", 10,
                         "margin-start", 30,
                         "margin-end", 30,
                         "margin-top", 10,
                         "margin-bottom", 10,
                         NULL);
  gtk_widget_add_css_class (qr_box, "card");
  gtk_box_append (GTK_BOX (qr_box), qr_widget);

  uri_label = g_object_new (GTK_TYPE_LABEL,
                            "label", uri,
                            "ellipsize", PANGO_ELLIPSIZE_MIDDLE,
                            "max-width-chars", 35,
                            "margin-bottom", 12,
                            "margin-start", 12,
                            "margin-end", 12,
                            NULL);

  gtk_widget_add_css_class (uri_label, "caption");
  gtk_widget_add_css_class (uri_label, "monospace");
  gtk_widget_set_tooltip_text (uri_label, uri);
  gtk_box_append (GTK_BOX (qr_box), GTK_WIDGET (uri_label));

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_margin_bottom(GTK_WIDGET (vbox), 18);
  gtk_box_append (GTK_BOX (vbox), qr_box);

  if (GTK_IS_LABEL (maybe_label))
    {
      GtkWidget *label;

      label = g_object_new (GTK_TYPE_LABEL,
                            "label", gtk_label_get_label (maybe_label),
                            "use-markup", gtk_label_get_use_markup (maybe_label),
                            "wrap-mode", PANGO_WRAP_WORD,
                            "max-width-chars", 35,
                            "wrap", TRUE,
                            "justify", GTK_JUSTIFY_CENTER,
                            "margin-top", 12,
                            "margin-start", 12,
                            "margin-end", 12,
                            NULL);

      gtk_widget_add_css_class (label, "caption");
      g_signal_connect (label, "activate-link", G_CALLBACK (ignore_uri_activation), NULL);

      gtk_box_append (GTK_BOX (vbox), GTK_WIDGET (label));
    }

  GtkWidget *toolbar_view = adw_toolbar_view_new ();
  adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), vbox);
  headerbar = adw_header_bar_new ();
  adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), headerbar);

  adw_dialog_set_child (dialog, toolbar_view);
  adw_dialog_set_follows_content_size(dialog, TRUE);

  adw_dialog_present (dialog, GTK_WIDGET (gtk_widget_get_root (parent)));

  return TRUE;
}

#ifdef HAVE_WEBKITGTK
static void
notify_progress_cb (GObject    *object,
                    GParamSpec *pspec,
                    gpointer    user_data)
{
  GtkWidget *progress_bar = user_data;
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  gdouble progress;

  progress = webkit_web_view_get_estimated_load_progress (web_view);

  gtk_widget_set_visible (progress_bar, progress != 1.0);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress_bar), progress);
}

static void
notify_title_cb (GObject    *object,
                 GParamSpec *pspec,
                 gpointer    user_data)
{
  GtkWindow *dialog = user_data;
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);

  gtk_window_set_title (dialog, webkit_web_view_get_title (web_view));
}

static void
open_in_default_browser_cb (GtkWidget *button,
                            gpointer   user_data)
{
  g_autoptr (GtkUriLauncher) uri_launcher = NULL;
  WebKitWebView *view = WEBKIT_WEB_VIEW (user_data);

  uri_launcher = gtk_uri_launcher_new (webkit_web_view_get_uri (view));
  gtk_uri_launcher_launch (uri_launcher,
                           GTK_WINDOW (gtk_widget_get_root (button)),
                           NULL, NULL, NULL);
}

static GtkWidget *
create_webview (void)
{
  GtkWidget *view;
  WebKitSettings *view_settings;

  view = webkit_web_view_new ();
  view_settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (view));

  webkit_settings_set_enable_page_cache (view_settings, FALSE);
  webkit_settings_set_enable_webrtc (view_settings, FALSE);
  webkit_settings_set_enable_html5_local_storage (view_settings, FALSE);
  webkit_settings_set_enable_html5_database (view_settings, FALSE);
  webkit_settings_set_enable_developer_extras (view_settings, FALSE);
  webkit_settings_set_enable_fullscreen (view_settings, FALSE);

  return webkit_web_view_new ();
}

gboolean
gis_activate_link (GtkLabel    *label,
                   const gchar *uri,
                   GtkWidget   *any_widget)
{
  GNetworkMonitor *monitor;
  GtkWidget *headerbar;
  GtkWidget *dialog;
  GtkWidget *overlay;
  GtkWidget *view;
  GtkWidget *progress_bar;

  monitor = g_network_monitor_get_default ();
  if (!g_str_has_prefix (uri, "file:") &&
      !g_network_monitor_get_network_available (monitor))
    {
      gis_show_qr_code_dialog (uri, any_widget, label);
      return TRUE;
    }

  headerbar = gtk_header_bar_new ();
  gtk_header_bar_set_show_title_buttons (GTK_HEADER_BAR (headerbar), TRUE);

  dialog = g_object_new (GTK_TYPE_WINDOW,
                         "destroy-with-parent", TRUE,
                         "transient-for", gtk_widget_get_root (any_widget),
                         "titlebar", headerbar,
                         "title", "", /* use empty title until it can be filled, instead of briefly flashing the default title */
                         "modal", TRUE,
                         "default-width", 800,
                         "default-height", 600,
                         NULL);

  overlay = gtk_overlay_new ();
  gtk_window_set_child (GTK_WINDOW (dialog), overlay);

  progress_bar = gtk_progress_bar_new ();
  gtk_widget_add_css_class (progress_bar, "osd");
  gtk_widget_set_halign (progress_bar, GTK_ALIGN_FILL);
  gtk_widget_set_valign (progress_bar, GTK_ALIGN_START);
  gtk_overlay_add_overlay (GTK_OVERLAY (overlay), progress_bar);

  view = create_webview ();
  gtk_widget_set_hexpand (view, TRUE);
  gtk_widget_set_vexpand (view, TRUE);
  g_signal_connect_object (view, "notify::estimated-load-progress",
                           G_CALLBACK (notify_progress_cb), progress_bar, 0);
  g_signal_connect_object (view, "notify::title",
                           G_CALLBACK (notify_title_cb), dialog, 0);
  gtk_overlay_set_child (GTK_OVERLAY (overlay), view);

  if (gis_driver_get_mode (gis_driver_get_default ()) != GIS_DRIVER_MODE_NEW_USER)
    {
      GtkWidget *button;
      GtkIconTheme *icon_theme;
      const char *icon_name = "external-link-symbolic";
      const char *label = _("Open in default web browser");

      if (g_str_has_prefix (uri, "file:"))
        label = _("Open in default file handler");

      icon_theme = gtk_icon_theme_get_for_display (gtk_widget_get_display (any_widget));
      if (!gtk_icon_theme_has_icon (icon_theme, icon_name))
        {
          /* This is ugly, but for now we just fallback to something
           * similar in non-yaru
           */
          icon_name = "emblem-symbolic-link";
        }

      button = gtk_button_new_from_icon_name (icon_name);
      gtk_widget_set_tooltip_text (button, label);
      gtk_accessible_update_property (GTK_ACCESSIBLE (button),
                                      GTK_ACCESSIBLE_PROPERTY_LABEL,
                                      label,
                                      -1);

      g_signal_connect_object (button, "clicked",
                               G_CALLBACK (open_in_default_browser_cb),
                               view, G_CONNECT_DEFAULT);

      gtk_header_bar_pack_start (GTK_HEADER_BAR (headerbar), button);
    }

  gtk_window_present (GTK_WINDOW (dialog));

  webkit_web_view_load_uri (WEBKIT_WEB_VIEW (view), uri);

  return TRUE;
}
#else
gboolean
gis_activate_link (GtkLabel    *label,
                   const gchar *uri,
                   GtkWidget   *any_widget)
{
  gis_show_qr_code_dialog (uri, any_widget, label);
  return TRUE;
}
#endif
