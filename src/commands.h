/*
 * commands.h -- read and parse commands from stdin / unix socket
 * 
 * Copyright 2020 Daniel Kondor <kondor.dani@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Redshift is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Redshift.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */


#ifndef COMMANDS_H
#define COMMANDS_H

#define BUF_LEN 256

#include <stdio.h>
#include <poll.h>

#include "redshift.h"


/* hepler struct containing input buffers */
typedef struct {
	char* buf; /* buffer to read data to */
	char* pos; /* current position in buf (if partly read) */
	size_t buf_len; /* size of buffer (= maximum line length) */
	int skip; /* if nonzero, input is read, but not processed
			(this is used to ignore too long lines) */
} command_input_t;

/* helper struct to allocate memory for polling and buffers together */
typedef struct {
	struct pollfd* pollfds;
	command_input_t* bufs;
	int max_socket_fds;
} command_fds_t;


command_fds_t* command_fds_alloc(const int max_socket_fds);

void command_fds_free(command_fds_t* cmdfds);


int handle_commands_file(command_input_t* c, FILE* f, transition_scheme_t* scheme, const color_setting_t* current, int* disabled);
int handle_commands_socket(command_input_t* c, int fd, transition_scheme_t* scheme, const color_setting_t* current, int* disabled);

/* handle result of polling potentially multiple fds,
 * process commands given on any open connections, handle new
 * connections and closed connections
 * 
 * pollfds contains the collection of file descriptors given to poll
 *  -- pollfds[0] is the pipe used for notifications of location changes
 * 		(ignored by this function)
 *  -- pollfds[1] is stdin (or -1, in which case it is ignored)
 *  -- pollfds[2] is the socket for listening (receiving POLLIN
 * 		indicates new client connection)
 *  -- pollfds[3 -- max_fds+2] are possible open connections on the
 * 		socket (or -1 if not active)
 * *active_infds gives the number of open active connections
 * cmds contain the input buffers used for each open fd:
 * 	cmds[0] corresponds to stdin
 * 	cmds[1+i] corresponds to pollfds[3+i]
 * 
 * pollfds are adjusted if a connection is closed or a new connection
 * is accepted
 */
int handle_poll_results(command_fds_t* cmdfds,
	transition_scheme_t* scheme, const color_setting_t* current,
	int* disabled, int verbose);

/* create a socket and connect for listening */
int create_socket(const char* name, command_fds_t* cmdfds);


/* send commands to a running instance */
int send_commands(const char* name, char** argv, int argc);

#endif


