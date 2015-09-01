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

#ifndef __NM_SETTING_IP_TUNNEL_H__
#define __NM_SETTING_IP_TUNNEL_H__

#if !defined (__NETWORKMANAGER_H_INSIDE__) && !defined (NETWORKMANAGER_COMPILATION)
#error "Only <NetworkManager.h> can be included directly."
#endif

#include <nm-setting.h>

G_BEGIN_DECLS

#define NM_TYPE_SETTING_IP_TUNNEL            (nm_setting_ip_tunnel_get_type ())
#define NM_SETTING_IP_TUNNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), NM_TYPE_SETTING_IP_TUNNEL, NMSettingIPTunnel))
#define NM_SETTING_IP_TUNNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), NM_TYPE_SETTING_IP_TUNNEL, NMSettingIPTunnelClass))
#define NM_IS_SETTING_IP_TUNNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NM_TYPE_SETTING_IP_TUNNEL))
#define NM_IS_SETTING_IP_TUNNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), NM_TYPE_SETTING_IP_TUNNEL))
#define NM_SETTING_IP_TUNNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NM_TYPE_SETTING_IP_TUNNEL, NMSettingIPTunnelClass))

#define NM_SETTING_IP_TUNNEL_SETTING_NAME "ip-tunnel"

#define NM_SETTING_IP_TUNNEL_LOCAL        "local"
#define NM_SETTING_IP_TUNNEL_MODE         "mode"
#define NM_SETTING_IP_TUNNEL_REMOTE       "remote"
#define NM_SETTING_IP_TUNNEL_TTL          "ttl"

struct _NMSettingIPTunnel {
	NMSetting parent;
};

typedef struct {
	NMSettingClass parent;

	/*< private >*/
	gpointer padding[4];
} NMSettingIPTunnelClass;

/**
 * NMSettingIpTunnelMode:
 * @NM_SETTING_IP_TUNNEL_MODE_UNKNOWN:  Mode not set / unknown
 * @NM_SETTING_IP_TUNNEL_MODE_IPIP:     IP in IP tunnel
 * @NM_SETTING_IP_TUNNEL_MODE_GRE:      GRE tunnel
 * @NM_SETTING_IP_TUNNEL_MODE_SIT:      SIT tunnel
 * @NM_SETTING_IP_TUNNEL_MODE_ISATAP:   ISATAP tunnel
 * @NM_SETTING_IP_TUNNEL_MODE_IP6IP6:   IP6 in IP6 tunnel
 * @NM_SETTING_IP_TUNNEL_MODE_IPIP6:    IP in IP6 tunnel
 * @NM_SETTING_IP_TUNNEL_MODE_IP6GRE:   IP6 GRE tunnel
 *
 * The tunneling mode.
 *
 * Since: 1.2
 */
typedef enum {
	NM_SETTING_IP_TUNNEL_MODE_UNKNOWN         = 0,
	NM_SETTING_IP_TUNNEL_MODE_IPIP            = 1,
	NM_SETTING_IP_TUNNEL_MODE_GRE             = 2,
	NM_SETTING_IP_TUNNEL_MODE_SIT             = 3,
	NM_SETTING_IP_TUNNEL_MODE_ISATAP          = 4,
	NM_SETTING_IP_TUNNEL_MODE_IP6IP6          = 5,
	NM_SETTING_IP_TUNNEL_MODE_IPIP6           = 6,
	NM_SETTING_IP_TUNNEL_MODE_IP6GRE          = 7,
	__NM_SETTING_IP_TUNNEL_MODE_LAST,                                                /*< skip >*/
	NM_SETTING_IP_TUNNEL_MODE_MAX             = __NM_SETTING_IP_TUNNEL_MODE_LAST - 1 /*< skip >*/
} NMSettingIPTunnelMode;

NM_AVAILABLE_IN_1_2
GType nm_setting_ip_tunnel_get_type (void);

NM_AVAILABLE_IN_1_2
NMSetting * nm_setting_ip_tunnel_new (void);

NM_AVAILABLE_IN_1_2
NMSettingIPTunnelMode nm_setting_ip_tunnel_get_mode (NMSettingIPTunnel *setting);
NM_AVAILABLE_IN_1_2
const char *nm_setting_ip_tunnel_get_local (NMSettingIPTunnel *setting);
NM_AVAILABLE_IN_1_2
const char *nm_setting_ip_tunnel_get_remote (NMSettingIPTunnel *setting);
NM_AVAILABLE_IN_1_2
guint nm_setting_ip_tunnel_get_ttl (NMSettingIPTunnel *setting);
NM_AVAILABLE_IN_1_2
guint nm_setting_ip_tunnel_get_input_key (NMSettingIPTunnel *setting);
NM_AVAILABLE_IN_1_2
guint nm_setting_ip_tunnel_get_output_key (NMSettingIPTunnel *setting);

G_END_DECLS

#endif /* __NM_SETTING_IP_TUNNEL_H__ */
