#include "common.h"
#include <cstring>
#include <unistd.h>

using namespace hashhash;

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("USAGE: %s <hostname>\n", argv[0]);
		return RETVAL_INVALID_ARG;
	}
	
	int srv_fd;
	if(!rslvconn(&srv_fd, argv[1], PORT_MASTER_CLIENTS)) {
		printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
		return RETVAL_CONN_FAILED;
	}
	
	if(!sendpkt(srv_fd, OPC_HEY, NULL, 0, 0)) {
		handle_error("registration sendpkt()");
	}
	
	while(true) {
		usleep(500000);
		if(!sendpkt(srv_fd, OPC_SUP, NULL, 0, 0)) {
			handle_error("keepalive sendpkt()");
		}
	}
}
