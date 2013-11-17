#include "common.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>

using namespace hashhash;

int main() {
	int client_socket = IP_SOCKET();
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(PORT_MASTER_CLIENTS);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	const int areyouserious = true;
	if(setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &areyouserious, sizeof areyouserious) < 0)
		handle_error("setsockopt()");
	if(bind(client_socket, (const struct sockaddr *)&server_addr, sizeof server_addr))
		handle_error("bind()");
	struct sockaddr_in client_addr;
	if(listen(client_socket, MAX_MASTER_CLIENTS) < 0)
		handle_error("listen()");
	int this_client_in_particular = accepta(client_socket, &client_addr);
	printf("%s\n", (const char *)recvpkt(this_client_in_particular));
}
