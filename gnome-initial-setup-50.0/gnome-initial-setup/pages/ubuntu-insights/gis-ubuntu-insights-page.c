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

/* Ubuntu insights page {{{1 */

#include "config.h"

#define PAGE_ID "ubuntu-insights"
#define TELEMETRY_LOG_PATH "/var/log/installer/telemetry"

/* Used for allow-list on the server side.
 * The naming originates from the fact that the source metrics originate from
 * the provisioner.
 */
#define APP_SOURCE "ubuntu_desktop_provision"

#include "gis-ubuntu-insights-page.h"
#include "gis-page-header.h"
#include "gis-webkit.h"
#include "ubuntu-insights-resources.h"

#include <gio/gio.h>
#include <glib/gi18n.h>
#include <insights/insights.h>
#include <insights/types.h>

struct _GisUbuntuInsightsPage {
  GisPage parent;

  GtkWidget *insights_switch;
  GtkWidget *header;
  GtkWidget *show_report_link;
  GtkWidget *whoopsie_group;
  GtkWidget *whoopsie_switch;

  AdwDialog *report_dialog;
  GtkWidget *report_stack;
  GtkWidget *report_loading_spinner;
  GtkWidget *report_scroll;
  GtkWidget *report_text_view;

  GDBusProxy *whoopsie;
  GCancellable *cancellable;
  gchar *report;
};

G_DEFINE_TYPE (GisUbuntuInsightsPage, gis_ubuntu_insights_page, GIS_TYPE_PAGE);

struct
{
  const char *source;
  const char *error_message_consent;
  const char *error_message_withdraw;
} static const INSIGHT_SOURCES[] = {
  {
    APP_SOURCE, /* GNOME Initial Setup */
    /* TRANSLATORS: The place-holder is an error message */
    N_("Failed to grant consent for the Initial Setup application: %s"),
    /* TRANSLATORS: The place-holder is an error message */
    N_("Failed to withdraw consent for the Initial Setup application: %s"),
  },
  {
    "linux", /* Platform source used by timed systemd collection */
    /* TRANSLATORS: The place-holder is an error message */
    N_("Failed to grant consent for the system data collector: %s"),
    /* TRANSLATORS: The place-holder is an error message */
    N_("Failed to withdraw consent for the system data collector: %s"),
  },
  {
    "ubuntu_release_upgrader", /* Platform source used by release upgrader collection */
    /* TRANSLATORS: The place-holder is an error message */
    N_("Failed to grant consent for the release upgrader application: %s"),
    /* TRANSLATORS: The place-holder is an error message */
    N_("Failed to withdraw consent for the release upgrader application: %s"),
  },
};

static void
cancel_report (GisUbuntuInsightsPage *page)
{
  g_cancellable_cancel (page->cancellable);
  g_clear_object (&page->cancellable);
  page->cancellable = g_cancellable_new ();
}

static void
on_report_dialog_closed (GisUbuntuInsightsPage *self)
{
  GtkWidget *err_page;

  err_page = gtk_stack_get_child_by_name (GTK_STACK (self->report_stack), "error");
  if (err_page)
    gtk_stack_remove (GTK_STACK (self->report_stack), err_page);

  gtk_text_buffer_set_text (
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (self->report_text_view)), "", 0);

  g_signal_handlers_disconnect_by_func (self->report_dialog,
                                        G_CALLBACK (on_report_dialog_closed),
                                        self);

  cancel_report (self);
}

static void
page_shown (GisPage *page)
{
  g_print ("Insights page shown: %p, type: %s\n", page, g_type_name_from_instance ((GTypeInstance *)page));
  GisUbuntuInsightsPage *self = GIS_UBUNTU_INSIGHTS_PAGE (page);
  gtk_widget_grab_focus (self->header);
}

typedef enum {
  ACCOUNT_TYPE_STANDARD      = 0,
  ACCOUNT_TYPE_ADMINISTRATOR = 1,
} AccountType;

static char *
get_user_object_path (GDBusConnection *connection,
                      uid_t            uid,
                      GCancellable    *cancellable,
                      GError         **error)
{
  g_autofree char *path = NULL;
  g_autoptr(GVariant) result = NULL;

  result = g_dbus_connection_call_sync (connection,
                                        "org.freedesktop.Accounts",
                                        "/org/freedesktop/Accounts",
                                        "org.freedesktop.Accounts",
                                        "FindUserById",
                                        g_variant_new ("(x)", (gint64) uid),
                                        G_VARIANT_TYPE ("(o)"),
                                        G_DBUS_CALL_FLAGS_NONE, -1,
                                        cancellable, error);

  if (!result)
    return NULL;

  g_variant_get (result, "(o)", &path);

  return g_steal_pointer (&path);
}

gboolean
user_is_admin (GCancellable  *cancellable,
               GError       **error)
{
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GDBusProxy) proxy = NULL;
  g_autoptr(GVariant) val = NULL;
  g_autofree char *object_path = NULL;

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
  if (!connection)
    return FALSE;

  object_path = get_user_object_path (connection, getuid (), cancellable, error);
  if (!object_path)
    return FALSE;

  proxy = g_dbus_proxy_new_sync (connection,
                                 G_DBUS_PROXY_FLAGS_NONE, NULL,
                                 "org.freedesktop.Accounts",
                                 object_path,
                                 "org.freedesktop.Accounts.User",
                                 cancellable, error);

  if (!proxy)
    return FALSE;

  val = g_dbus_proxy_get_cached_property (proxy, "AccountType");
  if (!val)
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                   "Failed to get AccountType property");
      return FALSE;
    }

  return (AccountType) g_variant_get_int32 (val) == ACCOUNT_TYPE_ADMINISTRATOR;
}

static void
whoopsie_properties_changed (GisUbuntuInsightsPage *page)
{
  g_autofree char *name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (page->whoopsie));
  g_autoptr (GVariant) auto_report = NULL;

  /* Bail out if dbus service has gone away */
  if (G_UNLIKELY (!name_owner))
    return;

  auto_report = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (page->whoopsie),
                                                  "AutomaticallyReportCrashes");
  gboolean enabled = g_variant_get_boolean (auto_report);
  adw_switch_row_set_active (ADW_SWITCH_ROW (page->whoopsie_switch), enabled);
}

static void
whoopsie_bus_owner_changed (GDBusProxy *proxy)
{
  g_autofree char *name_owner = g_dbus_proxy_get_name_owner (proxy);

  if (!name_owner)
    {
      /* Trigger the service to start again, as it has just been stopped by a timeout
       * don't bother on waiting for the call results.
       */
      g_dbus_proxy_call (proxy, "GetIdentifier", NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
    }
}

static void
on_whoopsie_preferences_proxy_ready (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  GisUbuntuInsightsPage *page = GIS_UBUNTU_INSIGHTS_PAGE (user_data);
  g_autoptr(GError) error = NULL;

  page->whoopsie = g_dbus_proxy_new_for_bus_finish (res, &error);
  if (!page->whoopsie)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed to create WhoopsiePreferences proxy: %s",
                 error->message);
      return;
    }

  gtk_widget_set_visible (page->whoopsie_group, TRUE);
  whoopsie_properties_changed (page);

  g_signal_connect_object (page->whoopsie, "g-properties-changed",
                           G_CALLBACK (whoopsie_properties_changed),
                           page, G_CONNECT_SWAPPED);

  g_signal_connect_object (page->whoopsie, "notify::g-name-owner",
                           G_CALLBACK (whoopsie_bus_owner_changed),
                           page, 0);
}

static void
whoopsie_check_thread (GTask        *task,
                       gpointer      source_object,
                       gpointer      task_data,
                       GCancellable *cancellable)
{
  g_autoptr(GError) error = NULL;
  gboolean is_admin = user_is_admin (cancellable, &error);

  if (error)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  g_task_return_boolean (task, is_admin);
}

static void
on_whoopsie_availability_check_done (GObject      *source_object,
                                     GAsyncResult *res,
                                     gpointer      task_data)
{
  GisUbuntuInsightsPage *page = GIS_UBUNTU_INSIGHTS_PAGE (source_object);
  g_autoptr(GError) error = NULL;
  gboolean is_admin;

  gis_page_set_complete (GIS_PAGE (page), TRUE);

  is_admin = g_task_propagate_boolean (G_TASK (res), &error);
  if (error)
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Error checking admin status: %s", error->message);

      return;
    }

  if (!is_admin)
    {
      g_message ("Not an administrator account, skipping report collection");
      return;
    }

  g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM, G_DBUS_PROXY_FLAGS_NONE, NULL,
                            "com.ubuntu.WhoopsiePreferences",
                            "/com/ubuntu/WhoopsiePreferences",
                            "com.ubuntu.WhoopsiePreferences",
                            page->cancellable,
                            on_whoopsie_preferences_proxy_ready,
                            page);
}

static void
gis_ubuntu_insights_page_constructed (GObject *object)
{
  GisUbuntuInsightsPage *page = GIS_UBUNTU_INSIGHTS_PAGE (object);
  g_autoptr(GTask) task = NULL;

  G_OBJECT_CLASS (gis_ubuntu_insights_page_parent_class)->constructed (object);

  page->cancellable = g_cancellable_new ();

  task = g_task_new (page, page->cancellable, on_whoopsie_availability_check_done, page);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_run_in_thread (task, whoopsie_check_thread);
}

static void
gis_ubuntu_insights_page_finalize (GObject *object)
{
  GisUbuntuInsightsPage *page = GIS_UBUNTU_INSIGHTS_PAGE (object);

  g_cancellable_cancel (page->cancellable);
  g_clear_object (&page->cancellable);
  g_clear_object (&page->whoopsie);

  g_free (page->report);

  G_OBJECT_CLASS (gis_ubuntu_insights_page_parent_class)->finalize (object);
}

/**
 * compile_insights_report:
 * @error: (out) (optional): Return location for an error, or NULL
 *
 * Helper function to compile an insights report.
 * If compilation fails using %TELEMETRY_LOG_PATH, it retries without it.
 *
 * Returns: (transfer full): The generated report, or NULL on error.
 */
static char *
compile_insights_report (gboolean   include_telemetry,
                         GError   **error)
{
  g_autofree char *error_msg = NULL;
  g_autofree char *telemetry_error_msg = NULL;
  g_autofree char *report = NULL;
  insights_compile_flags flags = {0};

  if (include_telemetry && g_file_test (TELEMETRY_LOG_PATH, G_FILE_TEST_EXISTS))
    flags.source_metrics_path = TELEMETRY_LOG_PATH;

  error_msg = insights_compile (NULL, &flags, &report);
  if (error_msg != NULL && flags.source_metrics_path != NULL)
    {
      g_warning ("Failed to compile with telemetry log, "
                 "retrying without source metrics: %s",
                 error_msg);

      telemetry_error_msg = g_steal_pointer (&error_msg);
      flags.source_metrics_path = NULL;

      error_msg = insights_compile (NULL, &flags, &report);
    }

  if (error_msg != NULL)
    {
      if (telemetry_error_msg != NULL)
        {
          /* TRANSLATORS: Both placeholders are error messages */
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       _("With telemetry log: %s\n"
                         "Without telemetry log: %s"),
                       telemetry_error_msg, error_msg);
        }
      else
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, error_msg);
        }
      return NULL;
    }

  return g_steal_pointer(&report);
}

static void
on_collect_report_thread (GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
  g_autoptr (GError) error = NULL;
  g_autofree char *report = NULL;
  gboolean use_telemetry;

  use_telemetry = GPOINTER_TO_INT (task_data);
  report = compile_insights_report (use_telemetry, &error);

  if (!report)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               _("Failed to collect telemetry report: %s"),
                               error->message);
      return;
    }

  g_task_return_pointer (task, g_steal_pointer (&report), g_free);
}

static void
on_show_report_callback (GObject      *source_object,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  GisUbuntuInsightsPage *page = user_data;
  GTask *task = G_TASK (res);
  g_autoptr(GError) error = NULL;
  g_autofree char *report = NULL;

  report = g_task_propagate_pointer (task, &error);

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return;

  if (error != NULL)
    {
      GtkWidget *err_label;

      err_label = g_object_new (GTK_TYPE_LABEL,
                                "label", error->message,
                                "wrap", TRUE,
                                "justify", GTK_JUSTIFY_CENTER,
                                "margin-top", 12,
                                "margin-bottom", 12,
                                "margin-start", 12,
                                "margin-end", 12,
                                NULL);

      gtk_stack_add_named (GTK_STACK (page->report_stack), err_label, "error");
      gtk_stack_set_visible_child (GTK_STACK (page->report_stack), err_label);
      return;
    }

  page->report = g_steal_pointer (&report);

  gtk_text_buffer_set_text (
    gtk_text_view_get_buffer (GTK_TEXT_VIEW (page->report_text_view)),
    page->report, -1);

  gtk_stack_set_visible_child (GTK_STACK (page->report_stack),
                               page->report_scroll);
}

static void
show_report (GisUbuntuInsightsPage *page)
{
  g_autoptr(GTask) task = NULL;
  gboolean use_telemetry_path;

  gtk_stack_set_visible_child (GTK_STACK (page->report_stack), page->report_loading_spinner);

  g_signal_connect_object (page->report_dialog, "closed",
                           G_CALLBACK (on_report_dialog_closed),
                           page, G_CONNECT_SWAPPED);

  adw_dialog_present (page->report_dialog,
                      GTK_WIDGET (gtk_widget_get_root (GTK_WIDGET (page))));

  task = g_task_new (page, page->cancellable, on_show_report_callback, page);

  if (page->report)
    {
      g_task_return_pointer (task, g_steal_pointer (&page->report), g_free);
      return;
    }

  use_telemetry_path = (gis_driver_get_mode (GIS_PAGE (page)->driver) != GIS_DRIVER_MODE_UPGRADE);
  g_task_set_task_data (task, GINT_TO_POINTER (use_telemetry_path), NULL);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_run_in_thread (task, on_collect_report_thread);
}

static gboolean
on_show_report_activated (GisUbuntuInsightsPage *self,
                          GtkLinkButton         *link_button)
{
  show_report (self);
  return TRUE;
}

typedef struct {
  gboolean consent;
  gboolean collect;
  gchar *report;

  gboolean whoopsie_reporting;
  GDBusProxy *whoopsie;
} InsightsApplyThreadData;

static void
ubuntu_insights_apply_thread_data_free (InsightsApplyThreadData *data)
{
  g_clear_object (&data->whoopsie);
  g_free (data->report);
  g_free (data);
}

static void
on_ubuntu_insights_apply_thread (GTask        *task,
                                 gpointer      source_object,
                                 gpointer      task_data,
                                 GCancellable *cancellable)
{
  InsightsApplyThreadData *data = task_data;
  g_autofree char *error_msg = NULL;

  if (data->whoopsie)
    {
      g_dbus_proxy_call (data->whoopsie, "SetReportCrashes",
                         g_variant_new ("(b)", data->whoopsie_reporting),
                         G_DBUS_CALL_FLAGS_NONE, -1, cancellable, NULL, NULL);

      g_dbus_proxy_call (data->whoopsie, "SetAutomaticallyReportCrashes",
                         g_variant_new ("(b)", data->whoopsie_reporting),
                         G_DBUS_CALL_FLAGS_NONE, -1, cancellable, NULL, NULL);
    }

  for (size_t i = 0; i < G_N_ELEMENTS (INSIGHT_SOURCES); i++) {
    error_msg = insights_set_consent_state (NULL, INSIGHT_SOURCES[i].source,
                                            data->consent);

    if (error_msg != NULL)
      {
        const char *consent_error_msg;

        if (data->consent)
          consent_error_msg = INSIGHT_SOURCES[i].error_message_consent;
        else
          consent_error_msg = INSIGHT_SOURCES[i].error_message_withdraw;

        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 _(consent_error_msg), error_msg);
        return;
      }
  }

  if (!data->collect)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  /* Write report */
  if (!data->report)
    {
      g_autoptr (GError) error = NULL;

      data->report = compile_insights_report (TRUE, &error);
      if (error)
        {
          g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                   _("Failed to generate report: %s"),
                                   error->message);
          return;
        }
    }

  g_assert (data->report);

  /* We only write, and allow the systemd service/timer to do the uploading */
  insights_write_flags flags = {0};
  flags.period = G_MAXINT32; /* Ensure uniqueness */
  flags.force = TRUE; /* Overwrite existing reports within period */

  error_msg = insights_write (NULL, APP_SOURCE,
                              data->report, &flags);
  if (error_msg != NULL)
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               _("Failed to write report: %s"), error_msg);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

static void
on_ubuntu_insights_apply_callback (GObject     *source_object,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  GTask *task = G_TASK (res);
  g_autoptr (GError) error = NULL;

  if (!g_task_propagate_boolean (task, &error))
    g_warning ("%s", error->message);

  gis_page_apply_complete (GIS_PAGE (source_object), TRUE);
}

/* Set consent and if not in upgrade mode, retrieve and write the report to disk.
 * Ubuntu Insights will take care of sending it via a timed Systemd service.
 * If report compile or write operations fails, we still attempt to
 * set consent, and allow the user to continue on.
 */
static gboolean
gis_ubuntu_insights_page_apply (GisPage *gis_page, GCancellable *cancellable)
{
  GisUbuntuInsightsPage *page = GIS_UBUNTU_INSIGHTS_PAGE (gis_page);
  g_autoptr(GTask) task = NULL;
  InsightsApplyThreadData *data;

  data = g_new0 (InsightsApplyThreadData, 1);
  data->consent = adw_switch_row_get_active (ADW_SWITCH_ROW (page->insights_switch));
  data->collect = gis_driver_get_mode (gis_page->driver) != GIS_DRIVER_MODE_UPGRADE;
  data->report = g_steal_pointer (&page->report);

  data->whoopsie_reporting = adw_switch_row_get_active (ADW_SWITCH_ROW (page->whoopsie_switch));
  g_set_object (&data->whoopsie, page->whoopsie);

  task = g_task_new (page, cancellable, on_ubuntu_insights_apply_callback, NULL);
  g_task_set_task_data (task, g_steal_pointer (&data),
                        (GDestroyNotify) ubuntu_insights_apply_thread_data_free);

  g_task_run_in_thread (task, on_ubuntu_insights_apply_thread);

  return TRUE;
}

static void
gis_ubuntu_insights_page_locale_changed (GisPage *gis_page)
{
  GisUbuntuInsightsPage *page = GIS_UBUNTU_INSIGHTS_PAGE (gis_page);

  if (gis_driver_get_mode (gis_page->driver) != GIS_DRIVER_MODE_UPGRADE)
    {
      gtk_button_set_label (GTK_BUTTON (page->show_report_link),
                            _("Show First Report"));
    }
}

static gboolean
on_link_activated (GisUbuntuInsightsPage *self,
                   GtkLinkButton         *link_button)
{
  return gis_activate_link (NULL,
                            gtk_link_button_get_uri (link_button),
                            GTK_WIDGET (self));
}

static void
gis_ubuntu_insights_page_class_init (GisUbuntuInsightsPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntu-insights-page.ui");
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, header);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, show_report_link);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, insights_switch);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, whoopsie_group);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, whoopsie_switch);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, report_dialog);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, report_stack);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, report_loading_spinner);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, report_scroll);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisUbuntuInsightsPage, report_text_view);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_link_activated);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_show_report_activated);

  page_class->page_id = PAGE_ID;
  page_class->apply = gis_ubuntu_insights_page_apply;
  page_class->locale_changed = gis_ubuntu_insights_page_locale_changed;
  page_class->shown = page_shown;
  object_class->constructed = gis_ubuntu_insights_page_constructed;
  object_class->finalize = gis_ubuntu_insights_page_finalize;

  gis_add_style_from_resource ("/org/gnome/initial-setup/gis-ubuntu-insights-page.css");
}

insights_consent_state
insight_needs_consent (void)
{
  gboolean all_granted = TRUE;

  for (size_t i = 0; i < G_N_ELEMENTS (INSIGHT_SOURCES); i++)
    {
      const char *source = INSIGHT_SOURCES[i].source;
      insights_consent_state state = insights_get_consent_state (NULL, source);

      if (state == INSIGHTS_CONSENT_UNKNOWN)
        return INSIGHTS_CONSENT_UNKNOWN;

      if (state != INSIGHTS_CONSENT_TRUE)
        all_granted = FALSE;
    }

  return all_granted ? INSIGHTS_CONSENT_TRUE : INSIGHTS_CONSENT_FALSE;
}

static gboolean
whoopsie_is_available (void)
{
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GVariant) result = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree const char **names = NULL;

  if (!user_is_admin (NULL, &error))
    {
      if (error)
        g_warning ("Failed to check admin status: %s", error->message);
      return FALSE;
    }

  connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (!connection)
    {
      g_warning ("Failed to connect to system bus: %s", error->message);
      return FALSE;
    }

  result = g_dbus_connection_call_sync (connection,
                                        "org.freedesktop.DBus",
                                        "/org/freedesktop/DBus",
                                        "org.freedesktop.DBus",
                                        "ListActivatableNames",
                                        NULL,
                                        G_VARIANT_TYPE ("(as)"),
                                        G_DBUS_CALL_FLAGS_NONE, -1,
                                        NULL, &error);
  if (!result)
    {
      g_warning ("Failed to list activatable D-Bus names: %s", error->message);
      return FALSE;
    }

  g_variant_get (result, "(^a&s)", &names);

  return g_strv_contains (names, "com.ubuntu.WhoopsiePreferences");
}

static void
gis_ubuntu_insights_page_init (GisUbuntuInsightsPage *page)
{
  g_resources_register (ubuntu_insights_get_resource ());
  g_type_ensure (GIS_TYPE_PAGE_HEADER);

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_ubuntu_insights_page (GisDriver *driver)
{
  GisUbuntuInsightsPage *page;
  insights_consent_state consent;

  consent = insight_needs_consent ();

  if (gis_driver_get_mode (driver) == GIS_DRIVER_MODE_UPGRADE &&
      consent != INSIGHTS_CONSENT_UNKNOWN &&
      !g_getenv ("GIS_SHOW_ALL_PAGES") &&
      !whoopsie_is_available ())
    {
      g_message ("Ubuntu Insights consent has already been set and whoopsie is not available. "
                 "Skipping Ubuntu Insights page.");
      return NULL;
    }

  page = g_object_new (GIS_TYPE_UBUNTU_INSIGHTS_PAGE,
                       "driver", driver,
                       NULL);
  adw_switch_row_set_active (ADW_SWITCH_ROW (page->insights_switch),
                             consent == INSIGHTS_CONSENT_TRUE);

  return GIS_PAGE (page);
}
