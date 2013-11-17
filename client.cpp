#include "common.h"
#include <sys/socket.h>
#include <cstring>
#include <unistd.h>

using namespace hashhash;

int main() {
	int local_socket = tcpskt(0, 0); // ephemeral port, don't listen
	struct sockaddr_in remote_addr;
	remote_addr.sin_family = AF_INET;
	remote_addr.sin_port = htons(1030);
	remote_addr.sin_addr.s_addr = INADDR_ANY;
	if(connect(local_socket, (const struct sockaddr *)&remote_addr, sizeof remote_addr) < 0)
		handle_error("connect()");
	while(true) {
		char input[MAX_PACKET_LEN];
		memset(input, 0, sizeof input);
		scanf("%s", input);
		write(local_socket, input, sizeof input);
	}
}
