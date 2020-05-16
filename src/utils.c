/* utils.c -- common functions for redshift and redshift-dbus
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
   Copyright (c) 2020 Daniel Kondor <kondor.dani@gmail.com>
 */


#include <string.h>
#include <math.h>
#include <time.h>
#include <poll.h>

#include "utils.h"
#include "systemtime.h"


#ifdef ENABLE_NLS
# include <libintl.h>
# define _(s) gettext(s)
# define N_(s) (s)
#else
# define _(s) s
# define N_(s) s
# define gettext(s) s
#endif


#define MIN_LAT   -90.0
#define MAX_LAT    90.0
#define MIN_LON  -180.0
#define MAX_LON   180.0


/* try starting an adjustment method */
int
method_try_start(const gamma_method_t *method,
		 gamma_state_t **state, config_ini_state_t *config, char *args)
{
	int r;

	r = method->init(state);
	if (r < 0) {
		fprintf(stderr, _("Initialization of %s failed.\n"),
			method->name);
		return -1;
	}

	/* Set method options from config file. */
	config_ini_section_t *section =
		config_ini_get_section(config, method->name);
	if (section != NULL) {
		config_ini_setting_t *setting = section->settings;
		while (setting != NULL) {
			r = method->set_option(
				*state, setting->name, setting->value);
			if (r < 0) {
				method->free(*state);
				fprintf(stderr, _("Failed to set %s"
						  " option.\n"),
					method->name);
				/* TRANSLATORS: `help' must not be
				   translated. */
				fprintf(stderr, _("Try `-m %s:help' for more"
						  " information.\n"),
					method->name);
				return -1;
			}
			setting = setting->next;
		}
	}

	/* Set method options from command line. */
	while (args != NULL) {
		char *next_arg = strchr(args, ':');
		if (next_arg != NULL) *(next_arg++) = '\0';

		const char *key = args;
		char *value = strchr(args, '=');
		if (value == NULL) {
			fprintf(stderr, _("Failed to parse option `%s'.\n"),
				args);
			return -1;
		} else {
			*(value++) = '\0';
		}

		r = method->set_option(*state, key, value);
		if (r < 0) {
			method->free(*state);
			fprintf(stderr, _("Failed to set %s option.\n"),
				method->name);
			/* TRANSLATORS: `help' must not be translated. */
			fprintf(stderr, _("Try -m %s:help' for more"
					  " information.\n"), method->name);
			return -1;
		}

		args = next_arg;
	}

	/* Start method. */
	r = method->start(*state);
	if (r < 0) {
		method->free(*state);
		fprintf(stderr, _("Failed to start adjustment method %s.\n"),
			method->name);
		return -1;
	}

	return 0;
}




int
provider_try_start(const location_provider_t *provider,
		   location_state_t **state, config_ini_state_t *config,
		   char *args)
{
	int r;

	r = provider->init(state);
	if (r < 0) {
		fprintf(stderr, _("Initialization of %s failed.\n"),
			provider->name);
		return -1;
	}

	/* Set provider options from config file. */
	config_ini_section_t *section =
		config_ini_get_section(config, provider->name);
	if (section != NULL) {
		config_ini_setting_t *setting = section->settings;
		while (setting != NULL) {
			r = provider->set_option(*state, setting->name,
						 setting->value);
			if (r < 0) {
				provider->free(*state);
				fprintf(stderr, _("Failed to set %s"
						  " option.\n"),
					provider->name);
				/* TRANSLATORS: `help' must not be
				   translated. */
				fprintf(stderr, _("Try `-l %s:help' for more"
						  " information.\n"),
					provider->name);
				return -1;
			}
			setting = setting->next;
		}
	}

	/* Set provider options from command line. */
	const char *manual_keys[] = { "lat", "lon" };
	int i = 0;
	while (args != NULL) {
		char *next_arg = strchr(args, ':');
		if (next_arg != NULL) *(next_arg++) = '\0';

		const char *key = args;
		char *value = strchr(args, '=');
		if (value == NULL) {
			/* The options for the "manual" method can be set
			   without keys on the command line for convencience
			   and for backwards compatability. We add the proper
			   keys here before calling set_option(). */
			if (strcmp(provider->name, "manual") == 0 &&
			    i < sizeof(manual_keys)/sizeof(manual_keys[0])) {
				key = manual_keys[i];
				value = args;
			} else {
				fprintf(stderr, _("Failed to parse option `%s'.\n"),
					args);
				return -1;
			}
		} else {
			*(value++) = '\0';
		}

		r = provider->set_option(*state, key, value);
		if (r < 0) {
			provider->free(*state);
			fprintf(stderr, _("Failed to set %s option.\n"),
				provider->name);
			/* TRANSLATORS: `help' must not be translated. */
			fprintf(stderr, _("Try `-l %s:help' for more"
					  " information.\n"), provider->name);
			return -1;
		}

		args = next_arg;
		i += 1;
	}

	/* Start provider. */
	r = provider->start(*state);
	if (r < 0) {
		provider->free(*state);
		fprintf(stderr, _("Failed to start provider %s.\n"),
			provider->name);
		return -1;
	}

	return 0;
}

int
providers_try_start_all(options_t *options, config_ini_state_t *config_state,
	location_state_t **location_state, const location_provider_t *location_providers)
{
	int r;
	if (options->provider != NULL) {
		/* Use provider specified on command line or config file. */
		r = provider_try_start(
			options->provider, location_state,
			config_state, options->provider_args);
		if (r < 0) return r;
	} else {
		/* Try all providers, use the first that works. */
		for (int i = 0;
			 location_providers[i].name != NULL; i++) {
			const location_provider_t *p = &location_providers[i];
			fprintf(stderr, _("Trying location provider `%s'...\n"),
				p->name);
			r = provider_try_start(p, location_state,
						   config_state, NULL);
			if (r < 0) {
				fputs(_("Trying next provider...\n"), stderr);
				continue;
			}

			/* Found provider that works. */
			printf(_("Using provider `%s'.\n"), p->name);
			options->provider = p;
			break;
		}

		/* Failure if no providers were successful at this
		   point. */
		if (options->provider == NULL) {
			fputs(_("No more location providers"
				" to try.\n"), stderr);
			return -1;
		}
	}

	/* Solar elevations */
	if (options->scheme.high < options->scheme.low) {
		fprintf(stderr,
			_("High transition elevation cannot be lower than"
			  " the low transition elevation.\n"));
		return -1;
	}

	if (options->verbose) {
		/* TRANSLATORS: Append degree symbols if possible. */
		printf(_("Solar elevations: day above %.1f, night below %.1f\n"),
			   options->scheme.high, options->scheme.low);
	}
	return 0;
}


int
methods_try_start_all(options_t *options, config_ini_state_t *config_state,
	gamma_state_t **method_state, const gamma_method_t* gamma_methods)
{
	int r;
	if (options->method != NULL) {
		/* Use method specified on command line. */
		r = method_try_start(
			options->method, method_state, config_state,
			options->method_args);
		if (r < 0) return r;
	} else {
		/* Try all methods, use the first that works. */
		for (int i = 0; gamma_methods[i].name != NULL; i++) {
			const gamma_method_t *m = &gamma_methods[i];
			if (!m->autostart) continue;

			r = method_try_start(m, method_state, config_state, NULL);
			if (r < 0) {
				fputs(_("Trying next method...\n"), stderr);
				continue;
			}

			/* Found method that works. */
			printf(_("Using method `%s'.\n"), m->name);
			options->method = m;
			break;
		}

		/* Failure if no methods were successful at this point. */
		if (options->method == NULL) {
			fputs(_("No more methods to try.\n"), stderr);
			return -1;
		}
	}
	return 0;
}


/* Try to get location from provider.

   If location provider is dynamic, waits until timeout (milliseconds)
   has elapsed or forever if timeout is -1.

   Otherwise just checks if a new location is available and returns
   immediately.

   Writes location to loc. Returns -1 on error, 1 if location became
   available, 0 if not (timeout reached or not dynamic provider). */
int
provider_get_location(
	const location_provider_t *provider, location_state_t *state,
	int timeout, location_t *loc)
{
	int available = 0;
	struct pollfd pollfds[1];
	int loc_fd = provider->get_fd(state);
	if (loc_fd >= 0) {
		/* Provider is dynamic with a pipe to signal for updated.
		   In this case, it makes sense to wait here. */
		/* TODO: This should use a monotonic time source. */
		while (1) {
			double now;
			int r = systemtime_get_time(&now);
			if (r < 0) {
				fputs(_("Unable to read system time.\n"),
				      stderr);
				return -1;
			}

			/* Poll on file descriptor until ready. */
			pollfds[0].fd = loc_fd;
			pollfds[0].events = POLLIN;
			r = poll(pollfds, 1, timeout);
			if (r < 0) {
				perror("poll");
				return -1;
			} else if (r == 0) {
				return 0;
			}

			double later;
			r = systemtime_get_time(&later);
			if (r < 0) {
				fputs(_("Unable to read system time.\n"),
				      stderr);
				return -1;
			}

			/* Adjust timeout by elapsed time */
			if (timeout >= 0) {
				timeout -= (later - now) * 1000;
				timeout = timeout < 0 ? 0 : timeout;
			}
			r = provider->handle(state, loc, &available);
			if (r < 0) return -1;
			if (available) return 1;
			if (!timeout) return available;
		}
	}
	else {
		/* Location provider does not use pipe, just check if there
		   was an update */
		int r = provider->handle(state, loc, &available);
		if (r < 0) return -1;
		return available;
	}
}



int 
redshift_init_options(options_t* options, config_ini_state_t* config_state,
		int argc, char** argv, const gamma_method_t* gamma_methods,
		const location_provider_t* location_providers)
{
	options_init(options);
	options_parse_args(
		options, argc, argv, gamma_methods, location_providers);

	/* Load settings from config file. */
	int r = config_ini_init(config_state, options->config_filepath);
	if (r < 0) {
		fputs("Unable to load config file.\n", stderr);
		return r;
	}

	free(options->config_filepath);

	options_parse_config_file(
		options, config_state, gamma_methods, location_providers);

	options_set_defaults(options);

	if (options->scheme.dawn.start >= 0 || options->scheme.dawn.end >= 0 ||
	    options->scheme.dusk.start >= 0 || options->scheme.dusk.end >= 0) {
		if (options->scheme.dawn.start < 0 ||
		    options->scheme.dawn.end < 0 ||
		    options->scheme.dusk.start < 0 ||
		    options->scheme.dusk.end < 0) {
			fputs(_("Partitial time-configuration not"
				" supported!\n"), stderr);
			return -1;
		}

		if (options->scheme.dawn.start > options->scheme.dawn.end ||
		    options->scheme.dawn.end > options->scheme.dusk.start ||
		    options->scheme.dusk.start > options->scheme.dusk.end) {
			fputs(_("Invalid dawn/dusk time configuration!\n"),
			      stderr);
			return -1;
		}

		options->scheme.use_time = 1;
	}
	return 0;
}


/* Check whether location is valid.
   Prints error message on stderr and returns 0 if invalid, otherwise
   returns 1. */
int
location_is_valid(const location_t *location)
{
	/* Latitude */
	if (location->lat < MIN_LAT || location->lat > MAX_LAT) {
		/* TRANSLATORS: Append degree symbols if possible. */
		fprintf(stderr,
			_("Latitude must be between %.1f and %.1f.\n"),
			MIN_LAT, MAX_LAT);
		return 0;
	}

	/* Longitude */
	if (location->lon < MIN_LON || location->lon > MAX_LON) {
		/* TRANSLATORS: Append degree symbols if possible. */
		fprintf(stderr,
			_("Longitude must be between"
			  " %.1f and %.1f.\n"), MIN_LON, MAX_LON);
		return 0;
	}

	return 1;
}

/* Print location */
void
print_location(const location_t *location)
{
	/* TRANSLATORS: Abbreviation for `north' */
	const char *north = _("N");
	/* TRANSLATORS: Abbreviation for `south' */
	const char *south = _("S");
	/* TRANSLATORS: Abbreviation for `east' */
	const char *east = _("E");
	/* TRANSLATORS: Abbreviation for `west' */
	const char *west = _("W");

	/* TRANSLATORS: Append degree symbols after %f if possible.
	   The string following each number is an abreviation for
	   north, source, east or west (N, S, E, W). */
	printf(_("Location: %.2f %s, %.2f %s\n"),
	       fabs(location->lat), location->lat >= 0.f ? north : south,
	       fabs(location->lon), location->lon >= 0.f ? east : west);
}




/* Determine which period we are currently in based on time offset. */
period_t
get_period_from_time(const transition_scheme_t *transition, int time_offset)
{
	if (time_offset < transition->dawn.start ||
	    time_offset >= transition->dusk.end) {
		return PERIOD_NIGHT;
	} else if (time_offset >= transition->dawn.end &&
		   time_offset < transition->dusk.start) {
		return PERIOD_DAYTIME;
	} else {
		return PERIOD_TRANSITION;
	}
}

/* Determine which period we are currently in based on solar elevation. */
period_t
get_period_from_elevation(
	const transition_scheme_t *transition, double elevation)
{
	if (elevation < transition->low) {
		return PERIOD_NIGHT;
	} else if (elevation < transition->high) {
		return PERIOD_TRANSITION;
	} else {
		return PERIOD_DAYTIME;
	}
}

/* Determine how far through the transition we are based on time offset. */
double
get_transition_progress_from_time(
	const transition_scheme_t *transition, int time_offset)
{
	if (time_offset < transition->dawn.start ||
	    time_offset >= transition->dusk.end) {
		return 0.0;
	} else if (time_offset < transition->dawn.end) {
		return (transition->dawn.start - time_offset) /
			(double)(transition->dawn.start -
				transition->dawn.end);
	} else if (time_offset > transition->dusk.start) {
		return (transition->dusk.end - time_offset) /
			(double)(transition->dusk.end -
				transition->dusk.start);
	} else {
		return 1.0;
	}
}

/* Determine how far through the transition we are based on elevation. */
double
get_transition_progress_from_elevation(
	const transition_scheme_t *transition, double elevation)
{
	if (elevation < transition->low) {
		return 0.0;
	} else if (elevation < transition->high) {
		return (transition->low - elevation) /
			(transition->low - transition->high);
	} else {
		return 1.0;
	}
}

/* Return number of seconds since midnight from timestamp. */
int
get_seconds_since_midnight(double timestamp)
{
	time_t t = (time_t)timestamp;
	struct tm tm;
	localtime_r(&t, &tm);
	return tm.tm_sec + tm.tm_min * 60 + tm.tm_hour * 3600;
}





