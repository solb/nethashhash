#include "common.h"
#include <cstring>

#define SOL_DONE false

using namespace hashhash;

// "Sex appeal", as Sol would say
static const char *const SHL_PS1 = "tftp> ";

// Interactive commands (must not share a first character)
static const char *const CMD_PUT = "put";
static const char *const CMD_GET = "get";
static const char *const CMD_GFO = "quit";
static const char *const CMD_HLP = "?";

static bool readin(char **, size_t *);
static bool homog(const char *, char);
static void usage(const char *, const char *, const char *);
static void hand();

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("USAGE: %s <hostname>\n", argv[0]);
		return RETVAL_INVALID_ARG;
	}
	
	int srv_fd = -1;
	if(SOL_DONE) {
		if(!rslvconn(&srv_fd, argv[1], PORT_MASTER_CLIENTS)) {
			printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
			return RETVAL_CONN_FAILED;
		}
	}
	
	// Allocate (small) space to store user input:
	char *buf = (char*)malloc(1);
	size_t cap = 1;
	char *cmd; // First word of buf
	size_t len; // Length of cmd

	// Main input loop, which normally only breaks upon a GFO:
	do { 
		// Keep prompting until the user brings us back something good:
		do { 
			printf("%s", SHL_PS1);
			if(!readin(&buf, &cap)) {
				free(buf);
				hand();
			}
		}
		while(homog(buf, ' '));
		
		// Cleave off the command (first word):
		cmd = strtok(buf, " ");
		len = strlen(cmd);
		
		if(strncmp(cmd, CMD_GET, len) == 0 || strncmp(cmd, CMD_PUT, len) == 0) {
			bool putting = strncmp(cmd, CMD_PUT, 1) == 0;
			
			// Make sure we were given a filename argument:
			char *filename = strtok(NULL, " ");
			char *filedata = strtok(NULL, " ");
			if(putting) {
				if(!filename) {
					usage(CMD_PUT, "filename", "filedata");
					continue;
				} else if(!filedata) {
					usage(CMD_PUT, "filedata", NULL);
					continue;
				}
			} else {
				if(!filename) {
					usage(CMD_GET, "filename", NULL);
					continue;
				}
			}

			// Since basename() might modify pathname, copy it:
			//char filename[strlen(pathname)+1];
			//memcpy(filename, pathname, sizeof filename);
			
			if(putting) {
				printf("Given [%s]: [%s]\n", filename, filedata);
			}
			else {
				printf("Asked for [%s]\n", filename);
			}
		}
		else if(strncmp(cmd, CMD_HLP, len) == 0) { 
			printf("Commands may be abbreviated.  Commands are:\n\n");
			printf("%s\t\tsend file\n", CMD_PUT);
			printf("%s\t\treceive file\n", CMD_GET);
			printf("%s\t\texit #hashtable\n", CMD_GFO);
			printf("%s\t\tprint help information\n", CMD_HLP);
		}
		else if(strncmp(cmd, CMD_GFO, len) != 0) { 
			fprintf(stderr, "%s: unknown directive\n", cmd);
			fprintf(stderr, "Try ? for help.\n");
		}
	}
	while(strncmp(cmd, CMD_GFO, len) != 0);
}

// Reads one line of input from standard input into the provided buffer.  Each time the buffer would overflow, it is reallocated at double its previous size.
// Accepts: the target buffer, its length in bytes
// Returns: whether we got EOF
bool readin(char **bufptr, size_t *bufcap)
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
bool homog(const char *str, char chr)
{
	size_t len = strlen(str);
	size_t ind;
	for(ind = 0; ind < len; ++ind)
		if(str[ind] != chr)
			return 0;
	return 1;
}

// Prints to standard error the usage string describing a command expecting one required argument and up to one optional argument.
// Accepts: the command, its required argument, and its second required argument (which can be NULL)
void usage(const char *cmd, const char *reqd, const char *reqd2) {
	if(reqd2) {
		fprintf(stderr, "USAGE: %s %s %s\n", cmd, reqd, reqd2);
		fprintf(stderr, "Both required arguments %s and %s not provided.\n", reqd, reqd2);
	} else {
		fprintf(stderr, "USAGE: %s %s\n", cmd, reqd);
		fprintf(stderr, "Required argument %s not provided.\n", reqd);
	}
}

void hand() {
	printf("\nhave a nice day ;)\n");
	exit(0);
}
