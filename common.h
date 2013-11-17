#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define IP_SOCKET() socket(AF_INET, SOCK_STREAM, 0)

namespace hashhash {
	const int PORT_MASTER_CLIENTS = 1030;
	const int PORT_MASTER_REGISTER = 1031;
	const int PORT_MASTER_HEARTBEAT = 1032;
	const int PORT_SLAVE_MAIN = 1030;

	const int MAX_MASTER_CLIENTS = 1;

	const int MAX_PACKET_LEN = 512;

	int accepta(int, struct sockaddr_in *);
	void *recvpkt(int);
	void *recvpkta(int, struct sockaddr_in *);
	void *recvpktal(int, size_t *, struct sockaddr_in *);

	void handle_error(const char *);
}

#endif
