/*
 * commands.c -- read and parse commands from stdin / unix socket
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


#include "commands.h"
#include "signals.h"

#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


#define BRIGHTNESS_ADJUST 0.1f
#define TEMP_ADJUST 500


static int parse_command(const char* buf, size_t len, transition_scheme_t* scheme, const color_setting_t* current, int* disabled) {
	if(!strncmp(buf,"brightness",10)) {
		if(len > 10) {
			size_t i = 10;
			for(;i<len && (buf[i] == ' ' || buf[i] == '\t');i++);
			if(i<len) {
				if(isdigit(buf[i]) || buf[i] == '.') {
					char* endptr;
					float brightness = strtof(buf + i,&endptr);
					if(buf + i != endptr) {
						brightness = CLAMP(MIN_BRIGHTNESS,brightness,MAX_BRIGHTNESS);
						scheme->override.brightness = brightness;
						scheme->use_override = scheme->use_override | USE_OVERRIDE_BRIGHTNESS;
						return 1;
					}
					return 0;
				}
				float delta = 0.0f;
				if(!strncmp(buf+i,"up",2)) delta = BRIGHTNESS_ADJUST;
				else if(!strncmp(buf+i,"down",2)) delta = -1.0*BRIGHTNESS_ADJUST;
				if(delta != 0.0f) {
					float brightness = (scheme->use_override & USE_OVERRIDE_BRIGHTNESS) ? scheme->override.brightness : current->brightness;
					brightness = CLAMP(MIN_BRIGHTNESS, brightness + delta, MAX_BRIGHTNESS);
					scheme->override.brightness = brightness;
					scheme->use_override = scheme->use_override | USE_OVERRIDE_BRIGHTNESS;
					return 1;
				}
				else if(!strncmp(buf+i,"reset",5))
					scheme->use_override &= ~USE_OVERRIDE_BRIGHTNESS;
			}
		}
		return 0;
	}
	if(!strncmp(buf,"enable",6)) {
		*disabled = 0;
		return 1;
	}
	if(!strncmp(buf,"disable",7)) {
		*disabled = 1;
		return 1;
	}
	if(!strncmp(buf,"toggle",6)) {
		*disabled = *disabled ? 0 : 1;
		return 1;
	}
	if(!strncmp(buf,"temp",4)) {
		size_t i = 4;
		for(;i<len && (buf[i] == ' ' || buf[i] == '\t');i++);
			if(i<len) {
				if(isdigit(buf[i])) {
					char* endptr;
					long int temp = strtol(buf+i,&endptr,10);
					if(buf + i != endptr) {
						int temp2 = (int)CLAMP(MIN_TEMP,temp,MAX_TEMP);
						scheme->override.temperature = temp2;
						scheme->use_override = scheme->use_override | USE_OVERRIDE_TEMP;
						return 1;
					}
					return 0;
				}
				int delta = 0;
				if(!strncmp(buf+i,"up",2)) delta = TEMP_ADJUST;
				else if(!strncmp(buf+i,"down",2)) delta = -1*TEMP_ADJUST;
				if(delta) {
					int temp = (scheme->use_override & USE_OVERRIDE_TEMP) ? scheme->override.temperature : current->temperature;
					temp = CLAMP(MIN_TEMP, temp + delta, MAX_TEMP);
					scheme->override.temperature = temp;
					scheme->use_override = scheme->use_override | USE_OVERRIDE_TEMP;
					return 1;
				}
				else if(!strncmp(buf+i,"reset",5))
					scheme->use_override &= ~USE_OVERRIDE_TEMP;
			}
	}
	if(!strncmp(buf,"shutdown",8)) exiting = 1;
	return 0;
}



int handle_commands_file(command_input_t* c, FILE* f, transition_scheme_t* scheme, const color_setting_t* current, int* disabled) {
	char* buf = c->buf;
	char* pos = c->pos;
	size_t buf_len = c->buf_len;
	
	if(!fgets(pos,buf_len - (pos-buf),f)) return -1;
	
	/* check if there is a newline in the text read */
	for(;pos < buf + buf_len && *pos && *pos != '\n';pos++);
	if(pos == buf + buf_len) {
		/* too long line, should be skipped */
		c->pos = buf;
		c->skip = 1;
		return 0;
	}
	
	if(*pos == 0) {
		/* short read without newline, might continue next time */
		c->pos = pos;
		return 0;
	}
	
	/* in this case, we have a newline */
	if(c->skip) {
		/* skip this line */
		c->pos = buf;
		c->skip = 0;
		return 0;
	}
	
	c->pos = buf;
	/* we have one line, parse the command in it */
	return parse_command(buf,pos-buf,scheme,current,disabled);
}

int handle_commands_socket(command_input_t* c, int fd, transition_scheme_t* scheme, const color_setting_t* current, int* disabled) {
	char* buf = c->buf;
	char* pos = c->pos;
	size_t buf_len = c->buf_len;
	
	ssize_t r = recv(fd,pos,buf_len - (pos-buf),MSG_PEEK);
	
	if(r < 0) return -1;
	if(r == 0) {
		/* TODO: handle closed socket! */
	}
	
	/* check if there is a newline in the text read */
	char* pos0 = pos;
	for(;pos < pos0 + r && *pos != '\n';pos++);
	
	/* perform the actual read */
	size_t len1 = pos-pos0;
	if(pos < pos0 + r) len1++; /* in this case, read the newline as well */
	ssize_t r2 = recv(fd,pos0,len1,0);
	if(r2 != len1) {
		/* error on second read */
		return -1;
	}
	
	if(pos == buf + buf_len) {
		/* too long line, should be skipped */
		c->pos = buf;
		c->skip = 1;
		return 0;
	}
	
	if(pos == pos0 + r) {
		/* short read without newline, might continue next time */
		c->pos = pos;
		return 0;
	}
	
	/* in this case, we have a newline */
	if(c->skip) {
		/* skip this line */
		c->pos = buf;
		c->skip = 0;
		return 0;
	}
	/* replace newline with null terminator */
	*pos = 0;
	
	c->pos = buf;
	/* we have one line, parse the command in it */
	return parse_command(buf,pos-buf,scheme,current,disabled);
}


int handle_poll_results(command_fds_t* cmdfds,
	transition_scheme_t* scheme, const color_setting_t* current,
	int* disabled, int verbose) {
	
	int r = 0;
	const int max_fds = cmdfds->max_socket_fds;
	int i;
	
	struct pollfd* pollfds = cmdfds->pollfds;
	command_input_t* cmds = cmdfds->bufs;
	
	/* 1. handle stdin */
	if(pollfds[1].fd >= 0 && pollfds[1].revents) {
		if(pollfds[1].revents & POLLERR) {
			if(verbose) printf(_("Error reading from standard input!\n"));
			pollfds[1].fd = -1;
		}
		else if(pollfds[1].revents & POLLIN) {
			int r1 = handle_commands_file(cmds,stdin,scheme,current,disabled);
			if(r1 < 0) {
				/* stdin is closed now */
				if(verbose) printf(_("Standard input closed\n"));
				pollfds[1].fd = -1;
			}
			/* count the inputs with valid command as the return value */
			if(r1 > 0) r++;
		}
		/* note: POLLHUP is ignored, there might be still data after it */
	}
	
	/* 2. handle listening socket */
	if(pollfds[2].fd >= 0 && (pollfds[2].revents & POLLIN)) {
		int fd;
		/* note: try to ensure close-on-exec semantics on any
		 * file descriptor opened -- accept4() is Linux-specific */
#ifdef _GNU_SOURCE
		fd = accept4(pollfds[2].fd,0,0,SOCK_CLOEXEC);
#else
		fd = accept(pollfds[2].fd,0,0);
		if(fd >= 0) {
			int tmp = fcntl(fd,F_GETFD);
			if(tmp >= 0) fcntl(fd,F_SETFD,tmp | FD_CLOEXEC);
		}
#endif
		/* find the first available entry in pollfds */
		for(i=0;i<max_fds;i++) {
			if(pollfds[3+i].fd == -1) {
				pollfds[3+i].fd = fd;
				pollfds[3+i].events = POLLIN;
				pollfds[3+i].revents = 0;
				cmds[1+i].pos = cmds[1+i].buf;
				cmds[1+i].skip = 0;
				break;
			}
		}
		if(i == max_fds) {
			if(verbose) printf(_("Error: too many connected clients, new connection rejected!\n"));
			close(fd);
		}
	}
	
	
	/* 3. handle open connections on the socket */
	for(i = 0; i < max_fds; i++) {
		if(pollfds[3+i].fd >= 0 && pollfds[3+i].revents) {
			if(pollfds[3+i].revents & POLLERR) {
				if(verbose) printf(_("Error reading from incoming connection!\n"));
				close(pollfds[3+i].fd);
				pollfds[3+i].fd = -1;
			}
			else if(pollfds[3+i].revents & (POLLIN | POLLHUP)) {
				int r1 = handle_commands_socket(cmds + 1 + i,
					pollfds[3+i].fd, scheme, current, disabled);
				if(r1 < 0) {
					/* no data read / read error */
					close(pollfds[3+i].fd);
					pollfds[3+i].fd = -1;
				}
				else if(r1) r++;
			}
		}
	}
	
	return r;
}


command_fds_t* command_fds_alloc(const int max_socket_fds) {
	int i;
	char* buf1;
	command_fds_t* r = malloc(sizeof(command_fds_t));
	if(!r) return r;
	
	r->pollfds = malloc(sizeof(struct pollfd)*(max_socket_fds + 3));
	r->bufs = malloc(sizeof(command_input_t)*(max_socket_fds + 1));
	r->max_socket_fds = max_socket_fds;
	buf1 = malloc(BUF_LEN*(max_socket_fds + 1));
	
	if( ! (r->pollfds && r->bufs && buf1) ) {
		if(r->pollfds) free(r->pollfds);
		if(r->bufs) free(r->bufs);
		if(buf1) free(buf1);
		free(r);
		return 0;
	}
	
	for(i = 0; i < max_socket_fds + 3; i++) {
		r->pollfds[i].fd = -1;
		r->pollfds[i].events = 0;
		r->pollfds[i].revents = 0;
	}
	for(i = 0; i < max_socket_fds + 1; i++) {
		r->bufs[i].buf = buf1 + BUF_LEN*i;
		r->bufs[i].pos = r->bufs[i].buf;
		r->bufs[i].buf_len = BUF_LEN;
		r->bufs[i].skip = 0;
	}
	return r;
}

void command_fds_free(command_fds_t* cmdfds) {
	int i;
	if(cmdfds) {
		if(cmdfds->pollfds) {
			for(i = 2; i < cmdfds->max_socket_fds + 3; i++)
				if(cmdfds->pollfds[i].fd >= 0) {
					close(cmdfds->pollfds[i].fd);
					cmdfds->pollfds[i].fd = -1;
				}
			free(cmdfds->pollfds);
			cmdfds->pollfds = 0;
		}
		if(cmdfds->bufs) {
			if(cmdfds->bufs[0].buf) free(cmdfds->bufs[0].buf);
			free(cmdfds->bufs);
			cmdfds->bufs = 0;
		}
		free(cmdfds);
	}
}

int create_socket(const char* name, command_fds_t* cmdfds) {
	if(!name) return -1;
	if(!cmdfds->max_socket_fds) return -1;
	
	int fd;
#ifdef _GNU_SOURCE
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd >= 0) {
		int tmp = fcntl(fd,F_GETFD);
		if(tmp >= 0) fcntl(fd,F_SETFD,tmp | FD_CLOEXEC);
	}
#endif
	if(fd < 0) {
		perror("socket");
		return -1;
	}
	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, name, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path)-1] = 0;
	
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) ) {
		perror("bind");
		close(fd);
		return -1;
	}
	
	if (listen(fd,16)) {
		perror("listen");
		close(fd);
		unlink(name);
		return -1;
	}
	
	cmdfds->pollfds[2].fd = fd;
	cmdfds->pollfds[2].events = POLLIN;
	cmdfds->pollfds[2].revents = 0;
	return 0;
}


int send_commands(const char* name, char** argv, int argc) {
	if (!argc) return -1;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}
	
	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, name, sizeof(addr.sun_path));
	addr.sun_path[sizeof(addr.sun_path)-1] = 0;
	
	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) ) {
		perror("connect");
		close(fd);
		return -1;
	}
	
	int i;
	for(i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]);
		const char* tmp = argv[i];
		char tmp2 = '\n';
		ssize_t l2 = 0;
		while(len) {
			l2 = write(fd,tmp,len);
			if(l2 < 0) break; 
			len -= l2;
			tmp += l2;
		}
		
		if(l2 >= 0) {
			len = 1;
			do {
				l2 = write(fd,&tmp2,len);
				if(l2 < 0) break;
			} while(l2 == 0);
		}
		
		if(l2 < 0) {
			perror("write");
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}



