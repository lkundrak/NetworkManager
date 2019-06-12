#include "nm-default.h"

#include "nm-simple-connection.h"
#include "nm-setting-connection.h"
#include "nm-setting-ovs-bridge.h"
#include "nm-setting-ovs-port.h"
#include "nm-setting-ovs-interface.h"
#include "nm-setting-ovs-dpdk.h"
#include "nm-ovsdb.h"

static void
ovsdb_device_added (NMOvsdb *ovsdb, const char *name, NMDeviceType device_type,
                    gpointer user_data)
{
	g_printerr ("ADDED {%s} {%d}\n", name, device_type);
}

static void
ovsdb_device_removed (NMOvsdb *ovsdb, const char *name, NMDeviceType device_type,
                      gpointer user_data)
{
	g_printerr ("REMOVED {%s} {%d}\n", name, device_type);
}

static void
ovsdb_interface_failed (NMOvsdb *ovsdb, const char *name, const char *uuid, const char *error,
                      gpointer user_data)
{
	g_printerr ("FAILED {%s} {%s} {%s}\n", name, error);
}

static void
deleted_2 (GError *error, gpointer user_data)
{
	GMainLoop *loop = user_data;

	g_printerr ("DELETED {%s}\n", error ? error->message : "(success)");
	g_main_loop_quit (loop);
}

static void
added (GError *error, gpointer user_data)
{
	GMainLoop *loop = user_data;

	g_printerr ("ADDED {%s}\n", error ? error->message : "(success)");
	if (0)
		g_main_loop_quit (loop);
	if (0)
		nm_ovsdb_del_interface (nm_ovsdb_get (), "test_interface", deleted_2, loop);
}

static void
deleted_1 (GError *error, gpointer user_data)
{
	GMainLoop *loop = user_data;
	NMConnection *bridge;
	NMConnection *port;
	NMConnection *interface;
	GError *local = NULL;

	g_printerr ("=== bridge ===\n");
	bridge = nm_simple_connection_new ();
	nm_connection_add_setting (bridge,
		g_object_new (NM_TYPE_SETTING_CONNECTION,
	                      NM_SETTING_CONNECTION_TYPE, NM_SETTING_OVS_BRIDGE_SETTING_NAME,
	                      NM_SETTING_CONNECTION_ID, "test_bridge",
	                      NM_SETTING_CONNECTION_INTERFACE_NAME, "test_bridge",
	                      NM_SETTING_CONNECTION_UUID, "97e9c957-2270-4451-b138-d09df4bd8063",
	                      NULL));
	nm_connection_normalize (bridge, NULL, NULL, &local);
	g_assert_no_error (local);
	nm_connection_dump (bridge);

	g_printerr ("\n=== port ===\n");
	port = nm_simple_connection_new ();
	nm_connection_add_setting (port,
		g_object_new (NM_TYPE_SETTING_CONNECTION,
	                      NM_SETTING_CONNECTION_TYPE, NM_SETTING_OVS_PORT_SETTING_NAME,
	                      NM_SETTING_CONNECTION_ID, "test_port",
	                      NM_SETTING_CONNECTION_INTERFACE_NAME, "test_port",
	                      NM_SETTING_CONNECTION_UUID, "841bbfb0-a03d-42f6-8e9a-776566741e69",
	                      NM_SETTING_CONNECTION_MASTER, "test_bridge",
	                      NULL));
	nm_connection_add_setting (port,
		g_object_new (NM_TYPE_SETTING_OVS_PORT,
	                      NULL));
	nm_connection_normalize (port, NULL, NULL, &local);
	g_assert_no_error (local);
	nm_connection_dump (port);

	g_printerr ("\n=== interface ===\n");
	interface = nm_simple_connection_new ();
	nm_connection_add_setting (interface,
		g_object_new (NM_TYPE_SETTING_CONNECTION,
	                      NM_SETTING_CONNECTION_TYPE, NM_SETTING_OVS_INTERFACE_SETTING_NAME,
	                      NM_SETTING_CONNECTION_ID, "test_interface",
	                      NM_SETTING_CONNECTION_INTERFACE_NAME, "test_interface",
	                      NM_SETTING_CONNECTION_UUID, "aa659d72-b42b-4106-bd01-4beaea47db77",
	                      NM_SETTING_CONNECTION_MASTER, "test_port",
	                      NULL));
	nm_connection_add_setting (interface,
		g_object_new (NM_TYPE_SETTING_OVS_INTERFACE,
	                      NM_SETTING_OVS_INTERFACE_TYPE, "dpdk",
	                      NULL));
	nm_connection_add_setting (interface,
		g_object_new (NM_TYPE_SETTING_OVS_DPDK,
	                      NM_SETTING_OVS_DPDK_DEVARGS, "eth_af_packet0,iface=eth0",
	                      NULL));
	nm_connection_normalize (interface, NULL, NULL, &local);
	g_assert_no_error (local);
	nm_connection_dump (interface);
	g_printerr ("\n");

	nm_ovsdb_add_interface (nm_ovsdb_get (), bridge, port, interface, added, loop);
}

int
main (int argc, char *argv[])
{
	NMOvsdb *ovsdb;
	GMainLoop *loop;

	if (!g_getenv ("G_MESSAGES_DEBUG"))
		g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	nm_logging_setup ("TRACE", "ALL", NULL, NULL);

	loop = g_main_loop_new (NULL, FALSE);
	ovsdb = nm_ovsdb_get ();

	g_signal_connect_object (ovsdb, NM_OVSDB_DEVICE_ADDED, G_CALLBACK (ovsdb_device_added), NULL, (GConnectFlags) 0);
	g_signal_connect_object (ovsdb, NM_OVSDB_DEVICE_REMOVED, G_CALLBACK (ovsdb_device_removed), NULL, (GConnectFlags) 0);
	g_signal_connect_object (ovsdb, NM_OVSDB_INTERFACE_FAILED, G_CALLBACK (ovsdb_interface_failed), NULL, (GConnectFlags) 0);

	nm_ovsdb_del_interface (ovsdb, "test_interface", deleted_1, loop);

	g_main_loop_run (loop);

	return 0;
}
