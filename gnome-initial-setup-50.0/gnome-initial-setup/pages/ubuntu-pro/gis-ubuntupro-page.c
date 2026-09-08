/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2022 Canonical Ltd.
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

/* Canonical Ubuntu Pro page {{{1 */

#include "config.h"

#define PAGE_ID "UbuntuPro"
#define CHECK_ICON "pro-checkmark"
#define FAIL_ICON "pro-fail"
#define LOGO "/org/gnome/initial-setup/ubuntu-pro.svg"
#define LOGO_DARK "/org/gnome/initial-setup/ubuntu-pro-dark.svg"
#define UBUNTU_ADVANTAGE_STATUS_FILE "/var/lib/ubuntu-advantage/status.json"

#include "gis-page-header.h"
#include "gis-ubuntupro-page.h"
#include "gis-webkit.h"
#include "ubuntupro-resources.h"
#include "utils.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <polkit/polkit.h>
#include <json-glib/json-glib.h>
#include <time.h>
#include <pthread.h>

/* For debugging faster, flip this to be able to skip to 3rd page
 * without needing to attach to Ubuntu Pro first. */
static gboolean SuccessfullyAttached = FALSE;

typedef enum {
  NONE = 0,
  SUCCESS,
  FAIL,
  EXPIRED,
} status_t;

struct _GisUbuntuProPagePrivate
{
  GtkWidget *offer_page;
  GtkWidget *attach_page;
  GtkWidget *services_page;
  GtkWidget *stack;

  enum {
    OFFER_PAGE,
    ATTACH_PAGE,
    SERVICES_PAGE,
  } current_page;
};
typedef struct _GisUbuntuProPagePrivate GisUbuntuProPagePrivate;

struct _GisUbuntuProOfferPagePrivate
{
  GtkWidget *ubuntu_pro_logo;
  GtkWidget *enable_pro_row;
  GtkWidget *skip_pro_select;
  GtkWidget *offline_warning;
  GtkWidget *pro_status_image;
  GtkWidget *security_updates_label;
  GtkWidget *header;
  GtkWidget *learn_more;
  gint       network_cb;
};
typedef struct _GisUbuntuProOfferPagePrivate GisUbuntuProOfferPagePrivate;

struct _GisUbuntuProAttachPagePrivate
{
  GtkWidget *ubuntu_pro_logo;
  GtkWidget *pin_label;
  GtkWidget *token_field;
  GtkWidget *token_status;
  GtkWidget *token_spinner;
  GtkWidget *pin_spinner;
  GtkWidget *token_status_icon;
  GtkWidget *token_hint;
  GtkWidget *pin_hint;
  GtkWidget *pin_status;
  GtkWidget *pin_status_icon;
  GtkWidget *token_radio;
  GtkWidget *magic_radio;
  GtkWidget *header;
  /* This being here is a hack that makes changing the 2nd page's button
   * from Skip to Next once the machine is attached possible. */
  GisUbuntuProPage *main_page;

  gboolean          complete;
  gboolean          magic_token_polling;
  gboolean          attaching;
  gpointer          expired_by;
  pthread_t         poll_id;
  guint             attach_idle;
  enum active_radio_t {
    MAGIC,
    TOKEN,
  } active_radio;
  gchar *contract_token;
  gchar *token;
};
typedef struct _GisUbuntuProAttachPagePrivate GisUbuntuProAttachPagePrivate;

struct _GisUbuntuProServicesPagePrivate
{
  GtkWidget *ubuntu_pro_logo;
  GtkWidget *enabled_services_header;
  GtkWidget *available_services_header;
  GtkWidget *contract_name;
  GtkWidget *checkmark;
  GtkWidget *header;
};
typedef struct _GisUbuntuProServicesPagePrivate GisUbuntuProServicesPagePrivate;

struct _RestJSONResponse
{
  gint64 expires_in;
  gchar *token;
  gchar *code;
  gchar *contract_token;
};

typedef struct _RestJSONResponse RestJSONResponse;
static gboolean magic_parser (gchar *,
                              RestJSONResponse *);
static gboolean gis_ubuntupro_page_apply (GisPage *,
                                          GCancellable *);

G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProPage, gis_ubuntupro_page, GIS_TYPE_PAGE);
G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProOfferPage, gis_ubuntupro_offer_page, ADW_TYPE_BIN);
G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProAttachPage, gis_ubuntupro_attach_page, ADW_TYPE_BIN);
G_DEFINE_TYPE_WITH_PRIVATE (GisUbuntuProServicesPage, gis_ubuntupro_services_page, ADW_TYPE_BIN);

/* The user may go back any number of pages during attachment, so we can't
 * blindly skip to the services list page.
 * Furthermore, after attachment the user can again navigate back to any page
 * and then hit the attach page again.
 */
static void
consider_going_to_next_page (GisUbuntuProAttachPagePrivate *priv)
{
  static gboolean never_called = TRUE;
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (priv->main_page);
  GisUbuntuProPagePrivate *main_priv = gis_ubuntupro_page_get_instance_private (page);

  if (never_called && main_priv->current_page == ATTACH_PAGE)
    {
      gis_page_set_complete (GIS_PAGE (priv->main_page), TRUE);
      gis_page_apply_begin (GIS_PAGE (priv->main_page), NULL, NULL);
    }
  never_called = FALSE;
}

static void
update_attach_page (GisUbuntuProAttachPage *attach_page,
                    status_t                status)
{
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);
  GtkWidget *label, *icon, *spinner;
  g_autofree gchar *hint = NULL;
  g_autofree gchar *subtitle = NULL;
  gboolean ready_to_attach;

  if (priv->active_radio == MAGIC)
    {
      icon = priv->pin_status_icon;
      label = priv->pin_status;
      spinner = priv->pin_spinner;
      ready_to_attach = SuccessfullyAttached ||
                        (priv->contract_token && !priv->attaching);
    }
  else
    {
      icon = priv->token_status_icon;
      label = priv->token_status;
      spinner = priv->token_spinner;
      ready_to_attach = SuccessfullyAttached || (!priv->attaching &&
                                                 gtk_entry_get_text_length (GTK_ENTRY (priv->token_field)) > 0);
    }
  gis_page_set_complete (GIS_PAGE (priv->main_page), ready_to_attach);

  if (priv->attaching)
    gtk_spinner_start (GTK_SPINNER (spinner));
  else
    gtk_spinner_stop (GTK_SPINNER (spinner));
  gtk_widget_set_visible (spinner, priv->attaching);

  gtk_widget_set_sensitive (priv->token_radio, !priv->attaching);
  gtk_widget_set_sensitive (priv->magic_radio, !priv->attaching);
  gtk_widget_set_sensitive (priv->token_field, !priv->attaching &&
                            priv->active_radio == TOKEN);
  /* I don't know why this is needed, but otherwise even though the entry is
   * sensitive, the blinking cursor doesn't appear and one cannot type in it */
  if (gtk_widget_is_sensitive (priv->token_field))
    gtk_entry_grab_focus_without_selecting (GTK_ENTRY (priv->token_field));

  if (SuccessfullyAttached)
    {
      consider_going_to_next_page (priv);
      status = SUCCESS;
    }
  switch (status)
    {
    case SUCCESS:
      subtitle = g_strconcat ("<span foreground=\"green\"><b>",
                              _("Valid token"), "</b></span>", NULL);
      adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (label), subtitle);
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), CHECK_ICON);
      gtk_widget_set_sensitive (GTK_WIDGET (priv->token_field), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (priv->magic_radio), FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (priv->token_radio), FALSE);
      break;

    case FAIL:
      subtitle = g_strconcat ("<span foreground=\"red\"><b>",
                              /* TRANSLATORS: The error shown when attaching an
                                 Ubuntu Pro contact fails.
                                 It may be due to various reasons, most common is
                                 an invalid token.
                               */
                              _("Invalid token or attach failure"), "</b></span>",
                              NULL);
      adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (label), subtitle);
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "dialog-error-symbolic");
      break;

    case EXPIRED:
      hint = g_strdup ("Click the button to generate a new code.");
      adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (label), _("Code expired"));
      gtk_image_set_from_icon_name (GTK_IMAGE (icon), "dialog-warning-symbolic");
      break;

    default:
      adw_expander_row_set_subtitle (ADW_EXPANDER_ROW (label), NULL);
      break;
    }

  if (!hint)
    {
      g_autofree gchar *url = g_strdup_printf ("<a href=\"https://ubuntu.com/pro/attach?magic-attach-code=%s\">ubuntu.com/pro/attach</a>",
                                               gtk_label_get_text(GTK_LABEL (priv->pin_label)));
      // TRANSLATORS: the placeholder %s is a link to ubuntu.com/pro/attach.
      // Use HTML encoded format for special characters such as '&'.
      hint = g_strdup_printf (_("Enter this code on %s"), url);
    }
  gtk_label_set_markup (GTK_LABEL (priv->pin_hint), hint);
  gtk_widget_set_visible (GTK_WIDGET (priv->token_status_icon), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (priv->pin_status_icon), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (icon), status);

  if (gis_driver_get_mode (GIS_PAGE (priv->main_page)->driver) == GIS_DRIVER_MODE_NEW_USER)
    {
      g_signal_connect_object (G_OBJECT (priv->pin_hint), "activate-link",
                               G_CALLBACK (gis_activate_link), G_OBJECT (attach_page),
                               G_CONNECT_DEFAULT);
    }
}

typedef struct _UpdateUIData
{
  GisUbuntuProAttachPage *page;
  status_t status;
} UpdateUIData;

static void
update_attach_page_callback (gpointer data)
{
  g_autofree UpdateUIData *update_data = g_steal_pointer (&data);

  update_attach_page (update_data->page, update_data->status);
  g_clear_object (&update_data->page);
}

static void
update_attach_page_in_idle (GisUbuntuProAttachPage *page,
                            status_t                status)
{
  UpdateUIData *update_data;

  update_data = g_new0 (UpdateUIData, 1);
  update_data->page = g_object_ref (page);
  update_data->status = status;

  g_idle_add_once (update_attach_page_callback, g_steal_pointer (&update_data));
}

static void
update_network_status (GisUbuntuProOfferPage *offer_page,
                       gboolean               available)
{
  GisUbuntuProOfferPagePrivate *priv =
    gis_ubuntupro_offer_page_get_instance_private (offer_page);

  gtk_widget_set_sensitive (priv->enable_pro_row, available);
  gtk_widget_set_visible (priv->offline_warning, !available);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (priv->skip_pro_select),
                               !available);
  adw_action_row_set_subtitle (ADW_ACTION_ROW (priv->enable_pro_row),
                               available ?
                               NULL :
                               _("An internet connection is required to enable Ubuntu Pro"));
}

static void
network_status_changed (GNetworkMonitor *monitor,
                        gboolean         available,
                        gpointer         user_data)
{
  GisUbuntuProOfferPage *offer_page = user_data;

  update_network_status (offer_page, available);
}

/* This allows to use start padding for GtkCheckButtons that do not support it
 * natively.
 */
static void
set_children_valign_start (GtkWidget *widget)
{
  for (GtkWidget *c = gtk_widget_get_first_child (widget); c;
       c = gtk_widget_get_next_sibling (c))
    gtk_widget_set_valign (c, GTK_ALIGN_START);
}

/* In order to align the elements correctly we use insensitive check buttons
 * as items, so that they are padded well with the rest of items.
 * However we need to hide the check or radio button, so we use CSS for that.
 */
static void
set_check_button_as_padded_label (GtkWidget *widget)
{
  g_return_if_fail (GTK_IS_CHECK_BUTTON (widget));

  gtk_widget_set_sensitive (widget, FALSE);
  gtk_widget_add_css_class (widget, "as-padded-label");
}

static void
gis_ubuntupro_offer_page_dispose (GObject *object)
{
  GisUbuntuProOfferPage *offer_page = GIS_UBUNTUPRO_OFFER_PAGE (object);
  GisUbuntuProOfferPagePrivate *offer_priv = gis_ubuntupro_offer_page_get_instance_private (offer_page);

  GNetworkMonitor *network_monitor = g_network_monitor_get_default ();

  g_signal_handler_disconnect (network_monitor, offer_priv->network_cb);

  G_OBJECT_CLASS (gis_ubuntupro_offer_page_parent_class)->dispose (object);
}

static void
gis_ubuntupro_page_dispose (GObject *object)
{
  G_OBJECT_CLASS (gis_ubuntupro_page_parent_class)->dispose (object);
}

static guint
get_end_of_support_year (void)
{
  g_autofree char *version_id = NULL;

  version_id = g_get_os_info (G_OS_INFO_KEY_VERSION_ID);
  if (!version_id || !strchr (version_id, '.'))
    return 36;

  g_auto (GStrv) version_parts = g_strsplit (version_id, ".", 2);
  gint64 year = g_ascii_strtoll (version_parts[0], NULL, 10);

  return year + 10;
}

static void
update_theme (GisUbuntuProPage *page,
              GParamSpec *pspec,
              AdwStyleManager *style_manager)
{
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);

  GisUbuntuProOfferPage *offer_page = GIS_UBUNTUPRO_OFFER_PAGE (priv->offer_page);
  GisUbuntuProOfferPagePrivate *offer_priv = gis_ubuntupro_offer_page_get_instance_private (offer_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);
  GisUbuntuProServicesPage *services_page = GIS_UBUNTUPRO_SERVICES_PAGE (priv->services_page);
  GisUbuntuProServicesPagePrivate *services_priv = gis_ubuntupro_services_page_get_instance_private (services_page);

  const gchar *logo = adw_style_manager_get_dark (style_manager) ? LOGO_DARK : LOGO;
  gtk_picture_set_resource (GTK_PICTURE (offer_priv->ubuntu_pro_logo), logo);
  gtk_picture_set_resource (GTK_PICTURE (attach_priv->ubuntu_pro_logo), logo);
  gtk_picture_set_resource (GTK_PICTURE (services_priv->ubuntu_pro_logo), logo);
}

static void
gis_ubuntupro_page_constructed (GObject *object)
{
  AdwStyleManager *style_manager;
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (object);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);

  g_autoptr(GtkCssProvider) css_provider = NULL;

  css_provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_string (css_provider,
                                     "checkbutton.as-padded-label:disabled > * { color: inherit; }\n"
                                     "checkbutton.as-padded-label > radio, check { opacity: 0; }\n");
  gtk_style_context_add_provider_for_display (gdk_display_get_default (),
                                              GTK_STYLE_PROVIDER (css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  G_OBJECT_CLASS (gis_ubuntupro_page_parent_class)->constructed (object);

  GisUbuntuProOfferPage *offer_page = GIS_UBUNTUPRO_OFFER_PAGE (priv->offer_page);
  GisUbuntuProOfferPagePrivate *offer_priv = gis_ubuntupro_offer_page_get_instance_private (offer_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  style_manager = adw_style_manager_get_default ();
  g_signal_connect_object (style_manager,
                           "notify::dark",
                           G_CALLBACK (update_theme),
                           G_OBJECT (page),
                           G_CONNECT_SWAPPED);
  update_theme (page, NULL, style_manager);

  GNetworkMonitor *network_monitor = g_network_monitor_get_default ();
  offer_priv->network_cb = g_signal_connect (network_monitor, "network-changed",
                                             G_CALLBACK (network_status_changed),
                                             offer_page);

  update_network_status (offer_page,
                         g_network_monitor_get_network_available (network_monitor));

  /* Initializate priv values */
  g_atomic_int_set (&attach_priv->magic_token_polling, FALSE);
  g_atomic_pointer_set (&attach_priv->expired_by, 0);
  attach_priv->active_radio = MAGIC;
  attach_priv->contract_token = NULL;
  attach_priv->main_page = page;
  priv->current_page = OFFER_PAGE;

  g_object_bind_property (offer_priv->skip_pro_select, "active", page, "skippable",
                          G_BINDING_SYNC_CREATE);
  g_object_bind_property (offer_priv->skip_pro_select, "active", page, "complete",
                          G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
  gtk_check_button_set_active (GTK_CHECK_BUTTON (offer_priv->skip_pro_select), TRUE);

  g_autofree char *year_str = g_strdup_printf ("20%02u", get_end_of_support_year ());
  g_autofree char *security_updates_label = NULL;

  // TRANSLATORS: translators: %s is a year, for example 2036.
  security_updates_label = g_strdup_printf (
    _("Get security updates on a wide range of packages until %s"), year_str);

  gtk_label_set_label (GTK_LABEL (offer_priv->security_updates_label), security_updates_label);
}

static void
gis_ubuntupro_page_locale_changed (GisPage *page)
{
  gis_page_set_title (GIS_PAGE (page), _("Ubuntu Pro"));
}

static gboolean
poll_token_attach (GisUbuntuProAttachPagePrivate *priv)
{
  gchar *std_out, *std_err, *cmd;
  RestJSONResponse resp; /* relevant response fields */

  g_autoptr(GError) error = NULL;

  cmd = g_strconcat ("/usr/bin/pro api u.pro.attach.magic.wait.v1 --args magic_token=", priv->token, NULL);
  if (!g_spawn_command_line_sync (cmd, &std_out, &std_err, NULL, &error))
    {
      g_warning ("Failed to request magic token: %s", error->message);
    }
  else if (!magic_parser (std_out, &resp))
    {
      g_warning ("Couldn't parse response.");
    }
  else if (resp.contract_token != NULL)
    {
      g_free (g_atomic_pointer_exchange (&priv->contract_token,
                                         g_strdup (resp.contract_token)));
      return TRUE;
    }
  return FALSE;
}

static void *
poll_magic_token (void *data)
{
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (data);
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);
  gboolean token_received;
  gsize expired_by;

  if (priv->active_radio != MAGIC)
    pthread_exit (NULL);

  g_atomic_int_set (&priv->magic_token_polling, TRUE);
  expired_by = GPOINTER_TO_SIZE (g_atomic_pointer_get (&priv->expired_by));

  if (g_get_monotonic_time () > expired_by)
    {
      update_attach_page_in_idle (attach_page, EXPIRED);
      g_atomic_int_set (&priv->magic_token_polling, FALSE);
    }

  token_received = poll_token_attach (priv);
  if (token_received)
    update_attach_page_in_idle (attach_page, SUCCESS);
  else if (g_get_monotonic_time () > expired_by)
    update_attach_page_in_idle (attach_page, EXPIRED);
  else
    update_attach_page_in_idle (attach_page, FAIL);

  g_atomic_int_set (&priv->magic_token_polling, FALSE);
  pthread_exit (NULL);
}

static gboolean
magic_parser (gchar            *ptr,  /* pointer to actual response */
              RestJSONResponse *resp) /* relevant response fields */
{
  JsonNode *root;
  JsonObject *data, *attributes, *response;
  g_autoptr(JsonParser) parser = json_parser_new ();
  g_autoptr(GError) error = NULL;

  if (!json_parser_load_from_data (parser, ptr, -1, &error))
    {
      g_warning ("Couldn't parse magic token JSON; %s", error->message);
      return FALSE;
    }

  root = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root))
    {
      g_warning ("Invalid magic token JSON");
      return FALSE;
    }

  response = json_node_get_object (root);

  if (g_strcmp0 (json_object_get_string_member (response, "_schema_version"), "v1") != 0)
    {
      g_critical ("Schema version not handled in JSON: '%s'", ptr);
      return FALSE;
    }

  if (g_strcmp0 (json_object_get_string_member (response, "result"), "success") != 0)
    {
      g_critical ("Magic fetching failed due to an error: '%s'", ptr);
      return FALSE;
    }

  data = json_object_get_object_member (response, "data");
  attributes = json_object_get_object_member (data, "attributes");

  resp->expires_in = json_object_has_member (attributes, "expires_in") ?
                     json_object_get_int_member (attributes, "expires_in") :
                     0;
  resp->token = json_object_has_member (attributes, "token") ?
                g_strdup (json_object_get_string_member (attributes, "token")) :
                NULL;
  resp->code = json_object_has_member (attributes, "user_code") ?
               g_strdup (json_object_get_string_member (attributes, "user_code")) :
               NULL;
  resp->contract_token = json_object_has_member (attributes, "contract_token") ?
                         g_strdup (json_object_get_string_member (attributes, "contract_token")) :
                         NULL;

  return resp->expires_in > 0 &&
         ((resp->token != NULL && *resp->token) ||
          (resp->code != NULL && *resp->code) ||
          (resp->contract_token != NULL && *resp->contract_token));
}

static void
preferences_group_remove_all (AdwPreferencesGroup *group)
{
  GtkWidget *row;

  while ((row = adw_preferences_group_get_row (group, 0)))
    adw_preferences_group_remove (group, row);
}

const struct
{
  const char *name;
  const char *description;
} service_names[] = {
  { "anbox-cloud",
    /* TRANSLATORS: The anbox-cloud Ubuntu Pro Service description. */
    N_("Scalable Android in the cloud") },
  { "cc-eal",
    /* TRANSLATORS: The cc-eal Ubuntu Pro Service description. */
    N_("Common Criteria EAL2 Provisioning Packages") },
  { "esm-apps",
    /* TRANSLATORS: The esm-apps Ubuntu Pro Service description. */
    N_("Expanded Security Maintenance for Applications") },
  { "esm-infra",
    /* TRANSLATORS: The esm-infra Ubuntu Pro Service description. */
    N_("Expanded Security Maintenance for Infrastructure") },
  { "fips",
    /* TRANSLATORS: The fips Ubuntu Pro Service description. */
    N_("NIST-certified FIPS crypto packages") },
  { "fips-preview",
    /* TRANSLATORS: The fips-preview Ubuntu Pro Service description. */
    N_("Preview of FIPS crypto packages undergoing certification with NIST") },
  { "fips-updates",
    /* TRANSLATORS: The fips-updates Ubuntu Pro Service description. */
    N_("FIPS compliant crypto packages with stable security updates") },
  { "landscape",
    /* TRANSLATORS: The landscape Ubuntu Pro Service description. */
    N_("Management and administration tool for Ubuntu") },
  { "livepatch",
    /* TRANSLATORS: The livepatch Ubuntu Pro Service description. */
    N_("Canonical kernel Livepatch service") },
  { "realtime-kernel",
    /* TRANSLATORS: The realtime-kernel Ubuntu Pro Service description. */
    N_("Ubuntu kernel with PREEMPT_RT patches integrated") },
  { "ros",
    /* TRANSLATORS: The ros Ubuntu Pro Service description. */
    N_("Security Updates for the Robot Operating System") },
  { "ros-updates",
    /* TRANSLATORS: The ros-updates Ubuntu Pro Service description. */
    N_("All Updates for the Robot Operating System") },
  { "usg",
    /* TRANSLATORS: The usg Ubuntu Pro Service description. */
    N_("Security compliance and audit tools") },
};

static const char *
get_translated_service_description (const char *service_name)
{
  for (unsigned i = 0; i < G_N_ELEMENTS (service_names); ++i)
    if (g_strcmp0 (service_name, service_names[i].name) == 0)
      return service_names[i].description;

  g_return_val_if_reached (NULL);
}

static void
display_ua_services (GisUbuntuProServicesPagePrivate *priv)
{
  g_autoptr(JsonParser) parser = json_parser_new ();
  JsonNode *root_node;
  JsonObject *root, *services, *contract;
  JsonArray *services_array;
  g_autoptr(GError) error = NULL;
  guint i, n_services;
  gboolean has_enabled_services = FALSE;
  gboolean has_available_services = FALSE;
  const char *status, *description, *available, *contract_name, *name;

  if (!json_parser_load_from_file (parser, UBUNTU_ADVANTAGE_STATUS_FILE, &error))
    {
      g_warning ("Could not parse JSON from %s: %s",
                 UBUNTU_ADVANTAGE_STATUS_FILE,
                 error->message);
      return;
    }

  root_node = json_parser_get_root (parser);
  if (!JSON_NODE_HOLDS_OBJECT (root_node))
    {
      g_warning ("Invalid status JSON in %s, not an object",
                 UBUNTU_ADVANTAGE_STATUS_FILE);
      return;
    }

  root = json_node_get_object (root_node);

  contract = json_object_get_object_member (root, "contract");
  contract_name = json_object_get_string_member (contract, "name");
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (priv->contract_name),
                                 contract_name);

  services_array = json_object_get_array_member (root, "services");
  n_services = json_array_get_length (services_array);

  preferences_group_remove_all (ADW_PREFERENCES_GROUP (priv->enabled_services_header));
  preferences_group_remove_all (ADW_PREFERENCES_GROUP (priv->available_services_header));

  /* Get services description, status and availability */
  for (i = 0; i < n_services; i++)
    {
      services = json_array_get_object_element (services_array, i);
      if (json_object_has_member (services, "status") &&
          json_object_has_member (services, "available"))
        {
          AdwPreferencesGroup *target_group = NULL;
          AdwActionRow *row;
          gboolean is_service;

          name = json_object_get_string_member (services, "name");
          status = json_object_get_string_member (services, "status");
          available = json_object_get_string_member (services, "available");

          if (g_str_equal (status, "enabled"))
            {
              target_group = ADW_PREFERENCES_GROUP (priv->enabled_services_header);
              has_enabled_services = TRUE;
              is_service = TRUE;
            }
          else if (g_str_equal (available, "yes"))
            {
              target_group = ADW_PREFERENCES_GROUP (priv->available_services_header);
              has_available_services = TRUE;
              is_service = FALSE;
            }

          if (!target_group)
            continue;

          row = ADW_ACTION_ROW (adw_action_row_new ());
          if (is_service)
            {
              GtkWidget *icon = gtk_image_new_from_icon_name ("pro-checkmark");

              gtk_image_set_pixel_size (GTK_IMAGE (icon), 22);
              adw_action_row_add_suffix (row, icon);
            }

          description = get_translated_service_description (name);
          if (G_UNLIKELY (!description))
            description = json_object_get_string_member (services, "status");
          if (G_UNLIKELY (!description))
            description = name;

          adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), description);
          adw_preferences_group_add (target_group, GTK_WIDGET (row));
        }
    }

  /* Display enabled and disabled but available services */

  gtk_widget_set_visible (priv->enabled_services_header, has_enabled_services);
  gtk_widget_set_visible (priv->available_services_header, has_available_services);
}

static void
go_to_attach_page (GisUbuntuProPage *pro_page,
                   status_t          status)
{
  GisUbuntuProPagePrivate *pro_page_priv = gis_ubuntupro_page_get_instance_private (pro_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (pro_page_priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_page_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  if (gis_page_get_applying (GIS_PAGE (pro_page)))
    gis_page_apply_complete (GIS_PAGE (pro_page), FALSE);

  gtk_stack_set_visible_child (GTK_STACK (pro_page_priv->stack), GTK_WIDGET (attach_page));
  pro_page_priv->current_page = ATTACH_PAGE;
  gtk_entry_buffer_set_text (gtk_entry_get_buffer (GTK_ENTRY (attach_page_priv->token_field)), "", 0);
  update_attach_page (attach_page, status);
}

static void
go_to_attach_page_idle (void *data)
{
  go_to_attach_page (GIS_UBUNTUPRO_PAGE (data), NONE);
}

static void
on_magic_read (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
  GisUbuntuProPage *pro_page = user_data;
  GisUbuntuProPagePrivate *pro_page_priv = gis_ubuntupro_page_get_instance_private (pro_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (pro_page_priv->attach_page);
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);
  g_autoptr(GError) error = NULL;
  g_autofree char *std_out = NULL;
  int rc;

  std_out = g_data_input_stream_read_line_finish (G_DATA_INPUT_STREAM (source_object),
                                                  res, NULL, &error);
  g_debug ("Received response %s", std_out);

  if (!std_out)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          gis_page_apply_complete (GIS_PAGE (pro_page), FALSE);
          return;
        }

      g_warning ("Failed to request magic token: %s",
                 error ? error->message : "empty token result received");
      go_to_attach_page (pro_page, FAIL);
      return;
    }

  RestJSONResponse resp;
  if (!magic_parser (std_out, &resp))
    {
      g_warning ("Couldn't parse response: %s", std_out);
      go_to_attach_page (pro_page, FAIL);
      return;
    }

  gtk_label_set_text (GTK_LABEL (priv->pin_label), resp.code);
  g_atomic_pointer_set (&priv->expired_by,
                        GSIZE_TO_POINTER (g_get_monotonic_time () +
                                          (resp.expires_in * G_USEC_PER_SEC)));
  g_atomic_pointer_set (&priv->token, g_steal_pointer (&resp.token));
  g_atomic_int_set (&priv->magic_token_polling, TRUE);

  rc = pthread_create (&priv->poll_id, NULL, poll_magic_token, attach_page);
  if (rc)
    {
      g_warning ("Couldn't create PIN polling thread");
      go_to_attach_page (pro_page, FAIL);
      return;
    }
  g_free (resp.token);
  g_free (resp.code);

  go_to_attach_page (pro_page, NONE);
}

static void
request_magic_attach (GisUbuntuProPage *page,
                      GCancellable     *cancellable)
{
  GisUbuntuProPagePrivate *pro_page_priv = gis_ubuntupro_page_get_instance_private (page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (pro_page_priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  g_autoptr(GIOChannel) channel = NULL;
  g_autoptr(GInputStream) stream = NULL;
  g_autoptr(GDataInputStream) data_stream = NULL;
  g_autoptr(GSource) source = NULL;
  g_autoptr(GError) error = NULL;
  const char * cmd_line[] = {"/usr/bin/pro", "api", "u.pro.attach.magic.initiate.v1", NULL};
  int stdout_fd;

  if (g_atomic_int_get (&attach_priv->magic_token_polling) || attach_priv->attaching)
    {
      gis_page_apply_complete (GIS_PAGE (page), FALSE);
      g_idle_add_once (go_to_attach_page_idle, page);
      return;
    }

  if (!g_spawn_async_with_pipes (NULL, (char **) cmd_line, NULL, G_SPAWN_DEFAULT,
                                 NULL, NULL, NULL, NULL, &stdout_fd, NULL,
                                 &error))
    {
      g_warning ("Failed to request magic token: %s", error->message);
      return;
    }

  stream = g_unix_input_stream_new (stdout_fd, TRUE);
  data_stream = g_data_input_stream_new (stream);
  g_data_input_stream_read_line_async (data_stream, G_PRIORITY_DEFAULT,
                                       cancellable, on_magic_read, page);
}

static void
on_ua_attach_requested (GObject      *source,
                        GAsyncResult *result,
                        gpointer      user_data)
{
  g_autoptr (GisUbuntuProAttachPage) attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (g_steal_pointer (&user_data));
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
  status_t status;

  gis_page_set_hide_navigation (GIS_PAGE (priv->main_page), FALSE);
  gis_page_apply_complete (GIS_PAGE (priv->main_page), FALSE);
  g_clear_handle_id (&priv->attach_idle, g_source_remove);

  if (retval == NULL)
    {
      g_warning ("Failed to attach token: %s", error->message);
      status = FAIL;
    }
  else
    {
      SuccessfullyAttached = TRUE;
      status = SUCCESS;
      pthread_cancel (priv->poll_id);
      gis_page_set_complete (GIS_PAGE (priv->main_page), TRUE);
      g_clear_pointer (&priv->contract_token, g_free);
      g_free (g_atomic_pointer_exchange (&priv->token, NULL));
    }
  priv->attaching = FALSE;
  update_attach_page (attach_page, status);
}

static void
ua_attach (const gchar            *token,
           GisUbuntuProAttachPage *attach_page,
           GCancellable           *cancellable)
{
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);
  GDBusConnection *bus;

  g_autoptr(GError) error = NULL;
  status_t status = NONE;

  if (priv->attaching)
    return;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
  if (bus == NULL)
    {
      g_warning ("Failed to get system bus: %s", error->message);
    }
  else
    {
      priv->attaching = TRUE;
      update_attach_page (attach_page, status);

      g_dbus_connection_call (bus,
                              "com.canonical.UbuntuAdvantage",
                              "/com/canonical/UbuntuAdvantage/Manager",
                              "com.canonical.UbuntuAdvantage.Manager",
                              "Attach",
                              g_variant_new ("(s)", token),
                              G_VARIANT_TYPE ("()"),
                              G_DBUS_CALL_FLAGS_NONE,
                              543210, /* I have observed that -1, the default timeout, is not enough. */
                              cancellable,
                              on_ua_attach_requested,
                              g_object_ref (attach_page));
    }
}

static void
on_token_typing (GtkButton *button, GisUbuntuProAttachPage *page)
{
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (page);

  gis_page_set_complete (
    GIS_PAGE (priv->main_page),
    gtk_entry_get_text_length (GTK_ENTRY (priv->token_field)) > 0);
}

static void
request_token_attach (GtkButton *button, GisUbuntuProAttachPage *page)
{
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (page);

  gis_page_apply_begin (GIS_PAGE (priv->main_page), NULL, NULL);
}

static void
on_magic_clicked (GtkButton *button, GisUbuntuProAttachPage *page)
{
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (page);

  priv->active_radio = MAGIC;

  gis_page_apply_begin (GIS_PAGE (priv->main_page), NULL, NULL);
}

static void
on_radio_toggled (GtkButton *button, GisUbuntuProAttachPage *page)
{
  GisUbuntuProAttachPagePrivate *priv = gis_ubuntupro_attach_page_get_instance_private (page);
  status_t status = NONE;

  priv->active_radio = gtk_check_button_get_active (GTK_CHECK_BUTTON (priv->token_radio)) ? TOKEN : MAGIC;
  update_attach_page (page, status);
}

static void
page_shown (GisPage *gis_page)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (gis_page);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  GisUbuntuProOfferPage *offer_page = GIS_UBUNTUPRO_OFFER_PAGE (priv->offer_page);
  GisUbuntuProOfferPagePrivate *offer_priv = gis_ubuntupro_offer_page_get_instance_private (offer_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);
  GisUbuntuProServicesPage *services_page = GIS_UBUNTUPRO_SERVICES_PAGE (priv->services_page);
  GisUbuntuProServicesPagePrivate *services_priv = gis_ubuntupro_services_page_get_instance_private (services_page);

  switch (priv->current_page)
    {
      case OFFER_PAGE:
        gtk_widget_grab_focus (offer_priv->header);
        break;

      case ATTACH_PAGE:
        gtk_widget_grab_focus (attach_priv->header);
        break;

      case SERVICES_PAGE:
        gtk_widget_grab_focus (services_priv->header);
        break;
    }
}


/* Callback for the Previous button */
static gboolean
gis_ubuntupro_page_go_back (GisPage *gis_page)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (gis_page);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  GisUbuntuProOfferPage *offer_page = GIS_UBUNTUPRO_OFFER_PAGE (priv->offer_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_page_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  switch (priv->current_page)
    {
    case OFFER_PAGE:
      return FALSE;

    case ATTACH_PAGE:
      gis_page_set_complete (gis_page, TRUE);
      if (g_get_monotonic_time () + (60 * G_USEC_PER_SEC) >
          GPOINTER_TO_SIZE (g_atomic_pointer_get (&attach_page_priv->expired_by)))
        {
          if (attach_page_priv->poll_id)
            {
              pthread_cancel (attach_page_priv->poll_id);
              attach_page_priv->poll_id = 0;
            }

          g_atomic_int_set (&attach_page_priv->magic_token_polling, FALSE);
          g_free (g_atomic_pointer_exchange (&attach_page_priv->token, NULL));
        }
      gtk_stack_set_visible_child (GTK_STACK (priv->stack), GTK_WIDGET (offer_page));
      priv->current_page = OFFER_PAGE;
      break;

    case SERVICES_PAGE:
      gtk_stack_set_visible_child (GTK_STACK (priv->stack), GTK_WIDGET (attach_page));
      priv->current_page = ATTACH_PAGE;
      break;
    }
  page_shown(gis_page);
  return TRUE;
}

static void
on_attach_idle (gpointer data)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (data);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  gis_page_set_hide_navigation (GIS_PAGE (page), TRUE);
  attach_priv->attach_idle = 0;
}

/* Callback for the Next button */
static gboolean
gis_ubuntupro_page_apply (GisPage      *gis_page,
                          GCancellable *cancellable)
{
  GisUbuntuProPage *page = GIS_UBUNTUPRO_PAGE (gis_page);
  GisUbuntuProPagePrivate *priv = gis_ubuntupro_page_get_instance_private (page);
  GisUbuntuProOfferPage *offer_page = GIS_UBUNTUPRO_OFFER_PAGE (priv->offer_page);
  GisUbuntuProOfferPagePrivate *offer_priv = gis_ubuntupro_offer_page_get_instance_private (offer_page);
  GisUbuntuProAttachPage *attach_page = GIS_UBUNTUPRO_ATTACH_PAGE (priv->attach_page);
  GisUbuntuProAttachPagePrivate *attach_priv = gis_ubuntupro_attach_page_get_instance_private (attach_page);

  switch (priv->current_page)
    {
    case OFFER_PAGE:
      if (gtk_check_button_get_active (GTK_CHECK_BUTTON (offer_priv->skip_pro_select)))
        return FALSE;
      /* Request magic token already if didn't yet and advance to next local page */
      request_magic_attach (page, cancellable);
      break;

    case ATTACH_PAGE:
      if (SuccessfullyAttached)
        {
          GisUbuntuProServicesPage *services_page = GIS_UBUNTUPRO_SERVICES_PAGE (priv->services_page);
          display_ua_services (gis_ubuntupro_services_page_get_instance_private (services_page));
          gtk_stack_set_visible_child (GTK_STACK (priv->stack), priv->services_page);
          gis_page_apply_complete (GIS_PAGE (page), FALSE);
          priv->current_page = SERVICES_PAGE;
        }
      else
        {
          const char *token;
          if (attach_priv->active_radio == MAGIC)
            token = attach_priv->contract_token;
          else
            token = gtk_editable_get_text (GTK_EDITABLE (attach_priv->token_field));
          ua_attach (token, attach_page, cancellable);
          g_clear_handle_id (&attach_priv->attach_idle, g_source_remove);
          attach_priv->attach_idle = g_idle_add_once (on_attach_idle, page);
        }
      break;

    case SERVICES_PAGE:
      return FALSE;
    }
  page_shown(gis_page);
  return TRUE;
}

static void
gis_ubuntupro_page_class_init (GisUbuntuProPageClass *klass)
{
  GisPageClass *page_class = GIS_PAGE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, offer_page);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, attach_page);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, services_page);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProPage, stack);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), request_magic_attach);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_radio_toggled);

  page_class->page_id = PAGE_ID;
  page_class->locale_changed = gis_ubuntupro_page_locale_changed;
  page_class->apply = gis_ubuntupro_page_apply;
  page_class->go_back = gis_ubuntupro_page_go_back;
  page_class->shown = page_shown;
  object_class->constructed = gis_ubuntupro_page_constructed;
  object_class->dispose = gis_ubuntupro_page_dispose;
}
static void
gis_ubuntupro_offer_page_class_init (GisUbuntuProOfferPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-offer-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, ubuntu_pro_logo);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, enable_pro_row);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, skip_pro_select);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, offline_warning);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, security_updates_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, header);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProOfferPage, learn_more);

  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), set_children_valign_start);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gis_activate_link);

  object_class->dispose = gis_ubuntupro_offer_page_dispose;
}

static void
gis_ubuntupro_attach_page_class_init (GisUbuntuProAttachPageClass *klass)
{
  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-attach-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, ubuntu_pro_logo);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, pin_label);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, token_field);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, token_status);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, token_spinner);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, token_status_icon);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, token_hint);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, pin_status);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, pin_spinner);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, pin_status_icon);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, pin_hint);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, token_radio);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, magic_radio);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProAttachPage, header);

  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_magic_clicked);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_radio_toggled);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), on_token_typing);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), request_token_attach);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), set_children_valign_start);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), set_check_button_as_padded_label);
  gtk_widget_class_bind_template_callback (GTK_WIDGET_CLASS (klass), gis_activate_link);
}
static void
gis_ubuntupro_services_page_class_init (GisUbuntuProServicesPageClass *klass)
{
  gtk_widget_class_set_template_from_resource (GTK_WIDGET_CLASS (klass), "/org/gnome/initial-setup/gis-ubuntupro-services-page.ui");

  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProServicesPage, ubuntu_pro_logo);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProServicesPage, enabled_services_header);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProServicesPage, available_services_header);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProServicesPage, contract_name);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProServicesPage, checkmark);
  gtk_widget_class_bind_template_child_private (GTK_WIDGET_CLASS (klass), GisUbuntuProServicesPage, header);
}

static void
gis_ubuntupro_page_init (GisUbuntuProPage *page)
{
  g_resources_register (ubuntupro_get_resource ());

  /* Magic that makes stuff compile */
  g_type_ensure (GIS_TYPE_PAGE_HEADER);
  g_type_ensure (GIS_TYPE_UBUNTUPRO_OFFER_PAGE);
  g_type_ensure (GIS_TYPE_UBUNTUPRO_ATTACH_PAGE);
  g_type_ensure (GIS_TYPE_UBUNTUPRO_SERVICES_PAGE);

  gtk_widget_init_template (GTK_WIDGET (page));
}
static void
gis_ubuntupro_offer_page_init (GisUbuntuProOfferPage *page)
{
  GisUbuntuProOfferPagePrivate *offer_priv;

  g_resources_register (ubuntupro_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));

  offer_priv = gis_ubuntupro_offer_page_get_instance_private (page);

  /* TRANSLATORS: Use HTML encoded format for special characters such as &amp' for '&'. %s is a URL. */
  g_autofree gchar *link = g_strdup_printf (_("Learn more at %s"),
                                            "<a href=\"https://ubuntu.com/pro\">ubuntu.com/pro</a>");
  gtk_label_set_label (GTK_LABEL (offer_priv->learn_more), link);
}
static void
gis_ubuntupro_attach_page_init (GisUbuntuProAttachPage *page)
{
  GisUbuntuProAttachPagePrivate *attach_priv;

  g_resources_register (ubuntupro_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));

  attach_priv = gis_ubuntupro_attach_page_get_instance_private (page);
  /* TRANSLATORS: Subtitle telling who can provide the Ubuntu Pro token.
     Use HTML encoded format for special characters such as &amp' for '&'. %s is a URL.
   */
  g_autofree gchar *link = g_strdup_printf (_("From your admin, or from %s"),
                                            "<a href=\"https://ubuntu.com/pro/dashboard\">ubuntu.com/pro</a>");
  gtk_label_set_label (GTK_LABEL (attach_priv->token_hint), link);
}
static void
gis_ubuntupro_services_page_init (GisUbuntuProServicesPage *page)
{
  g_resources_register (ubuntupro_get_resource ());

  gtk_widget_init_template (GTK_WIDGET (page));
}

GisPage *
gis_prepare_ubuntu_pro_page (GisDriver *driver)
{
  gboolean attached;
  gboolean skip_checks = !!g_getenv ("GIS_SHOW_ALL_PAGES");

  if (!skip_checks && !is_ubuntu_pro_supported ())
    {
      g_message ("Ubuntu Pro is not supported on non-LTS Ubuntu releases.");
      return NULL;
    }

  if (skip_checks && !get_ubuntu_advantage_attached (&attached))
    {
      g_warning ("Couldn't fetch the Ubuntu Pro status.");
      return NULL;
    }

  if (skip_checks && attached && !g_getenv ("GIS_SHOW_ALL_PAGES"))
    {
      g_warning ("Ubuntu Pro is already attached. Skipping Ubuntu Pro pages.");
      return NULL;
    }

  return g_object_new (GIS_TYPE_UBUNTUPRO_PAGE,
                       "driver", driver,
                       NULL);
}
