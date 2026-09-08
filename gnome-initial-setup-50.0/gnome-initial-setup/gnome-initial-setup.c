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

#include "config.h"

#include "gnome-initial-setup.h"

#include <adwaita.h>
#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include "pages/welcome/gis-welcome-page.h"
#include "pages/language/gis-language-page.h"
#include "pages/keyboard/gis-keyboard-page.h"
#include "pages/eula/gis-eula-page.h"
#include "pages/network/gis-network-page.h"
#include "pages/timezone/gis-timezone-page.h"
#include "pages/privacy/gis-privacy-page.h"
#include "pages/software/gis-software-page.h"
#include "pages/account/gis-account-pages.h"
#include "pages/parental-controls/gis-parental-controls-page.h"
#include "pages/password/gis-password-page.h"
#include "pages/summary/gis-summary-page.h"
#include "pages/ubuntu-insights/gis-ubuntu-insights-page.h"
#include "pages/ubuntu-pro/gis-ubuntupro-page.h"
#include "pages/apps/gis-apps-page.h"
#include "pages/appearance/gis-appearance-page.h"

#define VENDOR_PAGES_GROUP "pages"
#define VENDOR_SKIP_KEY "skip"
#define VENDOR_NEW_USER_ONLY_KEY "new_user_only"
#define VENDOR_EXISTING_USER_ONLY_KEY "existing_user_only"

static gboolean is_desktop (const gchar *name);

static gboolean force_existing_user_mode;
static gboolean force_upgrade_user_mode;

static GPtrArray *skipped_pages;

typedef GisPage *(*PreparePage) (GisDriver *driver);

typedef struct {
  const gchar *page_id;
  PreparePage prepare_page_func;
  gboolean new_user_only;
  gboolean upgrade_user;
} PageData;

#define PAGE(name, new_user_only, upgrade_user) \
  { #name, gis_prepare_ ## name ## _page, new_user_only, upgrade_user }

static PageData page_table[] = {
  PAGE (welcome, FALSE, FALSE),
  PAGE (language, FALSE, FALSE),
  PAGE (keyboard, FALSE, FALSE),
  PAGE (eula,     TRUE, FALSE),
  PAGE (network,  FALSE, FALSE),
  PAGE (account,  TRUE, FALSE),
  PAGE (password, TRUE, FALSE),
  PAGE (ubuntu_pro, TRUE, FALSE),
  PAGE (privacy,  FALSE, FALSE),
  PAGE (ubuntu_insights, FALSE, TRUE),
  PAGE (timezone, TRUE, FALSE),
  PAGE (software, TRUE, FALSE),
#ifdef HAVE_PARENTAL_CONTROLS
  PAGE (parental_controls, TRUE, FALSE),
  PAGE (parent_password, TRUE, FALSE),
#endif
  PAGE (summary,  FALSE, FALSE),
  { NULL },
};

static PageData ubuntu_page_table[] = {
  PAGE (welcome,         FALSE, TRUE),
  PAGE (ubuntu_pro,      FALSE, TRUE),
  /* Showing privacy panel since we switched to baeconDB since last LTS,
   * so it's better to ask for consent again.
   * This can be dropped after 26.04.
   */
  PAGE (privacy,         FALSE, TRUE),
  PAGE (ubuntu_insights, FALSE, TRUE),
  PAGE (account,         TRUE, FALSE),
  PAGE (password,        TRUE, FALSE),
  PAGE (appearance,      FALSE, TRUE),
  PAGE (apps,            FALSE, FALSE),
  { NULL },
};

static PageData unity_page_table[] = {
  PAGE (welcome,         FALSE, TRUE),
  /* Showing privacy panel since we switched to baeconDB since last LTS,
   * so it's better to ask for consent again.
   * This can be dropped after 26.04.
   */
  PAGE (privacy,         FALSE, TRUE),
  PAGE (ubuntu_insights, FALSE, TRUE),
  PAGE (account,         TRUE, FALSE),
  PAGE (password,        TRUE, FALSE),
  PAGE (appearance,      FALSE, FALSE),
  PAGE (apps,            FALSE, FALSE),
  { NULL },
};

#undef PAGE

static gboolean
should_skip_page (const gchar  *page_id,
                  gchar       **skip_pages)
{
  guint i = 0;

  /* special case welcome. We only want to show it if language
   * is skipped
   */
  if ((strcmp (page_id, "welcome") == 0) &&
      !is_desktop ("ubuntu") &&
      !is_desktop ("unity"))
    return !should_skip_page ("language", skip_pages);

  /* check through our skip pages list for pages we don't want */
  if (skip_pages) {
    while (skip_pages[i]) {
      if (g_strcmp0 (skip_pages[i], page_id) == 0)
        return TRUE;
      i++;
    }
  }

  return FALSE;
}

static gchar **
strv_append (gchar **a,
             gchar **b)
{
  guint n = g_strv_length (a);
  guint m = g_strv_length (b);

  a = g_renew (gchar *, a, n + m + 1);
  for (guint i = 0; i < m; i++)
    a[n + i] = g_strdup (b[i]);
  a[n + m] = NULL;

  return a;
}

static gchar **
pages_to_skip_from_file (GisDriver *driver,
                         gboolean   is_new_user)
{
  GStrv skip_pages = NULL;
  GStrv additional_skip_pages = NULL;

  /* This code will read the keyfile containing vendor customization options and
   * look for options under the "pages" group, and supports the following keys:
   *   - skip (optional): list of pages to be skipped always
   *   - new_user_only (optional): list of pages to be skipped in existing user mode
   *   - existing_user_only (optional): list of pages to be skipped in new user mode
   *
   * This is how this file might look on a vendor image:
   *
   *   [pages]
   *   skip=timezone
   *   existing_user_only=language;keyboard
   */

  skip_pages = gis_driver_conf_get_string_list (driver, VENDOR_PAGES_GROUP,
                                                VENDOR_SKIP_KEY, NULL);
  additional_skip_pages =
  	gis_driver_conf_get_string_list (driver, VENDOR_PAGES_GROUP,
                                     is_new_user ? VENDOR_EXISTING_USER_ONLY_KEY : VENDOR_NEW_USER_ONLY_KEY,
                                     NULL);

  if (!skip_pages && additional_skip_pages) {
    skip_pages = additional_skip_pages;
  } else if (skip_pages && additional_skip_pages) {
    skip_pages = strv_append (skip_pages, additional_skip_pages);
    g_strfreev (additional_skip_pages);
  }

  return skip_pages;
}

static void
destroy_pages_after (GisAssistant *assistant,
                     GisPage      *page)
{
  GList *pages, *l, *next;

  pages = gis_assistant_get_all_pages (assistant);

  for (l = pages; l != NULL; l = l->next)
    if (l->data == page)
      break;

  l = l->next;
  for (; l != NULL; l = next) {
    next = l->next;
    gis_assistant_remove_page (assistant, l->data);
  }
}

static void
destroy_page (gpointer data)
{
  GtkWidget *assistant;
  GisPage *page;

  page = data;
  assistant = gtk_widget_get_ancestor (GTK_WIDGET (page), GIS_TYPE_ASSISTANT);

  if (assistant)
    gis_assistant_remove_page (GIS_ASSISTANT (assistant), page);
}

static gboolean
is_desktop (const gchar *name)
{
  /* note that, for first user account creation inside gnome-initial-setup user,
   * XDG_CURRENT_DESKTOP is set to "GNOME-Greeter:GNOME"
   */

  const gchar *xdg_current_desktop;
  g_auto(GStrv) tokens = NULL;

  xdg_current_desktop = g_getenv ("XDG_CURRENT_DESKTOP");
  if (xdg_current_desktop == NULL)
    return FALSE;

  tokens = g_strsplit (xdg_current_desktop, ":", -1);
  return g_strv_contains ((const gchar * const*) tokens, name);
}

static void
rebuild_pages_cb (GisDriver *driver)
{
  PageData *page_data;
  GisPage *page;
  GisAssistant *assistant;
  GisPage *current_page;
  gchar **skip_pages;
  gboolean is_new_user, skipped;

  assistant = gis_driver_get_assistant (driver);
  current_page = gis_assistant_get_current_page (assistant);

  if (is_desktop ("ubuntu"))
    page_data = ubuntu_page_table;
  else if (is_desktop ("unity"))
    page_data = unity_page_table;
  else
    page_data = page_table;

  g_ptr_array_free (skipped_pages, TRUE);
  skipped_pages = g_ptr_array_new_with_free_func (destroy_page);

  if (current_page != NULL) {
    destroy_pages_after (assistant, current_page);

    for (; page_data->page_id != NULL; ++page_data)
      if (g_str_equal (page_data->page_id, GIS_PAGE_GET_CLASS (current_page)->page_id))
        break;

    ++page_data;
  }

  is_new_user = (gis_driver_get_mode (driver) == GIS_DRIVER_MODE_NEW_USER);
  gboolean is_upgrade = (gis_driver_get_mode (driver) == GIS_DRIVER_MODE_UPGRADE);
  skip_pages = pages_to_skip_from_file (driver, is_new_user);

  for (; page_data->page_id != NULL; ++page_data) {
    skipped = FALSE;

    if ((page_data->new_user_only && !is_new_user) ||
        (should_skip_page (page_data->page_id, skip_pages)))
      skipped = TRUE;

    if (is_upgrade)
      {
        skipped = TRUE;

        if (page_data->upgrade_user)
          skipped = should_skip_page (page_data->page_id, skip_pages);
      }

    page = page_data->prepare_page_func (driver);
    if (!page)
      continue;

    if (skipped) {
      gis_page_skip (page);
      g_ptr_array_add (skipped_pages, page);
    } else {
      gis_driver_add_page (driver, page);
    }
  }

  g_strfreev (skip_pages);
}

static GisDriverMode
get_mode (void)
{
  if (force_upgrade_user_mode)
    return GIS_DRIVER_MODE_UPGRADE;

  if (force_existing_user_mode)
    return GIS_DRIVER_MODE_EXISTING_USER;
  else
    return GIS_DRIVER_MODE_NEW_USER;
}

static gboolean
initial_setup_disabled_by_anaconda (void)
{
  const gchar *file_name = SYSCONFDIR "/sysconfig/anaconda";
  g_autoptr(GError) error = NULL;
  g_autoptr(GKeyFile) key_file = g_key_file_new ();

  if (!g_key_file_load_from_file (key_file, file_name, G_KEY_FILE_NONE, &error)) {
    if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT) &&
        !g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_NOT_FOUND)) {
      g_warning ("Could not read %s: %s", file_name, error->message);
    }
    return FALSE;
  }

  return g_key_file_get_boolean (key_file, "General", "post_install_tools_disabled", NULL);
}

int
main (int argc, char *argv[])
{
  GisDriver *driver;
  int status;
  GOptionContext *context;
  GisDriverMode mode;

  GOptionEntry entries[] = {
    { "existing-user", 0, 0, G_OPTION_ARG_NONE, &force_existing_user_mode,
      _("Force existing user mode"), NULL },
    { "upgrade-user", 0, 0, G_OPTION_ARG_NONE, &force_upgrade_user_mode,
      _("Force Upgrade user"), NULL },
    { NULL }
  };

  g_unsetenv ("GIO_USE_VFS");

  /* By default, libadwaita reads settings from the Settings portal, which causes
   * the portal to be started, which causes gnome-keyring to be started. This
   * interferes with our attempt below to manually start gnome-keyring and set
   * the login keyring password to a well-known value, which we overwrite with
   * the user's password once they choose one.
   */
  g_setenv ("ADW_DISABLE_PORTAL", "1", TRUE);

  context = g_option_context_new (_("— GNOME initial setup"));
  g_option_context_add_main_entries (context, entries, NULL);

  g_option_context_parse (context, &argc, &argv, NULL);

  bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_message ("Starting gnome-initial-setup");
  if (gis_get_mock_mode ())
    g_message ("Mock mode: changes will not be saved to disk");
  else
    g_message ("Production mode: changes will be saved to disk");

  skipped_pages = g_ptr_array_new_with_free_func (destroy_page);
  mode = get_mode ();

  driver = gis_driver_new (mode);

  /* We only do this in existing-user mode, because if gdm launches us
   * in new-user mode and we just exit, gdm's special g-i-s session
   * never terminates. */
  if (initial_setup_disabled_by_anaconda () &&
      mode == GIS_DRIVER_MODE_EXISTING_USER) {
    gis_ensure_stamp_files (driver);
    exit (EXIT_SUCCESS);
  }

  g_signal_connect (driver, "rebuild-pages", G_CALLBACK (rebuild_pages_cb), NULL);
  status = g_application_run (G_APPLICATION (driver), argc, argv);

  g_ptr_array_free (skipped_pages, TRUE);

  g_object_unref (driver);
  g_option_context_free (context);
  return status;
}

void
gis_ensure_stamp_files (GisDriver *driver)
{
  g_autofree gchar *done_file = NULL;
  g_autoptr(GError) error = NULL;

  done_file = g_build_filename (g_get_user_config_dir (), "gnome-initial-setup-done", NULL);
  if (!g_file_set_contents (done_file, "yes", -1, &error)) {
      g_warning ("Unable to create %s: %s", done_file, error->message);
      g_clear_error (&error);
  }
}

static gboolean
touch_file (GFile   *file,
            GError **error)
{
  g_autoptr (GFile) parent = NULL;
  g_autoptr (GFileOutputStream) stream = NULL;
  g_autoptr (GError) local_error = NULL;

  parent = g_file_get_parent (file);
  g_file_make_directory_with_parents (parent, NULL, &local_error);

  if (local_error && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }
  g_clear_error (&local_error);

  stream = g_file_create (file, G_FILE_CREATE_NONE, NULL, &local_error);

  if (local_error && !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, g_steal_pointer (&local_error));
      return FALSE;
    }

  if (stream)
    g_output_stream_close (G_OUTPUT_STREAM (stream), NULL, NULL);

  return TRUE;
}

void
gis_ensure_upgrade_stamp_files (GisDriver *driver)
{
  const char *done_file_name = NULL;
  g_autoptr (GFile) done_file = NULL;
  g_autoptr (GError) error = NULL;

  done_file_name = "upgrade-" LSB_RELEASE_VERSION_ID "-done";
  done_file = g_file_new_build_filename (g_get_user_config_dir (),
                                         "gnome-initial-setup",
                                         done_file_name, NULL);

  if (!touch_file (done_file, &error))
    g_critical ("Unable to create %s: %s", g_file_peek_path (done_file), error->message);
}

/**
 * gis_get_mock_mode:
 *
 * Gets whether gnome-initial-setup has been built for development, and hence
 * shouldn’t permanently change any system configuration.
 *
 * By default, mock mode is enabled when running in a build environment. This
 * heuristic may be changed in future.
 *
 * Returns: %TRUE if in mock mode, %FALSE otherwise
 */
gboolean
gis_get_mock_mode (void)
{
  return (g_getenv ("UNDER_JHBUILD") != NULL);
}
