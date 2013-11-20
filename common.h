#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <cstdint>

namespace hashhash {
	const int PORT_MASTER_CLIENTS = 1030;
	const int PORT_MASTER_REGISTER = 1031;
	const int PORT_MASTER_HEARTBEAT = 1032;
	const int PORT_SLAVE_MAIN = 1030;

	const int MAX_MASTER_CLIENTS = 1;

	const int MAX_PACKET_LEN = 512;

	const uint8_t OPC_PLZ = 0;
	const uint8_t OPC_HRZ = 1;
	const uint8_t OPC_STF = 2;
	const uint8_t OPC_HEY = 4;
	const uint8_t OPC_BYE = 8;
	const uint8_t OPC_THX = 16;
	const uint8_t OPC_FKU = 32;
	const uint8_t OPC_SUP = 64;

	const int RETVAL_INVALID_ARG = 1;
	const int RETVAL_CONN_FAILED = 2;

	int tcpskt(int, int);
	int accepta(int, struct sockaddr_in *);
	bool rslvconn(int *, const char *, in_port_t);
	bool recvpkt(int, uint16_t, char **, uint16_t *);
	bool sendpkt(int, uint8_t, const char *, uint16_t, int);
	bool sendfile(int, const char *, const char*);

	void handle_error(const char *);
}

#endif
