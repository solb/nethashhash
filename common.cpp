#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <errno.h>
#include <netdb.h>
#include <unistd.h>

// Creates a socket and binds it to the specified port, optionally listening for incoming connections
// Accepts: socket file descriptor (0 for ephemeral), queue length (0 to skip listening)
// Returns: file descriptor
int hashhash::tcpskt(int port, int max_clients) {
	int client_socket = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	server_addr.sin_addr.s_addr = INADDR_ANY;
	const int areyouserious = true;
	if(setsockopt(client_socket, SOL_SOCKET, SO_REUSEADDR, &areyouserious, sizeof areyouserious) < 0)
		handle_error("setsockopt()");
	if(bind(client_socket, (const struct sockaddr *)&server_addr, sizeof server_addr))
		handle_error("bind()");
	if(max_clients && listen(client_socket, max_clients) < 0)
		handle_error("listen()");

	return client_socket;
}

// Blocks until an incoming TCP connection arrives
// Accepts: socket file descriptor, pointer to socket address structure (or NULL)
// Returns: file descriptor for the specific connection
int hashhash::accepta(int sfd, struct sockaddr_in *rmt_saddr)
{
	socklen_t rmt_saddr_len = sizeof(struct sockaddr_in);
	int realfd;
	if((realfd = accept(sfd, (struct sockaddr *)rmt_saddr, &rmt_saddr_len)) < 0)
		handle_error("accept()");

	return realfd;
}

// Resolves and connects to the specified hostname/address and port via TCP, providing a file descriptor
// Accepts: destination file descriptor, address or hostname, port number
// Returns: whether the resolution/connection succeeded
bool hashhash::rslvconn(int *sfd, const char *hname, in_port_t port)
{
	static const struct addrinfo filt = {0, AF_INET, SOCK_STREAM, 0, 0, NULL, NULL, NULL};

	if((*sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		handle_error("socket()");
	struct addrinfo *res = NULL;
	if(getaddrinfo(hname, NULL, &filt, &res))
		return false; // failed to resolve
	struct sockaddr_in dest = *(struct sockaddr_in *)res->ai_addr;
	freeaddrinfo(res);
	dest.sin_port = htons(port);
	if(connect(*sfd, (const struct sockaddr *)&dest, sizeof dest))
		return false; // failed to connect

	return true; // did EVERYTHING to get an A
}

// Listens on socket, ensuring the next packet to arrive is of one of the requested opcodes. If it is an carries data, that data is returned.
// Accepts: file descriptor, OR of acceptable opcodes, caller-owned buffer if that opcode provides data, caller's number of packets if that opcode provides it
// Returns: whether the expected opcode was received
bool hashhash::recvpkt(int sfd, uint16_t opcsel, char **buf, uint16_t *numpkts)
{
	uint16_t size;
	if(recv(sfd, &size, sizeof size, MSG_PEEK) < 0)
		handle_error("recv()");

	uint8_t packet[size+3];
	if(read(sfd, packet, size+3) < 0)
		handle_error("read()");

	uint8_t opcode = packet[2]; // actual opcode
	if(!(opcode&opcsel))
		return false; // not the opcode you're looking for

	switch(opcode) {
		case OPC_HRZ:
			*numpkts = *(uint16_t *)(packet+4);
			*buf = (char *)malloc(size-2);
			memcpy(buf, packet+5, size-2);
			return true;

		case OPC_PLZ:
		case OPC_STF:
			*buf = (char *)malloc(size);
			memcpy(buf, packet+3, size);
			return true;

		case OPC_HEY:
		case OPC_BYE:
		case OPC_FKU:
		case OPC_SUP:
			return true; // opcode matched
		default:
			return false; // invalid opcode
	}
}

// Builds a packet in the #hashtag protocol fashion and sends it through a socket.
// Accepts: file descriptor, opcode for packet, string data (in case packet needs it), number of STF packets to come (for hrz packets only), amount of data to read from buffer (for stf packets only)
// Returns: whether or not the packet was successfully sent
bool hashhash::sendpkt(int sfd, uint8_t opcode, const char *data, uint16_t hrzlen, int stfbytes) {
	uint16_t pktsize;
	uint8_t *pkt = NULL;
	int datalen;
	
	switch(opcode) {
		case OPC_HEY:
		case OPC_BYE:
		case OPC_THX:
		case OPC_FKU:
		case OPC_SUP:
			pktsize = 3 * sizeof(uint8_t); // simple packets are 3 bytes
			pkt = (uint8_t*)malloc(pktsize);
			break;
		case OPC_PLZ:
			datalen = strlen(data);
			pktsize = (3 + datalen) * sizeof(uint8_t);
			
			pkt = (uint8_t*)malloc(pktsize);
			memcpy((void*)(pkt + 3), data, datalen);
			
			break;
		case OPC_HRZ:
			datalen = strlen(data);
			pktsize = (3 + 2 + datalen) * sizeof(uint8_t);
			
			pkt = (uint8_t*)malloc(pktsize);
			*(uint16_t *)(pkt + 3) = hrzlen;
			memcpy((void*)(pkt + 5), data, datalen);
			
			break;
		case OPC_STF:
			if(stfbytes > MAX_PACKET_LEN - 3) {
				fprintf(stderr, "STF packet was given %d bytes; cannot accommodate more than %d bytes per packet\n", datalen, MAX_PACKET_LEN - 3);
				return false;
			}
			pktsize = (3 + stfbytes) * sizeof(uint8_t);
			
			pkt = (uint8_t*)malloc(pktsize);
			memcpy((void*)(pkt + 3), data, stfbytes);
			
			break;
	}
	
	// Encode the packet size minus three to account for the bytes that are always there
	// pkt[0] = (uint8_t)((pktsize - 3) >> 8);
	// pkt[1] = (uint8_t)((pktsize - 3) & 0xFF);
	*(uint16_t *)pkt = (pktsize - 3);
	pkt[2] = opcode;
	
	// for(int i = 0; i < pktsize; ++i) {
	// 	printf("%d, ", pkt[i]);
	// }
	// printf("\n");
	
	send(sfd, (void*)pkt, pktsize, 0);
	
	free(pkt);
	
	return true;
}

bool hashhash::sendfile(int sfd, const char *filename, const char *data) {
	// We should be careful; this is the maximum number of bytes we can have.
	int datalen = strlen(data);
	
	int maxdatabytes = MAX_PACKET_LEN - 3;
	int numpkt = (int)ceil((double)datalen/maxdatabytes);
	int lastpkt = datalen % maxdatabytes;
	
	sendpkt(sfd, OPC_HRZ, filename, numpkt, -1);
	
	for(int i = 0; i < numpkt; ++i) {
		int databytes = maxdatabytes;
		if(lastpkt != 0 && i == numpkt - 1) {
			databytes = lastpkt;
		}
		if(!sendpkt(sfd, OPC_STF, (data + i*databytes), -1, databytes)) {
			return false;
		}
	}
	
	return true;
}

// Bails out of the program, printing an error based on the given context and errno.
// Accepts: the context of the problem
void hashhash::handle_error(const char *desc)
{
	int errcode = errno;
	perror(desc);
	exit(errcode);
}

/*int main() {
	// hashhash::sendpkt(0, hashhash::OPC_HRZ, "abcde", 39744, -1);
	// hashhash::sendpkt(0, hashhash::OPC_PLZ, "filename", -1, -1);
	// hashhash::sendpkt(0, hashhash::OPC_STF, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", -1, 509);
	
	const char *data = "this is my data";
	hashhash::sendfile(0, "filename", data);
}*/
