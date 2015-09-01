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

#include "nm-setting-tunnel.h"
#include "nm-setting-private.h"
#include "nm-macros-internal.h"

/**
 * SECTION:nm-setting-tunnel
 * @short_description: Describes connection properties for tunnel devices
 **/

G_DEFINE_TYPE_WITH_CODE (NMSettingTunnel, nm_setting_tunnel, NM_TYPE_SETTING,
                         _nm_register_setting (TUNNEL, 1))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_TUNNEL)

#define NM_SETTING_TUNNEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_SETTING_TUNNEL, NMSettingTunnelPrivate))

typedef struct {
	NMSettingTunnelMode mode;
	char *local;
	char *remote;
	guint ttl;
} NMSettingTunnelPrivate;

enum {
	PROP_0,
	PROP_MODE,
	PROP_LOCAL,
	PROP_REMOTE,
	PROP_TTL,
	LAST_PROP
};

/**
 * nm_setting_tunnel_get_mode:
 * @setting: the #NMSettingTunnel
 *
 * Returns the #NMSettingTunnel:mode property of the setting.
 *
 * Returns: the tunnel mode
 *
 * Since: 1.2
 **/
NMSettingTunnelMode
nm_setting_tunnel_get_mode (NMSettingTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_TUNNEL (setting), NM_SETTING_TUNNEL_MODE_UNKNOWN);

	return NM_SETTING_TUNNEL_GET_PRIVATE (setting)->mode;
}

/**
 * nm_setting_tunnel_get_local:
 * @setting: the #NMSettingTunnel
 *
 * Returns the #NMSettingTunnel:local property of the setting.
 *
 * Returns: the local endpoint
 *
 * Since: 1.2
 **/
const char *
nm_setting_tunnel_get_local (NMSettingTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_TUNNEL (setting), NULL);

	return NM_SETTING_TUNNEL_GET_PRIVATE (setting)->local;
}

/**
 * nm_setting_tunnel_get_remote:
 * @setting: the #NMSettingTunnel
 *
 * Returns the #NMSettingTunnel:remote property of the setting.
 *
 * Returns: the remote endpoint
 *
 * Since: 1.2
 **/
const char *
nm_setting_tunnel_get_remote (NMSettingTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_TUNNEL (setting), NULL);

	return NM_SETTING_TUNNEL_GET_PRIVATE (setting)->remote;
}

/**
 * nm_setting_tunnel_get_ttl:
 * @setting: the #NMSettingTunnel
 *
 * Returns the #NMSettingTunnel:ttl property of the setting.
 *
 * Returns: the Time-to-live
 *
 * Since: 1.2
 **/

guint
nm_setting_tunnel_get_ttl (NMSettingTunnel *setting)
{
	g_return_val_if_fail (NM_IS_SETTING_TUNNEL (setting), 0);

	return NM_SETTING_TUNNEL_GET_PRIVATE (setting)->ttl;
}

static gboolean
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingTunnelPrivate *priv = NM_SETTING_TUNNEL_GET_PRIVATE (setting);

	if (priv->mode != NM_SETTING_TUNNEL_MODE_GRE) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' tunnel mode not supported"),
		             nm_utils_enum_to_str (nm_setting_tunnel_mode_get_type(), priv->mode));
		g_prefix_error (error, "%s.%s: ", NM_SETTING_TUNNEL_SETTING_NAME, NM_SETTING_TUNNEL_MODE);
		return FALSE;
	}

	if (priv->local && !nm_utils_ipaddr_valid (AF_INET, priv->local)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s': invalid local endpoint"),
		             priv->local);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_TUNNEL_SETTING_NAME, NM_SETTING_TUNNEL_LOCAL);
		return FALSE;
	}

	if (!priv->remote || !nm_utils_ipaddr_valid (AF_INET, priv->remote)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("%s%s%s: invalid or missing remote endpoint"),
		             NM_PRINT_FMT_QUOTE_STRING (priv->remote));
		g_prefix_error (error, "%s.%s: ", NM_SETTING_TUNNEL_SETTING_NAME, NM_SETTING_TUNNEL_REMOTE);
		return FALSE;
	}

	return TRUE;
}

/**
 * nm_setting_tunnel_new:
 *
 * Creates a new #NMSettingTunnel object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingTunnel object
 *
 * Since: 1.2
 **/
NMSetting *
nm_setting_tunnel_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_TUNNEL, NULL);
}

static void
nm_setting_tunnel_init (NMSettingTunnel *setting)
{
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingTunnel *setting = NM_SETTING_TUNNEL (object);
	NMSettingTunnelPrivate *priv = NM_SETTING_TUNNEL_GET_PRIVATE (setting);

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
	NMSettingTunnel *setting = NM_SETTING_TUNNEL (object);
	NMSettingTunnelPrivate *priv = NM_SETTING_TUNNEL_GET_PRIVATE (setting);

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
	NMSettingTunnel *setting = NM_SETTING_TUNNEL (object);
	NMSettingTunnelPrivate *priv = NM_SETTING_TUNNEL_GET_PRIVATE (setting);

	g_free (priv->local);
	g_free (priv->remote);

	G_OBJECT_CLASS (nm_setting_tunnel_parent_class)->finalize (object);
}

static void
nm_setting_tunnel_class_init (NMSettingTunnelClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	g_type_class_add_private (setting_class, sizeof (NMSettingTunnelPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;
	parent_class->verify       = verify;
	/**
	 * NMSettingTunnel:mode:
	 *
	 * The tunneling mode.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_MODE,
		 g_param_spec_uint (NM_SETTING_TUNNEL_MODE, "", "",
		                    0, G_MAXUINT, NM_SETTING_TUNNEL_MODE_UNKNOWN,
		                    G_PARAM_READWRITE |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingTunnel:local:
	 *
	 * The local endpoint of the tunnel; the value can be empty, otherwise it
	 * must contain an IPv4 or IPv6 address.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_LOCAL,
		 g_param_spec_string (NM_SETTING_TUNNEL_LOCAL, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingTunnel:remote:
	 *
	 * The remote endpoint of the tunnel; the value must contain an IPv4 or
	 * IPv6 address.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_REMOTE,
		 g_param_spec_string (NM_SETTING_TUNNEL_REMOTE, "", "",
		                      NULL,
		                      G_PARAM_READWRITE |
		                      NM_SETTING_PARAM_INFERRABLE |
		                      G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingTunnel:ttl
	 *
	 * The TTL to assign to tunneled packets. 0 is a special value meaning that
	 * packets inherit the TTL value.
	 *
	 * Since: 1.2
	 **/
	g_object_class_install_property
		(object_class, PROP_TTL,
		 g_param_spec_uint (NM_SETTING_TUNNEL_TTL, "", "",
		                    0, 255, 0,
		                    G_PARAM_READWRITE |
		                    NM_SETTING_PARAM_INFERRABLE |
		                    G_PARAM_STATIC_STRINGS));
}
