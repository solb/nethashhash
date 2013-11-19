#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>

namespace hashhash {
	const int PORT_MASTER_CLIENTS = 1030;
	const int PORT_MASTER_REGISTER = 1031;
	const int PORT_MASTER_HEARTBEAT = 1032;
	const int PORT_SLAVE_MAIN = 1030;

	const int MAX_MASTER_CLIENTS = 1;

	const short MAX_PACKET_LEN = 512;

	const short OPC_PLZ = 0;
	const short OPC_HRZ = 1;
	const short OPC_STF = 2;
	const short OPC_HEY = 4;
	const short OPC_BYE = 8;
	const short OPC_THX = 16;
	const short OPC_FKU = 32;
	const short OPC_SUP = 64;

	const int RETVAL_INVALID_ARG = 1;
	const int RETVAL_CONN_FAILED = 2;

	int tcpskt(int, int);
	int accepta(int, struct sockaddr_in *);
	bool rslvconn(int *, const char *, in_port_t);
	void *recvpkt(int);
	void *recvpkta(int, struct sockaddr_in *);
	void *recvpktal(int, size_t *, struct sockaddr_in *);

	void handle_error(const char *);
}

#endif
