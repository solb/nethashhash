/*
 * Copyright (C) 2013 Sol Boucher and Lane Lawley
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with it.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "common.h"
#include <cstring>

using namespace hashhash;

// "Sex appeal", as Sol would say
static const char *const SHL_PS1 = "#hashtable> ";

// Interactive commands (must not share a first character)
static const char *const CMD_PUT = "put";
static const char *const CMD_SND = "send";
static const char *const CMD_GET = "get";
static const char *const CMD_GFO = "quit";
static const char *const CMD_HLP = "?";

static size_t readfile(const char *, char **);
static bool writefile(const char *, const char *, unsigned int);

static void usage(const char *, const char *, const char *);
static void hand();

int main(int argc, char **argv) {
	if(argc < 2) {
		printf("USAGE: %s <hostname>\n", argv[0]);
		return RETVAL_INVALID_ARG;
	}
	
	int srv_fd = -1;
	if(!rslvconn(&srv_fd, argv[1], PORT_MASTER_CLIENTS)) {
		printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
		return RETVAL_CONN_FAILED;
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
		
		if(strncmp(cmd, CMD_PUT, len) == 0) {
			char *key = strtok(NULL, " ");
			char *val = strtok(NULL, " ");
			
			if(!key) {
				usage(CMD_PUT, "key", "val");
				continue;
			} else if(!val) {
				usage(CMD_PUT, "val", NULL);
				continue;
			}
				
			sendfile(srv_fd, key, val, strlen(val));
		} else if(strncmp(cmd, CMD_SND, len) == 0) {
			char *key = strtok(NULL, " ");
			char *fileval = strtok(NULL, " ");
			
			if(!key) {
				usage(CMD_PUT, "key", "fileval");
				continue;
			} else if(!fileval) {
				usage(CMD_PUT, "fileval", NULL);
				continue;
			}
			
			char *val = NULL;
			
			size_t valsize = readfile(fileval, &val);
			
			if(valsize == 0) {
				printf("Couldn't read file \n");
				continue;
			}
				
			sendfile(srv_fd, key, val, valsize);
			
			free(val);
		} else if(strncmp(cmd, CMD_GET, len) == 0) {
			char *key = strtok(NULL, " ");
			char *filedest = strtok(NULL, " ");
			
			if(!key) {
				usage(CMD_GET, "key", NULL);
				continue;
			}
			
			sendpkt(srv_fd, OPC_PLZ, key, 0);
			
			char *rcvfiledata;
			char *rcvfilename;
			size_t dlen;
			
			bool incoming = false;
			recvpkt(srv_fd, OPC_HRZ|OPC_FKU, &rcvfilename, &incoming, NULL, false);
			
			if(!incoming) {
				printf("The master couldn't give us the value! Oh well.\n");
				continue;
			}
			
			printf("Receiving value of '%s'\n", rcvfilename);
			recvfile(srv_fd, &rcvfiledata, &dlen);
			printf("Got %lu bytes\n", dlen);
			
			if(filedest) {
				if(!writefile(filedest, rcvfiledata, dlen)) {
					printf("Failed to write data to local file\n");
				}
			} else {
				printf("The master says that [%s] = [%s]\n", rcvfilename, rcvfiledata);
			}
		}
		else if(strncmp(cmd, CMD_HLP, len) == 0) { 
			printf("Commands may be abbreviated.  Commands are:\n\n");
			printf("%s\t\tsend text value as key\n", CMD_PUT);
			printf("%s\t\tsend text file as key\n", CMD_SND);
			printf("%s\t\treceive value of key (optional path to receive to file)\n", CMD_GET);
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

// Read from a text file into a dynamically allocated char array
// Accepts: the path of the text file to read from
// Returns: the size of the file
size_t readfile(const char *path, char **buf) {
	size_t fsize = 0;
	
	FILE *file = fopen(path, "rb");
	if(file == NULL) {
		return 0;
	}
	
	fseek(file, 0L, SEEK_END);
	fsize = ftell(file);
	fseek(file, 0L, SEEK_SET);
	
	char *data = (char *)malloc((fsize + 1) * sizeof(char));
	
	fread(data, fsize, 1, file);
	
	fclose(file);
	
	data[fsize] = '\0'; // null terminate
	
	printf("Read in %ld bytes to send\n", fsize);
	
	(*buf) = data;
	
	return fsize;
}

// Write a string to a text file
// Accepts: the path of the text file to write to and a string to write
// Returns: whether or not the write succeeded
bool writefile(const char *path, const char *data, unsigned int dlen) {
	FILE *file = fopen(path, "wb");
	if(file == NULL) {
		return false;
	}
	
	for(unsigned int i = 0; i < dlen; ++i) {
		fputc(data[i], file);
	}
	printf("Wrote %d bytes\n", dlen);
	
	fclose(file);
	
	return true;
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
