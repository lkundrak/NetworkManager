#include "nm-default.h"

#include "nm-config.h"
#include "nm-connectivity.h"
#include "platform/nmp-object.h"
#include "platform/nm-linux-platform.h"

#include <stdlib.h>
#include <syslog.h>

#include "nm-test-utils-core.h"

NMTST_DEFINE ();

int pending = 0;
GMainLoop *loop;

static void
conncheck_cb (NMConnectivity *self, NMConnectivityCheckHandle *handle,
              NMConnectivityState state, GError *error, gpointer user_data)
{
	NMPlatformLink *link = user_data;

	if (--pending == 0)
		g_main_loop_quit (loop);

	g_printerr ("%d: %s [%s] {%s}\n",
	            link->ifindex, link->name,
	            nm_connectivity_state_to_string (state),
                    error ? error->message : "Success");
}


int
main (int argc, char **argv)
{
	NMConfig *config;
	NMConnectivity *connectivity;
	NMPlatform *platform;
	GKeyFile *keyfile;
	GPtrArray *links;
	int i;

	if (!g_getenv ("G_MESSAGES_DEBUG"))
		g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	nmtst_init_with_logging (&argc, &argv, "TRACE", "CONCHECK");

	loop = g_main_loop_new (NULL, FALSE);

	config = nm_config_setup (NULL, NULL, NULL);

	keyfile = g_key_file_new ();
	g_key_file_load_from_data (keyfile,
		"[connectivity]\n"
		"uri=http://fedoraproject.org/static/hotspot.txt\n"
		"response=OK\n"
		"interval=300\n"
		"enabled=true\n",
		-1, G_KEY_FILE_NONE, NULL);
	nm_config_set_values (config, keyfile, FALSE, FALSE);

	//{
	//	NMConfigData *config_data = nm_config_get_data (config);
	//	nm_config_data_log (config_data, "", "", stdout);
	//}
	nm_linux_platform_setup ();


	platform = nm_platform_get ();
	connectivity = nm_connectivity_get ();

	links = nm_platform_link_get_all (platform, FALSE);

	for (i = 0; i < links->len; i++) {
		NMPlatformLink *link = NMP_OBJECT_CAST_LINK (g_ptr_array_index (links, i));

		if (argc > 1 && strcmp (argv[1], link->name) != 0)
			continue;

		pending++;
		nm_connectivity_check_start (connectivity, AF_INET6, link->ifindex, link->name, conncheck_cb, link);
		pending++;
		nm_connectivity_check_start (connectivity, AF_INET, link->ifindex, link->name, conncheck_cb, link);
	}

	g_main_loop_run (loop);

	g_ptr_array_unref (links);
	g_object_unref (connectivity);
	g_object_unref (platform);

	return EXIT_SUCCESS;
}
