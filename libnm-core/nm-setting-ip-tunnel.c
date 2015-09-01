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

#include "nm-setting-ip-tunnel.h"
#include "nm-setting-private.h"
#include "nm-macros-internal.h"

/**
 * SECTION:nm-setting-ip-tunnel
 * @short_description: Describes connection properties for IP tunnels
 **/

G_DEFINE_TYPE_WITH_CODE (NMSettingIPTunnel, nm_setting_ip_tunnel, NM_TYPE_SETTING,
                         _nm_register_setting (IP_TUNNEL, 1))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_IP_TUNNEL)

#define NM_SETTING_IP_TUNNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_IP_TUNNEL, NMSettingIPTunnelPrivate))

typedef struct {
	NMSettingIPTunnelMode mode;
	char *local;
	char *remote;
	guint ttl;
} NMSettingIPTunnelPrivate;

enum {
	PROP_0,
	PROP_MODE,
	PROP_LOCAL,
	PROP_REMOTE,
	PROP_TTL,
	LAST_PROP
};

static int tunnel_encap[NM_SETTING_IP_TUNNEL_MODE_MAX + 1] = {
		[NM_SETTING_IP_TUNNEL_MODE_IPIP]    = AF_INET,
		[NM_SETTING_IP_TUNNEL_MODE_GRE]     = AF_INET,
		[NM_SETTING_IP_TUNNEL_MODE_SIT]     = AF_INET,
		[NM_SETTING_IP_TUNNEL_MODE_ISATAP]  = AF_INET,
		[NM_SETTING_IP_TUNNEL_MODE_IP6IP6]  = AF_INET6,
		[NM_SETTING_IP_TUNNEL_MODE_IPIP6]   = AF_INET6,
		[NM_SETTING_IP_TUNNEL_MODE_IP6GRE]  = AF_INET6,
};

/**
 * nm_setting_ip_tunnel_get_mode:
 * @setting: the #NMSettingIpTunnel
 *
 * Returns the #NMSettingIpTunnel:mode property of the setting.
 *
 * Returns: the tunnel mode
 *
 * Since: 1.2
 **/
NMSettingIPTunnelMode
nm_setting_ip_tunnel_get_mode (NMSettingIPTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_IP_TUNNEL (setting), NM_SETTING_IP_TUNNEL_MODE_UNKNOWN);

	return NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting)->mode;
}

/**
 * nm_setting_ip_tunnel_get_local:
 * @setting: the #NMSettingIpTunnel
 *
 * Returns the #NMSettingIpTunnel:local property of the setting.
 *
 * Returns: the local endpoint
 *
 * Since: 1.2
 **/
const char *
nm_setting_ip_tunnel_get_local (NMSettingIPTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_IP_TUNNEL (setting), NULL);

	return NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting)->local;
}

/**
 * nm_setting_ip_tunnel_get_remote:
 * @setting: the #NMSettingIpTunnel
 *
 * Returns the #NMSettingIpTunnel:remote property of the setting.
 *
 * Returns: the remote endpoint
 *
 * Since: 1.2
 **/
const char *
nm_setting_ip_tunnel_get_remote (NMSettingIPTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_IP_TUNNEL (setting), NULL);

	return NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting)->remote;
}

/**
 * nm_setting_ip_tunnel_get_ttl:
 * @setting: the #NMSettingIpTunnel
 *
 * Returns the #NMSettingIpTunnel:ttl property of the setting.
 *
 * Returns: the Time-to-live
 *
 * Since: 1.2
 **/

guint
nm_setting_ip_tunnel_get_ttl (NMSettingIPTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_IP_TUNNEL (setting), 0);

	return NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting)->ttl;
}

static gboolean
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingIPTunnelPrivate *priv = NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting);
	gboolean endpoint4;

	if (priv->mode <= 0 || priv->mode > NM_SETTING_IP_TUNNEL_MODE_MAX) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%d' tunnel mode not supported"), (int) priv->mode);
		g_prefix_error (error, "%s.%s: ",
		                NM_SETTING_IP_TUNNEL_SETTING_NAME,
		                NM_SETTING_IP_TUNNEL_MODE);
		return FALSE;
	}

	g_return_val_if_fail (   tunnel_encap[priv->mode] == AF_INET
	                      || tunnel_encap[priv->mode] == AF_INET6, FALSE);

	endpoint4 = (tunnel_encap[priv->mode] == AF_INET);

	if (priv->local) {
		if (   (endpoint4 && !nm_utils_ipaddr_valid (AF_INET, priv->local))
			|| (!endpoint4 && !nm_utils_ipaddr_valid (AF_INET6, priv->local))) {
			g_set_error (error,
			             NM_CONNECTION_ERROR,
			             NM_CONNECTION_ERROR_INVALID_PROPERTY,
			             _("'%s': invalid local endpoint"),
			             priv->local);
			g_prefix_error (error, "%s.%s: ",
			                NM_SETTING_IP_TUNNEL_SETTING_NAME,
			                NM_SETTING_IP_TUNNEL_LOCAL);
			return FALSE;
		}
	}

	if (!priv->remote) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("missing remote endpoint"));
		g_prefix_error (error, "%s.%s: ",
		                NM_SETTING_IP_TUNNEL_SETTING_NAME,
		                NM_SETTING_IP_TUNNEL_REMOTE);
		return FALSE;
	}

	if (   (endpoint4 && !nm_utils_ipaddr_valid (AF_INET, priv->remote))
		|| (!endpoint4 && !nm_utils_ipaddr_valid (AF_INET6, priv->remote))) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s': invalid remote endpoint"),
		             priv->remote);
		g_prefix_error (error, "%s.%s: ",
		                NM_SETTING_IP_TUNNEL_SETTING_NAME,
		                NM_SETTING_IP_TUNNEL_REMOTE);
		return FALSE;
	}

	return TRUE;
}

/**
 * nm_setting_ip_tunnel_new:
 *
 * Creates a new #NMSettingIpTunnel object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingIpTunnel object
 *
 * Since: 1.2
 **/
NMSetting *
nm_setting_ip_tunnel_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_IP_TUNNEL, NULL);
}

static void
nm_setting_ip_tunnel_init (NMSettingIPTunnel *setting)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingIPTunnel *setting = NM_SETTING_IP_TUNNEL (object);
	NMSettingIPTunnelPrivate *priv = NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting);

	switch (prop_id) {
	case PROP_MODE:
		priv->mode = g_value_get_uint (value);
		break;
	case PROP_LOCAL:
		g_free (priv->local);
		priv->local = g_value_dup_string (value);
		break;
	case PROP_REMOTE:
		g_free (priv->remote);
		priv->remote = g_value_dup_string (value);
		break;
	case PROP_TTL:
		priv->ttl = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMSettingIPTunnel *setting = NM_SETTING_IP_TUNNEL (object);
	NMSettingIPTunnelPrivate *priv = NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting);

	switch (prop_id) {
	case PROP_MODE:
		g_value_set_uint (value, priv->mode);
		break;
	case PROP_LOCAL:
		g_value_set_string (value, priv->local);
		break;
	case PROP_REMOTE:
		g_value_set_string (value, priv->remote);
		break;
	case PROP_TTL:
		g_value_set_uint (value, priv->ttl);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
finalize (GObject *object)
{
	NMSettingIPTunnel *setting = NM_SETTING_IP_TUNNEL (object);
	NMSettingIPTunnelPrivate *priv = NM_SETTING_IP_TUNNEL_GET_PRIVATE (setting);

	g_free (priv->local);
	g_free (priv->remote);

	G_OBJECT_CLASS (nm_setting_ip_tunnel_parent_class)->finalize (object);
}

static void
nm_setting_ip_tunnel_class_init (NMSettingIPTunnelClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingIPTunnelPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;
	parent_class->verify       = verify;
	/**
	 * NMSettingIPTunnel:mode:
	 *
	 * The tunneling mode.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_MODE,
		 g_param_spec_uint (NM_SETTING_IP_TUNNEL_MODE, "", "",
		                    0, G_MAXUINT, NM_SETTING_IP_TUNNEL_MODE_UNKNOWN,
		                    G_PARAM_READWRITE |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingIPTunnel:local:
	 *
	 * The local endpoint of the tunnel; the value can be empty, otherwise it
	 * must contain an IPv4 or IPv6 address.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_LOCAL,
		 g_param_spec_string (NM_SETTING_IP_TUNNEL_LOCAL, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingIPTunnel:remote:
	 *
	 * The remote endpoint of the tunnel; the value must contain an IPv4 or
	 * IPv6 address.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_REMOTE,
		 g_param_spec_string (NM_SETTING_IP_TUNNEL_REMOTE, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingIPTunnel:ttl
	 *
	 * The TTL to assign to tunneled packets. 0 is a special value meaning that
	 * packets inherit the TTL value.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_TTL,
		 g_param_spec_uint (NM_SETTING_IP_TUNNEL_TTL, "", "",
		                    0, 255, 0,
		                    G_PARAM_READWRITE |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));
}
