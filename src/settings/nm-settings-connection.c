/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/* NetworkManager system settings service
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
 * (C) Copyright 2008 Novell, Inc.
 * (C) Copyright 2008 - 2011 Red Hat, Inc.
 */

#include <string.h>

#include <NetworkManager.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <nm-setting-connection.h>
#include <nm-utils.h>

#include "nm-settings-connection.h"
#include "nm-session-monitor.h"
#include "nm-dbus-manager.h"
#include "nm-settings-error.h"
#include "nm-dbus-glib-types.h"
#include "nm-polkit-helpers.h"
#include "nm-logging.h"
#include "nm-manager-auth.h"
#include "nm-marshal.h"

static void impl_settings_connection_get_settings (NMSettingsConnection *connection,
                                                   DBusGMethodInvocation *context);

static void impl_settings_connection_update (NMSettingsConnection *connection,
                                             GHashTable *new_settings,
                                             DBusGMethodInvocation *context);

static void impl_settings_connection_delete (NMSettingsConnection *connection,
                                             DBusGMethodInvocation *context);

static void impl_settings_connection_get_secrets (NMSettingsConnection *connection,
                                                  const gchar *setting_name,
                                                  DBusGMethodInvocation *context);

#include "nm-settings-connection-glue.h"

G_DEFINE_TYPE (NMSettingsConnection, nm_settings_connection, NM_TYPE_CONNECTION)

#define NM_SETTINGS_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
                                               NM_TYPE_SETTINGS_CONNECTION, \
                                               NMSettingsConnectionPrivate))

enum {
	PROP_0 = 0,
	PROP_VISIBLE,
};

enum {
	UPDATED,
	REMOVED,
	GET_SECRETS,
	CANCEL_SECRETS,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
	gboolean disposed;

	PolkitAuthority *authority;
	GSList *pending_auths; /* List of pending authentication requests */
	NMConnection *secrets;
	gboolean visible; /* Is this connection is visible by some session? */

	GSList *reqs;  /* in-progress secrets requests */

	NMSessionMonitor *session_monitor;
	guint session_changed_id;
} NMSettingsConnectionPrivate;

/**************************************************************/

#define USER_TAG "user:"

/* Extract the username from the permission string and dump to a buffer */
static gboolean
perm_to_user (const char *perm, char *out_user, gsize out_user_size)
{
	const char *end;
	gsize userlen;

	g_return_val_if_fail (perm != NULL, FALSE);
	g_return_val_if_fail (out_user != NULL, FALSE);

	if (!g_str_has_prefix (perm, USER_TAG))
		return FALSE;
	perm += strlen (USER_TAG);

	/* Look for trailing ':' */
	end = strchr (perm, ':');
	if (!end)
		end = perm + strlen (perm);

	userlen = end - perm;
	if (userlen > (out_user_size + 1))
		return FALSE;
	memcpy (out_user, perm, userlen);
	out_user[userlen] = '\0';
	return TRUE;
}

/**************************************************************/

static void
set_visible (NMSettingsConnection *self, gboolean new_visible)
{
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);

	if (new_visible == priv->visible)
		return;
	priv->visible = new_visible;
	g_object_notify (G_OBJECT (self), NM_SETTINGS_CONNECTION_VISIBLE);
}

gboolean
nm_settings_connection_is_visible (NMSettingsConnection *self)
{
	g_return_val_if_fail (NM_SETTINGS_CONNECTION (self), FALSE);

	return NM_SETTINGS_CONNECTION_GET_PRIVATE (self)->visible;
}

void
nm_settings_connection_recheck_visibility (NMSettingsConnection *self)
{
	NMSettingsConnectionPrivate *priv;
	NMSettingConnection *s_con;
	guint32 num, i;

	g_return_if_fail (NM_SETTINGS_CONNECTION (self));

	priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);

	s_con = (NMSettingConnection *) nm_connection_get_setting (NM_CONNECTION (self), NM_TYPE_SETTING_CONNECTION);
	g_assert (s_con);

	/* Check every user in the ACL for a session */
	num = nm_setting_connection_get_num_permissions (s_con);
	if (num == 0) {
		/* Visible to all */
		set_visible (self, TRUE);
		return;
	}

	for (i = 0; i < num; i++) {
		const char *perm;
		char buf[75];

		perm = nm_setting_connection_get_permission (s_con, i);
		g_assert (perm);
		if (perm_to_user (perm, buf, sizeof (buf))) {
			if (nm_session_monitor_user_has_session (priv->session_monitor, buf, NULL, NULL)) {
				set_visible (self, TRUE);
				return;
			}
		}
	}

	set_visible (self, FALSE);
}

static void
session_changed_cb (NMSessionMonitor *self, gpointer user_data)
{
	nm_settings_connection_recheck_visibility (NM_SETTINGS_CONNECTION (user_data));
}

/**************************************************************/

/* Update the settings of this connection to match that of 'new', taking care to
 * make a private copy of secrets. */
gboolean
nm_settings_connection_replace_settings (NMSettingsConnection *self,
                                         NMConnection *new,
                                         GError **error)
{
	NMSettingsConnectionPrivate *priv;
	GHashTable *new_settings;
	gboolean success = FALSE;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (NM_IS_SETTINGS_CONNECTION (self), FALSE);
	g_return_val_if_fail (new != NULL, FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (new), FALSE);

	priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);

	new_settings = nm_connection_to_hash (new, NM_SETTING_HASH_FLAG_ALL);
	g_assert (new_settings);
	if (nm_connection_replace_settings (NM_CONNECTION (self), new_settings, error)) {
		/* Copy the connection to keep its secrets around even if NM
		 * calls nm_connection_clear_secrets().
		 */
		if (priv->secrets)
			g_object_unref (priv->secrets);
		priv->secrets = nm_connection_duplicate (NM_CONNECTION (self));

		nm_settings_connection_recheck_visibility (self);
		success = TRUE;
	}
	g_hash_table_destroy (new_settings);
	return success;
}

static void
ignore_cb (NMSettingsConnection *connection,
           GError *error,
           gpointer user_data)
{
}

/* Replaces the settings in this connection with those in 'new'. If any changes
 * are made, commits them to permanent storage and to any other subsystems
 * watching this connection. Before returning, 'callback' is run with the given
 * 'user_data' along with any errors encountered.
 */
void
nm_settings_connection_replace_and_commit (NMSettingsConnection *self,
                                           NMConnection *new,
                                           NMSettingsConnectionCommitFunc callback,
                                           gpointer user_data)
{
	GError *error = NULL;

	g_return_if_fail (self != NULL);
	g_return_if_fail (NM_IS_SETTINGS_CONNECTION (self));
	g_return_if_fail (new != NULL);
	g_return_if_fail (NM_IS_CONNECTION (new));

	if (!callback)
		callback = ignore_cb;

	/* Do nothing if there's nothing to update */
	if (nm_connection_compare (NM_CONNECTION (self),
	                           NM_CONNECTION (new),
	                           NM_SETTING_COMPARE_FLAG_EXACT)) {
		callback (self, NULL, user_data);
		return;
	}

	if (nm_settings_connection_replace_settings (self, new, &error)) {
		nm_settings_connection_commit_changes (self, callback, user_data);
	} else {
		callback (self, error, user_data);
		g_clear_error (&error);
	}
}

void
nm_settings_connection_commit_changes (NMSettingsConnection *connection,
                                       NMSettingsConnectionCommitFunc callback,
                                       gpointer user_data)
{
	g_return_if_fail (connection != NULL);
	g_return_if_fail (NM_IS_SETTINGS_CONNECTION (connection));
	g_return_if_fail (callback != NULL);

	if (NM_SETTINGS_CONNECTION_GET_CLASS (connection)->commit_changes) {
		NM_SETTINGS_CONNECTION_GET_CLASS (connection)->commit_changes (connection,
		                                                               callback,
		                                                               user_data);
	} else {
		GError *error = g_error_new (NM_SETTINGS_ERROR,
		                             NM_SETTINGS_ERROR_INTERNAL_ERROR,
		                             "%s: %s:%d commit_changes() unimplemented", __func__, __FILE__, __LINE__);
		callback (connection, error, user_data);
		g_error_free (error);
	}
}

void
nm_settings_connection_delete (NMSettingsConnection *connection,
                               NMSettingsConnectionDeleteFunc callback,
                               gpointer user_data)
{
	g_return_if_fail (connection != NULL);
	g_return_if_fail (NM_IS_SETTINGS_CONNECTION (connection));
	g_return_if_fail (callback != NULL);

	if (NM_SETTINGS_CONNECTION_GET_CLASS (connection)->delete) {
		NM_SETTINGS_CONNECTION_GET_CLASS (connection)->delete (connection,
		                                                       callback,
		                                                       user_data);
	} else {
		GError *error = g_error_new (NM_SETTINGS_ERROR,
		                             NM_SETTINGS_ERROR_INTERNAL_ERROR,
		                             "%s: %s:%d delete() unimplemented", __func__, __FILE__, __LINE__);
		callback (connection, error, user_data);
		g_error_free (error);
	}
}

static void
commit_changes (NMSettingsConnection *connection,
                NMSettingsConnectionCommitFunc callback,
                gpointer user_data)
{
	g_object_ref (connection);
	g_signal_emit (connection, signals[UPDATED], 0);
	callback (connection, NULL, user_data);
	g_object_unref (connection);
}

static void
do_delete (NMSettingsConnection *connection,
           NMSettingsConnectionDeleteFunc callback,
           gpointer user_data)
{
	g_object_ref (connection);
	set_visible (connection, FALSE);
	g_signal_emit (connection, signals[REMOVED], 0);
	callback (connection, NULL, user_data);
	g_object_unref (connection);
}

/**************************************************************/

static gboolean
supports_secrets (NMSettingsConnection *connection, const char *setting_name)
{
	/* All secrets supported */
	return TRUE;
}

/**
 * nm_settings_connection_get_secrets:
 * @connection: the #NMSettingsConnection
 * @setting_name: the setting to return secrets for
 * @error: an error on return, if an error occured
 *
 * Return secrets in persistent storage, if any.  Does not query any Secret
 * Agents for secrets.
 *
 * Returns: a hash mapping setting names to hash tables, each inner hash
 * containing string:value mappings of secrets
 **/
GHashTable *
nm_settings_connection_get_secrets (NMSettingsConnection *connection,
                                    const char *setting_name,
                                    GError **error)
{
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (connection);
	NMSetting *setting;

	/* Use priv->secrets to work around the fact that nm_connection_clear_secrets()
	 * will clear secrets on this object's settings.  priv->secrets should be
	 * a complete copy of this object and kept in sync by
	 * nm_settings_connection_replace_settings().
	 */
	if (!priv->secrets) {
		g_set_error (error, NM_SETTINGS_ERROR, NM_SETTINGS_ERROR_INVALID_CONNECTION,
		             "%s.%d - Internal error; secrets cache invalid.",
		             __FILE__, __LINE__);
		return NULL;
	}

	/* FIXME: if setting_name is empty, return all secrets */

	setting = nm_connection_get_setting_by_name (priv->secrets, setting_name);
	if (!setting) {
		g_set_error (error, NM_SETTINGS_ERROR, NM_SETTINGS_ERROR_INVALID_SETTING,
		             "%s.%d - Connection didn't have requested setting '%s'.",
		             __FILE__, __LINE__, setting_name);
		return NULL;
	}

	return nm_connection_to_hash (priv->secrets, NM_SETTING_HASH_FLAG_ONLY_SECRETS);
}

/**** User authorization **************************************/

typedef void (*AuthCallback) (NMSettingsConnection *connection, 
                              DBusGMethodInvocation *context,
                              GError *error,
                              gpointer data);

static void
pk_auth_cb (NMAuthChain *chain,
            GError *chain_error,
            DBusGMethodInvocation *context,
            gpointer user_data)
{
	NMSettingsConnection *self = NM_SETTINGS_CONNECTION (user_data);
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);
	GError *error = NULL;
	NMAuthCallResult result;
	AuthCallback callback;
	gpointer callback_data;

	priv->pending_auths = g_slist_remove (priv->pending_auths, chain);

	/* If our NMSettingsConnection is already gone, do nothing */
	if (chain_error) {
		error = g_error_new (NM_SETTINGS_ERROR,
		                     NM_SETTINGS_ERROR_GENERAL,
		                     "Error checking authorization: %s",
		                     chain_error->message ? chain_error->message : "(unknown)");
	} else {
		result = nm_auth_chain_get_result (chain, NM_AUTH_PERMISSION_SETTINGS_CONNECTION_MODIFY);

		/* Caller didn't successfully authenticate */
		if (result != NM_AUTH_CALL_RESULT_YES) {
			error = g_error_new_literal (NM_SETTINGS_ERROR,
			                             NM_SETTINGS_ERROR_NOT_PRIVILEGED,
			                             "Insufficient privileges.");
		}
	}

	callback = nm_auth_chain_get_data (chain, "callback");
	callback_data = nm_auth_chain_get_data (chain, "callback-data");
	callback (self, context, error, callback_data);

	g_clear_error (&error);
	nm_auth_chain_unref (chain);
}

static void
auth_start (NMSettingsConnection *self,
            DBusGMethodInvocation *context,
            gboolean check_modify,
            AuthCallback callback,
            gpointer callback_data)
{
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);
	NMAuthChain *chain;
	gulong sender_uid = G_MAXULONG;
	GError *error = NULL;
	char *error_desc = NULL;

	/* Get the caller's UID */
	if (!nm_auth_get_caller_uid (context,  NULL, &sender_uid, &error_desc)) {
		error = g_error_new_literal (NM_SETTINGS_ERROR,
		                             NM_SETTINGS_ERROR_PERMISSION_DENIED,
		                             error_desc);
		g_free (error);
		goto error;
	}

	/* Make sure the UID can view this connection */
	if (0 != sender_uid) {
		if (!nm_auth_uid_in_acl (NM_CONNECTION (self),
		                         priv->session_monitor,
		                         sender_uid,
		                         &error_desc)) {
			error = g_error_new_literal (NM_SETTINGS_ERROR,
			                             NM_SETTINGS_ERROR_PERMISSION_DENIED,
			                             error_desc);
			g_free (error_desc);
			goto error;
		}
	}

	if (check_modify) {
		chain = nm_auth_chain_new (priv->authority, context, NULL, pk_auth_cb, self);
		g_assert (chain);
		priv->pending_auths = g_slist_append (priv->pending_auths, chain);
		nm_auth_chain_add_call (chain, NM_AUTH_PERMISSION_SETTINGS_CONNECTION_MODIFY, TRUE);
		nm_auth_chain_set_data (chain, "callback", callback, NULL);
		nm_auth_chain_set_data (chain, "callback-data", callback_data, NULL);
	} else {
		/* Don't need polkit auth, automatic success */
		callback (self, context, NULL, callback_data);
	}

	return;

error:
	callback (self, context, error, callback_data);
	g_error_free (error);
}

/**** DBus method handlers ************************************/

static gboolean
check_writable (NMConnection *connection, GError **error)
{
	NMSettingConnection *s_con;

	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	s_con = (NMSettingConnection *) nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
	if (!s_con) {
		g_set_error_literal (error,
		                     NM_SETTINGS_ERROR,
		                     NM_SETTINGS_ERROR_INVALID_CONNECTION,
		                     "Connection did not have required 'connection' setting");
		return FALSE;
	}

	/* If the connection is read-only, that has to be changed at the source of
	 * the problem (ex a system settings plugin that can't write connections out)
	 * instead of over D-Bus.
	 */
	if (nm_setting_connection_get_read_only (s_con)) {
		g_set_error_literal (error,
		                     NM_SETTINGS_ERROR,
		                     NM_SETTINGS_ERROR_READ_ONLY_CONNECTION,
		                     "Connection is read-only");
		return FALSE;
	}

	return TRUE;
}

static void
get_settings_auth_cb (NMSettingsConnection *self, 
	                  DBusGMethodInvocation *context,
	                  GError *error,
	                  gpointer data)
{
	if (error)
		dbus_g_method_return_error (context, error);
	else {
		GHashTable *settings;

		/* Secrets should *never* be returned by the GetSettings method, they
		 * get returned by the GetSecrets method which can be better
		 * protected against leakage of secrets to unprivileged callers.
		 */
		settings = nm_connection_to_hash (NM_CONNECTION (self), NM_SETTING_HASH_FLAG_NO_SECRETS);
		g_assert (settings);
		dbus_g_method_return (context, settings);
		g_hash_table_destroy (settings);
	}
}

static void
impl_settings_connection_get_settings (NMSettingsConnection *self,
                                       DBusGMethodInvocation *context)
{
	auth_start (self, context, FALSE, get_settings_auth_cb, NULL);
}

static void
con_update_cb (NMSettingsConnection *connection,
               GError *error,
               gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context);
}

static void
update_auth_cb (NMSettingsConnection *self, 
                DBusGMethodInvocation *context,
                GError *error,
                gpointer data)
{
	NMConnection *new_settings = data;

	if (error) {
		dbus_g_method_return_error (context, error);
		goto out;
	}

	/* Update and commit our settings. */
	nm_settings_connection_replace_and_commit (self, 
	                                           new_settings,
	                                           con_update_cb,
	                                           context);

out:
	g_object_unref (new_settings);
}

static void
impl_settings_connection_update (NMSettingsConnection *self,
                                 GHashTable *new_settings,
                                 DBusGMethodInvocation *context)
{
	NMConnection *tmp;
	GError *error = NULL;

	/* If the connection is read-only, that has to be changed at the source of
	 * the problem (ex a system settings plugin that can't write connections out)
	 * instead of over D-Bus.
	 */
	if (!check_writable (NM_CONNECTION (self), &error)) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	/* Check if the settings are valid first */
	tmp = nm_connection_new_from_hash (new_settings, &error);
	if (!tmp) {
		g_assert (error);
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	auth_start (self, context, TRUE, update_auth_cb, tmp);
}

static void
con_delete_cb (NMSettingsConnection *connection,
               GError *error,
               gpointer user_data)
{
	DBusGMethodInvocation *context = user_data;

	if (error)
		dbus_g_method_return_error (context, error);
	else
		dbus_g_method_return (context);
}

static void
delete_auth_cb (NMSettingsConnection *self, 
                DBusGMethodInvocation *context,
                GError *error,
                gpointer data)
{
	if (error) {
		dbus_g_method_return_error (context, error);
		return;
	}

	nm_settings_connection_delete (self, con_delete_cb, context);
}

static void
impl_settings_connection_delete (NMSettingsConnection *self,
                                 DBusGMethodInvocation *context)
{
	GError *error = NULL;
	
	if (!check_writable (NM_CONNECTION (self), &error)) {
		dbus_g_method_return_error (context, error);
		g_error_free (error);
		return;
	}

	auth_start (self, context, TRUE, delete_auth_cb, NULL);
}

/**************************************************************/

static gboolean
get_secrets_accumulator (GSignalInvocationHint *ihint,
                         GValue *return_accu,
                         const GValue *handler_return,
                         gpointer data)
{
	guint handler_call_id = g_value_get_uint (handler_return);

	if (handler_call_id > 0)
		g_value_set_uint (return_accu, handler_call_id);

	/* Abort signal emission if a valid call ID got returned */
	return handler_call_id ? FALSE : TRUE;
}

static void
dbus_get_agent_secrets_cb (NMSettingsConnection *self,
                           const char *setting_name,
                           guint32 call_id,
                           GError *error,
                           gpointer user_data)
{
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);
	DBusGMethodInvocation *context = user_data;
	GHashTable *hash;

	priv->reqs = g_slist_remove (priv->reqs, GUINT_TO_POINTER (call_id));

	/* The connection's secrets will have been updated by the agent manager,
	 * so we want to refresh the secrets cache.
	 */
	if (priv->secrets)
		g_object_unref (priv->secrets);
	priv->secrets = nm_connection_duplicate (NM_CONNECTION (self));

	if (error)
		dbus_g_method_return_error (context, error);
	else {
		hash = nm_connection_to_hash (NM_CONNECTION (self), NM_SETTING_HASH_FLAG_ONLY_SECRETS);
		dbus_g_method_return (context, hash);
		g_hash_table_destroy (hash);
	}
}

static void
dbus_secrets_auth_cb (NMSettingsConnection *self, 
                      DBusGMethodInvocation *context,
                      GError *error,
                      gpointer user_data)
{
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);
	char *sender, *setting_name = user_data;
	guint32 call_id = 0;
	GError *local = NULL;

	sender = dbus_g_method_get_sender (context);
	if (!sender) {
		local = g_error_new_literal (NM_SETTINGS_ERROR,
		                             NM_SETTINGS_ERROR_PERMISSION_DENIED,
		                             "Unable to get request D-Bus sender");
	} else if (!error) {
		g_signal_emit (self, signals[GET_SECRETS], 0,
		               sender,
		               setting_name,
		               dbus_get_agent_secrets_cb,
		               context,
		               &call_id);
		if (call_id > 0) {
			/* track the request and wait for the callback */
			priv->reqs = g_slist_append (priv->reqs, GUINT_TO_POINTER (call_id));
		} else {
			local = g_error_new_literal (NM_SETTINGS_ERROR,
			                             NM_SETTINGS_ERROR_SECRETS_UNAVAILABLE,
			                             "No secrets were available");
		}
	}

	if (error || local)
		dbus_g_method_return_error (context, error ? error : local);

	g_free (setting_name);
	g_free (sender);
	g_clear_error (&local);
}

static void
impl_settings_connection_get_secrets (NMSettingsConnection *self,
                                      const gchar *setting_name,
                                      DBusGMethodInvocation *context)
{
	auth_start (self, context, TRUE, dbus_secrets_auth_cb, g_strdup (setting_name));
}

/**************************************************************/

static void
nm_settings_connection_init (NMSettingsConnection *self)
{
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);
	static guint32 dbus_counter = 0;
	char *dbus_path;
	GError *error = NULL;

	priv->authority = polkit_authority_get_sync (NULL, NULL);
	if (!priv->authority) {
		nm_log_warn (LOGD_SETTINGS, "failed to create PolicyKit authority: (%d) %s",
		             error ? error->code : -1,
		             error && error->message ? error->message : "(unknown)");
		g_clear_error (&error);
	}

	dbus_path = g_strdup_printf ("%s/%u", NM_DBUS_PATH_SETTINGS, dbus_counter++);
	nm_connection_set_path (NM_CONNECTION (self), dbus_path);
	g_free (dbus_path);
	priv->visible = FALSE;

	priv->session_monitor = nm_session_monitor_get ();
	priv->session_changed_id = g_signal_connect (priv->session_monitor,
	                                             NM_SESSION_MONITOR_CHANGED,
	                                             G_CALLBACK (session_changed_cb),
	                                             self);
}

static void
dispose (GObject *object)
{
	NMSettingsConnection *self = NM_SETTINGS_CONNECTION (object);
	NMSettingsConnectionPrivate *priv = NM_SETTINGS_CONNECTION_GET_PRIVATE (self);
	GSList *iter;

	if (priv->disposed)
		goto out;
	priv->disposed = TRUE;

	if (priv->secrets)
		g_object_unref (priv->secrets);

	/* Cancel PolicyKit requests */
	for (iter = priv->pending_auths; iter; iter = g_slist_next (iter))
		nm_auth_chain_unref ((NMAuthChain *) iter->data);
	g_slist_free (priv->pending_auths);
	priv->pending_auths = NULL;

	/* Cancel in-progress secrets requests */
	for (iter = priv->reqs; iter; iter = g_slist_next (iter))
		g_signal_emit (self, signals[CANCEL_SECRETS], 0, GPOINTER_TO_UINT (iter->data));
	g_slist_free (priv->reqs);

	set_visible (self, FALSE);

	g_object_unref (priv->session_monitor);

out:
	G_OBJECT_CLASS (nm_settings_connection_parent_class)->dispose (object);
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	case PROP_VISIBLE:
		g_value_set_boolean (value, NM_SETTINGS_CONNECTION_GET_PRIVATE (object)->visible);
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
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
nm_settings_connection_class_init (NMSettingsConnectionClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	g_type_class_add_private (class, sizeof (NMSettingsConnectionPrivate));

	/* Virtual methods */
	object_class->dispose = dispose;
	object_class->get_property = get_property;
	object_class->set_property = set_property;

	class->commit_changes = commit_changes;
	class->delete = do_delete;
	class->supports_secrets = supports_secrets;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_VISIBLE,
		 g_param_spec_boolean (NM_SETTINGS_CONNECTION_VISIBLE,
		                       "Visible",
		                       "Visible",
		                       FALSE,
		                       G_PARAM_READABLE));

	/* Signals */
	signals[UPDATED] = 
		g_signal_new (NM_SETTINGS_CONNECTION_UPDATED,
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_FIRST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	signals[REMOVED] = 
		g_signal_new (NM_SETTINGS_CONNECTION_REMOVED,
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_FIRST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	/* not exported over D-Bus */
	signals[GET_SECRETS] = 
		g_signal_new (NM_SETTINGS_CONNECTION_GET_SECRETS,
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_FIRST,
		              G_STRUCT_OFFSET (NMSettingsConnectionClass, get_secrets),
		              get_secrets_accumulator, NULL,
		              _nm_marshal_UINT__STRING_STRING_POINTER_POINTER,
		              G_TYPE_UINT, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_POINTER);

	signals[CANCEL_SECRETS] =
		g_signal_new (NM_SETTINGS_CONNECTION_CANCEL_SECRETS,
		              G_TYPE_FROM_CLASS (class),
		              G_SIGNAL_RUN_FIRST,
		              0, NULL, NULL,
		              g_cclosure_marshal_VOID__UINT,
		              G_TYPE_NONE, 0);

	dbus_g_object_type_install_info (G_TYPE_FROM_CLASS (class),
	                                 &dbus_glib_nm_settings_connection_object_info);
}
