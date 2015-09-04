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
 * Copyright 2015 Red Hat, Inc.
 */

#include "config.h"

#include <string.h>
#include <arpa/inet.h>

#include <nm-setting-connection.h>
#include <nm-setting-tunnel.h>
#include <nm-utils.h>

#include "nm-default.h"
#include "nm-device-gre.h"
#include "nm-device-private.h"
#include "nm-object-private.h"

G_DEFINE_TYPE (NMDeviceGre, nm_device_gre, NM_TYPE_DEVICE)

#define NM_DEVICE_GRE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_GRE, NMDeviceGrePrivate))

typedef struct {
	NMDevice *parent;
	char *local;
	char *remote;
	guint8 ttl;
} NMDeviceGrePrivate;

enum {
	PROP_0,
	PROP_PARENT,
	PROP_LOCAL,
	PROP_REMOTE,
	PROP_TTL,

	LAST_PROP
};

/**
 * nm_device_gre_get_parent:
 * @device: a #NMDeviceGre
 *
 * Returns: (transfer none): the device's parent device
 **/
NMDevice *
nm_device_gre_get_parent (NMDeviceGre *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_GRE (device), FALSE);

	return NM_DEVICE_GRE_GET_PRIVATE (device)->parent;
}

/**
 * nm_device_gre_get_local:
 * @device: a #NMDeviceGre
 *
 * Gets the local endpoint of the #NMDeviceGre
 *
 * Returns: the local endpoint.
 **/
char *
nm_device_gre_get_local (NMDeviceGre *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_GRE (device), NULL);

	return NM_DEVICE_GRE_GET_PRIVATE (device)->local;
}

/**
 * nm_device_gre_get_remote:
 * @device: a #NMDeviceGre
 *
 * Gets the remote endpoint of the #NMDeviceGre
 *
 * Returns: the remote endpoint.
 **/
char *
nm_device_gre_get_remote (NMDeviceGre *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_GRE (device), NULL);

	return NM_DEVICE_GRE_GET_PRIVATE (device)->remote;
}

/**
 * nm_device_gre_get_ttl:
 * @device: a #NMDeviceGre
 *
 * Gets the TTL the #NMDeviceGre
 *
 * Returns: the TTL.
 **/
guint8
nm_device_gre_get_ttl (NMDeviceGre *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_GRE (device), 0);

	return NM_DEVICE_GRE_GET_PRIVATE (device)->ttl;
}

static gboolean
ip4_addr_match (const char *str1, const char *str2)
{
	in_addr_t a1, a2;

	if (!str1 && !str2)
		return TRUE;

	if (!str1 || !str2)
		return FALSE;

	if (inet_pton (AF_INET, str1, &a1) != 1)
		return FALSE;

	if (inet_pton (AF_INET, str2, &a2) != 1)
		return FALSE;

	return a1 == a2;
}

static gboolean
connection_compatible (NMDevice *device, NMConnection *connection, GError **error)
{
	NMDeviceGrePrivate *priv = NM_DEVICE_GRE_GET_PRIVATE (device);
	NMSettingTunnel *s_tunnel;


	if (!NM_DEVICE_CLASS (nm_device_gre_parent_class)->connection_compatible (device, connection, error))
		return FALSE;


	if (!nm_connection_is_type (connection, NM_SETTING_TUNNEL_SETTING_NAME)) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		                     _("The connection was not a tunnel connection."));
		return FALSE;
	}

	s_tunnel = nm_connection_get_setting_tunnel (connection);

	if (!ip4_addr_match (nm_setting_tunnel_get_local (s_tunnel), priv->local)) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		                     _("The local endpoints of the device and the connection didn't match"));
		return FALSE;
	}

	if (!ip4_addr_match (nm_setting_tunnel_get_remote (s_tunnel), priv->remote)) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		                     _("The remote endpoints of the device and the connection didn't match"));
		return FALSE;
	}

	if (nm_setting_tunnel_get_ttl (s_tunnel) != priv->ttl) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		                     _("The TTL of the device and the connection didn't match"));
		return FALSE;
	}

	return TRUE;
}

static GType
get_setting_type (NMDevice *device)
{
	return NM_TYPE_SETTING_TUNNEL;
}

/***********************************************************/

static void
nm_device_gre_init (NMDeviceGre *device)
{
	_nm_device_set_device_type (NM_DEVICE (device), NM_DEVICE_TYPE_GRE);
}

static void
init_dbus (NMObject *object)
{
	NMDeviceGrePrivate *priv = NM_DEVICE_GRE_GET_PRIVATE (object);
	const NMPropertiesInfo property_info[] = {
		{ NM_DEVICE_GRE_PARENT,     &priv->parent, NULL, NM_TYPE_DEVICE },
		{ NM_DEVICE_GRE_LOCAL,      &priv->local },
		{ NM_DEVICE_GRE_REMOTE,     &priv->remote },
		{ NM_DEVICE_GRE_TTL,        &priv->ttl },
		{ NULL },
	};

	NM_OBJECT_CLASS (nm_device_gre_parent_class)->init_dbus (object);

	_nm_object_register_properties (object,
	                                NM_DBUS_INTERFACE_DEVICE_GRE,
	                                property_info);
}

static void
finalize (GObject *object)
{
	NMDeviceGrePrivate *priv = NM_DEVICE_GRE_GET_PRIVATE (object);

	g_free (priv->local);
	g_free (priv->remote);
	g_clear_object (&priv->parent);

	G_OBJECT_CLASS (nm_device_gre_parent_class)->finalize (object);
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	NMDeviceGre *device = NM_DEVICE_GRE (object);

	switch (prop_id) {
	case PROP_LOCAL:
		g_value_set_string (value, nm_device_gre_get_local (device));
		break;
	case PROP_REMOTE:
		g_value_set_string (value, nm_device_gre_get_remote (device));
		break;
	case PROP_PARENT:
		g_value_set_object (value, nm_device_gre_get_parent (device));
		break;
	case PROP_TTL:
		g_value_set_uint (value, nm_device_gre_get_ttl (device));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_gre_class_init (NMDeviceGreClass *gre_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (gre_class);
	NMObjectClass *nm_object_class = NM_OBJECT_CLASS (gre_class);
	NMDeviceClass *device_class = NM_DEVICE_CLASS (gre_class);

	g_type_class_add_private (gre_class, sizeof (NMDeviceGrePrivate));

	_nm_object_class_add_interface (nm_object_class, NM_DBUS_INTERFACE_DEVICE_GRE);

	/* virtual methods */
	object_class->finalize = finalize;
	object_class->get_property = get_property;

	nm_object_class->init_dbus = init_dbus;

	device_class->connection_compatible = connection_compatible;
	device_class->get_setting_type = get_setting_type;

	/* properties */

	/**
	 * NMDeviceGre:parent:
	 *
	 * The devices's parent device.
	 **/
	g_object_class_install_property
	    (object_class, PROP_PARENT,
	     g_param_spec_object (NM_DEVICE_GRE_PARENT, "", "",
	                          NM_TYPE_DEVICE,
	                          G_PARAM_READABLE |
	                          G_PARAM_STATIC_STRINGS));

	/**
	 * NMDeviceGre:local:
	 *
	 * The local endpoint.
	 **/
	g_object_class_install_property
		(object_class, PROP_LOCAL,
		 g_param_spec_string (NM_DEVICE_GRE_LOCAL, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));
	/**
	 * NMDeviceGre:remote:
	 *
	 * The remote endpoint.
	 **/
	g_object_class_install_property
		(object_class, PROP_REMOTE,
		 g_param_spec_string (NM_DEVICE_GRE_REMOTE, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));
	/**
	 * NMDeviceGre:ttl:
	 *
	 * The time-to-live.
	 **/
	g_object_class_install_property
		(object_class, PROP_TTL,
		 g_param_spec_uint (NM_DEVICE_GRE_TTL, "", "",
		                    0, 255, 0,
		                    G_PARAM_READABLE |
		                    G_PARAM_STATIC_STRINGS));
}
