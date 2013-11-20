#include "common.h"
#include <cstring>

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

	if(!sendpkt(srv_fd, OPC_FKU, NULL, 0, 0))
		handle_error("sendpkt() 0");
	if(!sendpkt(srv_fd, OPC_FKU, NULL, 0, 0))
		handle_error("sendpkt() 1");
	if(!sendpkt(srv_fd, OPC_FKU, NULL, 0, 0))
		handle_error("sendpkt() 2");

	if(!sendpkt(srv_fd, OPC_PLZ, "key", 0, 0))
		handle_error("sendpkt() 3");

	char *arv = NULL;
	uint16_t flwng = 0;
	if(!recvpkt(srv_fd, OPC_HRZ, &arv, &flwng, NULL))
		handle_error("recvpkt() 0");
	if(strcmp(arv, "key"))
		handle_error("strcmp() 0");
	if(flwng != 2)
		handle_error("operator!=() 0");
	free(arv);
}
