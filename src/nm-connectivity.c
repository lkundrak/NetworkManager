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
 * Copyright (C) 2011 Thomas Bechtold <thomasbechtold@jpberlin.de>
 * Copyright (C) 2011 Dan Williams <dcbw@redhat.com>
 * Copyright (C) 2016 - 2018 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-connectivity.h"
#include "nm-dbus-manager.h"

#include <string.h>

#if WITH_CONCHECK
#include <curl/curl.h>
#endif

#include "c-list/src/c-list.h"
#include "nm-config.h"
#include "NetworkManagerUtils.h"

#define HEADER_STATUS_ONLINE "X-NetworkManager-Status: online\r\n"

/*****************************************************************************/

NM_UTILS_LOOKUP_STR_DEFINE_STATIC (_state_to_string, int /*NMConnectivityState*/,
	NM_UTILS_LOOKUP_DEFAULT_WARN ("???"),
	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_UNKNOWN,  "UNKNOWN"),
	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_NONE,     "NONE"),
	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_LIMITED,  "LIMITED"),
	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_PORTAL,   "PORTAL"),
	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_FULL,     "FULL"),

	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_ERROR,    "ERROR"),
	NM_UTILS_LOOKUP_STR_ITEM (NM_CONNECTIVITY_FAKE,     "FAKE"),
);

const char *
nm_connectivity_state_to_string (NMConnectivityState state)
{
	return _state_to_string (state);
}

/*****************************************************************************/

struct _NMConnectivityCheckHandle {
	CList handles_lst;
	NMConnectivity *self;
	NMConnectivityCheckCallback callback;
	gpointer user_data;

	char *ifspec;
	int addr_family;

#if WITH_CONCHECK
	struct {
		int ifindex;
		GCancellable *resolve_cancellable;

		char *response;

		CURLM *curl_mhandle;
		guint curl_timer;
		CURL *curl_ehandle;
		struct curl_slist *request_headers;
		struct curl_slist *hosts;

		GString *recv_msg;
	} concheck;
#endif

	guint timeout_id;
};

enum {
	CONFIG_CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
	CList handles_lst_head;
	char *uri;
	char *host;
	char *port;
	char *response;
	gboolean enabled;
	guint interval;
	NMConfig *config;
} NMConnectivityPrivate;

struct _NMConnectivity {
	GObject parent;
	NMConnectivityPrivate _priv;
};

struct _NMConnectivityClass {
	GObjectClass parent;
};

G_DEFINE_TYPE (NMConnectivity, nm_connectivity, G_TYPE_OBJECT)

#define NM_CONNECTIVITY_GET_PRIVATE(self) _NM_GET_PRIVATE (self, NMConnectivity, NM_IS_CONNECTIVITY)

NM_DEFINE_SINGLETON_GETTER (NMConnectivity, nm_connectivity_get, NM_TYPE_CONNECTIVITY);

/*****************************************************************************/

#define _NMLOG_DOMAIN      LOGD_CONCHECK
#define _NMLOG(level, ...) __NMLOG_DEFAULT (level, _NMLOG_DOMAIN, "connectivity", __VA_ARGS__)

#define _NMLOG2_DOMAIN     LOGD_CONCHECK
#define _NMLOG2(level, ...) \
    G_STMT_START { \
        const NMLogLevel __level = (level); \
        \
        if (nm_logging_enabled (__level, _NMLOG2_DOMAIN)) { \
            _nm_log (__level, _NMLOG2_DOMAIN, 0, \
                     (cb_data->ifspec ? &cb_data->ifspec[3] : NULL), \
                     NULL, \
                     "connectivity: (%s,AF_INET%s) " \
                     _NM_UTILS_MACRO_FIRST (__VA_ARGS__), \
                     (cb_data->ifspec ? &cb_data->ifspec[3] : ""), \
                     (cb_data->addr_family == AF_INET6 ? "6" : "") \
                     _NM_UTILS_MACRO_REST (__VA_ARGS__)); \
        } \
    } G_STMT_END

/*****************************************************************************/

static void
cb_data_invoke_callback (NMConnectivityCheckHandle *cb_data,
                         NMConnectivityState state,
                         GError *error,
                         const char *log_message)
{
	NMConnectivityCheckCallback callback;

	nm_assert (cb_data);
	nm_assert (NM_IS_CONNECTIVITY (cb_data->self));

	callback = cb_data->callback;
	if (!callback)
		return;

	cb_data->callback = NULL;

	nm_assert (log_message);

	_LOG2D ("check completed: %s; %s",
	        nm_connectivity_state_to_string (state),
	        log_message);

	callback (cb_data->self,
	          cb_data,
	          state,
	          error,
	          cb_data->user_data);
}

static void
cb_data_free (NMConnectivityCheckHandle *cb_data,
              NMConnectivityState state,
              GError *error,
              const char *log_message)
{
	NMConnectivity *self;

	nm_assert (cb_data);

	self = cb_data->self;

	nm_assert (NM_IS_CONNECTIVITY (self));

	c_list_unlink (&cb_data->handles_lst);

#if WITH_CONCHECK
	if (cb_data->concheck.curl_ehandle) {
		/* Contrary to what cURL manual claim it is *not* safe to remove
		 * the easy handle "at any moment"; specifically not from the
		 * write function. Thus here we just dissociate the cb_data from
		 * the easy handle and the easy handle will be cleaned up when the
		 * message goes to CURLMSG_DONE in curl_check_connectivity(). */
		curl_easy_setopt (cb_data->concheck.curl_ehandle, CURLOPT_WRITEFUNCTION, NULL);
		curl_easy_setopt (cb_data->concheck.curl_ehandle, CURLOPT_WRITEDATA, NULL);
		curl_easy_setopt (cb_data->concheck.curl_ehandle, CURLOPT_HEADERFUNCTION, NULL);
		curl_easy_setopt (cb_data->concheck.curl_ehandle, CURLOPT_HEADERDATA, NULL);
		curl_easy_setopt (cb_data->concheck.curl_ehandle, CURLOPT_PRIVATE, NULL);
		curl_easy_setopt (cb_data->concheck.curl_ehandle, CURLOPT_HTTPHEADER, NULL);

		curl_multi_remove_handle (cb_data->concheck.curl_mhandle,
		                          cb_data->concheck.curl_ehandle);
		curl_easy_cleanup (cb_data->concheck.curl_ehandle);
		curl_multi_cleanup (cb_data->concheck.curl_mhandle);

		curl_slist_free_all (cb_data->concheck.request_headers);
		curl_slist_free_all (cb_data->concheck.hosts);
	}
	nm_clear_g_source (&cb_data->concheck.curl_timer);
	nm_clear_g_cancellable (&cb_data->concheck.resolve_cancellable);
#endif

	nm_clear_g_source (&cb_data->timeout_id);

	cb_data_invoke_callback (cb_data, state, error, log_message);

#if WITH_CONCHECK
	g_free (cb_data->concheck.response);
	if (cb_data->concheck.recv_msg)
		g_string_free (cb_data->concheck.recv_msg, TRUE);
#endif
	g_free (cb_data->ifspec);
	g_slice_free (NMConnectivityCheckHandle, cb_data);
}

/*****************************************************************************/

#if WITH_CONCHECK
static const char *
_check_handle_get_response (NMConnectivityCheckHandle *cb_data)
{
	return cb_data->concheck.response ?: NM_CONFIG_DEFAULT_CONNECTIVITY_RESPONSE;
}

static void
curl_check_connectivity (CURLM *mhandle, int sockfd, int ev_bitmask)
{
	NMConnectivityCheckHandle *cb_data;
	CURLMsg *msg;
	CURLcode eret;
	gint m_left;
	long response_code;
	CURLMcode ret;
	int running_handles;

	ret = curl_multi_socket_action (mhandle, sockfd, ev_bitmask, &running_handles);
	if (ret != CURLM_OK)
		_LOGE ("connectivity check failed: (%d) %s", ret, curl_easy_strerror (ret));

	while ((msg = curl_multi_info_read (mhandle, &m_left))) {

		if (msg->msg != CURLMSG_DONE)
			continue;

		/* Here we have completed a session. Check easy session result. */
		eret = curl_easy_getinfo (msg->easy_handle, CURLINFO_PRIVATE, (char **) &cb_data);
		if (eret != CURLE_OK) {
			_LOGE ("curl cannot extract cb_data for easy handle, skipping msg: (%d) %s",
			       eret, curl_easy_strerror (eret));
			continue;
		}

		if (!cb_data->callback) {
			/* callback was already invoked earlier. */
			cb_data_free (cb_data, NM_CONNECTIVITY_UNKNOWN, NULL, NULL);
		} else if (msg->data.result != CURLE_OK) {
			gs_free char *log_message = NULL;

			log_message = g_strdup_printf ("check failed: (%d) %s",
			                               msg->data.result,
			                               curl_easy_strerror (msg->data.result));
			cb_data_free (cb_data, NM_CONNECTIVITY_LIMITED, NULL,
			              log_message);
		} else if (   !((_check_handle_get_response (cb_data))[0])
		           && (curl_easy_getinfo (msg->easy_handle, CURLINFO_RESPONSE_CODE, &response_code) == CURLE_OK)
		           && response_code == 204) {
			/* If we got a 204 response code (no content) and we actually
			 * requested no content, report full connectivity. */
			cb_data_free (cb_data, NM_CONNECTIVITY_FULL, NULL,
			              "no content, as expected");
		} else {
			/* If we get here, it means that easy_write_cb() didn't read enough
			 * bytes to be able to do a match, or that we were asking for no content
			 * (204 response code) and we actually got some. Either way, that is
			 * an indication of a captive portal */
			cb_data_free (cb_data, NM_CONNECTIVITY_PORTAL, NULL,
			              "unexpected short response");
		}
	}
}

static gboolean
curl_timeout_cb (gpointer user_data)
{
	NMConnectivityCheckHandle *cb_data = user_data;

	cb_data->concheck.curl_timer = 0;
	curl_check_connectivity (cb_data->concheck.curl_mhandle, CURL_SOCKET_TIMEOUT, 0);
	return G_SOURCE_REMOVE;
}

static int
multi_timer_cb (CURLM *multi, long timeout_ms, void *userdata)
{
	NMConnectivityCheckHandle *cb_data = userdata;

	nm_clear_g_source (&cb_data->concheck.curl_timer);
	if (timeout_ms != -1)
		cb_data->concheck.curl_timer = g_timeout_add (timeout_ms, curl_timeout_cb, cb_data);
	return 0;
}

static gboolean
curl_socketevent_cb (GIOChannel *ch, GIOCondition condition, gpointer user_data)
{
	NMConnectivityCheckHandle *cb_data = user_data;
	int fd = g_io_channel_unix_get_fd (ch);
	int action = 0;

	if (condition & G_IO_IN)
		action |= CURL_CSELECT_IN;
	if (condition & G_IO_OUT)
		action |= CURL_CSELECT_OUT;
	if (condition & G_IO_ERR)
		action |= CURL_CSELECT_ERR;

	curl_check_connectivity (cb_data->concheck.curl_mhandle, fd, action);
	return G_SOURCE_CONTINUE;
}

typedef struct {
	GIOChannel *ch;
	guint ev;
} CurlSockData;

static int
multi_socket_cb (CURL *e_handle, curl_socket_t s, int what, void *userdata, void *socketp)
{
	NMConnectivityCheckHandle *cb_data = userdata;
	CurlSockData *fdp = socketp;
	GIOCondition condition = 0;

	if (what == CURL_POLL_REMOVE) {
		if (fdp) {
			curl_multi_assign (cb_data->concheck.curl_mhandle, s, NULL);
			nm_clear_g_source (&fdp->ev);
			g_io_channel_unref (fdp->ch);
			g_slice_free (CurlSockData, fdp);
		}
	} else {
		if (!fdp) {
			fdp = g_slice_new0 (CurlSockData);
			fdp->ch = g_io_channel_unix_new (s);
			curl_multi_assign (cb_data->concheck.curl_mhandle, s, fdp);
		} else
			nm_clear_g_source (&fdp->ev);

		if (what == CURL_POLL_IN)
			condition = G_IO_IN;
		else if (what == CURL_POLL_OUT)
			condition = G_IO_OUT;
		else if (what == CURL_POLL_INOUT)
			condition = G_IO_IN | G_IO_OUT;

		if (condition)
			fdp->ev = g_io_add_watch (fdp->ch, condition, curl_socketevent_cb, cb_data);
	}

	return CURLM_OK;
}

static size_t
easy_header_cb (char *buffer, size_t size, size_t nitems, void *userdata)
{
	NMConnectivityCheckHandle *cb_data = userdata;
	size_t len = size * nitems;

	if (   len >= sizeof (HEADER_STATUS_ONLINE) - 1
	    && !g_ascii_strncasecmp (buffer, HEADER_STATUS_ONLINE, sizeof (HEADER_STATUS_ONLINE) - 1)) {
		cb_data_invoke_callback (cb_data, NM_CONNECTIVITY_FULL,
		                         NULL, "status header found");
		return 0;
	}

	return len;
}

static size_t
easy_write_cb (void *buffer, size_t size, size_t nmemb, void *userdata)
{
	NMConnectivityCheckHandle *cb_data = userdata;
	size_t len = size * nmemb;
	const char *response = _check_handle_get_response (cb_data);;

	if (!cb_data->concheck.recv_msg)
		cb_data->concheck.recv_msg = g_string_sized_new (len + 10);

	g_string_append_len (cb_data->concheck.recv_msg, buffer, len);

	if (   response
	    && cb_data->concheck.recv_msg->len >= strlen (response)) {
		/* We already have enough data -- check response */
		if (g_str_has_prefix (cb_data->concheck.recv_msg->str, response)) {
			cb_data_invoke_callback (cb_data, NM_CONNECTIVITY_FULL, NULL,
			                         "expected response");
		} else {
			cb_data_invoke_callback (cb_data, NM_CONNECTIVITY_PORTAL, NULL,
			                         "unexpected response");
		}

		return 0;
	}

	return len;
}

static gboolean
_timeout_cb (gpointer user_data)
{
	NMConnectivityCheckHandle *cb_data = user_data;
	NMConnectivity *self;

	nm_assert (NM_IS_CONNECTIVITY (cb_data->self));

	self = cb_data->self;

	nm_assert (c_list_contains (&NM_CONNECTIVITY_GET_PRIVATE (self)->handles_lst_head, &cb_data->handles_lst));

	cb_data_free (cb_data, NM_CONNECTIVITY_LIMITED, NULL, "timeout");
	return G_SOURCE_REMOVE;
}
#endif

static gboolean
_idle_cb (gpointer user_data)
{
	NMConnectivityCheckHandle *cb_data = user_data;

	nm_assert (NM_IS_CONNECTIVITY (cb_data->self));
	nm_assert (c_list_contains (&NM_CONNECTIVITY_GET_PRIVATE (cb_data->self)->handles_lst_head, &cb_data->handles_lst));

	cb_data->timeout_id = 0;
	if (!cb_data->ifspec) {
		gs_free_error GError *error = NULL;

		/* the invocation was with an invalid ifname. It is a fail. */
		g_set_error (&error, NM_UTILS_ERROR, NM_UTILS_ERROR_INVALID_ARGUMENT,
		             "no interface specified for connectivity check");
		cb_data_free (cb_data, NM_CONNECTIVITY_ERROR, NULL, "missing interface");
	} else
		cb_data_free (cb_data, NM_CONNECTIVITY_FAKE, NULL, "fake result");
	return G_SOURCE_REMOVE;
}

static void
do_curl_request (NMConnectivityCheckHandle *cb_data)
{
	NMConnectivityPrivate *priv = NM_CONNECTIVITY_GET_PRIVATE (cb_data->self);
	CURLM *mhandle;
	CURL *ehandle;
	long resolve;

	mhandle = curl_multi_init ();
	if (!mhandle) {
		cb_data_free (cb_data, NM_CONNECTIVITY_ERROR, NULL, "curl error");
		return;
	}

	ehandle = curl_easy_init ();
	if (!ehandle) {
		curl_multi_cleanup (mhandle);
		cb_data_free (cb_data, NM_CONNECTIVITY_ERROR, NULL, "curl error");
		return;
	}

	cb_data->concheck.response = g_strdup (priv->response);
	cb_data->concheck.curl_mhandle = mhandle;
	cb_data->concheck.curl_ehandle = ehandle;
	cb_data->concheck.request_headers = curl_slist_append (NULL, "Connection: close");
	cb_data->timeout_id = g_timeout_add_seconds (20, _timeout_cb, cb_data);

	curl_multi_setopt (mhandle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
	curl_multi_setopt (mhandle, CURLMOPT_SOCKETDATA, cb_data);
	curl_multi_setopt (mhandle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
	curl_multi_setopt (mhandle, CURLMOPT_TIMERDATA, cb_data);
	curl_multi_setopt (mhandle, CURLOPT_VERBOSE, 1);

	switch (cb_data->addr_family) {
	case AF_INET:
		resolve = CURL_IPRESOLVE_V4;
		break;
	case AF_INET6:
		resolve = CURL_IPRESOLVE_V6;
		break;
	case AF_UNSPEC:
		resolve = CURL_IPRESOLVE_WHATEVER;
		break;
	default:
		resolve = CURL_IPRESOLVE_WHATEVER;
		g_warn_if_reached ();
	}

	curl_easy_setopt (ehandle, CURLOPT_URL, priv->uri);
	curl_easy_setopt (ehandle, CURLOPT_WRITEFUNCTION, easy_write_cb);
	curl_easy_setopt (ehandle, CURLOPT_WRITEDATA, cb_data);
	curl_easy_setopt (ehandle, CURLOPT_HEADERFUNCTION, easy_header_cb);
	curl_easy_setopt (ehandle, CURLOPT_HEADERDATA, cb_data);
	curl_easy_setopt (ehandle, CURLOPT_PRIVATE, cb_data);
	curl_easy_setopt (ehandle, CURLOPT_HTTPHEADER, cb_data->concheck.request_headers);
	curl_easy_setopt (ehandle, CURLOPT_INTERFACE, cb_data->ifspec);
	curl_easy_setopt (ehandle, CURLOPT_RESOLVE, cb_data->concheck.hosts);
	curl_easy_setopt (ehandle, CURLOPT_IPRESOLVE, resolve);

	curl_multi_add_handle (mhandle, ehandle);
}

static void
resolve_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	NMConnectivityCheckHandle *cb_data = user_data;
	NMConnectivity *self;
	NMConnectivityPrivate *priv;
	GVariant *result;
	GVariant *addresses;
	gsize no_addresses;
	int ifindex;
	int addr_family;
	GVariant *address = NULL;
	const guchar *address_buf;
	gsize len = 0;
	char str[INET6_ADDRSTRLEN + 1] = { 0, };
	char *host;
	int i;
	gs_free_error GError *error = NULL;

	result = g_dbus_proxy_call_finish (G_DBUS_PROXY (object), res, &error);
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return;

	self = cb_data->self;
	priv = NM_CONNECTIVITY_GET_PRIVATE (self);

	if (!result) {
		/* Never mind. Just let do curl do its own resolving. */
		_LOG2D ("can't resolve a name via systemd-resolved: %s", error->message);
		do_curl_request (cb_data);
		return;
	}

	addresses = g_variant_get_child_value (result, 0);
	no_addresses = g_variant_n_children (addresses);
	g_variant_unref (result);

	for (i = 0; i < no_addresses; i++) {
		g_variant_get_child (addresses, i, "(ii@ay)", &ifindex, &addr_family, &address);
		address_buf = g_variant_get_fixed_array (address, &len, 1);

		if (   (addr_family == AF_INET && len == sizeof (struct in_addr))
		    || (addr_family == AF_INET6 && len == sizeof (struct in6_addr))) {
			inet_ntop (addr_family, address_buf, str, sizeof (str));
			host = g_strdup_printf ("%s:%s:%s", priv->host,
						priv->port ? priv->port : "80", str);
			cb_data->concheck.hosts = curl_slist_append (cb_data->concheck.hosts, host);
			_LOG2T ("adding '%s' to curl resolve list", host);
			g_free (host);
		}

		g_variant_unref (address);
	}

	g_variant_unref (addresses);
	do_curl_request (cb_data);
}

#define SD_RESOLVED_DNS 1

static void
resolved_proxy_created (GObject *object, GAsyncResult *res, gpointer user_data)
{
	NMConnectivityCheckHandle *cb_data = user_data;
	NMConnectivity *self;
	NMConnectivityPrivate *priv;
	gs_free_error GError *error = NULL;
	GDBusProxy *proxy;

	proxy = g_dbus_proxy_new_for_bus_finish (res, &error);
	if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return;

	self = cb_data->self;
	priv = NM_CONNECTIVITY_GET_PRIVATE (self);

	if (!proxy) {
		/* Log a warning, but still proceed without systemd-resolved */
		_LOG2W ("failed to connect to resolved via DBus: %s", error->message);
		do_curl_request (cb_data);
		return;
	}

	g_dbus_proxy_call (proxy,
	                   "ResolveHostname",
	                   g_variant_new ("(isit)",
	                                  cb_data->concheck.ifindex,
	                                  priv->host,
	                                  (gint32) cb_data->addr_family,
	                                  (guint64) SD_RESOLVED_DNS),
	                   G_DBUS_CALL_FLAGS_NONE,
	                   -1,
	                   cb_data->concheck.resolve_cancellable,
	                   resolve_cb,
	                   cb_data);
	g_object_unref (proxy);

	_LOG2D ("resolving '%s' for '%s' using systemd-resolved", priv->host, priv->uri);
}

NMConnectivityCheckHandle *
nm_connectivity_check_start (NMConnectivity *self,
                             int addr_family,
                             int ifindex,
                             const char *iface,
                             NMConnectivityCheckCallback callback,
                             gpointer user_data)
{
	NMConnectivityPrivate *priv;
	NMConnectivityCheckHandle *cb_data;

	g_return_val_if_fail (NM_IS_CONNECTIVITY (self), NULL);
	g_return_val_if_fail (!iface || iface[0], NULL);
	g_return_val_if_fail (callback, NULL);

	priv = NM_CONNECTIVITY_GET_PRIVATE (self);

	cb_data = g_slice_new0 (NMConnectivityCheckHandle);
	cb_data->self = self;
	c_list_link_tail (&priv->handles_lst_head, &cb_data->handles_lst);
	cb_data->callback = callback;
	cb_data->user_data = user_data;
	cb_data->addr_family = addr_family;

	if (iface)
		cb_data->ifspec = g_strdup_printf ("if!%s", iface);

#if WITH_CONCHECK
	if (iface && ifindex > 0 && priv->enabled && priv->host) {
		cb_data->concheck.ifindex = ifindex;
		cb_data->concheck.resolve_cancellable = g_cancellable_new ();

		g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
		                          G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
		                          G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
		                          NULL,
		                          "org.freedesktop.resolve1",
		                          "/org/freedesktop/resolve1",
		                          "org.freedesktop.resolve1.Manager",
		                          cb_data->concheck.resolve_cancellable,
		                          resolved_proxy_created,
		                          cb_data);

		_LOG2D ("start request to '%s'", priv->uri);
	}
	else
#endif
	{
		_LOG2D ("start fake request");
		cb_data->timeout_id = g_idle_add (_idle_cb, cb_data);
	}

	return cb_data;
}

void
nm_connectivity_check_cancel (NMConnectivityCheckHandle *cb_data)
{
	NMConnectivity *self;
	gs_free_error GError *error = NULL;

	g_return_if_fail (cb_data);

	self = cb_data->self;

	g_return_if_fail (NM_IS_CONNECTIVITY (self));
	g_return_if_fail (!c_list_is_empty (&cb_data->handles_lst));
	g_return_if_fail (cb_data->callback);

	nm_assert (c_list_contains (&NM_CONNECTIVITY_GET_PRIVATE (self)->handles_lst_head, &cb_data->handles_lst));

	nm_utils_error_set_cancelled (&error, FALSE, "NMConnectivity");

	cb_data_free (cb_data, NM_CONNECTIVITY_ERROR, error, "cancelled");
}

/*****************************************************************************/

gboolean
nm_connectivity_check_enabled (NMConnectivity *self)
{
	g_return_val_if_fail (NM_IS_CONNECTIVITY (self), FALSE);

	return NM_CONNECTIVITY_GET_PRIVATE (self)->enabled;
}

/*****************************************************************************/

guint
nm_connectivity_get_interval (NMConnectivity *self)
{
	return nm_connectivity_check_enabled (self)
	       ? NM_CONNECTIVITY_GET_PRIVATE (self)->interval
	       : 0;
}

static gboolean
host_and_port_from_uri (const char *uri, char **host, char **port)
{
	const char *p = uri;
	const char *host_begin = NULL;
	size_t host_len = 0;
	const char *port_begin = NULL;
	size_t port_len = 0;

	/* scheme */
	while (*p != ':' && *p != '/') {
		if (!*p++)
			return FALSE;
	}

	/* :// */
	if (*p++ != ':')
		return FALSE;
	if (*p++ != '/')
		return FALSE;
	if (*p++ != '/')
		return FALSE;
	/* host */
	if (*p == '[')
		return FALSE;
	host_begin = p;
	while (*p && *p != ':' && *p != '/') {
		host_len++;
		p++;
	}
	if (host_len == 0)
		return FALSE;
	*host = strndup (host_begin, host_len);

	/* port */
	if (*p++ == ':') {
		port_begin = p;
		while (*p && *p != '/') {
			port_len++;
			p++;
		}
		if (port_len)
			*port = strndup (port_begin, port_len);
	}

	return TRUE;
}

static void
update_config (NMConnectivity *self, NMConfigData *config_data)
{
	NMConnectivityPrivate *priv = NM_CONNECTIVITY_GET_PRIVATE (self);
	const char *uri, *response;
	guint interval;
	gboolean enabled;
	gboolean changed = FALSE;

	/* Set the URI. */
	uri = nm_config_data_get_connectivity_uri (config_data);
	if (uri && !*uri)
		uri = NULL;
	changed = g_strcmp0 (uri, priv->uri) != 0;
	if (uri) {
		char *scheme = g_uri_parse_scheme (uri);

		if (!scheme) {
			_LOGE ("invalid URI '%s' for connectivity check.", uri);
			uri = NULL;
		} else if (strcasecmp (scheme, "https") == 0) {
			_LOGW ("use of HTTPS for connectivity checking is not reliable and is discouraged (URI: %s)", uri);
		} else if (strcasecmp (scheme, "http") != 0) {
			_LOGE ("scheme of '%s' uri does't use a scheme that is allowed for connectivity check.", uri);
			uri = NULL;
		}

		if (scheme)
			g_free (scheme);
	}
	if (changed) {
		g_free (priv->uri);
		priv->uri = g_strdup (uri);

		g_clear_pointer (&priv->host, g_free);
		g_clear_pointer (&priv->port, g_free);
		host_and_port_from_uri (uri, &priv->host, &priv->port);
	}

	/* Set the interval. */
	interval = nm_config_data_get_connectivity_interval (config_data);
	interval = MIN (interval, (7 * 24 * 3600));
	if (priv->interval != interval) {
		priv->interval = interval;
		changed = TRUE;
	}

	enabled = FALSE;
#if WITH_CONCHECK
	if (   priv->uri
	    && priv->interval)
		enabled = nm_config_data_get_connectivity_enabled (config_data);
#endif

	if (priv->enabled != enabled) {
		priv->enabled = enabled;
		changed = TRUE;
	}

	/* Set the response. */
	response = nm_config_data_get_connectivity_response (config_data);
	if (!nm_streq0 (response, priv->response)) {
		/* a response %NULL means, NM_CONFIG_DEFAULT_CONNECTIVITY_RESPONSE. Any other response
		 * (including "") is accepted. */
		g_free (priv->response);
		priv->response = g_strdup (response);
		changed = TRUE;
	}

	if (changed)
		g_signal_emit (self, signals[CONFIG_CHANGED], 0);
}

static void
config_changed_cb (NMConfig *config,
                   NMConfigData *config_data,
                   NMConfigChangeFlags changes,
                   NMConfigData *old_data,
                   NMConnectivity *self)
{
	update_config (self, config_data);
}

/*****************************************************************************/

static void
nm_connectivity_init (NMConnectivity *self)
{
	NMConnectivityPrivate *priv = NM_CONNECTIVITY_GET_PRIVATE (self);
#if WITH_CONCHECK
	CURLcode ret;
#endif

	c_list_init (&priv->handles_lst_head);

	priv->config = g_object_ref (nm_config_get ());
	g_signal_connect (G_OBJECT (priv->config),
	                  NM_CONFIG_SIGNAL_CONFIG_CHANGED,
	                  G_CALLBACK (config_changed_cb),
	                  self);

#if WITH_CONCHECK
	ret = curl_global_init (CURL_GLOBAL_ALL);
	if (!ret == CURLE_OK) {
		 _LOGE ("unable to init cURL, connectivity check will not work: (%d) %s",
		        ret, curl_easy_strerror (ret));
	}
#endif

	update_config (self, nm_config_get_data (priv->config));
}

static void
dispose (GObject *object)
{
	NMConnectivity *self = NM_CONNECTIVITY (object);
	NMConnectivityPrivate *priv = NM_CONNECTIVITY_GET_PRIVATE (self);
	NMConnectivityCheckHandle *cb_data;
	GError *error = NULL;

again:
	c_list_for_each_entry (cb_data, &priv->handles_lst_head, handles_lst) {
		if (!error)
			nm_utils_error_set_cancelled (&error, TRUE, "NMConnectivity");
		cb_data_free (cb_data, NM_CONNECTIVITY_ERROR, error, "shutting down");
		goto again;
	}
	g_clear_error (&error);

	g_clear_pointer (&priv->uri, g_free);
	g_clear_pointer (&priv->host, g_free);
	g_clear_pointer (&priv->port, g_free);
	g_clear_pointer (&priv->response, g_free);

#if WITH_CONCHECK
	curl_global_cleanup ();
#endif

	if (priv->config) {
		g_signal_handlers_disconnect_by_func (priv->config, config_changed_cb, self);
		g_clear_object (&priv->config);
	}

	G_OBJECT_CLASS (nm_connectivity_parent_class)->dispose (object);
}

static void
nm_connectivity_class_init (NMConnectivityClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	signals[CONFIG_CHANGED] =
	    g_signal_new (NM_CONNECTIVITY_CONFIG_CHANGED,
	                  G_OBJECT_CLASS_TYPE (object_class),
	                  G_SIGNAL_RUN_FIRST,
	                  0, NULL, NULL, NULL,
	                  G_TYPE_NONE, 0);

	object_class->dispose = dispose;
}
