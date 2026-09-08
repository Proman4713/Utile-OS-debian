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
 */

/* Summary page {{{1 */

#include "config.h"

#define PAGE_ID "summary"

#include <locale.h>

#define GNOME_SYSTEM_LOCALE_DIR "org.gnome.system.locale"
#define REGION_KEY "region"

#include "summary-resources.h"
#include "gis-summary-page.h"

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <polkit/polkit.h>
#include <stdlib.h>
#include <errno.h>

#include <act/act-user-manager.h>

#define SERVICE_NAME "gdm-password"

struct _GisSummaryPagePrivate {
  GtkWidget *start_button;
  GtkWidget *start_box;
  GtkWidget *spinner;
  GtkWidget *cancel_button;
  GtkWidget *status_stack;

  GPermission *permission;
  GDBusProxy *localed;

  AdwStatusPage *status_page;

  ActUser *user_account;
  const gchar *user_password;

  GCancellable *cancellable;
};
typedef struct _GisSummaryPagePrivate GisSummaryPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GisSummaryPage, gis_summary_page, GIS_TYPE_PAGE);

static void
set_localed_locale (GisSummaryPage *self)
{
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (self);
  g_autoptr (GVariantBuilder) b = NULL;
  g_autofree gchar *s = NULL;
  GisDriver *driver;

  driver = GIS_PAGE (self)->driver;
  const char *locale = gis_driver_get_user_language(driver);

  b = g_variant_builder_new (G_VARIANT_TYPE ("as"));
  s = g_strconcat ("LANG=", locale, NULL);
  g_variant_builder_add (b, "s", s);

  g_dbus_proxy_call (priv->localed,
                     "SetLocale",
                     g_variant_new ("(asb)", b, TRUE),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1, NULL, NULL, NULL);

  act_user_set_language (priv->user_account, locale);
}

static void
change_locale_permission_acquired (GObject      *source,
                                   GAsyncResult *res,
                                   gpointer      data)
{
  GisSummaryPage *page = GIS_SUMMARY_PAGE(data);
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  GError *error = NULL;
  gboolean allowed;

  allowed = g_permission_acquire_finish (priv->permission, res, &error);
  if (error) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_message ("Failed to acquire permission: %s", error->message);
      g_error_free (error);
      return;
  }

  if (allowed)
    set_localed_locale (page);
}

static void
apply_system_locale_changes (GisSummaryPage  *page)
{
  GisDriver *driver;

  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  driver = GIS_PAGE (page)->driver;

  if (gis_driver_get_mode (driver) == GIS_DRIVER_MODE_NEW_USER) {
      if (g_permission_get_allowed (priv->permission)) {
          set_localed_locale (page);
      }
      else if (g_permission_get_can_acquire (priv->permission)) {
          g_permission_acquire_async (priv->permission,
                                      NULL,
                                      change_locale_permission_acquired,
                                      page);
      }
  }
}

static void
request_info_query (GisSummaryPage  *page,
                    GdmUserVerifier *user_verifier,
                    const char      *question,
                    gboolean         is_secret)
{
  /* TODO: pop up modal dialog */
  g_debug ("user verifier asks%s question: %s",
           is_secret ? " secret" : "",
           question);
}

static void
on_info (GdmUserVerifier *user_verifier,
         const char      *service_name,
         const char      *info,
         GisSummaryPage  *page)
{
  g_debug ("PAM module info: %s", info);
}

static void
on_problem (GdmUserVerifier *user_verifier,
            const char      *service_name,
            const char      *problem,
            GisSummaryPage  *page)
{
  g_warning ("PAM module error: %s", problem);
}

static void
on_info_query (GdmUserVerifier *user_verifier,
               const char      *service_name,
               const char      *question,
               GisSummaryPage  *page)
{
  request_info_query (page, user_verifier, question, FALSE);
}

static void
on_secret_info_query (GdmUserVerifier *user_verifier,
                      const char      *service_name,
                      const char      *question,
                      GisSummaryPage  *page)
{
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  gboolean should_send_password = priv->user_password != NULL;

  g_debug ("PAM module secret info query: %s", question);
  if (should_send_password) {
    g_debug ("sending password\n");
    gdm_user_verifier_call_answer_query (user_verifier,
                                         service_name,
                                         priv->user_password,
                                         NULL, NULL, NULL);
    priv->user_password = NULL;
  } else {
    request_info_query (page, user_verifier, question, TRUE);
  }
}

static void
on_session_opened (GdmGreeter     *greeter,
                   const char     *service_name,
                   GisSummaryPage *page)
{
  gdm_greeter_call_start_session_when_ready_sync (greeter, service_name,
                                                  TRUE, NULL, NULL);
}

static void
add_uid_file (uid_t uid)
{
  gchar *gis_uid_path;
  gchar *uid_str;
  g_autoptr(GError) error = NULL;

  gis_uid_path = g_build_filename (g_get_home_dir (),
                                   "gnome-initial-setup-uid",
                                   NULL);
  uid_str = g_strdup_printf ("%u", uid);

  if (!g_file_set_contents (gis_uid_path, uid_str, -1, &error))
      g_warning ("Unable to create %s: %s", gis_uid_path, error->message);

  g_free (uid_str);
  g_free (gis_uid_path);
}

static void
log_user_in (GisSummaryPage *page)
{
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  g_autoptr(GError) error = NULL;
  GdmGreeter *greeter = NULL;
  GdmUserVerifier *user_verifier = NULL;

  if (!gis_driver_get_gdm_objects (GIS_PAGE (page)->driver,
                                   &greeter, &user_verifier)) {
    g_warning ("No GDM connection; not initiating login");
    return;
  }

  g_signal_connect (user_verifier, "info",
                    G_CALLBACK (on_info), page);
  g_signal_connect (user_verifier, "problem",
                    G_CALLBACK (on_problem), page);
  g_signal_connect (user_verifier, "info-query",
                    G_CALLBACK (on_info_query), page);
  g_signal_connect (user_verifier, "secret-info-query",
                    G_CALLBACK (on_secret_info_query), page);

  g_signal_connect (greeter, "session-opened",
                    G_CALLBACK (on_session_opened), page);

  /* We are in NEW_USER mode and we want to make it possible for third
   * parties to find out which user ID we created.
   */
  add_uid_file (act_user_get_uid (priv->user_account));

  gdm_user_verifier_call_begin_verification_for_user_sync (user_verifier,
                                                           SERVICE_NAME,
                                                           act_user_get_user_name (priv->user_account),
                                                           NULL, &error);

  if (error != NULL)
    g_warning ("Could not begin verification: %s", error->message);
}

static void
done_cb (GtkButton *button, GisSummaryPage *page)
{
  gis_ensure_stamp_files (GIS_PAGE (page)->driver);

  switch (gis_driver_get_mode (GIS_PAGE (page)->driver))
    {
    case GIS_DRIVER_MODE_NEW_USER:
      gis_driver_hide_window (GIS_PAGE (page)->driver);
      log_user_in (page);
      break;
    case GIS_DRIVER_MODE_EXISTING_USER:
    case GIS_DRIVER_MODE_UPGRADE:
      g_application_quit (G_APPLICATION (GIS_PAGE (page)->driver));
    default:
      break;
    }
}

static GPtrArray *
get_lang_support_packages_for_locale (const gchar   *locale,
                                      GCancellable  *cancellable,
                                      GError       **error)
{
  g_autoptr (GSubprocess) subprocess = NULL;
  g_autofree gchar *stdout_buf = NULL;
  g_autofree gchar *stderr_buf = NULL;

  subprocess = g_subprocess_new (G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                 G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                 error,
                                 "/usr/bin/check-language-support",
                                 "-l",
                                 locale,
                                 NULL);

  if (!subprocess)
          return NULL;

  if (!g_subprocess_communicate_utf8 (subprocess, NULL, cancellable,
                                      &stdout_buf, &stderr_buf, error)) {
    g_prefix_error_literal (error, "Failed to communicate with check-language-support: ");
    return NULL;
  }



  if (!g_subprocess_wait_check (subprocess, cancellable, error)) {
    g_prefix_error_literal (error, "check-language-support returned an error: ");
    return NULL;
  }

  if (stderr_buf && *stderr_buf != '\0')
    g_warning ("check-language-support stderr: %s", stderr_buf);

  if (!stdout_buf || *stdout_buf == '\0')
    return g_ptr_array_new_with_free_func (g_free);

  g_auto (GStrv) packages_split = g_strsplit (g_strstrip (stdout_buf), " ", -1);
  return g_ptr_array_new_take_null_terminated ((gpointer *) g_steal_pointer (&packages_split), g_free);
}

static gboolean
install_language_support (const gchar   *locale,
                          GCancellable  *cancellable,
                          GError       **error)
{
  g_autoptr (GPtrArray) packages = NULL;
  g_autofree gchar *locale_for_install = NULL;

  char *suffix = strchr (locale, '.');
  if (suffix)
    locale_for_install = g_strndup (locale, suffix - locale);
  else
    locale_for_install = g_strdup (locale);

  packages = get_lang_support_packages_for_locale (locale_for_install, cancellable, error);
  if (!packages)
    return FALSE;

  if (packages->len == 0) {
    g_message ("No packages to install for locale: %s", locale_for_install);
    return TRUE;
  }

  if (!apt_get_install (packages, cancellable, error)) {
    g_prefix_error_literal (error, "Error installing language support: ");
    return FALSE;
  }

  return TRUE;
}

static gboolean
check_network_connection (void)
{
    g_autoptr (GNetworkMonitor) monitor = g_network_monitor_get_default ();
    return g_network_monitor_get_network_available (monitor);
}

static void
installation_task_async (GTask        *task,
                         gpointer      source_object,
                         gpointer      task_data,
                         GCancellable *cancellable)
{
  const gchar *locale = task_data;
  g_autoptr (GError) error = NULL;

  if (g_cancellable_set_error_if_cancelled (cancellable, &error)) {
    g_task_return_error (task, g_steal_pointer(&error));
    return;
  }

  if (!install_language_support (locale, cancellable, &error)) {
    g_task_return_error (task, g_steal_pointer (&error));
    return;
  }

  g_task_return_boolean (task, TRUE);
}

static void
update_status_stack (GisSummaryPage    *page)
{
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  gtk_stack_set_visible_child (GTK_STACK (priv->status_stack), priv->start_box);
}

static void
update_distro_name (GisSummaryPage *page)
{
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  g_autofree char *name = g_get_os_info (G_OS_INFO_KEY_NAME);
  char *text;

  if (!name)
    name = g_strdup ("GNOME");

  /* Translators: the parameter here is the name of a distribution,
   * like "Fedora" or "Ubuntu". It falls back to "GNOME" if we can't
   * detect any distribution. */
  text = g_strdup_printf (_("_Start Using %s"), name);
  gtk_button_set_label (GTK_BUTTON (priv->start_button), text);
  g_free (text);

  /* Translators: the parameter here is the name of a distribution,
   * like "Fedora" or "Ubuntu". It falls back to "GNOME" if we can't
   * detect any distribution. */
  text = g_strdup_printf (_("%s is ready to be used."), name);
  adw_status_page_set_description (priv->status_page, text);
  g_free (text);
}

static void
installation_task_done (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr (GError) error = NULL;
  GisSummaryPage *page = user_data;
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);

  if (g_task_propagate_boolean (G_TASK (res), &error)) {
    apply_system_locale_changes(page);
    update_status_stack (page);
    update_distro_name (page);
    adw_status_page_set_title (priv->status_page, _("All done!"));
    return;
  }

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
    update_status_stack (page);
    return;
  }

  update_status_stack (page);
  g_warning ("Installation failed: %s", error->message);
}

static void
gis_summary_page_shown (GisPage *page)
{
  GisSummaryPage *summary = GIS_SUMMARY_PAGE (page);
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (summary);
  g_autoptr(GError) local_error = NULL;

  if (!gis_driver_save_data (GIS_PAGE (page)->driver, &local_error))
    {
      g_warning ("Error saving data: %s", local_error->message);

      GtkWindow *parent = GTK_WINDOW (gtk_widget_get_root (GTK_WIDGET (page)));
      GtkWidget *dialog = adw_message_dialog_new (parent,
                                                  _("Setup Failed"),
                                                  local_error->message);
      adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog), "close", _("Close"));
      /* FIXME: Provide some more options for debugging or recovery */

      gtk_window_present (GTK_WINDOW (dialog));
    }

  gis_driver_get_user_permissions (GIS_PAGE (page)->driver,
                                   &priv->user_account,
                                   &priv->user_password);

  /* Skip installation attempt if there is no active connection. */
  if (check_network_connection ()) {
    adw_status_page_set_description (priv->status_page, "");
    adw_status_page_set_title (priv->status_page, _("Almost done"));
    gtk_stack_set_visible_child_name (GTK_STACK(priv->status_stack), "download_box");
    gtk_spinner_start (GTK_SPINNER (priv->spinner));

    g_cancellable_cancel (priv->cancellable);

    g_clear_pointer (&priv->cancellable, g_free);
    priv->cancellable = g_cancellable_new ();

    g_autoptr (GTask) task = g_task_new (page, priv->cancellable,
                                             installation_task_done, page);

    GisDriver *driver = GIS_PAGE(page)->driver;
    const char *locale = gis_driver_get_user_language(driver);
    g_task_set_task_data (task, g_strdup (locale), g_free);
    g_task_set_return_on_cancel (task, TRUE);
    g_task_run_in_thread (task, installation_task_async);
  }
}

static void
status_stack_reset_cb (GtkButton *button, GisSummaryPage *page)
{
    GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private(page);
    g_cancellable_cancel(priv->cancellable);
    update_status_stack(page);
    update_distro_name(page);
    adw_status_page_set_title(priv->status_page, _("All done!"));
}

static void
localed_proxy_ready (GObject      *source,
                     GAsyncResult *res,
                     gpointer      data)
{
  GisSummaryPage *self = data;
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (self);
  GDBusProxy *proxy;
  GError *error = NULL;

  proxy = g_dbus_proxy_new_finish (res, &error);

  if (!proxy) {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_message ("Failed to contact localed: %s", error->message);
      g_error_free (error);
      return;
  }

  priv->localed = proxy;
}

static void
gis_summary_page_constructed (GObject *object)
{
  GisSummaryPage *page = GIS_SUMMARY_PAGE (object);
  GisSummaryPagePrivate *priv = gis_summary_page_get_instance_private (page);
  g_autoptr(GtkCssProvider) css_provider = NULL;
  GDBusConnection *bus;

  G_OBJECT_CLASS (gis_summary_page_parent_class)->constructed (object);

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (css_provider,
    "statuspage.ready-to-go .icon { -gtk-icon-size: 280px; }");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  gis_page_set_hide_navigation (GIS_PAGE (page), TRUE);

  update_distro_name (page);
  g_signal_connect (priv->start_button, "clicked", G_CALLBACK (done_cb), page);
  g_signal_connect (priv->cancel_button, "clicked", G_CALLBACK (status_stack_reset_cb), page);

  priv->permission = polkit_permission_new_sync ("org.freedesktop.locale1.set-locale", NULL, NULL, NULL);
  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);
  g_dbus_proxy_new (bus,
                    G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES,
                    NULL,
                    "org.freedesktop.locale1",
                    "/org/freedesktop/locale1",
                    "org.freedesktop.locale1",
                    priv->cancellable,
                    (GAsyncReadyCallback) localed_proxy_ready,
                    object);
  g_object_unref (bus);

  gis_page_set_complete (GIS_PAGE (page), TRUE);

  gtk_widget_set_visible (GTK_WIDGET (page), TRUE);
}

static void
gis_summary_page_locale_changed (GisPage *page)
{
  gis_page_set_title (page, _("Setup Complete"));
  update_distro_name (GIS_SUMMARY_PAGE (page));
}

static void
gis_summary_page_class_init (GisSummaryPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-summary-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, start_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, start_box);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, status_page);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, spinner);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, cancel_button);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisSummaryPage, status_stack);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_summary_page_locale_changed;
  page_class->shown = gis_summary_page_shown;
  object_class->constructed = gis_summary_page_constructed;
}

static void
gis_summary_page_init (GisSummaryPage *page)
{
  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_summary_page (GisDriver *driver)
{
  return g_object_new (GIS_TYPE_SUMMARY_PAGE,
                       "driver", driver,
                       NULL);
}
