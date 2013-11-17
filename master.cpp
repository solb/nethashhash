#include "common.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>

using namespace hashhash;

int main() {
	int client_socket = tcpskt(PORT_MASTER_CLIENTS, MAX_MASTER_CLIENTS);
	struct sockaddr_in client_addr;
	int this_client_in_particular = accepta(client_socket, &client_addr);
	printf("%s\n", (const char *)recvpkt(this_client_in_particular)); // TODO free me, Darth Malloc
}
