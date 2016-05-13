/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

/* NetworkManager system settings service (ofono)
 *
 * Mathieu Trudel-Lapierre <mathieu-tl@ubuntu.com>
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
 * (C) Copyright 2013-2016 Canonical Ltd.
 */

#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include <glib-object.h>

#define PLUGIN_NAME "ofono"

#define SETTINGS_TYPE_PLUGIN_OFONO            (settings_plugin_ofono_get_type ())
#define SETTINGS_PLUGIN_OFONO(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SETTINGS_TYPE_PLUGIN_OFONO, SettingsPluginOfono))
#define SETTINGS_PLUGIN_OFONO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SETTINGS_TYPE_PLUGIN_OFONO, SettingsPluginOfonoClass))
#define SETTINGS_IS_PLUGIN_OFONO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SETTINGS_TYPE_PLUGIN_OFONO))
#define SETTINGS_IS_PLUGIN_OFONO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SETTINGS_TYPE_PLUGIN_OFONO))
#define SETTINGS_PLUGIN_OFONO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SETTINGS_TYPE_PLUGIN_OFONO, SettingsPluginOfonoClass))

typedef struct _SettingsPluginOfono SettingsPluginOfono;
typedef struct _SettingsPluginOfonoClass SettingsPluginOfonoClass;

struct _SettingsPluginOfono {
	NMSettingsPlugin parent;
};

struct _SettingsPluginOfonoClass {
	NMSettingsPluginClass parent;
};

GType settings_plugin_ofono_get_type (void);

GQuark ofono_plugin_error_quark (void);

#endif	/* _PLUGIN_H_ */
