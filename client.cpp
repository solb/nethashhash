#include "common.h"
#include <cstring>

using namespace hashhash;

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("USAGE: %s <hostname>\n", argv[0]);
		return RETVAL_INVALID_ARG;
	}

	int srv_fd;
	if(!rslvconn(&srv_fd, argv[1], PORT_MASTER_CLIENTS)) {
		printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
		return RETVAL_CONN_FAILED;
	}

	if(!sendpkt(srv_fd, OPC_FKU, NULL, 0, 0))
		handle_error("sendpkt() 0");
	if(!sendpkt(srv_fd, OPC_FKU, NULL, 0, 0))
		handle_error("sendpkt() 1");
	if(!sendpkt(srv_fd, OPC_FKU, NULL, 0, 0))
		handle_error("sendpkt() 2");

	if(!sendpkt(srv_fd, OPC_PLZ, "key", 0, 0))
		handle_error("sendpkt() 3");

	char *arv = NULL;
	uint16_t flwng = 0;
	if(!recvpkt(srv_fd, OPC_HRZ, &arv, &flwng, NULL))
		handle_error("recvpkt() 0");
	if(strcmp(arv, "key"))
		handle_error("strcmp() 0");
	if(flwng != 2)
		handle_error("operator!=() 0");
	free(arv);

	unsigned int datln;
	if(!recvpkt(srv_fd, OPC_HRZ, &arv, &flwng, NULL))
		handle_error("recvpkt() 1");
	if(strcmp(arv, "fname"))
		handle_error("strcmp() 1");
	//if(flwng != 1)
	if(flwng != 2)
		handle_error("operator!=() 1");
	free(arv);
	if(!recvfile(srv_fd, flwng, &arv, &datln))
		handle_error("recvfile() 0");
	if(datln != strlen(arv))
		handle_error("operator!=() 2");
	//if(strcmp(arv, "filecontentswhicharentlongenoughtomeritsplittingthemintotwoseparatetransmissions"))
	printf("%s\n", arv);
	if(strcmp(arv, "As in Protestant Europe, by contrast, where sects divided endlessly into smaller competing sects and no church dominated any other, all is different in the fragmented world of IBM.  That realm is now a chaos of conflicting norms and standards that not even IBM can hope to control.  You can buy a computer that works like an IBM machine but contains nothing made or sold by IBM itself.  Renegades from IBM constantly set up rival firms and establish standards of their own.  When IBM recently abandoned some of its original standards and decreed new ones, many of its rivals declared a puritan allegiance to IBM's original faith, and denounced the company as a divisive innovator.  Still, the IBM world is united by its distrust of icons and imagery.  IBM's screens are designed for language, not pictures.  Graven images may be tolerated by the luxurious cults, but the true IBM faith relies on the austerity of the word.  -- Edward Mendelson, \"The New Republic\", February 22, 1988"))
		handle_error("strcmp() 2");
}
