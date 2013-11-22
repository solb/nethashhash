#include "common.h"
#include <cstring>
#include <unistd.h>
#include <pthread.h>

using namespace hashhash;

static int master_fd;

void *heartbeat(void *ptr) {
	while(true) {
		usleep(SLAVE_KEEPALIVE_TIME);
		if(!sendpkt(master_fd, OPC_SUP, NULL, 0, 0)) {
			handle_error("keepalive sendpkt()");
			return NULL;
		}
	}
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
	if(accept(incoming, NULL, 0) == -1) {
		handle_error("incoming from master accept()");
	}
}
