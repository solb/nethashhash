#include "common.h"
#include <cstring>
#include <pthread.h>
#include <unistd.h>
#include <unordered_map>

using namespace hashhash;
using std::unordered_map;

static int master_fd;

void *heartbeat(void *ptr) {
	while(true) {
		usleep(SLAVE_KEEPALIVE_TIME);
		printf("Heart\n");
		if(!sendpkt(master_fd, OPC_SUP, NULL, 0, 0)) {
			handle_error("keepalive sendpkt()");
			return NULL;
		}
	}

	return NULL;
}

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("USAGE: %s <hostname>\n", argv[0]);
		return RETVAL_INVALID_ARG;
	}
	
	printf("here0\n");
	
	if(!rslvconn(&master_fd, argv[1], PORT_MASTER_REGISTER)) {
		printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
		return RETVAL_CONN_FAILED;
	}
	
	printf("here1\n");
	
	if(!sendpkt(master_fd, OPC_HEY, NULL, 0, 0)) {
		handle_error("registration sendpkt()");
	}
	
	printf("here2\n");
	
	pthread_t thread;
	memset(&thread, 0, sizeof(pthread_t));
	
	pthread_create(&thread, NULL, heartbeat, NULL);
	
	int incoming = tcpskt(PORT_SLAVE_MAIN, 1);
	usleep(10000); // TODO fix this crap
	if((incoming = accept(incoming, NULL, 0)) == -1) {
		handle_error("incoming from master accept()");
	}

	unordered_map<const char *, char *> stor; // TODO allocate on heap...
	while(true) {
		char *payld = NULL;
		uint16_t hrzcnt = 0; // sentinel for not a HRZ
		if(recvpkt(incoming, OPC_PLZ|OPC_HRZ, &payld, &hrzcnt, 0, false)) {
			if(hrzcnt) { // HRZ
				char *junk = NULL;
				unsigned int jsize;
				recvfile(incoming, hrzcnt, &junk, &jsize);
				stor[payld] = junk;
			}
			else { // PLZ
				if(stor.find(payld) == stor.end()) // Couldn't find it!
					handle_error("find()");

				if(!sendfile(incoming, payld, stor.at(payld)))
					handle_error("sendfile()");

				free(payld);
			}
		}
	}

	for(auto it = stor.begin(); it != stor.end(); ++it) {
		free((char *)it->first);
		free(it->second);
		it->second = NULL;
	}
}
