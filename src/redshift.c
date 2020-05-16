/* redshift.c -- Main program source
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

   Copyright (c) 2009-2017  Jon Lund Steffensen <jonlst@gmail.com>
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <locale.h>
#include <errno.h>

/* poll.h is not available on Windows but there is no Windows location provider
   using polling. On Windows, we just define some stubs to make things compile.
   */
#ifndef _WIN32
# include <poll.h>
#else
#define POLLIN 0
struct pollfd {
	int fd;
	short events;
	short revents;
};
int poll(struct pollfd *fds, int nfds, int timeout) { abort(); return -1; }
#endif

#if defined(HAVE_SIGNAL_H) && !defined(__WIN32__)
# include <signal.h>
#endif

#ifdef ENABLE_NLS
# include <libintl.h>
# define _(s) gettext(s)
# define N_(s) (s)
#else
# define _(s) s
# define N_(s) s
# define gettext(s) s
#endif

#include "redshift.h"
#include "config-ini.h"
#include "solar.h"
#include "systemtime.h"
#include "hooks.h"
#include "signals.h"
#include "options.h"
#include "utils.h"

/* pause() is not defined on windows platform but is not needed either.
   Use a noop macro instead. */
#ifdef __WIN32__
# define pause()
#endif

#include "gamma-dummy.h"

#ifdef ENABLE_DRM
# include "gamma-drm.h"
#endif

#ifdef ENABLE_RANDR
# include "gamma-randr.h"
#endif

#ifdef ENABLE_VIDMODE
# include "gamma-vidmode.h"
#endif

#ifdef ENABLE_QUARTZ
# include "gamma-quartz.h"
#endif

#ifdef ENABLE_WINGDI
# include "gamma-w32gdi.h"
#endif


#include "location-manual.h"

#ifdef ENABLE_GEOCLUE2
# include "location-geoclue2.h"
#endif

#ifdef ENABLE_CORELOCATION
# include "location-corelocation.h"
#endif


/* Duration of sleep between screen updates (milliseconds). */
#define SLEEP_DURATION        5000
#define SLEEP_DURATION_SHORT  100

/* Length of fade in numbers of short sleep durations. */
#define FADE_LENGTH  40


/* Names of periods of day */
static const char *period_names[] = {
	/* TRANSLATORS: Name printed when period of day is unknown */
	N_("None"),
	N_("Daytime"),
	N_("Night"),
	N_("Transition")
};


/* Print verbose description of the given period. */
static void
print_period(period_t period, double transition)
{
	switch (period) {
	case PERIOD_NONE:
	case PERIOD_NIGHT:
	case PERIOD_DAYTIME:
		printf(_("Period: %s\n"), gettext(period_names[period]));
		break;
	case PERIOD_TRANSITION:
		printf(_("Period: %s (%.2f%% day)\n"),
		       gettext(period_names[period]),
		       transition*100);
		break;
	}
}

/* Easing function for fade.
   See https://github.com/mietek/ease-tween */
static double
ease_fade(double t)
{
	if (t <= 0) return 0;
	if (t >= 1) return 1;
	return 1.0042954579734844 * exp(
		-6.4041738958415664 * exp(-7.2908241330981340 * t));
}


/* Run continual mode loop
   This is the main loop of the continual mode which keeps track of the
   current time and continuously updates the screen to the appropriate
   color temperature. */
static int
run_continual_mode(const location_provider_t *provider,
		   location_state_t *location_state,
		   const transition_scheme_t *scheme,
		   const gamma_method_t *method,
		   gamma_state_t *method_state,
		   int use_fade, int preserve_gamma, int verbose)
{
	int r;

	/* Short fade parameters */
	int fade_length = 0;
	int fade_time = 0;
	color_setting_t fade_start_interp;

	r = signals_install_handlers();
	if (r < 0) {
		return r;
	}

	/* Save previous parameters so we can avoid printing status updates if
	   the values did not change. */
	period_t prev_period = PERIOD_NONE;

	/* Previous target color setting and current actual color setting.
	   Actual color setting takes into account the current color fade. */
	color_setting_t prev_target_interp;
	color_setting_reset(&prev_target_interp);

	color_setting_t interp;
	color_setting_reset(&interp);

	location_t loc = { NAN, NAN };
	int need_location = !scheme->use_time;
	if (need_location) {
		fputs(_("Waiting for initial location"
			" to become available...\n"), stderr);

		/* Get initial location from provider */
		int timeout = -1;
		r = provider_get_location(provider, location_state, &timeout, &loc);
		if (r < 0) {
			fputs(_("Unable to get location"
				" from provider.\n"), stderr);
			return -1;
		}

		if (!location_is_valid(&loc)) {
			fputs(_("Invalid location returned from provider.\n"),
			      stderr);
			return -1;
		}

		print_location(&loc);
	}

	if (verbose) {
		printf(_("Color temperature: %uK\n"), interp.temperature);
		printf(_("Brightness: %.2f\n"), interp.brightness);
	}

	/* Continuously adjust color temperature */
	int done = 0;
	int prev_disabled = 1;
	int disabled = 0;
	int location_available = 1;
	while (1) {
		/* Check to see if disable signal was caught */
		if (disable && !done) {
			disabled = !disabled;
			disable = 0;
		}

		/* Check to see if exit signal was caught */
		if (exiting) {
			if (done) {
				/* On second signal stop the ongoing fade. */
				break;
			} else {
				done = 1;
				disabled = 1;
			}
			exiting = 0;
		}

		/* Print status change */
		if (verbose && disabled != prev_disabled) {
			printf(_("Status: %s\n"), disabled ?
			       _("Disabled") : _("Enabled"));
		}

		prev_disabled = disabled;

		/* Read timestamp */
		double now;
		r = systemtime_get_time(&now);
		if (r < 0) {
			fputs(_("Unable to read system time.\n"), stderr);
			return -1;
		}

		period_t period;
		double transition_prog;
		if (scheme->use_time) {
			int time_offset = get_seconds_since_midnight(now);

			period = get_period_from_time(scheme, time_offset);
			transition_prog = get_transition_progress_from_time(
				scheme, time_offset);
		} else {
			/* Current angular elevation of the sun */
			double elevation = solar_elevation(
				now, loc.lat, loc.lon);

			period = get_period_from_elevation(scheme, elevation);
			transition_prog =
				get_transition_progress_from_elevation(
					scheme, elevation);
		}

		/* Use transition progress to get target color
		   temperature. */
		color_setting_t target_interp;
		interpolate_transition_scheme(
			scheme, transition_prog, &target_interp);

		if (disabled) {
			period = PERIOD_NONE;
			color_setting_reset(&target_interp);
		}

		if (done) {
			period = PERIOD_NONE;
		}

		/* Print period if it changed during this update,
		   or if we are in the transition period. In transition we
		   print the progress, so we always print it in
		   that case. */
		if (verbose && (period != prev_period ||
				period == PERIOD_TRANSITION)) {
			print_period(period, transition_prog);
		}

		/* Activate hooks if period changed */
		if (period != prev_period) {
			hooks_signal_period_change(prev_period, period);
		}

		/* Start fade if the parameter differences are too big to apply
		   instantly. */
		if (use_fade) {
			if ((fade_length == 0 &&
			     color_setting_diff_is_major(
				     &interp,
				     &target_interp)) ||
			    (fade_length != 0 &&
			     color_setting_diff_is_major(
				     &target_interp,
				     &prev_target_interp))) {
				fade_length = FADE_LENGTH;
				fade_time = 0;
				fade_start_interp = interp;
			}
		}

		/* Handle ongoing fade */
		if (fade_length != 0) {
			fade_time += 1;
			double frac = fade_time / (double)fade_length;
			/* no need for CLAMP here, since
			   interpolate_color_settings() will apply CLAMP */
			double alpha = ease_fade(frac);

			interpolate_color_settings(
				&fade_start_interp, &target_interp, alpha,
				&interp);

			if (fade_time > fade_length) {
				fade_time = 0;
				fade_length = 0;
			}
		} else {
			interp = target_interp;
		}

		/* Break loop when done and final fade is over */
		if (done && fade_length == 0) break;

		if (verbose) {
			if (prev_target_interp.temperature !=
			    target_interp.temperature) {
				printf(_("Color temperature: %uK\n"),
				       target_interp.temperature);
			}
			if (prev_target_interp.brightness !=
			    target_interp.brightness) {
				printf(_("Brightness: %.2f\n"),
				       target_interp.brightness);
			}
		}

		/* Adjust temperature */
		r = method->set_temperature(
			method_state, &interp, preserve_gamma);
		if (r < 0) {
			fputs(_("Temperature adjustment failed.\n"),
			      stderr);
			return -1;
		}

		/* Save period and target color setting as previous */
		prev_period = period;
		prev_target_interp = target_interp;

		/* Sleep length depends on whether a fade is ongoing. */
		int delay = SLEEP_DURATION;
		if (fade_length != 0) {
			delay = SLEEP_DURATION_SHORT;
		}

		/* Update location. */
		int loc_fd = -1;
		if (need_location && provider->is_dynamic()) {
			/* Get new location and availability
			   information. */
			location_t new_loc;
			int new_available;
			int r = provider_get_location(provider,
				location_state, &delay, &new_loc);
			
			if (r < 0) {
				fputs(_("Unable to get location"
					" from provider.\n"), stderr);
				return -1;
			}
			new_available = r;

			if (!new_available &&
			    new_available != location_available) {
				fputs(_("Location is temporarily"
				        " unavailable; Using previous"
					" location until it becomes"
					" available...\n"), stderr);
			}

			if (!location_is_valid(&loc)) {
				fputs(_("Invalid location returned"
					" from provider.\n"), stderr);
				return -1;
			}

			if (new_available &&
			    (new_loc.lat != loc.lat ||
			     new_loc.lon != loc.lon ||
			     new_available != location_available)) {
				loc = new_loc;
				print_location(&loc);
			} else if (delay) {
				/* do the remaining sleep if
				 * location has not changed */
				systemtime_msleep(delay);
			}

			location_available = new_available;

		} else {
			systemtime_msleep(delay);
		}
	}

	/* Restore saved gamma ramps */
	method->restore(method_state);

	return 0;
}


int
main(int argc, char *argv[])
{
	int r;

#ifdef ENABLE_NLS
	/* Init locale */
	setlocale(LC_CTYPE, "");
	setlocale(LC_MESSAGES, "");

	/* Internationalisation */
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif

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

	/* Flush messages consistently even if redirected to a pipe or
	   file.  Change the flush behaviour to line-buffered, without
	   changing the actual buffers being used. */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	options_t options;
	config_ini_state_t config_state;
	r = redshift_init_options(&options, &config_state, argc, argv,
		gamma_methods, location_providers);
	if(r < 0) exit(EXIT_FAILURE);
	
	/* Initialize location provider if needed. If provider is NULL
	   try all providers until one that works is found. */
	location_state_t *location_state;

	/* Location is not needed for reset mode and manual mode. */
	int need_location =
		options.mode != PROGRAM_MODE_RESET &&
		options.mode != PROGRAM_MODE_MANUAL &&
		!options.scheme.use_time;
	if (need_location) {
		r = providers_try_start_all(&options, &config_state,
			&location_state, location_providers);
		if(r < 0) exit(EXIT_FAILURE);
	}

	if (options.mode != PROGRAM_MODE_RESET &&
	    options.mode != PROGRAM_MODE_MANUAL) {
		if (options.verbose) {
			printf(_("Temperatures: %dK at day, %dK at night\n"),
			       options.scheme.day.temperature,
			       options.scheme.night.temperature);
		}

		/* Color temperature */
		if (options.scheme.day.temperature < MIN_TEMP ||
		    options.scheme.day.temperature > MAX_TEMP ||
		    options.scheme.night.temperature < MIN_TEMP ||
		    options.scheme.night.temperature > MAX_TEMP) {
			fprintf(stderr,
				_("Temperature must be between %uK and %uK.\n"),
				MIN_TEMP, MAX_TEMP);
			exit(EXIT_FAILURE);
		}
	}

	if (options.mode == PROGRAM_MODE_MANUAL) {
		/* Check color temperature to be set */
		if (options.temp_set < MIN_TEMP ||
		    options.temp_set > MAX_TEMP) {
			fprintf(stderr,
				_("Temperature must be between %uK and %uK.\n"),
				MIN_TEMP, MAX_TEMP);
			exit(EXIT_FAILURE);
		}
	}

	transition_scheme_t *scheme = &options.scheme;

	/* Initialize gamma adjustment method. If method is NULL
	   try all methods until one that works is found. */
	gamma_state_t *method_state;

	/* Gamma adjustment not needed for print mode */
	if (options.mode != PROGRAM_MODE_PRINT) {
		r = methods_try_start_all(&options, &config_state, &method_state,
			gamma_methods);
		if (r < 0) exit(EXIT_FAILURE);
	}

	config_ini_free(&config_state);

	switch (options.mode) {
	case PROGRAM_MODE_ONE_SHOT:
	case PROGRAM_MODE_PRINT:
	{
		location_t loc = { NAN, NAN };
		if (need_location) {
			fputs(_("Waiting for current location"
				" to become available...\n"), stderr);

			/* Wait for location provider. */
			int timeout = -1;
			int r = provider_get_location(
				options.provider, location_state, &timeout, &loc);
			if (r < 0) {
				fputs(_("Unable to get location"
					" from provider.\n"), stderr);
				exit(EXIT_FAILURE);
			}

			if (!location_is_valid(&loc)) {
				exit(EXIT_FAILURE);
			}

			print_location(&loc);
		}

		double now;
		r = systemtime_get_time(&now);
		if (r < 0) {
			fputs(_("Unable to read system time.\n"), stderr);
			options.method->free(method_state);
			exit(EXIT_FAILURE);
		}

		period_t period;
		double transition_prog;
		if (options.scheme.use_time) {
			int time_offset = get_seconds_since_midnight(now);
			period = get_period_from_time(scheme, time_offset);
			transition_prog = get_transition_progress_from_time(
				scheme, time_offset);
		} else {
			/* Current angular elevation of the sun */
			double elevation = solar_elevation(
				now, loc.lat, loc.lon);
			if (options.verbose) {
				/* TRANSLATORS: Append degree symbol if
				   possible. */
				printf(_("Solar elevation: %f\n"), elevation);
			}

			period = get_period_from_elevation(scheme, elevation);
			transition_prog =
				get_transition_progress_from_elevation(
					scheme, elevation);
		}

		/* Use transition progress to set color temperature */
		color_setting_t interp;
		interpolate_transition_scheme(
			scheme, transition_prog, &interp);

		if (options.verbose || options.mode == PROGRAM_MODE_PRINT) {
			print_period(period, transition_prog);
			printf(_("Color temperature: %uK\n"),
			       interp.temperature);
			printf(_("Brightness: %.2f\n"),
			       interp.brightness);
		}

		if (options.mode != PROGRAM_MODE_PRINT) {
			/* Adjust temperature */
			r = options.method->set_temperature(
				method_state, &interp, options.preserve_gamma);
			if (r < 0) {
				fputs(_("Temperature adjustment failed.\n"),
				      stderr);
				options.method->free(method_state);
				exit(EXIT_FAILURE);
			}

			/* In Quartz (macOS) the gamma adjustments will
			   automatically revert when the process exits.
			   Therefore, we have to loop until CTRL-C is received.
			   */
			if (strcmp(options.method->name, "quartz") == 0) {
				fputs(_("Press ctrl-c to stop...\n"), stderr);
				pause();
			}
		}
	}
	break;
	case PROGRAM_MODE_MANUAL:
	{
		if (options.verbose) {
			printf(_("Color temperature: %uK\n"),
			       options.temp_set);
		}

		/* Adjust temperature */
		color_setting_t manual = scheme->day;
		manual.temperature = options.temp_set;
		r = options.method->set_temperature(
			method_state, &manual, options.preserve_gamma);
		if (r < 0) {
			fputs(_("Temperature adjustment failed.\n"), stderr);
			options.method->free(method_state);
			exit(EXIT_FAILURE);
		}

		/* In Quartz (OSX) the gamma adjustments will automatically
		   revert when the process exits. Therefore, we have to loop
		   until CTRL-C is received. */
		if (strcmp(options.method->name, "quartz") == 0) {
			fputs(_("Press ctrl-c to stop...\n"), stderr);
			pause();
		}
	}
	break;
	case PROGRAM_MODE_RESET:
	{
		/* Reset screen */
		color_setting_t reset;
		color_setting_reset(&reset);

		r = options.method->set_temperature(method_state, &reset, 0);
		if (r < 0) {
			fputs(_("Temperature adjustment failed.\n"), stderr);
			options.method->free(method_state);
			exit(EXIT_FAILURE);
		}

		/* In Quartz (OSX) the gamma adjustments will automatically
		   revert when the process exits. Therefore, we have to loop
		   until CTRL-C is received. */
		if (strcmp(options.method->name, "quartz") == 0) {
			fputs(_("Press ctrl-c to stop...\n"), stderr);
			pause();
		}
	}
	break;
	case PROGRAM_MODE_CONTINUAL:
	{
		r = run_continual_mode(
			options.provider, location_state, scheme,
			options.method, method_state,
			options.use_fade, options.preserve_gamma,
			options.verbose);
		if (r < 0) exit(EXIT_FAILURE);
	}
	break;
	}

	/* Clean up gamma adjustment state */
	if (options.mode != PROGRAM_MODE_PRINT) {
		options.method->free(method_state);
	}

	/* Clean up location provider state */
	if (need_location) {
		options.provider->free(location_state);
	}

	return EXIT_SUCCESS;
}
