#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

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
// Accepts: file descriptor, OR of acceptable opcodes, caller-owned buffer if that opcode provides data, bool to set true if this is a HRZ, payload length (stf only), whether or not to enable non-blocking on the file descriptor
// Returns: whether the expected opcode was received, or false if not waiting and no SUP packet was available to be read
bool hashhash::recvpkt(int sfd, uint16_t opcsel, char **buf, bool *ishrz, uint16_t *stflen, bool nowait)
{
	uint16_t size;
	
	if(nowait) {
		fcntl(sfd, F_SETFL, O_NONBLOCK);
	}
	
	//TODO: handle case when recv returns 0?
	if(recv(sfd, &size, sizeof size, MSG_PEEK) < 0) {
		if(opcsel == OPC_SUP) {
			return false;
		} else {
			handle_error("recv()");
		}
	}

	uint8_t packet[size+3];
	if(read(sfd, packet, size+3) < 0)
		handle_error("read()");

	uint8_t opcode = packet[2]; // actual opcode
	if(!(opcode&opcsel))
		return false; // not the opcode you're looking for
	switch(opcode) {
		case OPC_HRZ:
			*ishrz = 1;
		case OPC_PLZ:
			*buf = (char *)malloc(size+1);
			memcpy(*buf, packet+3, size);
			(*buf)[size] = '\0';
			return true;

		case OPC_STF:
			*stflen = size;
			*buf = (char *)malloc(size);
			memcpy(*buf, packet+3, size);
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

// Reads a value from the given network socket.
// Accepts: file descriptor, caller-owned buffer, spot for the (newly) allocated buffer's length
// Returns: whether a file was received reasonably
bool hashhash::recvfile(int sfd, char **data, size_t *dlen) {
	size_t cap = MAX_PACKET_LEN-3+1;
	*data = (char *)malloc(cap);
	*dlen = 0;

	uint16_t llen;

	do {
		if(cap-*dlen <= MAX_PACKET_LEN-3) {
			// The buffer won't fit the next packet!
			char *buf = (char*)malloc(cap*2);
			memcpy(buf, *data, cap);
			free(*data);
			*data = buf;
			cap *= 2;
		}

		char *line;
		if(!recvpkt(sfd, OPC_STF, &line, NULL, &llen, false))
			return false; // bad shit happened
		memcpy(*data+*dlen, line, llen);
		free(line);
		*dlen += llen;
	}
	while(llen);

	(*data)[*dlen] = '\0';
	
	return true;
}

// Builds a packet in the #hashtag protocol fashion and sends it through a socket.
// Accepts: file descriptor, opcode for packet, string data (in case packet needs it), number of STF packets to come (for hrz packets only), amount of data to read from buffer (for stf packets only)
// Returns: whether or not the packet was successfully sent
bool hashhash::sendpkt(int sfd, uint8_t opcode, const char *data, int stfbytes) {
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
		case OPC_HRZ:
		case OPC_STF:
			if(opcode == OPC_STF)
				datalen = stfbytes;
			else
				datalen = strlen(data);

			pktsize = (3 + datalen) * sizeof(uint8_t);
			
			pkt = (uint8_t *)malloc(pktsize);
			memcpy((void *)(pkt + 3), data, datalen);
			break;
	}
	
	// Encode the packet size minus three to account for the bytes that are always there
	*(uint16_t *)pkt = (pktsize - 3);
	pkt[2] = opcode;
	
	send(sfd, (void*)pkt, pktsize, 0);
	
	free(pkt);
	
	return true;
}

// Sends a key/value pair out on the specified net socket.
// Accepts: file descriptor, key, value, length of value (needed because it might be binary)
// Returns: whether it was done sanely
bool hashhash::sendfile(int sfd, const char *filename, const char *data, size_t dlen) {
	// We should be careful; this is the maximum number of bytes we can have.
	int maxdatabytes = MAX_PACKET_LEN - 3;
	int numpkt = (int)ceil((double)dlen/maxdatabytes);
	int lastpkt = dlen % maxdatabytes;
	
	sendpkt(sfd, OPC_HRZ, filename, -1);
	
	for(int i = 0; i < numpkt; ++i) {
		int databytes = maxdatabytes;
		if(lastpkt != 0 && i == numpkt - 1) {
			databytes = lastpkt;
		}
		if(!sendpkt(sfd, OPC_STF, (data + i*maxdatabytes), databytes)) {
			return false;
		}
	}

	if(!sendpkt(sfd, OPC_STF, NULL, 0))
		return false;
	
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

// Accepts two unsigned long numbers and returns the smaller one
// Accepts: two unsigned long numbers
// Returns: the smaller one
unsigned long hashhash::min(unsigned long a, unsigned long b) {
	if(a <= b)
		return a;
	return b;
}

// Reads one line of input from standard input into the provided buffer.  Each time the buffer would overflow, it is reallocated at double its previous size.
// Accepts: the target buffer, its length in bytes
// Returns: whether we got EOF
bool hashhash::readin(char **bufptr, size_t *bufcap)
{
	char *buf = *bufptr;
	bool fits;
	size_t index = 0;

	while(1) {
		fits = 0;
		for(; index < *bufcap; ++index) {
			if((buf[index] = getc(stdin)) == EOF) {
				return false;
			}
			if(buf[index] == '\n')
				buf[index] = '\0';

			if(!buf[index]) {
				fits = 1;
				break;
			}
		}
		if(fits) break;

		buf = (char*)malloc(*bufcap*2);
		memcpy(buf, *bufptr, *bufcap);
		free(*bufptr);
		*bufptr = buf;
		*bufcap = *bufcap*2;
	}
	
	return true;
}

// Tests whether a string is entirely composed of a particular character.
// Accepts: the string and the character
bool hashhash::homog(const char *str, char chr)
{
	size_t len = strlen(str);
	size_t ind;
	for(ind = 0; ind < len; ++ind)
		if(str[ind] != chr)
			return 0;
	return 1;
}
