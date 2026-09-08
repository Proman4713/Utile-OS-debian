/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2024-2026 Canonical
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
 */

/* eula page */

#define PAGE_ID "eula"
#include "config.h"

#ifndef HAVE_WEBKITGTK
#error "WebKitGTK is required to build the EULA page"
#endif

#include "gis-eula-page.h"

#include <locale.h>
#include <webkit/webkit.h>

struct _GisEulaPage
{
  GisPage parent;

  GtkWidget *web_view;
  GtkWidget *page_stack;
  GtkWidget *loading_spinner;
  GtkWidget *document_missing;
  GCancellable *loading_cancellable;
  gchar *current_locale;
};

G_DEFINE_TYPE (GisEulaPage, gis_eula_page, GIS_TYPE_PAGE)

#define GIS_EULA_DEFAULT_PAGE_PATH "/usr/share/desktop-provision/eula"
#define GIS_EULA_DEFAULT_PAGE_NAME "EULA"

static void
load_pdf_thread (GTask *task,
                 gpointer source_object,
                 gpointer task_data,
                 GCancellable *cancellable)
{
  const char *current_locale = task_data;

  /* Current_locale can be en_US.UTF-8@euro for instance */
  g_auto(GStrv) lang_and_variants = g_get_locale_variants (current_locale);

  /* Iterate through the array of locale strings to attempt loading the PDF. */
  for (size_t i = 0; ; i++)
    {
      const char *locale = lang_and_variants[i];
      g_autofree char *localized = NULL;

      if (g_task_return_error_if_cancelled (task))
        return;

      if (!locale)
        localized = g_strdup (GIS_EULA_DEFAULT_PAGE_NAME ".pdf");
      else
        localized = g_strdup_printf (GIS_EULA_DEFAULT_PAGE_NAME "_%s.pdf", locale);

      g_autoptr(GFile) file =
        g_file_new_build_filename (GIS_EULA_DEFAULT_PAGE_PATH, localized, NULL);

      if (!g_file_query_exists (file, cancellable))
        {
          if (g_task_return_error_if_cancelled (task))
            return;

          g_debug ("EULA PDF not found for locale %s at %s",
                   locale ? locale : "default",
                   g_file_peek_path (file));

          if (!locale)
            break;

          continue;
        }

      g_task_return_pointer (task, g_steal_pointer (&file), g_object_unref);
      return;
    }

  /* If document is loaded, process it */
  g_task_return_new_error_literal (task, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                                   "Failed to load any EULA document");
}

static void
gis_eula_page_constructed (GObject *object)
{
  GisEulaPage *page = GIS_EULA_PAGE (object);
  WebKitSettings *view_settings;
  WebKitUserContentManager *user_content_manager;
  WebKitUserStyleSheet *stylesheet;

  view_settings = webkit_web_view_get_settings (WEBKIT_WEB_VIEW (page->web_view));

  webkit_settings_set_enable_page_cache (view_settings, FALSE);
  webkit_settings_set_enable_webrtc (view_settings, FALSE);
  webkit_settings_set_enable_html5_local_storage (view_settings, FALSE);
  webkit_settings_set_enable_html5_database (view_settings, FALSE);
  webkit_settings_set_enable_developer_extras (view_settings, FALSE);
  webkit_settings_set_enable_fullscreen (view_settings, FALSE);

  user_content_manager = webkit_web_view_get_user_content_manager (WEBKIT_WEB_VIEW (page->web_view));

  /* Hide toolbar buttons that would not be useful in this context */
  stylesheet = webkit_user_style_sheet_new (
    "#downloadButton.toolbarButton { display: none !important; } "
    "#download.toolbarButton { display: none !important; } "
    "#printButton.toolbarButton { display: none !important; } "
    "#print.toolbarButton { display: none !important; } "
    "#secondaryToolbarToggle.toolbarButton { display: none !important; } "
    "#secondaryToolbarToggleButton.toolbarButton { display: none !important; } ",
    WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
    WEBKIT_USER_STYLE_LEVEL_USER,
    NULL, NULL);
  webkit_user_content_manager_add_style_sheet (user_content_manager, stylesheet);
  g_clear_pointer (&stylesheet, webkit_user_style_sheet_unref);

  /* Ensure that no URI is set by default, since we rely on this later on */
  g_assert (webkit_web_view_get_uri (WEBKIT_WEB_VIEW (page->web_view)) == NULL);

  G_OBJECT_CLASS (gis_eula_page_parent_class)->constructed (object);
}

static void
gis_eula_page_finalize (GObject *object)
{
  GisEulaPage *page = GIS_EULA_PAGE (object);

  g_free (page->current_locale);
  g_cancellable_cancel (page->loading_cancellable);
  g_clear_object (&page->loading_cancellable);

  G_OBJECT_CLASS (gis_eula_page_parent_class)->finalize (object);
}

static void
pdf_loaded (GObject *source_object,
            GAsyncResult *res,
            gpointer user_data)
{
  GisEulaPage *eula_page = GIS_EULA_PAGE (source_object);
  g_autoptr (GFile) file = NULL;
  g_autoptr (GError) error = NULL;

  file = g_task_propagate_pointer (G_TASK (res), &error);
  if (error)
    {
      if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Failed loading PDF: %s", error->message);
    }

  if (file)
    {
      gtk_stack_set_visible_child (GTK_STACK (eula_page->page_stack),
                                   GTK_WIDGET (eula_page->web_view));
      g_autofree char *uri = g_file_get_uri (file);

      webkit_web_view_load_uri (WEBKIT_WEB_VIEW (eula_page->web_view), uri);
    }
  else if (webkit_web_view_get_uri (WEBKIT_WEB_VIEW (eula_page->web_view)) != NULL)
    {
      g_warning ("Failed to reload PDF on locale change, keep previous one");
    }
  else
    {
      gtk_stack_set_visible_child (GTK_STACK (eula_page->page_stack),
                                   GTK_WIDGET (eula_page->document_missing));
      g_critical ("No EULA to show!");
    }

  gis_page_set_complete (GIS_PAGE (eula_page), TRUE);
}

static void
gis_eula_page_locale_changed (GisPage *page)
{
  GisEulaPage *eula_page = GIS_EULA_PAGE(page);
  const gchar *language;

  language = gis_driver_get_user_language (GIS_PAGE (page)->driver);

  /* Locale has not changed, do nothing */
  if (!webkit_web_view_get_uri (WEBKIT_WEB_VIEW (eula_page->web_view)) &&
      g_strcmp0 (language, eula_page->current_locale) == 0)
      return;

  g_cancellable_cancel (eula_page->loading_cancellable);
  g_clear_object (&eula_page->loading_cancellable);
  eula_page->loading_cancellable = g_cancellable_new ();

  g_set_str (&eula_page->current_locale, language);

  gtk_stack_set_visible_child (GTK_STACK (eula_page->page_stack),
                               GTK_WIDGET (eula_page->loading_spinner));

  /* Attempt to reload the PDF based on the new locale */
  g_autoptr(GTask) task = g_task_new (page, eula_page->loading_cancellable,
                                      pdf_loaded, NULL);
  g_task_set_task_data (task, g_strdup (eula_page->current_locale), g_free);
  g_task_run_in_thread (task, load_pdf_thread);
}

static void
gis_eula_page_class_init (GisEulaPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-eula-page.ui");
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisEulaPage, web_view);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisEulaPage, page_stack);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisEulaPage, loading_spinner);
  gtk_widget_class_bind_template_child (GTK_WIDGET_CLASS (klass), GisEulaPage, document_missing);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_eula_page_locale_changed;

  object_class->constructed = gis_eula_page_constructed;
  object_class->finalize = gis_eula_page_finalize;
}

static void
gis_eula_page_init (GisEulaPage *page)
{
  /* UI setup if PDF loaded successfully */
  gtk_widget_init_template (GTK_WIDGET (page));
  gis_page_set_needs_accept (GIS_PAGE (page), TRUE);
}

GisPage *
gis_prepare_eula_page (GisDriver *driver)
{
  /* check for the existence of the EULA file, skip the eula
   * page if it is absent.
   */
  if (!g_file_test (GIS_EULA_DEFAULT_PAGE_PATH G_DIR_SEPARATOR_S
                    GIS_EULA_DEFAULT_PAGE_NAME ".pdf",
                    G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR |
                    G_FILE_TEST_IS_SYMLINK))
    {
      g_debug ("No EULA page found, skipping page");
      return NULL;
    }

  return g_object_new (GIS_TYPE_EULA_PAGE,
                       "driver", driver,
                       NULL);
}

