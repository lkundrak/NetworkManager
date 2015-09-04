/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2012 - 2014 Red Hat, Inc.
 */

#ifndef __NM_DEVICE_GRE_H__
#define __NM_DEVICE_GRE_H__

#if !defined (__NETWORKMANAGER_H_INSIDE__) && !defined (NETWORKMANAGER_COMPILATION)
#error "Only <NetworkManager.h> can be included directly."
#endif

#include <nm-device.h>

G_BEGIN_DECLS

#define NM_TYPE_DEVICE_GRE            (nm_device_gre_get_type ())
#define NM_DEVICE_GRE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_DEVICE_GRE, NMDeviceGre))
#define NM_DEVICE_GRE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_DEVICE_GRE, NMDeviceGreClass))
#define NM_IS_DEVICE_GRE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_DEVICE_GRE))
#define NM_IS_DEVICE_GRE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_DEVICE_GRE))
#define NM_DEVICE_GRE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_DEVICE_GRE, NMDeviceGreClass))

#define NM_DEVICE_GRE_PARENT      "parent"
#define NM_DEVICE_GRE_LOCAL       "local"
#define NM_DEVICE_GRE_REMOTE      "remote"
#define NM_DEVICE_GRE_TTL         "ttl"

struct _NMDeviceGre {
	NMDevice parent;
};

typedef struct {
	NMDeviceClass parent;

	/*< private >*/
	gpointer padding[4];
} NMDeviceGreClass;

GType nm_device_gre_get_type (void);

NMDevice *   nm_device_gre_get_parent  (NMDeviceGre *device);
char *       nm_device_gre_get_local   (NMDeviceGre *device);
char *       nm_device_gre_get_remote  (NMDeviceGre *device);
guint8       nm_device_gre_get_ttl     (NMDeviceGre *device);

G_END_DECLS

#endif /* __NM_DEVICE_GRE_H__ */
