#include "common.h"
#include <cstring>

using namespace hashhash;

int main() {
	int cli_fd = tcpskt(PORT_MASTER_CLIENTS, MAX_MASTER_CLIENTS);
	struct sockaddr_in cli_addr;
	int this_client_in_particular = accepta(cli_fd, &cli_addr);

	if(!recvpkt(this_client_in_particular, OPC_FKU, NULL, NULL))
		handle_error("recvpkt() 0");
	if(!recvpkt(this_client_in_particular, OPC_THX|OPC_FKU, NULL, NULL))
		handle_error("recvpkt() 1");
	if(recvpkt(this_client_in_particular, OPC_THX, NULL, NULL))
		handle_error("recvpkt() 2");
}
