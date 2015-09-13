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
 * Copyright 2015 Red Hat, Inc.
 */

#include "nm-device-ip-tunnel.h"

#include "config.h"

#include <string.h>

#include "nm-device-private.h"
#include "nm-device-factory.h"
#include "nm-default.h"
#include "nm-manager.h"
#include "nm-platform.h"

#include "nmdbus-device-ip-tunnel.h"

#include "nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceIPTunnel);

G_DEFINE_TYPE (NMDeviceIPTunnel, nm_device_ip_tunnel, NM_TYPE_DEVICE)

#define NM_DEVICE_IP_TUNNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_IP_TUNNEL, NMDeviceIPTunnelPrivate))

typedef struct {
	int parent_ifindex;
	char *local;
	char *remote;
	guint8 ttl;
	guint8 tos;
	gboolean path_mtu_discovery;
	NMLinkType link_type;
} NMDeviceIPTunnelPrivate;

enum {
	PROP_0,
	PROP_PARENT,
	PROP_LOCAL,
	PROP_REMOTE,
	PROP_TTL,
	PROP_TOS,
	PROP_PATH_MTU_DISCOVERY,
	LAST_PROP
};

static void
update_properties (NMDeviceIPTunnel *self)
{
	NMDeviceIPTunnelPrivate *priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (self);
	GObject *object = G_OBJECT (self);
	NMPlatformIPTunnelProperties props;
	char buf[INET6_ADDRSTRLEN];

	if (!nm_platform_ip_tunnel_get_properties (NM_PLATFORM_GET, priv->link_type,
		                                       nm_device_get_ifindex (NM_DEVICE (self)),
		                                       &props)) {
		_LOGW (LOGD_HW, "could not read IP tunnel properties");
		return;
	}

	g_object_freeze_notify (object);

	if (priv->parent_ifindex != props.parent_ifindex) {
		priv->parent_ifindex = props.parent_ifindex;
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_PARENT);
	}

	if (props.encap == AF_INET)
		inet_ntop (AF_INET, &props.local4, buf, sizeof (buf));
	else
		inet_ntop (AF_INET6, &props.local6, buf, sizeof (buf));

	if (!g_strcmp0 (priv->local, buf)) {
		g_free (priv->local);
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_LOCAL);
	}

	if (props.encap == AF_INET)
		inet_ntop (AF_INET, &props.remote4, buf, sizeof (buf));
	else
		inet_ntop (AF_INET6, &props.remote6, buf, sizeof (buf));

	if (!g_strcmp0 (priv->remote, buf)) {
		g_free (priv->remote);
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_REMOTE);
	}

	if (priv->ttl != props.ttl) {
		priv->ttl = props.ttl;
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_TTL);
	}

	if (priv->tos != props.tos) {
		priv->tos = props.tos;
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_TOS);
	}

	if (priv->path_mtu_discovery != props.path_mtu_discovery) {
		priv->path_mtu_discovery = props.path_mtu_discovery;
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_PATH_MTU_DISCOVERY);
	}

	g_object_thaw_notify (object);
}

static void
link_changed (NMDevice *device, NMPlatformLink *info)
{
	update_properties (NM_DEVICE_IP_TUNNEL (device));
}

static void
nm_device_ip_tunnel_init (NMDeviceIPTunnel *self)
{
}

static void
constructed (GObject *object)
{
	update_properties (NM_DEVICE_IP_TUNNEL (object));
	G_OBJECT_CLASS (nm_device_ip_tunnel_parent_class)->constructed (object);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDeviceIPTunnelPrivate *priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (object);
	NMDevice *parent;

	switch (prop_id) {
	case PROP_PARENT:
		parent = nm_manager_get_device_by_ifindex (nm_manager_get (), priv->parent_ifindex);
		nm_utils_g_value_set_object_path (value, parent);
		break;
	case PROP_LOCAL:
		g_value_set_string (value, priv->local);
		break;
	case PROP_REMOTE:
		g_value_set_string (value, priv->remote);
		break;
	case PROP_TTL:
		g_value_set_uchar (value, priv->ttl);
		break;
	case PROP_TOS:
		g_value_set_uchar (value, priv->tos);
		break;
	case PROP_PATH_MTU_DISCOVERY:
		g_value_set_boolean (value, priv->path_mtu_discovery);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_device_ip_tunnel_class_init (NMDeviceIPTunnelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	NMDeviceClass *device_class = NM_DEVICE_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMDeviceIPTunnelPrivate));

	object_class->constructed = constructed;
	object_class->get_property = get_property;

	device_class->link_changed = link_changed;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_PARENT,
		 g_param_spec_string (NM_DEVICE_IP_TUNNEL_PARENT, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_LOCAL,
		 g_param_spec_string (NM_DEVICE_IP_TUNNEL_LOCAL, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_REMOTE,
		 g_param_spec_string (NM_DEVICE_IP_TUNNEL_REMOTE, "", "",
		                      NULL,
		                      G_PARAM_READABLE |
		                      G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_TTL,
		 g_param_spec_uchar (NM_DEVICE_IP_TUNNEL_TTL, "", "",
		                     0, 255, 0,
		                     G_PARAM_READABLE |
		                     G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_TOS,
		 g_param_spec_uchar (NM_DEVICE_IP_TUNNEL_TOS, "", "",
		                     0, 255, 0,
		                     G_PARAM_READABLE |
		                     G_PARAM_STATIC_STRINGS));

	g_object_class_install_property
		(object_class, PROP_PATH_MTU_DISCOVERY,
		 g_param_spec_boolean (NM_DEVICE_IP_TUNNEL_PATH_MTU_DISCOVERY, "", "",
		                       FALSE,
		                       G_PARAM_READABLE |
		                       G_PARAM_STATIC_STRINGS));

	nm_exported_object_class_add_interface (NM_EXPORTED_OBJECT_CLASS (klass),
	                                        NMDBUS_TYPE_DEVICE_IPTUNNEL_SKELETON,
	                                        NULL);
}

#define NM_TYPE_IP_TUNNEL_FACTORY (nm_ip_tunnel_factory_get_type ())
#define NM_IP_TUNNEL_FACTORY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_IP_TUNNEL_FACTORY, NMIPTunnelFactory))

static NMDevice *
create_device (NMDeviceFactory *factory,
               const char *iface,
               NMPlatformLink *plink,
               NMConnection *connection,
               gboolean *out_ignore)
{
	NMDeviceIPTunnel *self;
	NMDeviceIPTunnelPrivate *priv;

	self = g_object_new (NM_TYPE_DEVICE_IP_TUNNEL,
	                     NM_DEVICE_IFACE, iface,
	                     NM_DEVICE_TYPE_DESC, "IPTunnel",
	                     NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_IP_TUNNEL,
	                     NULL);

	priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (self);
	priv->link_type = plink->type;

	return (NMDevice *) self;
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL (IP_TUNNEL, IPTunnel, ip_tunnel,
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES (NM_LINK_TYPE_IPIP, NM_LINK_TYPE_GRE, NM_LINK_TYPE_SIT, NM_LINK_TYPE_IPIP6, NM_LINK_TYPE_IP6IP6),
	factory_iface->create_device = create_device;
	)
