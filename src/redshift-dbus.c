/* redshift-dbus.c -- DBus server for Redshift
   This file is part of Redshift.

   Redshift is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Redshift is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Redshift.  If not, see <http://www.gnu.org/licenses/>.

   Copyright (c) 2013  Jon Lund Steffensen <jonlst@gmail.com>
   Copyright (c) 2020 Daniel Kondor <kondor.dani@gmail.com>
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>

#include "redshift.h"
#include "solar.h"
#include "options.h"
#include "config-ini.h"
#include "utils.h"
#include "systemtime.h"

options_t options;


#ifdef ENABLE_NLS
# include <libintl.h>
# define _(s) gettext(s)
# define N_(s) (s)
#else
# define _(s) s
# define N_(s) s
# define gettext(s) s
#endif


#ifdef ENABLE_RANDR
# include "gamma-randr.h"
#endif

#ifdef ENABLE_VIDMODE
# include "gamma-vidmode.h"
#endif

#ifdef ENABLE_DRM
# include "gamma-drm.h"
#endif

#ifdef ENABLE_QUARTZ
# include "gamma-quartz.h"
#endif
#ifdef ENABLE_WINGDI
# include "gamma-w32gdi.h"
#endif

#include "gamma-dummy.h"

/* State for gamma adjustment methods */
gamma_state_t* gamma_state;
static const gamma_method_t *current_method = NULL;

/* location providers */
#include "location-manual.h"

#ifdef ENABLE_GEOCLUE2
# include "location-geoclue2.h"
#endif

#ifdef ENABLE_CORELOCATION
# include "location-corelocation.h"
#endif

location_state_t *location_state;


/* DBus names */
#define REDSHIFT_BUS_NAME        "dk.jonls.redshift.Redshift"
#define REDSHIFT_OBJECT_PATH     "/dk/jonls/redshift/Redshift"
#define REDSHIFT_INTERFACE_NAME  "dk.jonls.redshift.Redshift"

/* Parameter bounds */
#define LAT_MIN   -90.0
#define LAT_MAX    90.0
#define LON_MIN  -180.0
#define LON_MAX   180.0

/* The color temperature when no adjustment is applied. */
#define TEMP_NEUTRAL  6500

/* Default values for parameters. */

static const gchar *period_names[] = {
	"None", "Day", "Night", "Transition"
};


static GDBusNodeInfo *introspection_data = NULL;

/* Cookies for programs wanting to interact though
   the DBus service. */
static GHashTable *cookies = NULL;
static gint32 next_cookie = 1;

/* Set of clients inhibiting the program. */
static GHashTable *inhibitors = NULL;
static gboolean inhibited = FALSE;

/* Solar elevation */
static gdouble elevation = 0.0;
static period_t period = PERIOD_NONE;

/* Location */
static int have_location = 0;
static gdouble latitude = 0.0;
static gdouble longitude = 0.0;


static color_setting_t color_setting;
static color_setting_t color_setting_now;
static color_setting_t color_setting_trans_start;

/* Short transition parameters */
static guint trans_timer = 0;
/* static guint trans_temp_start = 0; */
static guint trans_length = 0;
static guint trans_time = 0;

/* Forced temperature: 2-layered, so that an external program
   can drive the temperature, and at the same time an external
   program can demo temperatures as part of the user interface
   (priority mode). */
#define FORCED_TEMP_LAYERS  2
static gint32 forced_temp_cookie[FORCED_TEMP_LAYERS] = {0};
static guint forced_temp[FORCED_TEMP_LAYERS] = {0};

/* Forced location */
static gint32 forced_location_cookie = 0;
static gdouble forced_lat = 0.0;
static gdouble forced_lon = 0.0;

/* Screen update timer */
static guint screen_update_timer = 0;

/* Brightness */
#define BRIGHTNESS_STEP 0.1
static gdouble brightness = -1.0;
static gboolean brightness_changed = FALSE;


/* DBus service definition */
static const gchar introspection_xml[] =
	"<node>"
	" <interface name='dk.jonls.redshift.Redshift'>"
	"  <method name='AcquireCookie'>"
	"   <arg type='s' name='program' direction='in'/>"
	"   <arg type='i' name='cookie' direction='out'/>"
	"  </method>"
	"  <method name='ReleaseCookie'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"  </method>"
	"  <method name='Inhibit'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"  </method>"
	"  <method name='Uninhibit'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"  </method>"
	"  <method name='EnforceTemperature'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"   <arg type='u' name='temperature' direction='in'/>"
	"   <arg type='b' name='priority' direction='in'/>"
	"  </method>"
	"  <method name='UnenforceTemperature'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"   <arg type='b' name='priority' direction='in'/>"
	"  </method>"
	"  <method name='EnforceLocation'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"   <arg type='d' name='latitude' direction='in'/>"
	"   <arg type='d' name='longitude' direction='in'/>"
	"  </method>"
	"  <method name='UnenforceLocation'>"
	"   <arg type='i' name='cookie' direction='in'/>"
	"  </method>"
	"  <method name='GetElevation'>"
	"   <arg type='d' name='elevation' direction='out'/>"
	"  </method>"
	"  <method name='BrightnessUp'/>"
	"  <method name='BrightnessDown'/>"
	"  <property type='b' name='Inhibited' access='read'/>"
	"  <property type='s' name='Period' access='read'/>"
	"  <property type='u' name='Temperature' access='read'/>"
	"  <property type='d' name='CurrentLatitude' access='read'/>"
	"  <property type='d' name='CurrentLongitude' access='read'/>"
	"  <property type='u' name='TemperatureDay' access='readwrite'/>"
	"  <property type='u' name='TemperatureNight' access='readwrite'/>"
	"  <property type='d' name='Brightness' access='readwrite'/>"
	" </interface>"
	"</node>";


/* Update elevation from location and time. */
static int
update_elevation()
{
	gdouble time = g_get_real_time() / 1000000.0;

	gdouble lat;
	gdouble lon;
	if (forced_location_cookie != 0) {
		/* Check for forced location */
		lat = forced_lat;
		lon = forced_lon;
	}
	else {
		/* Otherwise, try to get update from location provider */
		location_t loc;
		int timeout = 0;
		int r = provider_get_location(options.provider,
			location_state, &timeout, &loc);
		/* TODO: on error, should we abort? */
		if(r) {
			latitude = loc.lat;
			longitude = loc.lon;
			have_location = 1;
		}
		if(have_location) {
			lat = latitude;
			lon = longitude;
		} else return -1;
	}

	elevation = solar_elevation(time, lat, lon);

	g_print("Location: %f, %f\n", lat, lon);
	g_print("Elevation: %f\n", elevation);
	return 0;
}

/* Update temperature from transition progress */
static void
update_temperature(double a)
{
	interpolate_transition_scheme(&options.scheme, a,
		&color_setting);
}


/* Timer callback to update short transitions */
static gboolean
short_transition_update_cb(gpointer data)
{
	trans_time += 1;
	if (trans_time <= trans_length) {
		gfloat a = trans_time/(gfloat)trans_length;
		interpolate_color_settings(&color_setting_trans_start,
			&color_setting, a, &color_setting_now);
	}
	
	/* brightness changes have a constant rate of 2% / 100ms */
	double brightness_change = trans_time * 0.02;
	if (color_setting.brightness >
		color_setting_trans_start.brightness) {
			double tmp_brightness = 
				color_setting_trans_start.brightness +
					brightness_change;
			color_setting_now.brightness =
				(tmp_brightness > color_setting.brightness) ? 
				color_setting.brightness : tmp_brightness;
		}
	else if (color_setting.brightness <
		color_setting_trans_start.brightness) {
			double tmp_brightness = 
				color_setting_trans_start.brightness -
					brightness_change;
			color_setting_now.brightness =
				(tmp_brightness < color_setting.brightness) ? 
				color_setting.brightness : tmp_brightness;
		}
	
	if (current_method != NULL) {
		current_method->set_temperature(gamma_state,
			&color_setting_now, options.preserve_gamma);
	}
	
	if (trans_time >= trans_length &&
			color_setting_now.brightness == color_setting.brightness) {
		trans_time = 0;
		trans_length = 0;
		return FALSE;
	}

	return TRUE;
}

/* Timer callback to update the screen */
static gboolean
screen_update_cb(gpointer data)
{
	GDBusConnection *conn = G_DBUS_CONNECTION(data);

	gboolean prev_inhibit = inhibited;
	color_setting_t color_setting_prev = color_setting;
	period_t prev_period = period;

	/* Update elevation from location */
	if (!options.scheme.use_time) {
		int r = update_elevation();
		if(r == 0) {
			/* Calculate period -- only if we have a valid location */
			period = get_period_from_elevation(&options.scheme, elevation);
			double a = get_transition_progress_from_elevation(
				&options.scheme, elevation);
			update_temperature(a);
		} else {
			/* If we have no location, set the temperature to neutral.
			   It can be still set to a forced temperature later. */
			color_setting.temperature = TEMP_NEUTRAL;
		}
	} else {
		double now;
		int r = systemtime_get_time(&now);
		if (r < 0) {
			fputs(_("Unable to read system time.\n"), stderr);
		} else {
			int time_offset = get_seconds_since_midnight(now);

			period = get_period_from_time(&options.scheme, time_offset);
			double a = get_transition_progress_from_time(
					&options.scheme, time_offset);
			update_temperature(a);
		}
	}

	/* Check for inhibition */
	inhibited = g_hash_table_size(inhibitors) > 0;

	if (inhibited) {
		color_setting_reset(&color_setting);
	} else {
		/* Check for forced temperature */
		int forced_index = -1;
		for (int i = FORCED_TEMP_LAYERS-1; i >= 0; i--) {
			if (forced_temp_cookie[i] != 0) {
				forced_index = i;
				break;
			}
		}

		if (forced_index >= 0) {
			color_setting.temperature = forced_temp[forced_index];
		}
	}
	
	/* set brightness */
	if (brightness >= MIN_BRIGHTNESS &&
			brightness <= MAX_BRIGHTNESS)
		color_setting.brightness = brightness;

	g_print("Temperature: %u\n", color_setting.temperature);

	/* Signal if temperature has changed */
	if (color_setting_prev.temperature != color_setting.temperature ||
	    prev_inhibit != inhibited ||
	    prev_period != period) {
		GError *local_error = NULL;
		GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
		if (color_setting_prev.temperature != color_setting.temperature) {
			g_variant_builder_add(builder, "{sv}",
					      "Temperature", g_variant_new_uint32(
					      color_setting.temperature));
		}
		if (prev_inhibit != inhibited) {
			g_variant_builder_add(builder, "{sv}",
					      "Inhibited", g_variant_new_boolean(inhibited));
		}
		if (prev_period != period) {
			const char *name = period_names[period];
			g_variant_builder_add(builder, "{sv}",
					      "Period", g_variant_new_string(name));
		}

		g_dbus_connection_emit_signal(conn, NULL,
					      REDSHIFT_OBJECT_PATH,
					      "org.freedesktop.DBus.Properties",
					      "PropertiesChanged",
					      g_variant_new("(sa{sv}as)",
							    REDSHIFT_INTERFACE_NAME,
							    builder,
							    NULL),
					      &local_error);
		g_assert_no_error(local_error);
	}

	/* If the temperature difference is large enough,
	   make a nice transition. */
	if (color_setting_diff_is_major(&color_setting_now,
			&color_setting)) {
		if (trans_timer != 0) {
			g_source_remove(trans_timer);
		}

		g_print("Create short transition: %u -> %u\n",
			color_setting_now.temperature, color_setting.temperature);
		color_setting_trans_start = color_setting_now;
		trans_length = 40 - trans_time;
		trans_time = 0;

		trans_timer = g_timeout_add(100, short_transition_update_cb, NULL);
	} else if (color_setting_diff(&color_setting_now, &color_setting) ) {
		if (trans_timer != 0) {
			g_source_remove(trans_timer);
		}
		if (current_method != NULL) {
			current_method->set_temperature(gamma_state,
				&color_setting, options.preserve_gamma);
		}
		color_setting_now = color_setting;
	}
	brightness_changed = FALSE;

	return TRUE;
}

static void
screen_update_restart(GDBusConnection *conn)
{
	if (screen_update_timer != 0) {
		g_source_remove(screen_update_timer);
	}
	screen_update_cb(conn);
	screen_update_timer = g_timeout_add_seconds(5, screen_update_cb, conn);
}


/* Emit signal that current position changed */
static void
emit_position_changed(GDBusConnection *conn, gdouble lat, gdouble lon)
{
	/* Signal change in location */
	GError *local_error = NULL;
	GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add(builder, "{sv}", "CurrentLatitude",
			      g_variant_new_double(lat));
	g_variant_builder_add(builder, "{sv}", "CurrentLongitude",
			      g_variant_new_double(lon));

	g_dbus_connection_emit_signal(conn, NULL,
				      REDSHIFT_OBJECT_PATH,
				      "org.freedesktop.DBus.Properties",
				      "PropertiesChanged",
				      g_variant_new("(sa{sv}as)",
						    REDSHIFT_INTERFACE_NAME,
						    builder,
						    NULL),
				      &local_error);
	g_assert_no_error(local_error);
}


static void
emit_brightness_changed(GDBusConnection *conn, gdouble br2)
{
	GError *local_error = NULL;
	GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add(builder, "{sv}", "Brightness",
				  g_variant_new_double(br2));

	g_dbus_connection_emit_signal(conn, NULL,
					  REDSHIFT_OBJECT_PATH,
					  "org.freedesktop.DBus.Properties",
					  "PropertiesChanged",
					  g_variant_new("(sa{sv}as)",
							REDSHIFT_INTERFACE_NAME,
							builder,
							NULL),
					  &local_error);
	g_assert_no_error(local_error);
}


/* DBus service functions */
static void
handle_method_call(GDBusConnection *conn,
		   const gchar *sender,
		   const gchar *obj_path,
		   const gchar *interface_name,
		   const gchar *method_name,
		   GVariant *parameters,
		   GDBusMethodInvocation *invocation,
		   gpointer data)
{
	if (g_strcmp0(method_name, "AcquireCookie") == 0) {
		gint32 cookie = next_cookie++;
		const gchar *program;
		g_variant_get(parameters, "(&s)", &program);

		g_print("AcquireCookie for `%s'.\n", program);

		g_hash_table_insert(cookies, GINT_TO_POINTER(cookie), g_strdup(program));
		g_dbus_method_invocation_return_value(invocation,
						      g_variant_new("(i)", cookie));
	} else if (g_strcmp0(method_name, "ReleaseCookie") == 0) {
		gint32 cookie;
		g_variant_get(parameters, "(i)", &cookie);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		g_print("ReleaseCookie for `%s'.\n", program);

		/* Remove all rules enforced by program */
		gboolean found = FALSE;
		found = g_hash_table_remove(inhibitors, GINT_TO_POINTER(cookie));

		for (int i = 0; i < FORCED_TEMP_LAYERS; i++) {
			if (forced_temp_cookie[i] == cookie) {
				forced_temp_cookie[i] = 0;
				found = TRUE;
			}
		}

		if (forced_location_cookie == cookie) {
			forced_location_cookie = 0;
			found = TRUE;
		}

		if (found) screen_update_restart(conn);

		/* Remove from list of cookies */
		g_hash_table_remove(cookies, GINT_TO_POINTER(cookie));
		g_free(program);

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "Inhibit") == 0) {
		gint32 cookie;
		g_variant_get(parameters, "(i)", &cookie);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		g_hash_table_add(inhibitors, GINT_TO_POINTER(cookie));

		if (!inhibited) {
			screen_update_restart(conn);
		}

		g_print("Inhibit for `%s'.\n", program);

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "Uninhibit") == 0) {
		gint32 cookie;
		g_variant_get(parameters, "(i)", &cookie);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		g_hash_table_remove(inhibitors, GINT_TO_POINTER(cookie));

		if (inhibited && g_hash_table_size(inhibitors) == 0) {
			screen_update_restart(conn);
		}

		g_print("Uninhibit for `%s'.\n", program);

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "EnforceTemperature") == 0) {
		gint32 cookie;
		guint32 temp;
		gboolean priority;
		g_variant_get(parameters, "(iub)", &cookie, &temp, &priority);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		int index = priority ? 1 : 0;

		if (forced_temp_cookie[index] != 0 &&
		    forced_temp_cookie[index] != cookie) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.AlreadyEnforced",
								   "Another client is already enforcing temperature");
			return;
		}

		/* Check parameter bounds */
		if (temp < MIN_TEMP || temp > MAX_TEMP) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.InvalidArgument",
								   "Temperature is invalid");
			return;
		}

		/* Set forced location */
		forced_temp_cookie[index] = cookie;
		forced_temp[index] = temp;

		screen_update_restart(conn);

		g_print("EnforceTemperature for `%s'.\n", program);

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "UnenforceTemperature") == 0) {
		gint32 cookie;
		gboolean priority;
		g_variant_get(parameters, "(ib)", &cookie, &priority);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		g_print("UnenforceTemperature for `%s'.\n", program);

		int index = priority ? 1 : 0;

		if (forced_temp_cookie[index] == cookie) {
			forced_temp_cookie[index] = 0;
			screen_update_restart(conn);
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "EnforceLocation") == 0) {
		gint32 cookie;
		gdouble lat;
		gdouble lon;
		g_variant_get(parameters, "(idd)", &cookie, &lat, &lon);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		if (forced_location_cookie != 0 &&
		    forced_location_cookie != cookie) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.AlreadyEnforced",
								   "Another client is already enforcing location");
			return;
		}

		/* Check parameter bounds */
		if (lat < LAT_MIN || lat > LAT_MAX ||
		    lon < LON_MIN || lon > LON_MAX) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.InvalidArgument",
								   "Location is invalid");
			return;
		}

		/* Set forced location */
		forced_location_cookie = cookie;
		forced_lat = lat;
		forced_lon = lon;

		/* Signal change in location */
		emit_position_changed(conn, forced_lat, forced_lon);

		screen_update_restart(conn);

		g_print("EnforceLocation for `%s'.\n", program);

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "UnenforceLocation") == 0) {
		gint32 cookie;
		g_variant_get(parameters, "(i)", &cookie);

		gchar *program = g_hash_table_lookup(cookies, GINT_TO_POINTER(cookie));
		if (program == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
								   "dk.jonls.redshift.Redshift.UnknownCookie",
								   "Unknown cookie value");
			return;
		}

		g_print("UnenforceLocation for `%s'.\n", program);

		if (forced_location_cookie == cookie) {
			forced_location_cookie = 0;
			screen_update_restart(conn);

			/* Signal change in location */
			emit_position_changed(conn, latitude, longitude);
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (g_strcmp0(method_name, "GetElevation") == 0) {
		g_dbus_method_invocation_return_value(invocation,
						      g_variant_new("(d)", elevation));
	} else if (g_strcmp0(method_name, "BrightnessUp") == 0 ||
			g_strcmp0(method_name, "BrightnessDown") == 0) {
		gdouble br2 = color_setting.brightness;

		if (g_strcmp0(method_name, "BrightnessUp") == 0) {
			br2 += BRIGHTNESS_STEP;
			if(br2 > MAX_BRIGHTNESS) br2 = MAX_BRIGHTNESS;
		} else {
			br2 -= BRIGHTNESS_STEP;
			if(br2 < MIN_BRIGHTNESS) br2 = MIN_BRIGHTNESS;
		}

		if(br2 != color_setting.brightness) {
			brightness = br2;
			brightness_changed = TRUE;
			screen_update_restart(conn);
			emit_brightness_changed(conn, br2);
		}

		g_dbus_method_invocation_return_value(invocation, NULL);
	}
}

static GVariant *
handle_get_property(GDBusConnection *conn,
		    const gchar *sender,
		    const gchar *obj_path,
		    const gchar *interface_name,
		    const gchar *prop_name,
		    GError **error,
		    gpointer data)
{
	GVariant *ret = NULL;

	if (g_strcmp0(prop_name, "Inhibited") == 0) {
		ret = g_variant_new_boolean(inhibited);
	} else if (g_strcmp0(prop_name, "Period") == 0) {
		ret = g_variant_new_string(period_names[period]);
	} else if (g_strcmp0(prop_name, "Temperature") == 0) {
		ret = g_variant_new_uint32(color_setting.temperature);
	} else if (g_strcmp0(prop_name, "CurrentLatitude") == 0) {
		gdouble lat = latitude;
		if (forced_location_cookie != 0) lat = forced_lat;
		ret = g_variant_new_double(lat);
	} else if (g_strcmp0(prop_name, "CurrentLongitude") == 0) {
		gdouble lon = longitude;
		if (forced_location_cookie != 0) lon = forced_lon;
		ret = g_variant_new_double(lon);
	} else if (g_strcmp0(prop_name, "TemperatureDay") == 0) {
		ret = g_variant_new_uint32(options.scheme.day.temperature);
	} else if (g_strcmp0(prop_name, "TemperatureNight") == 0) {
		ret = g_variant_new_uint32(options.scheme.night.temperature);
	} else if (g_strcmp0(prop_name, "Brightness") == 0) {
		ret = g_variant_new_double(color_setting.brightness);
	}

	return ret;
}

static gboolean
handle_set_property(GDBusConnection *conn,
		    const gchar *sender,
		    const gchar *obj_path,
		    const gchar *interface_name,
		    const gchar *prop_name,
		    GVariant *value,
		    GError **error,
		    gpointer data)
{
	if (g_strcmp0(prop_name, "TemperatureDay") == 0) {
		guint32 temp = g_variant_get_uint32(value);

		if (temp < MIN_TEMP || temp > MAX_TEMP) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Temperature out of bounds");
		} else {
			options.scheme.day.temperature = temp;

			screen_update_restart(conn);

			GError *local_error = NULL;
			GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
			g_variant_builder_add(builder, "{sv}", "TemperatureDay",
					      g_variant_new_uint32(options.scheme.day.temperature));

			g_dbus_connection_emit_signal(conn, NULL,
						      obj_path,
						      "org.freedesktop.DBus.Properties",
						      "PropertiesChanged",
						      g_variant_new("(sa{sv}as)",
								    interface_name,
								    builder,
								    NULL),
						      &local_error);
			g_assert_no_error(local_error);
		}
	} else if (g_strcmp0(prop_name, "TemperatureNight") == 0) {
		guint32 temp = g_variant_get_uint32(value);

		if (temp < MIN_TEMP || temp > MAX_TEMP) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Temperature out of bounds");
		} else {
			options.scheme.night.temperature = temp;

			screen_update_restart(conn);

			GError *local_error = NULL;
			GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
			g_variant_builder_add(builder, "{sv}", "TemperatureNight",
					      g_variant_new_uint32(options.scheme.night.temperature));

			g_dbus_connection_emit_signal(conn, NULL,
						      obj_path,
						      "org.freedesktop.DBus.Properties",
						      "PropertiesChanged",
						      g_variant_new("(sa{sv}as)",
								    interface_name,
								    builder,
								    NULL),
						      &local_error);
			g_assert_no_error(local_error);
		}
	} else if (g_strcmp0(prop_name, "Brightness") == 0) {
		gdouble br = g_variant_get_double(value);
		if (br < MIN_BRIGHTNESS || br > MAX_BRIGHTNESS) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_FAILED,
				    "Brightness out of bounds");
		} else if(br != color_setting.brightness) {
			brightness = br;
			brightness_changed = TRUE;
			screen_update_restart(conn);
			emit_brightness_changed(conn, br);
		}
	}

	return *error == NULL;
}


static const GDBusInterfaceVTable interface_vtable = {
	handle_method_call,
	handle_get_property,
	handle_set_property
};


static void
on_bus_acquired(GDBusConnection *conn,
		const gchar *name,
		gpointer data)
{
	g_printerr("Bus acquired: `%s'.\n", name);

	guint registration_id = g_dbus_connection_register_object(conn,
								  REDSHIFT_OBJECT_PATH,
								  introspection_data->interfaces[0],
								  &interface_vtable,
								  NULL, NULL,
								  NULL);
	g_assert(registration_id > 0);

	/* Set callback for location updates */
	options.provider->set_callback(location_state,
		(location_provider_callback_func*)screen_update_restart, conn);

	/* Start screen update timer */
	screen_update_restart(conn);
}

static void
on_name_acquired(GDBusConnection *conn,
		 const gchar *name,
		 gpointer data)
{
	g_printerr("Name acquired: `%s'.\n", name);
}

static void
on_name_lost(GDBusConnection *conn,
	     const gchar *name,
	     gpointer data)
{
	g_printerr("Name lost: `%s'.\n", name);
	exit(EXIT_FAILURE);
}


/* Handle termination signal */
static gboolean
term_signal_cb(gpointer data)
{
	GMainLoop *loop = (GMainLoop *)data;
	g_main_loop_quit(loop);
	return TRUE;
}


int
main(int argc, char *argv[])
{
#if !GLIB_CHECK_VERSION(2, 35, 0)
	g_type_init();
#endif

	/* Create hash table for cookies */
	cookies = g_hash_table_new(NULL, NULL);

	/* Create hash table for inhibitors (set) */
	inhibitors = g_hash_table_new(NULL, NULL);
	
	/* initialie color settings to default */
	color_setting_reset(&color_setting);
	color_setting_reset(&color_setting_now);
	color_setting_reset(&color_setting_trans_start);

	
	/* List of gamma methods. */
	const gamma_method_t gamma_methods[] = {
	#ifdef ENABLE_DRM
		drm_gamma_method,
	#endif
	#ifdef ENABLE_RANDR
		randr_gamma_method,
	#endif
	#ifdef ENABLE_VIDMODE
		vidmode_gamma_method,
	#endif
	#ifdef ENABLE_QUARTZ
		quartz_gamma_method,
	#endif
	#ifdef ENABLE_WINGDI
		w32gdi_gamma_method,
	#endif
		dummy_gamma_method,
		{ NULL }
	};
	
	/* List of location providers. */
	const location_provider_t location_providers[] = {
#ifdef ENABLE_GEOCLUE2
		geoclue2_location_provider,
#endif
#ifdef ENABLE_CORELOCATION
		corelocation_location_provider,
#endif
		manual_location_provider,
		{ NULL }
	};
	
	config_ini_state_t config_state;
	int r = redshift_init_options(&options, &config_state, argc, argv,
		gamma_methods, location_providers);
	if(r < 0) exit(EXIT_FAILURE);
	
	if (options.mode != PROGRAM_MODE_CONTINUAL) {
		fputs(_("Unsupported option for program mode!\n"), stderr);
		exit(EXIT_FAILURE);
	}

	/* Set up location provider if needed */
	int need_location = !options.scheme.use_time;
	if (need_location) {
		r = providers_try_start_all(&options, &config_state,
			&location_state, location_providers);
		if(r < 0) {
			fputs(_("No location provider is available. "
				"Use the DBus interface to set your location.\n"),
				stderr);
		} else {
			fputs(_("Trying to get initial location...\n"), stderr);

			/* Get initial location from provider */
			location_t loc = { NAN, NAN };
			int timeout = 1000;
			r = provider_get_location(options.provider,
				location_state, &timeout, &loc);
			if(r > 0) {
				/* We got a location */
				if (!location_is_valid(&loc)) {
					fputs(_("Invalid location returned from provider.\n"),
						  stderr);
					return -1;
				}

				print_location(&loc);
				latitude = loc.lat;
				longitude = loc.lon;
				have_location = 1;
			} else {
				/* No location yet -- this might not be an error if
				   provider is dynamic */
				if (r < 0 || !options.provider->is_dynamic()) {
					fputs(_("No location available."
						"Use the DBus interface to set your location.\n"),
						stderr);
					return -1;
				}
			}
		}
	}
	
	/* Setup gamma method */
	r = methods_try_start_all(&options, &config_state, &gamma_state,
			gamma_methods);
	if (r < 0) exit(EXIT_FAILURE);
	current_method = options.method;

	/* Build node info from XML */
	introspection_data = g_dbus_node_info_new_for_xml(introspection_xml,
							  NULL);
	g_assert(introspection_data != NULL);

	/* Obtain DBus bus name */
	GBusNameOwnerFlags flags =
		G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
		G_BUS_NAME_OWNER_FLAGS_REPLACE;
	guint owner_id = g_bus_own_name(G_BUS_TYPE_SESSION,
					REDSHIFT_BUS_NAME,
					flags,
					on_bus_acquired,
					on_name_acquired,
					on_name_lost,
					NULL,
					NULL);

	/* Create main loop */
	GMainLoop *mainloop = g_main_loop_new(NULL, FALSE);

	/* Attach signal handler for termination */
	g_unix_signal_add(SIGTERM, term_signal_cb, mainloop);
	g_unix_signal_add(SIGINT, term_signal_cb, mainloop);

	/* Start main loop */
	g_main_loop_run(mainloop);

	/* Clean up */
	g_bus_unown_name(owner_id);

	g_print("Restoring gamma ramps.\n");

	/* Restore gamma ramps */
	if (current_method != NULL) {
		current_method->restore(gamma_state);
		current_method->free(gamma_state);
	}
	
	/* Free up location provider */
	if (need_location)
		options.provider->free(location_state);

	return 0;
}
