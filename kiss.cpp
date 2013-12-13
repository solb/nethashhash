#include "common.h"

using namespace hashhash;

size_t readfile(const char *path, char **buf);
bool writefile(const char *path, const char *data, unsigned int dlen);

int main(int argc, char **argv) {
	if(argc == 2) { // server
		int incoming = tcpskt(argc-2 ? atoi(argv[2]) : 1024, 1);
		if((incoming = accept(incoming, NULL, 0)) == -1)
			handle_error("incoming from master accept()");

		char *payld = NULL;
		bool inbound;
		if(!recvpkt(incoming, OPC_HRZ, &payld, &inbound, 0, false))
			handle_error("recvpkt()");

		printf("Text 'ignored' came through as: '%s'\n", payld);

		char *data = NULL;
		size_t filesize;
		if(!recvfile(incoming, &data, &filesize))
			handle_error("recvfile()");

		if(!writefile(argv[1], data, filesize))
			handle_error("recvpkt()");

		printf("File received and stored!\n");
	}
	else if(argc > 2) { //client
		int master_fd;
		if(!rslvconn(&master_fd, argv[1], 1024)) {
			printf("FATAL: Couldn't resolve or connect to host: %s\n", argv[1]);
			return RETVAL_CONN_FAILED;
		}

		char *payld = NULL;
		size_t len = readfile(argv[2], &payld);
		if(!sendfile(master_fd, "ignored", payld, len))
			handle_error("sendfile()");

		printf("File read and sent!\n");
	}
	else
		printf("Someone can't follow directions, or the lack thereof");
}

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
