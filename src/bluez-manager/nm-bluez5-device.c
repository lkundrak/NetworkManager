/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager -- Network link manager
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2013 Intel Corporation.
 */

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <net/ethernet.h>
#include <netinet/ether.h>

#include "NetworkManager.h"
#include "nm-setting-bluetooth.h"

#include "nm-bluez-device.h"
#include "nm-bluez-common.h"
#include "nm-logging.h"


G_DEFINE_TYPE (NMBluezDevice, nm_bluez_device, G_TYPE_OBJECT)

#define NM_BLUEZ_DEVICE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_BLUEZ_DEVICE, NMBluezDevicePrivate))

typedef struct {
	char *path;
	GDBusProxy *proxy;
	GDBusProxy *adapter;
	GDBusConnection *connection;

	gboolean initialized;
	gboolean usable;
	NMBluetoothCapabilities connection_bt_type;

	char *address;
	guint8 bin_address[ETH_ALEN];
	char *name;
	guint32 capabilities;
	gint rssi;
	gboolean connected;

	char *bt_iface;
} NMBluezDevicePrivate;


enum {
	PROP_0,
	PROP_PATH,
	PROP_ADDRESS,
	PROP_NAME,
	PROP_CAPABILITIES,
	PROP_RSSI,
	PROP_USABLE,
	PROP_CONNECTED,

	LAST_PROP
};

/* Signals */
enum {
	INITIALIZED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

/***********************************************************/

const char *
nm_bluez_device_get_path (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), NULL);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->path;
}

const char *
nm_bluez_device_get_address (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), NULL);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->address;
}

gboolean
nm_bluez_device_get_initialized (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), FALSE);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->initialized;
}

gboolean
nm_bluez_device_get_usable (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), FALSE);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->usable;
}

const char *
nm_bluez_device_get_name (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), NULL);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->name;
}

guint32
nm_bluez_device_get_capabilities (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), 0);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->capabilities;
}

gint
nm_bluez_device_get_rssi (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), 0);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->rssi;
}

gboolean
nm_bluez_device_get_connected (NMBluezDevice *self)
{
	g_return_val_if_fail (NM_IS_BLUEZ_DEVICE (self), FALSE);

	return NM_BLUEZ_DEVICE_GET_PRIVATE (self)->connected;
}

static void
check_emit_usable (NMBluezDevice *self)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	gboolean new_usable;

	new_usable = (priv->initialized && priv->capabilities && priv->name &&
	              priv->address && priv->adapter && priv->connection);
	if (new_usable != priv->usable) {
		priv->usable = new_usable;
		g_object_notify (G_OBJECT (self), NM_BLUEZ_DEVICE_USABLE);
	}
}

/********************************************************************/

void
nm_bluez_device_call_disconnect (NMBluezDevice *self)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);

	g_return_if_fail (priv->connection);
	g_return_if_fail (priv->connection_bt_type == NM_BT_CAPABILITY_NAP);

	g_dbus_connection_call (priv->connection,
	                        BLUEZ_SERVICE,
	                        priv->path,
	                        BLUEZ_NETWORK_INTERFACE,
	                        "Disconnect",
	                        g_variant_new ("()"),
	                        NULL,
	                        G_DBUS_CALL_FLAGS_NONE,
	                        -1,
	                        NULL, NULL, NULL);

	priv->connection_bt_type = NM_BT_CAPABILITY_NONE;
}

static void
bluez_connect_pan_cb (GDBusConnection *connection,
                      GAsyncResult *res,
                      gpointer user_data)
{
	GSimpleAsyncResult *result = user_data;
	NMBluezDevice *self = NM_BLUEZ_DEVICE (g_async_result_get_source_object (G_ASYNC_RESULT (result)));
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	GVariant *variant;
	GError *error = NULL;
	char *device;

	variant = g_dbus_connection_call_finish (connection, res, &error);

	if (!variant) {
		g_simple_async_result_take_error (result, error);
	} else {
		g_variant_get (variant, "(s)", &device);

		g_simple_async_result_set_op_res_gpointer (result,
		                                           g_strdup (device),
		                                           g_free);
		priv->bt_iface = device;
		g_variant_unref (variant);
	}

	g_simple_async_result_complete (result);
	g_object_unref (result);
}

void
nm_bluez_device_connect_async (NMBluezDevice *self,
                               NMBluetoothCapabilities connection_bt_type,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *simple;
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);

	g_return_if_fail (connection_bt_type == NM_BT_CAPABILITY_NAP);

	simple = g_simple_async_result_new (G_OBJECT (self),
	                                    callback,
	                                    user_data,
	                                    nm_bluez_device_connect_async);

	/* For PAN we call Connect() on org.bluez.Network1 */
	g_dbus_connection_call (priv->connection,
	                        BLUEZ_SERVICE,
	                        priv->path,
	                        BLUEZ_NETWORK_INTERFACE,
	                        "Connect",
	                        g_variant_new ("(s)", BLUETOOTH_CONNECT_NAP),
	                        NULL,
	                        G_DBUS_CALL_FLAGS_NONE,
	                        20000,
	                        NULL,
	                        (GAsyncReadyCallback) bluez_connect_pan_cb,
	                        simple);

	priv->connection_bt_type = connection_bt_type;
}

const char *
nm_bluez_device_connect_finish (NMBluezDevice *self,
                                GAsyncResult *result,
                                GError **error)
{
	GSimpleAsyncResult *simple;
	const char *device;

	g_return_val_if_fail (g_simple_async_result_is_valid (result,
	                                                      G_OBJECT (self),
	                                                      nm_bluez_device_connect_async),
	                      NULL);

	simple = (GSimpleAsyncResult *) result;

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	device = (const char *) g_simple_async_result_get_op_res_gpointer (simple);
	return device;
}

/***********************************************************/

static guint32
convert_uuids_to_capabilities (const char **strings)
{
	const char **iter;
	guint32 capabilities = 0;

	for (iter = strings; iter && *iter; iter++) {
		char **parts;

		parts = g_strsplit (*iter, "-", -1);
		if (parts && parts[0]) {
			switch (g_ascii_strtoull (parts[0], NULL, 16)) {
			case 0x1116:
				capabilities |= NM_BT_CAPABILITY_NAP;
				break;
			default:
				break;
			}
		}
		g_strfreev (parts);
	}

	return capabilities;
}

static void
on_adapter_acquired (GObject *object, GAsyncResult *res, NMBluezDevice *self)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	GError *error;

	priv->adapter = g_dbus_proxy_new_for_bus_finish (res, &error);

	if (!priv->adapter) {
		nm_log_warn (LOGD_BT, "failed to acquire adapter proxy: %s.",
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
		return;
	}

	check_emit_usable (self);
}

static void
properties_changed (GDBusProxy *proxy,
                    GVariant *changed_properties,
                    GStrv invalidated_properties,
                    gpointer user_data)
{
	NMBluezDevice *self = NM_BLUEZ_DEVICE (user_data);
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	GVariantIter i;
	const char *property;
	const char *str;
	GVariant *v;
	guint32 uint_val;
	gint int_val;
	const char **strv;

	g_variant_iter_init (&i, changed_properties);
	while (g_variant_iter_next (&i, "{&sv}", &property, &v)) {
		if (!strcmp (property, "Name")) {
			str = g_variant_get_string (v, NULL);
			if (g_strcmp0 (priv->name, str)) {
				g_free (priv->name);
				priv->name = g_strdup (str);
				g_object_notify (G_OBJECT (self), NM_BLUEZ_DEVICE_NAME);
			}
		} else if (!strcmp (property, "RSSI")) {
			int_val = g_variant_get_int16 (v);
			if (priv->rssi != int_val) {
				priv->rssi = int_val;
				g_object_notify (G_OBJECT (self), NM_BLUEZ_DEVICE_RSSI);
			}
		} else if (!strcmp (property, "UUIDs")) {
			strv = g_variant_get_strv (v, NULL);
			uint_val = convert_uuids_to_capabilities (strv);
			g_free (strv);
			if (priv->capabilities != uint_val) {
				priv->capabilities = uint_val;
				g_object_notify (G_OBJECT (self), NM_BLUEZ_DEVICE_CAPABILITIES);
			}
		} else if (!strcmp (property, "Connected")) {
			gboolean connected = g_variant_get_boolean (v);
			if (priv->connected != connected) {
				priv->connected = connected;
				g_object_notify (G_OBJECT (self), NM_BLUEZ_DEVICE_CONNECTED);
			}
		}
		g_variant_unref (v);
	}

	check_emit_usable (self);
}

static void
query_properties (NMBluezDevice *self)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	GVariant *v;
	const char **uuids;
	struct ether_addr *tmp;

	v = g_dbus_proxy_get_cached_property (priv->proxy, "Address");
	priv->address = v ? g_variant_dup_string (v, NULL) : NULL;
	if (v)
		g_variant_unref (v);
	if (priv->address) {
		tmp = ether_aton (priv->address);
		g_assert (tmp);
		memcpy (priv->bin_address, tmp->ether_addr_octet, ETH_ALEN);
	}

	v = g_dbus_proxy_get_cached_property (priv->proxy, "Name");
	priv->name = v ? g_variant_dup_string (v, NULL) : NULL;
	if (v)
		g_variant_unref (v);

	v = g_dbus_proxy_get_cached_property (priv->proxy, "RSSI");
	priv->rssi = v ? g_variant_get_int16 (v) : 0;
	if (v)
		g_variant_unref (v);

	v = g_dbus_proxy_get_cached_property (priv->proxy, "UUIDs");
	if (v) {
		uuids = g_variant_get_strv (v, NULL);
		priv->capabilities = convert_uuids_to_capabilities (uuids);
		g_variant_unref (v);
	} else
		priv->capabilities = NM_BT_CAPABILITY_NONE;

	v = g_dbus_proxy_get_cached_property (priv->proxy, "Adapter");
	if (v) {
		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
		                          G_DBUS_PROXY_FLAGS_NONE,
		                          NULL,
		                          BLUEZ_SERVICE,
		                          g_variant_get_string (v, NULL),
		                          BLUEZ_ADAPTER_INTERFACE,
		                          NULL,
		                          (GAsyncReadyCallback) on_adapter_acquired,
		                          self);
		g_variant_unref (v);
	}

	priv->initialized = TRUE;
	g_signal_emit (self, signals[INITIALIZED], 0, TRUE);

	check_emit_usable (self);
}

static void
on_proxy_acquired (GObject *object, GAsyncResult *res, NMBluezDevice *self)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	GError *error;

	priv->proxy = g_dbus_proxy_new_for_bus_finish (res, &error);

	if (!priv->proxy) {
		nm_log_warn (LOGD_BT, "failed to acquire device proxy: %s.",
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
		g_signal_emit (self, signals[INITIALIZED], 0, FALSE);
		return;
	}

	g_signal_connect (priv->proxy, "g-properties-changed",
	                  G_CALLBACK (properties_changed), self);

	query_properties (self);
}

static void
on_bus_acquired (GObject *object, GAsyncResult *res, NMBluezDevice *self)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);
	GError *error = NULL;

	priv->connection = g_bus_get_finish (res, &error);

	if (!priv->connection) {
		nm_log_warn (LOGD_BT, "failed to acquire bus connection: %s.",
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
		g_signal_emit (self, signals[INITIALIZED], 0, FALSE);
		return;
	}

	check_emit_usable (self);
}

/********************************************************************/

NMBluezDevice *
nm_bluez_device_new (const char *path)
{
	NMBluezDevice *self;
	NMBluezDevicePrivate *priv;

	g_return_val_if_fail (path != NULL, NULL);

	self = (NMBluezDevice *) g_object_new (NM_TYPE_BLUEZ_DEVICE,
	                                       NM_BLUEZ_DEVICE_PATH, path,
	                                       NULL);
	if (!self)
		return NULL;

	priv = NM_BLUEZ_DEVICE_GET_PRIVATE (self);

	g_bus_get (G_BUS_TYPE_SYSTEM,
	           NULL,
	           (GAsyncReadyCallback) on_bus_acquired,
	           self);

	g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
	                          G_DBUS_PROXY_FLAGS_NONE,
	                          NULL,
	                          BLUEZ_SERVICE,
	                          priv->path,
	                          BLUEZ_DEVICE_INTERFACE,
	                          NULL,
	                          (GAsyncReadyCallback) on_proxy_acquired,
	                          self);

	return self;
}

static void
nm_bluez_device_init (NMBluezDevice *self)
{
}

static void
dispose (GObject *object)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (object);

	g_clear_object (&priv->adapter);
	g_clear_object (&priv->connection);

	G_OBJECT_CLASS (nm_bluez_device_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (object);

	g_free (priv->path);
	g_free (priv->address);
	g_free (priv->name);
	g_free (priv->bt_iface);
	g_object_unref (priv->proxy);

	G_OBJECT_CLASS (nm_bluez_device_parent_class)->finalize (object);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PATH:
		g_value_set_string (value, priv->path);
		break;
	case PROP_ADDRESS:
		g_value_set_string (value, priv->address);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_CAPABILITIES:
		g_value_set_uint (value, priv->capabilities);
		break;
	case PROP_RSSI:
		g_value_set_int (value, priv->rssi);
		break;
	case PROP_USABLE:
		g_value_set_boolean (value, priv->usable);
		break;
	case PROP_CONNECTED:
		g_value_set_boolean (value, priv->connected);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMBluezDevicePrivate *priv = NM_BLUEZ_DEVICE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_PATH:
		/* construct only */
		priv->path = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_bluez_device_class_init (NMBluezDeviceClass *config_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (config_class);

	g_type_class_add_private (config_class, sizeof (NMBluezDevicePrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->finalize = finalize;
	object_class->dispose = dispose;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_PATH,
		 g_param_spec_string (NM_BLUEZ_DEVICE_PATH,
		                      "DBus Path",
		                      "DBus Path",
		                      NULL,
		                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_ADDRESS,
		 g_param_spec_string (NM_BLUEZ_DEVICE_ADDRESS,
		                      "Address",
		                      "Address",
		                      NULL,
		                      G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_NAME,
		 g_param_spec_string (NM_BLUEZ_DEVICE_NAME,
		                      "Name",
		                      "Name",
		                      NULL,
		                      G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_CAPABILITIES,
		 g_param_spec_uint (NM_BLUEZ_DEVICE_CAPABILITIES,
		                      "Capabilities",
		                      "Capabilities",
		                      0, G_MAXUINT, 0,
		                      G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_RSSI,
		 g_param_spec_int (NM_BLUEZ_DEVICE_RSSI,
		                      "RSSI",
		                      "RSSI",
		                      G_MININT, G_MAXINT, 0,
		                      G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_USABLE,
		 g_param_spec_boolean (NM_BLUEZ_DEVICE_USABLE,
		                       "Usable",
		                       "Usable",
		                       FALSE,
		                       G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_CONNECTED,
		 g_param_spec_boolean (NM_BLUEZ_DEVICE_CONNECTED,
		                       "Connected",
		                       "Connected",
		                       FALSE,
		                       G_PARAM_READABLE));

	/* Signals */
	signals[INITIALIZED] = g_signal_new ("initialized",
	                                     G_OBJECT_CLASS_TYPE (object_class),
	                                     G_SIGNAL_RUN_LAST,
	                                     G_STRUCT_OFFSET (NMBluezDeviceClass, initialized),
	                                     NULL, NULL, NULL,
	                                     G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}
