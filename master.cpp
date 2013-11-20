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

	//if(!sendfile(this_client_in_particular, "fname", "filecontentswhicharentlongenoughtomeritsplittingthemintotwoseparatetransmissions"))
	if(!sendfile(this_client_in_particular, "fname", "As in Protestant Europe, by contrast, where sects divided endlessly into smaller competing sects and no church dominated any other, all is different in the fragmented world of IBM.  That realm is now a chaos of conflicting norms and standards that not even IBM can hope to control.  You can buy a computer that works like an IBM machine but contains nothing made or sold by IBM itself.  Renegades from IBM constantly set up rival firms and establish standards of their own.  When IBM recently abandoned some of its original standards and decreed new ones, many of its rivals declared a puritan allegiance to IBM's original faith, and denounced the company as a divisive innovator.  Still, the IBM world is united by its distrust of icons and imagery.  IBM's screens are designed for language, not pictures.  Graven images may be tolerated by the luxurious cults, but the true IBM faith relies on the austerity of the word.  -- Edward Mendelson, \"The New Republic\", February 22, 1988"))

		handle_error("sendfile() 0");
}
