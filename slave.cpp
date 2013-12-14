/*
 * Copyright (C) 2013 Sol Boucher and Lane Lawley
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with it.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <unordered_map>

using namespace hashhash;
using std::unordered_map;

struct cabbage {
	size_t len;
	char *junk;
};

static int master_fd;
static unordered_map<const char *, struct cabbage *> *stor = NULL;

static void *heartbeat(void *);

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("USAGE: %s <hostname> [port]\n", argv[0]);
		return RETVAL_INVALID_ARG;
	}
	
	printf("here0\n");
	
	if(!rslvconn(&master_fd, argv[1], PORT_MASTER_REGISTER)) {
		printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
		return RETVAL_CONN_FAILED;
	}
	
	printf("here1\n");
	
	if(!sendpkt(master_fd, OPC_HEY, NULL, 0)) {
		handle_error("registration sendpkt()");
	}
	
	printf("here2\n");
	
	pthread_t thread;
	memset(&thread, 0, sizeof(pthread_t));
	
	pthread_create(&thread, NULL, heartbeat, NULL);
	
	int incoming = tcpskt(argc-2 ? atoi(argv[2]) : PORT_SLAVE_MAIN, 1);
	usleep(10000); // TODO fix this crap
	if((incoming = accept(incoming, NULL, 0)) == -1) {
		handle_error("incoming from master accept()");
	}

	stor = new unordered_map<const char *, struct cabbage *>();

	while(true) {
		char *payld = NULL;
		bool inbound = false; // whether it's a HRZ
		if(recvpkt(incoming, OPC_PLZ|OPC_HRZ, &payld, &inbound, 0, false)) {
			if(inbound) { // HRZ
				struct cabbage *head = (struct cabbage *)malloc(sizeof(struct cabbage));
				head->junk = NULL;
				recvfile(incoming, &head->junk, &head->len);
				(*stor)[payld] = head;
			}
			else { // PLZ
				if(!stor->count(payld)) // Couldn't find it!
					handle_error("find()");

				struct cabbage *illbeback = stor->at(payld);

				if(!sendfile(incoming, payld, illbeback->junk, illbeback->len))
					handle_error("sendfile()");

				free(payld);
			}
		}
	}

	for(auto it = stor->begin(); it != stor->end(); ++it) {
		free((char *)it->first);
		free(it->second->junk);
		it->second->junk = NULL;
		free(it->second);
		it->second = NULL;
	}
	delete stor;
}

void *heartbeat(void *ptr) {
	while(true) {
		usleep(SLAVE_KEEPALIVE_TIME);
		// printf("Heart\n");
		if(!sendpkt(master_fd, OPC_SUP, NULL, 0)) {
			handle_error("keepalive sendpkt()");
			return NULL;
		}
	}

	return NULL;
}
