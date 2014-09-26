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
 * Copyright (C) 2014 Red Hat, Inc.
 */

#include <config.h>
#include <sys/socket.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/rfcomm.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "nm-bluez5-dun.h"
#include "nm-bt-error.h"
#include "nm-logging.h"
#include "NetworkManagerUtils.h"

typedef struct _NMBluez5DunContext {
	bdaddr_t src;
	bdaddr_t dst;
	char *source;
	char *dest;
	int rfcomm_channel;
	int rfcomm_fd;
	char *rfcomm_dev;
	int rfcomm_id;
	NMBluez5DunFunc callback;
	gpointer user_data;
	sdp_session_t *sdp_session;
	guint sdp_watch_id;
} NMBluez5DunContext;

static gboolean
dun_connect (NMBluez5DunContext *context, GError **error)
{
	struct sockaddr_rc sa;
	struct stat st;
	int devid, try = 30;
	char tty[100];
	const int ttylen = sizeof (tty) - 1;

	struct rfcomm_dev_req req = {
		.flags = (1 << RFCOMM_REUSE_DLC) | (1 << RFCOMM_RELEASE_ONHUP),
		.dev_id = -1,
		.channel = context->rfcomm_channel
	};

	context->rfcomm_fd = socket (AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
	if (context->rfcomm_fd < 0) {
		g_set_error (error, NM_BT_ERROR, NM_BT_ERROR_DUN_CONNECT_FAILED,
		             "Failed to create RFCOMM socket: (%d) %s",
		             errno, strerror (errno));
		return FALSE;
	}

	/* Connect to the remote device */
	sa.rc_family = AF_BLUETOOTH;
	sa.rc_channel = 0;
	memcpy (&sa.rc_bdaddr, &context->src, ETH_ALEN);
	if (bind (context->rfcomm_fd, (struct sockaddr *) &sa, sizeof(sa))) {
		nm_log_dbg (LOGD_BT, "(%s): failed to bind socket: (%d) %s",
		            context->source, errno, strerror (errno));
	}

	sa.rc_channel = context->rfcomm_channel;
	memcpy (&sa.rc_bdaddr, &context->dst, ETH_ALEN);
	if (connect (context->rfcomm_fd, (struct sockaddr *) &sa, sizeof (sa)) ) {
		g_set_error (error, NM_BT_ERROR, NM_BT_ERROR_DUN_CONNECT_FAILED,
		             "Failed to connect to remote device: (%d) %s",
		             errno, strerror (errno));
		return FALSE;
	}

	nm_log_dbg (LOGD_BT, "(%s): connected to %s on channel %d",
	            context->source, context->dest, context->rfcomm_channel);

	/* Create an RFCOMM kernel device for the DUN channel */
	memcpy (&req.src, &context->src, ETH_ALEN);
	memcpy (&req.dst, &context->dst, ETH_ALEN);
	devid = ioctl (context->rfcomm_fd, RFCOMMCREATEDEV, &req);
	if (devid < 0) {
		g_set_error (error, NM_BT_ERROR, NM_BT_ERROR_DUN_CONNECT_FAILED,
		             "(%s): failed to create rfcomm device: (%d) %s",
		             context->source, errno, strerror (errno));
		return FALSE;
	}
	context->rfcomm_id = devid;

	snprintf (tty, ttylen, "/dev/rfcomm%d", devid);
	while (stat (tty, &st) < 0 && try--) {
		if (try) {
			g_usleep (100 * 1000);
			continue;
		}

		g_set_error (error, NM_BT_ERROR, NM_BT_ERROR_DUN_CONNECT_FAILED,
		             "(%s): failed to find rfcomm device",
		             context->source);
		return FALSE;
	}

	context->rfcomm_dev = g_strdup (tty);
	return TRUE;
}

static void
sdp_search_cleanup (NMBluez5DunContext *context)
{
	if (context->sdp_session) {
		sdp_close (context->sdp_session);
		context->sdp_session = NULL;
	}

	if (context->sdp_watch_id) {
		g_source_remove (context->sdp_watch_id);
		context->sdp_watch_id = 0;
	}
}

static void
sdp_search_completed_cb (uint8_t type, uint16_t status, uint8_t *rsp, size_t size, void *user_data)
{
	NMBluez5DunContext *context = user_data;
	int scanned, seqlen = 0, bytesleft = size;
	uint8_t dataType;
	int channel = -1;

	if (status || type != SDP_SVC_SEARCH_ATTR_RSP) {
		err = -EPROTO;
		goto done;
	}

	scanned = sdp_extract_seqtype (rsp, bytesleft, &dataType, &seqlen);
	if (!scanned || !seqlen)
		goto done;

	rsp += scanned;
	bytesleft -= scanned;
	do {
		sdp_record_t *rec;
		int recsize = 0;
		sdp_list_t *protos;

		rec = sdp_extract_pdu (rsp, bytesleft, &recsize);
		if (!rec)
			break;

		if (!recsize) {
			sdp_record_free (rec);
			break;
		}

		if (sdp_get_access_protos (rec, &protos) == 0) {
			/* Extract the DUN channel number */
			channel = sdp_get_proto_port (protos, RFCOMM_UUID);
			sdp_list_free (protos, NULL);
		}
		sdp_record_free (rec);

		scanned += recsize;
		rsp += recsize;
		bytesleft -= recsize;
	} while ((scanned < (ssize_t) size) && (bytesleft > 0) && (channel < 0));

done:
	if (channel != -1) {
		context->rfcomm_channel = channel;
		dun_connect (context, NULL);
	}

}

static gboolean
sdp_search_process_cb (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
	NMBluez5DunContext *context = user_data;
	int err = 0;

	if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) {
		err = EIO;
		return FALSE;
	}

	if (sdp_process (context->sdp_session) < 0) {
		/* Search finished successfully. */
		return FALSE;
	}

	/* Search progressed successfully. */
	return TRUE;
}

static gboolean
sdp_connect_watch (GIOChannel *channel, GIOCondition condition, gpointer user_data)
{
	NMBluez5DunContext *context = user_data;
	sdp_list_t *search, *attrs;
	uuid_t svclass;
	uint16_t attr;
	int fd, err, fd_err = 0;
	socklen_t len = sizeof (fd_err);

	context->sdp_watch_id = 0;

	fd = g_io_channel_unix_get_fd (channel);
	if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &fd_err, &len) < 0)
		err = -errno;
	else
		err = -fd_err;
	if (sdp_set_notify (context->sdp_session, sdp_search_completed_cb, context) < 0) {
		err = -EIO;
	}

	sdp_uuid16_create (&svclass, DIALUP_NET_SVCLASS_ID);
	search = sdp_list_append (NULL, &svclass);
	attr = SDP_ATTR_PROTO_DESC_LIST;
	attrs = sdp_list_append (NULL, &attr);

	if (!sdp_service_search_attr_async (context->sdp_session, search, SDP_ATTR_REQ_INDIVIDUAL, attrs)) {
		/* Set callback responsible for update the internal SDP transaction */
		context->sdp_watch_id = g_io_add_watch (channel,
		                                        G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
		                                        sdp_search_process_cb,
		                                        context);
	}

	sdp_list_free (attrs, NULL);
	sdp_list_free (search, NULL);
	return G_SOURCE_REMOVE;
}

static gboolean
dun_find_channel (NMBluez5DunContext *context, GError **error)
{
	GIOChannel *channel;

	context->sdp_session = sdp_connect (&context->src, &context->dst, SDP_NON_BLOCKING);
	if (!context->sdp_session) {
		g_set_error (error, NM_BT_ERROR, NM_BT_ERROR_DUN_CONNECT_FAILED,
		             "(%s): failed to connect to the SDP server: (%d) %s",
		             context->source, errno, strerror (errno));
		return FALSE;
	}

	channel = g_io_channel_unix_new (sdp_get_socket (context->sdp_session));
	context->sdp_watch_id = g_io_add_watch (channel,
	                                        G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
	                                        sdp_connect_watch,
	                                        context);
	g_io_channel_unref (channel);
	return TRUE;
}

NMBluez5DunContext *
nm_bluez5_dun_new (const char *adapter,
                   const char *remote,
                   int rfcomm_channel,
                   NMBluez5DunFunc callback,
                   gpointer user_data,
                   GError **error)
{
	NMBluez5DunContext *context;

	context = g_slice_new0 (NMBluez5DunContext);
	str2ba (adapter, &context->src);
	str2ba (remote, &context->dst);
	context->source = g_strdup (adapter);
	context->dest = g_strdup (remote);
	context->rfcomm_channel = (rfcomm_channel >= 0) ? rfcomm_channel : -1;
	context->rfcomm_id = -1;
	context->rfcomm_fd = -1;
	context->callback = callback;
	context->user_data = user_data;
	return context;
}

gboolean
nm_bluez5_dun_connect (NMBluez5DunContext *context, GError **error)
{
	/* Find channel */
	if (context->rfcomm_channel < 0)
		return dun_find_channel (context, error);

	/* Connect */
	return dun_connect (context, error);
}

/* Only clean up connection-related stuff to allow reconnect */
void
nm_bluez5_dun_cleanup (NMBluez5DunContext *context)
{
	g_return_if_fail (context != NULL);

	sdp_search_cleanup (context);

	if (context->rfcomm_fd >= 0) {
		if (context->rfcomm_id >= 0) {
			struct rfcomm_dev_req req = { 0 };

			req.dev_id = context->rfcomm_id;
			ioctl (context->rfcomm_fd, RFCOMMRELEASEDEV, &req);
			context->rfcomm_id = -1;
		}
		close (context->rfcomm_fd);
		context->rfcomm_fd = -1;
	}

	g_free (context->source);
	context->source = NULL;

	g_free (context->dest);
	context->dest = NULL;
}

void
nm_bluez5_dun_free (NMBluez5DunContext *context)
{
	g_return_if_fail (context != NULL);

	nm_bluez5_dun_cleanup (context);
	g_slice_free (NMBluez5DunContext, context);
}
