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

   Copyright (c) 2013-2017  Jon Lund Steffensen <jonlst@gmail.com>
   Copyright (c) 2020 Daniel Kondor <kondor.dani@gmail.com>
*/

#ifndef REDSHIFT_UTILS_H
#define REDSHIFT_UTILS_H

#include "redshift.h"
#include "options.h"
#include "config-ini.h"

/* Try to get location from provider.

   If location provider has a pipe interface, waits until timeout
   (milliseconds) has elapsed or forever if timeout is -1.

   Otherwise just checks if a new location is available and returns
   immediately.

   Writes location to loc. Returns -1 on error, 1 if location became
   available, 0 if not (timeout reached or not dynamic provider).
   
   Updates timeout with the remaining time. */
int provider_get_location(const location_provider_t *provider,
	location_state_t *state, int *timeout, location_t *loc);

/* Try to start location provider */
int provider_try_start(const location_provider_t *provider,
	location_state_t **state, config_ini_state_t *config, char *args);

/* Try to start location provider specified in the options
   or any available location provider if not given in the options */
int providers_try_start_all(options_t *options, config_ini_state_t *config_state,
	location_state_t **location_state, const location_provider_t *location_providers);

/* try starting an adjustment method */
int method_try_start(const gamma_method_t *method,
	gamma_state_t **state, config_ini_state_t *config, char *args);

/* Try starting the gamm adjustment method specified in the options
   or any available adjustment method if none is given */
int methods_try_start_all(options_t *options, config_ini_state_t *config_state,
	gamma_state_t **method_state, const gamma_method_t* gamma_methods);

/* Parse options from the command line and the config file */
int redshift_init_options(options_t* options, config_ini_state_t* config_state,
	int argc, char** argv, const gamma_method_t* gamma_methods,
	const location_provider_t* location_providers);
	
/* Check whether location is valid.
   Prints error message on stderr and returns 0 if invalid, otherwise
   returns 1. */
int location_is_valid(const location_t *location);

/* Print location */
void print_location(const location_t *location);


/* Determine which period we are currently in based on time offset. */
period_t get_period_from_time(const transition_scheme_t *transition,
	int time_offset);
	
/* Determine which period we are currently in based on solar elevation. */
period_t get_period_from_elevation(
	const transition_scheme_t *transition, double elevation);

/* Determine how far through the transition we are based on time offset. */
double get_transition_progress_from_time(
	const transition_scheme_t *transition, int time_offset);

/* Determine how far through the transition we are based on elevation. */
double get_transition_progress_from_elevation(
	const transition_scheme_t *transition, double elevation);

/* Return number of seconds since midnight from timestamp. */
int get_seconds_since_midnight(double timestamp);

/* Interpolate color setting structs given alpha. */
void interpolate_color_settings(
	const color_setting_t *first,
	const color_setting_t *second,
	double alpha,
	color_setting_t *result);

/* Interpolate color setting structs transition scheme. */
void interpolate_transition_scheme(
	const transition_scheme_t *transition,
	double alpha,
	color_setting_t *result);

/* Return 1 if color settings have major differences, otherwise 0.
   Used to determine if a fade should be applied in continual mode. */
int color_setting_diff_is_major(
	const color_setting_t *first,
	const color_setting_t *second);

/* Return 1 if color settings differ at all, 0 otherwise.
   Used to determine if there should be an update. */
int color_setting_diff(
	const color_setting_t *first,
	const color_setting_t *second);

/* Reset color setting to default values. */
void color_setting_reset(color_setting_t *color);

#endif

