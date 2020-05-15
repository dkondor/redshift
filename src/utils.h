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

int provider_try_start(const location_provider_t *provider,
	location_state_t **state, config_ini_state_t *config, char *args);

int provider_get_location(const location_provider_t *provider,
	location_state_t *state, int timeout, location_t *loc);

int providers_try_start_all(options_t *options, config_ini_state_t *config_state,
	location_state_t **location_state, const location_provider_t *location_providers);

int redshift_init_options(options_t* options, config_ini_state_t* config_state,
	int argc, char** argv, const gamma_method_t* gamma_methods,
	const location_provider_t* location_providers);
	
/* try starting an adjustment method */
int method_try_start(const gamma_method_t *method,
	gamma_state_t **state, config_ini_state_t *config, char *args);

int methods_try_start_all(options_t *options, config_ini_state_t *config_state,
	gamma_state_t **method_state, const gamma_method_t* gamma_methods);


#endif

