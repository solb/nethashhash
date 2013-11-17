#include "common.h"

int hashhash::accepta(int sfd, struct sockaddr_in *rmt_saddr)
{
	socklen_t rmt_saddr_len = sizeof(struct sockaddr_in);
	int realfd;
	if((realfd = accept(sfd, (struct sockaddr *)rmt_saddr, &rmt_saddr_len)) < 0)
		handle_error("accept()");

	return realfd;
}

// Listens for a datagram arriving on the specified socket.
// Accepts: socket file descriptor
// Returns: caller-owned buffer
void *hashhash::recvpkt(int sfd)
{
	return recvpkta(sfd, NULL);
}

// Listens on socket for incoming datagram and reveals its source address.
// Accepts: file descriptor, pointer to socket address structure or NULL
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
