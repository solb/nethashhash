#include "common.h"
#include <cstring>

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

// Listens for a datagram arriving on the specified socket.
// Accepts: socket file descriptor
// Returns: caller-owned buffer
void *hashhash::recvpkt(int sfd)
{
	return recvpkta(sfd, NULL);
}

// Listens on socket for incoming datagram and reveals its source address.
// Accepts: file descriptor, pointer to socket address structure (or NULL)
// Returns: caller-owned buffer
void *hashhash::recvpkta(int sfd, struct sockaddr_in *rmt_saddr)
{
	return recvpktal(sfd, NULL, rmt_saddr);
}

// Listens on socket for incoming datagram and reveals its length and source address.
// Accepts: file descriptor, pointer to length or NULL, pointer to address or NULL
// Returns: caller-owned buffer
void *hashhash::recvpktal(int sfd, size_t *len_out, struct sockaddr_in *rmt_saddr)
{
	ssize_t msg_len;
	socklen_t rsaddr_len = sizeof(struct sockaddr_in);

	// Listen for an incoming message and note its length:
	void *msg = malloc(MAX_PACKET_LEN);
	if((msg_len = recvfrom(sfd, msg, MAX_PACKET_LEN, MSG_WAITALL, (struct sockaddr *)rmt_saddr, rmt_saddr ? &rsaddr_len : NULL)) <= 0)
		handle_error("recvfrom()");

	if(len_out)
		*len_out = msg_len;
	return msg;
}

// Bails out of the program, printing an error based on the given context and errno.
// Accepts: the context of the problem
void hashhash::handle_error(const char *desc)
{
	int errcode = errno;
	perror(desc);
	exit(errcode);
}
