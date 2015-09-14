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
#include "nm-setting-ip-tunnel.h"

#include "nmdbus-device-ip-tunnel.h"

#include "nm-device-logging.h"
_LOG_DECLARE_SELF(NMDeviceIPTunnel);

G_DEFINE_TYPE (NMDeviceIPTunnel, nm_device_ip_tunnel, NM_TYPE_DEVICE)

#define NM_DEVICE_IP_TUNNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_DEVICE_IP_TUNNEL, NMDeviceIPTunnelPrivate))

struct {
	NMSettingIPTunnelMode mode;
	NMLinkType type;
	gint encap;
} tunnel_types[] = {
		{ NM_SETTING_IP_TUNNEL_MODE_IPIP,    NM_LINK_TYPE_IPIP,    AF_INET },
		{ NM_SETTING_IP_TUNNEL_MODE_GRE,     NM_LINK_TYPE_GRE,     AF_INET },
		{ NM_SETTING_IP_TUNNEL_MODE_SIT,     NM_LINK_TYPE_SIT,     AF_INET },
		{ NM_SETTING_IP_TUNNEL_MODE_IPIP6,   NM_LINK_TYPE_IP6TNL,  AF_INET6 },
		{ NM_SETTING_IP_TUNNEL_MODE_IP6IP6,  NM_LINK_TYPE_IP6TNL,  AF_INET6 },
		{ NM_SETTING_IP_TUNNEL_MODE_IP6GRE,  NM_LINK_TYPE_IP6GRE,  AF_INET6 },
};

typedef struct {
	int parent_ifindex;
	union {
		in_addr_t local4;
		struct in6_addr local6;
	};
	union {
		in_addr_t remote4;
		struct in6_addr remote6;
	};
	guint8 ttl;
	guint8 tos;
	gboolean path_mtu_discovery;

	NMSettingIPTunnelMode mode;
	NMLinkType link_type;
	gint encap;
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

	if (!nm_platform_ip_tunnel_get_properties (NM_PLATFORM_GET, priv->link_type,
		                                       nm_device_get_ifindex (NM_DEVICE (self)),
		                                       &props)) {
		_LOGW (LOGD_HW, "could not read IP tunnel properties");
		return;
	}

	g_assert (props.encap == priv->encap);
	g_object_freeze_notify (object);

	if (priv->parent_ifindex != props.parent_ifindex) {
		priv->parent_ifindex = props.parent_ifindex;
		g_object_notify (object, NM_DEVICE_IP_TUNNEL_PARENT);
	}

	if (props.encap == AF_INET) {
		if (priv->local4 != props.local4) {
			priv->local4 = props.local4;
			g_object_notify (object, NM_DEVICE_IP_TUNNEL_LOCAL);
		}
		if (priv->remote4 != props.remote4) {
			priv->remote4 = props.remote4;
			g_object_notify (object, NM_DEVICE_IP_TUNNEL_REMOTE);
		}
	} else if (props.encap == AF_INET6) {
		if (!memcmp (&priv->local6, &props.local6, sizeof (struct in6_addr))) {
			memcpy (&priv->local6, &props.local6, sizeof (struct in6_addr));
			g_object_notify (object, NM_DEVICE_IP_TUNNEL_LOCAL);
		}
		if (!memcmp (&priv->remote6, &props.remote6, sizeof (struct in6_addr))) {
			memcpy (&priv->remote6, &props.remote6, sizeof (struct in6_addr));
			g_object_notify (object, NM_DEVICE_IP_TUNNEL_REMOTE);
		}
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

static gboolean
create_and_realize (NMDevice *device,
                    NMConnection *connection,
                    NMDevice *parent,
                    NMPlatformLink *out_plink,
                    GError **error)
{
	NMDeviceIPTunnelPrivate *priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (device);
	const char *iface = nm_device_get_iface (device);
	NMSettingIPTunnel *s_tunnel;
	NMPlatformError plerr;
	const char *str;

	s_tunnel = nm_connection_get_setting_ip_tunnel (connection);
	g_assert (s_tunnel);
	g_assert (out_plink);

	if (priv->encap == AF_INET) {
		in_addr_t local = 0, remote = 0;

		str = nm_setting_ip_tunnel_get_local (s_tunnel);
		if (str)
			inet_pton (AF_INET, str, &local);

		str = nm_setting_ip_tunnel_get_remote (s_tunnel);
		g_assert (str);
		inet_pton (AF_INET, str, &remote);

		plerr = nm_platform_ip4_tunnel_add (NM_PLATFORM_GET, priv->link_type, iface, local, remote,
		                                    nm_setting_ip_tunnel_get_ttl (s_tunnel), out_plink);
	} else {
		struct in6_addr local = { }, remote = { };

		str = nm_setting_ip_tunnel_get_local (s_tunnel);
		if (str)
			inet_pton (AF_INET6, str, &local);

		str = nm_setting_ip_tunnel_get_remote (s_tunnel);
		g_assert (str);
		inet_pton (AF_INET6, str, &remote);

		plerr = nm_platform_ip6_tunnel_add (NM_PLATFORM_GET, /* FIXME */ IPPROTO_IP, iface, &local, &remote,
		                                    nm_setting_ip_tunnel_get_ttl (s_tunnel), out_plink);
	}

	if (plerr != NM_PLATFORM_ERROR_SUCCESS && plerr != NM_PLATFORM_ERROR_EXISTS) {
		g_set_error (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_CREATION_FAILED,
		             "Failed to create IP tunnel interface '%s' for '%s': %s",
		             iface,
		             nm_connection_get_id (connection),
		             nm_platform_error_to_string (plerr));
		return FALSE;
	}

	return TRUE;
}

static gboolean
complete_connection (NMDevice *device,
                     NMConnection *connection,
                     const char *specific_object,
                     const GSList *existing_connections,
                     GError **error)
{
	NMSettingIPTunnel *s_tunnel;

	nm_utils_complete_generic (connection,
	                           NM_SETTING_IP_TUNNEL_SETTING_NAME,
	                           existing_connections,
	                           NULL,
	                           _("IP Tunnel connection"),
	                           NULL,
	                           TRUE);

	s_tunnel = nm_connection_get_setting_ip_tunnel (connection);
	if (!s_tunnel) {
		g_set_error_literal (error, NM_DEVICE_ERROR, NM_DEVICE_ERROR_INVALID_CONNECTION,
		                     "A 'ip-tunnel' setting is required.");
		return FALSE;
	}

	return TRUE;
}

static inline gboolean
ipv6_addr_any(const struct in6_addr *a)
{
	return     a->__in6_u.__u6_addr32[0] == 0
			&& a->__in6_u.__u6_addr32[1] == 0
			&& a->__in6_u.__u6_addr32[2] == 0
			&& a->__in6_u.__u6_addr32[3] == 0;
}


static void
update_connection (NMDevice *device, NMConnection *connection)
{
	NMDeviceIPTunnel *self = NM_DEVICE_IP_TUNNEL (device);
	NMDeviceIPTunnelPrivate *priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (self);
	NMSettingIPTunnel *s_tunnel = nm_connection_get_setting_ip_tunnel (connection);
	in_addr_t addr4;
	struct in6_addr addr6;
	const char *str;

	if (!s_tunnel) {
		s_tunnel = (NMSettingIPTunnel *) nm_setting_ip_tunnel_new ();
		nm_connection_add_setting (connection, (NMSetting *) s_tunnel);
	}

	update_properties (self);

	if (nm_setting_ip_tunnel_get_mode (s_tunnel) != priv->mode) {
		priv->mode = nm_setting_ip_tunnel_get_mode (s_tunnel);
		g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_MODE);
	}

	if (priv->encap == AF_INET) {
		str = nm_setting_ip_tunnel_get_local (s_tunnel);
		if (!str) {
			if (priv->local4) {
				priv->local4 = 0;
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_LOCAL);
			}
		} else {
			inet_pton (AF_INET, str, &addr4);
			if (priv->local4 != addr4) {
				priv->local4 = addr4;
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_LOCAL);
			}
		}

		str = nm_setting_ip_tunnel_get_remote (s_tunnel);
		if (!str) {
			if (priv->remote4) {
				priv->remote4 = 0;
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_REMOTE);
			}
		} else {
			inet_pton (AF_INET, str, &addr4);
			if (priv->remote4 != addr4) {
				priv->remote4 = addr4;
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_REMOTE);
			}
		}
	} else {
		str = nm_setting_ip_tunnel_get_local (s_tunnel);
		if (!str) {
			if (!ipv6_addr_any (&priv->local6)) {
				memset (&priv->local6, 0, sizeof (struct in6_addr));
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_LOCAL);
			}
		} else {
			inet_pton (AF_INET6, str, &addr6);
			if (memcmp (&priv->local6, &addr6, sizeof (struct in6_addr))) {
				memcpy (&priv->local6, &addr6, sizeof (struct in6_addr));
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_LOCAL);
			}
		}

		str = nm_setting_ip_tunnel_get_remote (s_tunnel);
		if (!str) {
			if (!ipv6_addr_any (&priv->remote6)) {
				memset (&priv->remote6, 0, sizeof (struct in6_addr));
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_LOCAL);
			}
		} else {
			inet_pton (AF_INET6, str, &addr6);
			if (memcmp (&priv->remote6, &addr6, sizeof (struct in6_addr))) {
				memcpy (&priv->remote6, &addr6, sizeof (struct in6_addr));
				g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_REMOTE);
			}
		}
	}

	if (nm_setting_ip_tunnel_get_ttl (s_tunnel) != priv->ttl) {
		priv->ttl = nm_setting_ip_tunnel_get_ttl (s_tunnel);
		g_object_notify (G_OBJECT (s_tunnel), NM_SETTING_IP_TUNNEL_TTL);
	}
}

static gboolean
addr_match (gint family, const char *str, void *addr)
{
	char buf[sizeof (struct in6_addr)] = { };

	if (str)
		inet_pton (family, str, buf);

	return !memcmp (buf, addr, family == AF_INET ? sizeof (in_addr_t) : sizeof (struct in6_addr));
}

static gboolean
check_connection_compatible (NMDevice *device, NMConnection *connection)
{
	NMDeviceIPTunnel *self = NM_DEVICE_IP_TUNNEL (device);
	NMDeviceIPTunnelPrivate *priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (self);
	NMSettingIPTunnel *s_tunnel;

	update_properties (self);

	if (!NM_DEVICE_CLASS (nm_device_ip_tunnel_parent_class)->check_connection_compatible (device, connection))
		return FALSE;

	s_tunnel = nm_connection_get_setting_ip_tunnel (connection);
	if (!s_tunnel)
		return FALSE;

	if (nm_setting_ip_tunnel_get_mode (s_tunnel) != priv->mode)
		return FALSE;

	if (!addr_match (priv->encap, nm_setting_ip_tunnel_get_local (s_tunnel), &priv->local4))
		return FALSE;

	if (!addr_match (priv->encap, nm_setting_ip_tunnel_get_remote (s_tunnel), &priv->remote4))
		return FALSE;

	if (nm_setting_ip_tunnel_get_ttl (s_tunnel) != priv->ttl)
		return FALSE;

	return TRUE;
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
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMDeviceIPTunnelPrivate *priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (object);
	NMDevice *parent;
	char buf[INET6_ADDRSTRLEN];

	switch (prop_id) {
	case PROP_PARENT:
		parent = nm_manager_get_device_by_ifindex (nm_manager_get (), priv->parent_ifindex);
		nm_utils_g_value_set_object_path (value, parent);
		break;
	case PROP_LOCAL:
		g_value_set_string (value, inet_ntop (priv->encap, &priv->local4, buf, sizeof (buf)));
		break;
	case PROP_REMOTE:
		g_value_set_string (value, inet_ntop (priv->encap, &priv->remote4, buf, sizeof (buf)));
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

	object_class->get_property = get_property;

	device_class->link_changed = link_changed;
	device_class->create_and_realize = create_and_realize;
	device_class->complete_connection = complete_connection;
	device_class->update_connection = update_connection;
	device_class->check_connection_compatible = check_connection_compatible;

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
	NMSettingIPTunnel *s_ip_tunnel = NULL;
	gint i, mode = -1;

	self = g_object_new (NM_TYPE_DEVICE_IP_TUNNEL,
	                     NM_DEVICE_IFACE, iface,
	                     NM_DEVICE_TYPE_DESC, "IPTunnel",
	                     NM_DEVICE_DEVICE_TYPE, NM_DEVICE_TYPE_IP_TUNNEL,
	                     NULL);

	priv = NM_DEVICE_IP_TUNNEL_GET_PRIVATE (self);

	if (connection) {
		s_ip_tunnel = nm_connection_get_setting_ip_tunnel (connection);
		if (s_ip_tunnel)
			mode = nm_setting_ip_tunnel_get_mode (s_ip_tunnel);
	}

	for (i = 0; i < G_N_ELEMENTS (tunnel_types); i++) {
		if (   (plink && plink->type == tunnel_types[i].type)
		    || (mode > NM_SETTING_IP_TUNNEL_MODE_UNKNOWN && mode == tunnel_types[i].mode)) {
			priv->mode = tunnel_types[i].mode;
			priv->link_type = tunnel_types[i].type;
			priv->encap = tunnel_types[i].encap;
			break;
		}
	}

	g_assert (i < G_N_ELEMENTS (tunnel_types));

	return (NMDevice *) self;
}

NM_DEVICE_FACTORY_DEFINE_INTERNAL (IP_TUNNEL, IPTunnel, ip_tunnel,
	NM_DEVICE_FACTORY_DECLARE_LINK_TYPES (NM_LINK_TYPE_IPIP, NM_LINK_TYPE_GRE, NM_LINK_TYPE_SIT, NM_LINK_TYPE_IP6TNL)
	NM_DEVICE_FACTORY_DECLARE_SETTING_TYPES (NM_SETTING_IP_TUNNEL_SETTING_NAME),
	factory_iface->create_device = create_device;
	)

