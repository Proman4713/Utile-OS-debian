/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2015-2016 Red Hat
 * Copyright (C) 2015-2017 Endless OS Foundation LLC
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by:
 *     Dan Nicholson <dbn@endlessos.org>
 *     Will Thompson <wjt@endlessos.org>
 */

#include "gis-elevate.h"

static GSubprocess *
elevate_subprocess_new (const gchar * const *args,
                        GError             **error)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  const char *tool = args[0];

  launcher = g_subprocess_launcher_new (G_SUBPROCESS_FLAGS_NONE);

  if (g_strcmp0 (tool, "pkexec") == 0) {
    /* pkexec won't let us run the program if $SHELL isn't in /etc/shells,
     * so remove it from the environment.
     */
    g_subprocess_launcher_unsetenv (launcher, "SHELL");
  }

  g_subprocess_launcher_setenv (launcher, "DEBCONF_NONINTERACTIVE_SEEN", "true", TRUE);
  g_subprocess_launcher_setenv (launcher, "DEBIAN_FRONTEND", "noninteractive", TRUE);

  return g_subprocess_launcher_spawnv (launcher, args, error);
}

static gboolean
elevate (const char  *tool,
         const char  *command,
         const char  *arg1,
         const char  *user,
         GCancellable *cancellable,
         GError     **error)
{
  g_autoptr(GSubprocess) process = NULL;
  const char * const root_argv[] = { tool, command, arg1, NULL };
  const char * const user_argv[] = { tool, "--user", user, command, arg1, NULL };
  const char * const *argv = user == NULL ? root_argv : user_argv;

  process = elevate_subprocess_new (argv, error);

  if (!process) {
    g_prefix_error (error, "Failed to create %s process: ", command);
    return FALSE;
  }

  if (!g_subprocess_wait_check (process, cancellable, error)) {
    g_prefix_error (error, "%s failed: ", command);
    return FALSE;
  }

  return TRUE;
}

gboolean
gis_elevate (const char  *command,
             const char  *arg1,
             const char  *user,
             GCancellable *cancellable,
             GError     **error)
{
  g_autoptr(GError) local_error = NULL;

  if (!elevate ("run0", command, arg1, user, cancellable, &local_error)) {
    /* If run0 is not found, fall back to pkexec */
    if (g_error_matches (local_error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT)) {
      return elevate ("pkexec", command, arg1, user, cancellable, error);
    }
    g_propagate_error (error, g_steal_pointer (&local_error));
    return FALSE;
  }
  return TRUE;
}

gboolean
remove_locales_all (GCancellable *cancellable,
                    GError **error) {
  g_autoptr(GSubprocess) process = NULL;

  const gchar * const argv[] = {
      "pkexec", "/usr/bin/apt-get", "remove", "locales-all", "-y", NULL
  };

  process = elevate_subprocess_new (argv, error);

  if (!process) {
    return FALSE;
  }

  if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
    return FALSE;
  }

  /* The apt-get remove succeeds regardless of if the package is installed, so
   * for the subprocess to error indicates a larger issue.
   *
   */
  if (!g_subprocess_wait_check (process, cancellable, error)) {
    return FALSE;
  }

  return TRUE;
}

gboolean
apt_get_install (GPtrArray     *packages,
                 GCancellable  *cancellable,
                 GError       **error) {
  g_autoptr(GSubprocess) process = NULL;
  static gboolean already_updated = FALSE;

  g_return_val_if_fail (packages != NULL, FALSE);
  g_return_val_if_fail (packages->len != 0, FALSE);

  if (!g_atomic_int_get (&already_updated)) {
    /* Avoid running apt-update again if it has been already completed in a previous iteration */
    if (!elevate ("pkexec", "/usr/bin/apt-get", "update", NULL, cancellable, error))
      return FALSE;

    g_atomic_int_set (&already_updated, TRUE);
  }

  /* Uninstall locales-all so that is does not take up space on the final
   * system.
   * */
  if (!remove_locales_all(cancellable, error)) {
    g_prefix_error_literal (error, "Failed to call apt-get remove locales-all: ");
    return FALSE;
  }

  /* Create space for 8 leading arguments of the install command, the number of
   * packages and a NULL terminator.
   */
  int total = 4 + packages->len + 1;
  g_autoptr (GPtrArray) argv = g_ptr_array_new_null_terminated (total, g_free, TRUE);

  g_ptr_array_add (argv, g_strdup ("pkexec"));
  g_ptr_array_add (argv, g_strdup ("/usr/bin/apt-get"));
  g_ptr_array_add (argv, g_strdup ("install"));
  g_ptr_array_add (argv, g_strdup ("-y"));
  while (packages->len) {
    g_ptr_array_add (argv, g_ptr_array_steal_index_fast (packages, 0));
  }

  process = elevate_subprocess_new ((const gchar * const *) argv->pdata, error);

  if (!process) {
    g_prefix_error_literal (error, "Failed to create apt install process: ");
    return FALSE;
  }

  if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
    g_prefix_error_literal (error, "User canceled subprocess: ");
    return FALSE;
  }

  if (!g_subprocess_wait_check (process, cancellable, error)) {
    g_prefix_error_literal (error, "apt-get install failed: ");
    return FALSE;
  }

  return TRUE;
}
