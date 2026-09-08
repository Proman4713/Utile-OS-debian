/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2018 Canonical Ltd.
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

#ifndef __GIS_UBUNTUPRO_PAGE_H__
#define __GIS_UBUNTUPRO_PAGE_H__

#include <glib-object.h>

#include "gis-page.h"

G_BEGIN_DECLS

#define GIS_TYPE_UBUNTUPRO_PAGE (gis_ubuntupro_page_get_type ())
#define GIS_TYPE_UBUNTUPRO_OFFER_PAGE (gis_ubuntupro_offer_page_get_type ())
#define GIS_TYPE_UBUNTUPRO_ATTACH_PAGE (gis_ubuntupro_attach_page_get_type ())
#define GIS_TYPE_UBUNTUPRO_SERVICES_PAGE (gis_ubuntupro_services_page_get_type ())

G_DECLARE_DERIVABLE_TYPE (GisUbuntuProPage, gis_ubuntupro_page, GIS, UBUNTUPRO_PAGE, GisPage)
G_DECLARE_DERIVABLE_TYPE (GisUbuntuProOfferPage, gis_ubuntupro_offer_page, GIS, UBUNTUPRO_OFFER_PAGE, AdwBin)
G_DECLARE_DERIVABLE_TYPE (GisUbuntuProAttachPage, gis_ubuntupro_attach_page, GIS, UBUNTUPRO_ATTACH_PAGE, AdwBin)
G_DECLARE_DERIVABLE_TYPE (GisUbuntuProServicesPage, gis_ubuntupro_services_page, GIS, UBUNTUPRO_SERVICES_PAGE, AdwBin)

struct _GisUbuntuProPageClass
{
  GisPageClass parent_class;
};

struct _GisUbuntuProOfferPageClass
{
  AdwBinClass parent_class;
};

struct _GisUbuntuProAttachPageClass
{
  AdwBinClass parent_class;
};

struct _GisUbuntuProServicesPageClass
{
  AdwBinClass parent_class;
};

GisPage *gis_prepare_ubuntu_pro_page (GisDriver *driver);


G_END_DECLS

#endif /* __GIS_UBUNTUPRO_PAGE_H__ */
