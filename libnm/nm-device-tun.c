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
#include <nm-setting-tun.h>
#include <nm-utils.h>

#include "nm-default.h"
#include "nm-device-tun.h"
#include "nm-device-private.h"
#include "nm-object-private.h"

G_DEFINE_TYPE (NMDeviceTun, nm_device_tun, NM_TYPE_DEVICE)

#define NM_DEVICE_TUN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_TUN, NMDeviceTunPrivate))

typedef struct {
	char *user;
	char *group;
	char *mode;
	gboolean pi;
	gboolean vnet_hdr;
	gboolean multi_queue;
} NMDeviceTunPrivate;

enum {
	PROP_0,
	PROP_USER,
	PROP_GROUP,
	PROP_MODE,
	PROP_PI,
	PROP_VNET_HDR,
	PROP_MULTI_QUEUE,

	LAST_PROP
};

/**
 * nm_device_tun_get_user:
 * @device: a #NMDeviceTun
 *
 * Gets the owner the #NMDeviceTun
 *
 * Returns: device owner
 *
 * Since: 1.2
 **/
const char *
nm_device_tun_get_user (NMDeviceTun *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_TUN (device), NULL);

	return NM_DEVICE_TUN_GET_PRIVATE (device)->user;
}

/**
 * nm_device_tun_get_group:
 * @device: a #NMDeviceTun
 *
 * Gets the owner group the #NMDeviceTun
 *
 * Returns: device owner group
 *
 * Since: 1.2
 **/
const char *
nm_device_tun_get_group (NMDeviceTun *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_TUN (device), NULL);

	return NM_DEVICE_TUN_GET_PRIVATE (device)->group;
}

/**
 * nm_device_tun_get_mode:
 * @device: a #NMDeviceTun
 *
 * Returns the TUN/TAP mode for the device.
 *
 * Returns: 'tun' or 'tap'
 *
 * Since: 1.2
 **/
const char *
nm_device_tun_get_mode (NMDeviceTun *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_TUN (device), NULL);

	return NM_DEVICE_TUN_GET_PRIVATE (device)->mode;
}

/**
 * nm_device_tun_get_pi:
 * @device: a #NMDeviceTun
 *
 * Returns whether the #NMDeviceTun has the IFF_NO_PI flag.
 *
 * Returns: %TRUE if the device doesn't have the flag, %FALSE otherwise
 *
 * Since: 1.2
 **/
gboolean
nm_device_tun_get_pi (NMDeviceTun *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_TUN (device), FALSE);

	return NM_DEVICE_TUN_GET_PRIVATE (device)->pi;
}

/**
 * nm_device_tun_get_vnet_hdr:
 * @device: a #NMDeviceTun
 *
 * Returns whether the #NMDeviceTun has the IFF_VNET_HDR flag.
 *
 * Returns: %TRUE if the device has the flag, %FALSE otherwise
 *
 * Since: 1.2
 **/
gboolean
nm_device_tun_get_vnet_hdr (NMDeviceTun *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_TUN (device), FALSE);

	return NM_DEVICE_TUN_GET_PRIVATE (device)->vnet_hdr;
}

/**
 * nm_device_tun_get_multi_queue:
 * @device: a #NMDeviceTun
 *
 * Returns whether the #NMDeviceTun has the IFF_MULTI_QUEUE flag.
 *
 * Returns: %TRUE if the device doesn't have the flag, %FALSE otherwise
 *
 * Since: 1.2
 **/
gboolean
nm_device_tun_get_multi_queue (NMDeviceTun *device)
{
	g_return_val_if_fail (NM_IS_DEVICE_TUN (device), FALSE);

	return NM_DEVICE_TUN_GET_PRIVATE (device)->multi_queue;
}

static gboolean
connection_compatible (NMDevice *device, NMConnection *connection, GError **error)
{
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (device);
	NMSettingTun *s_tun;

	if (!NM_DEVICE_CLASS (nm_device_tun_parent_class)->connection_compatible (device, connection, error))
		return FALSE;

	if (!nm_connection_is_type (connection, NM_SETTING_TUN_SETTING_NAME)) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		                     _("The connection was not a tun connection."));
		return FALSE;
	}

	s_tun = nm_connection_get_setting_tun (connection);

	if (s_tun && nm_setting_tun_get_tap (s_tun) != (!g_strcmp0 (priv->mode, "tap"))) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INCOMPATIBLE_CONNECTION,
		                     _("The mode of the device and the connection didn't match"));
		return FALSE;
	}

	return TRUE;
}

static GType
get_setting_type (NMDevice *device)
{
	return NM_TYPE_SETTING_TUN;
}

/***********************************************************/

static void
nm_device_tun_init (NMDeviceTun *device)
{
	_nm_device_set_device_type (NM_DEVICE (device), NM_DEVICE_TYPE_TUN);
}

static void
init_dbus (NMObject *object)
{
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (object);
	const NMPropertiesInfo property_info[] = {
		{ NM_DEVICE_TUN_USER,        &priv->user },
		{ NM_DEVICE_TUN_GROUP,       &priv->group },
		{ NM_DEVICE_TUN_MODE,        &priv->mode },
		{ NM_DEVICE_TUN_PI,          &priv->pi },
		{ NM_DEVICE_TUN_VNET_HDR,    &priv->vnet_hdr },
		{ NM_DEVICE_TUN_MULTI_QUEUE, &priv->multi_queue },
		{ NULL },
	};

	NM_OBJECT_CLASS (nm_device_tun_parent_class)->init_dbus (object);

	_nm_object_register_properties (object,
	                                NM_DBUS_INTERFACE_DEVICE_TUN,
	                                property_info);
}

static void
finalize (GObject *object)
{
	NMDeviceTunPrivate *priv = NM_DEVICE_TUN_GET_PRIVATE (object);

	g_free (priv->user);
	g_free (priv->group);

	G_OBJECT_CLASS (nm_device_tun_parent_class)->finalize (object);
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	NMDeviceTun *device = NM_DEVICE_TUN (object);

	switch (prop_id) {
	case PROP_USER:
		g_value_set_string (value, nm_device_tun_get_user (device));
		break;
	case PROP_GROUP:
		g_value_set_string (value, nm_device_tun_get_group (device));
		break;
	case PROP_MODE:
		g_value_set_string (value, nm_device_tun_get_mode (device));
		break;
	case PROP_PI:
		g_value_set_boolean (value, nm_device_tun_get_pi (device));
		break;
	case PROP_VNET_HDR:
		g_value_set_boolean (value, nm_device_tun_get_vnet_hdr (device));
		break;
	case PROP_MULTI_QUEUE:
		g_value_set_boolean (value, nm_device_tun_get_multi_queue (device));
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_tun_class_init (NMDeviceTunClass *gre_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (gre_class);
	NMObjectClass *nm_object_class = NM_OBJECT_CLASS (gre_class);
	NMDeviceClass *device_class = NM_DEVICE_CLASS (gre_class);

	g_type_class_add_private (gre_class, sizeof (NMDeviceTunPrivate));

	_nm_object_class_add_interface (nm_object_class, NM_DBUS_INTERFACE_DEVICE_TUN);

	/* virtual methods */
	object_class->finalize = finalize;
	object_class->get_property = get_property;

	nm_object_class->init_dbus = init_dbus;

	device_class->connection_compatible = connection_compatible;
	device_class->get_setting_type = get_setting_type;

	/* properties */

	/**
	 * NMDeviceTun:user:
	 *
	 * The owner of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_USER,
		 g_param_spec_string (NM_DEVICE_TUN_USER, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDeviceTun:group:
	 *
	 * The owner group of the device.
	 **/
	g_object_class_install_property
		(object_class, PROP_GROUP,
		 g_param_spec_string (NM_DEVICE_TUN_GROUP, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDeviceTun:mode:
	 *
	 * Whether the device TUN/TAP mode.
	 **/
	g_object_class_install_property
		(object_class, PROP_MODE,
		 g_param_spec_string (NM_DEVICE_TUN_MODE, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMDeviceTun:pi:
	 *
	 * Whether the IFF_NO_PI is set on the interface.
	 **/
	g_object_class_install_property
		(object_class, PROP_PI,
		 g_param_spec_boolean (NM_DEVICE_TUN_PI, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMDeviceTun:vnet-hdr:
	 *
	 * Whether the IFF_VNET_HDR flag is set on the interface.
	 **/
	g_object_class_install_property
		(object_class, PROP_VNET_HDR,
		 g_param_spec_boolean (NM_DEVICE_TUN_VNET_HDR, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));

	/**
	 * NMDeviceTun:multi-queue:
	 *
	 * Whether the IFF_MULTI_QUEUE flag is set on the interface.
	 **/
	g_object_class_install_property
		(object_class, PROP_MULTI_QUEUE,
		 g_param_spec_boolean (NM_DEVICE_TUN_MULTI_QUEUE, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));
}
