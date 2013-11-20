#include "common.h"
#include <cstring>

using namespace hashhash;

int main() {
	int cli_fd = tcpskt(PORT_MASTER_CLIENTS, MAX_MASTER_CLIENTS);
	struct sockaddr_in cli_addr;
	int this_client_in_particular = accepta(cli_fd, &cli_addr);

	if(!recvpkt(this_client_in_particular, OPC_FKU, NULL, NULL, NULL))
		handle_error("recvpkt() 0");
	if(!recvpkt(this_client_in_particular, OPC_THX|OPC_FKU, NULL, NULL, NULL))
		handle_error("recvpkt() 1");
	if(recvpkt(this_client_in_particular, OPC_THX, NULL, NULL, NULL))
		handle_error("recvpkt() 2");

	char *arv = NULL;
	if(!recvpkt(this_client_in_particular, OPC_PLZ, &arv, 0, NULL))
		handle_error("recvpkt() 3");
	if(strcmp(arv, "key"))
		handle_error("strcmp() 0");
	free(arv);

	if(!sendpkt(this_client_in_particular, OPC_HRZ, "key", 2, 0))
		handle_error("sendpkt() 0");
}
