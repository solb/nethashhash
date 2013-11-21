#include "common.h"
#include <cstring>
#include <pthread.h>
#include <queue>

using namespace hashhash;
using std::queue;
using std::vector;

struct slavinfo {
	bool alive;
	queue<int> *waiting_clients;
	int supfd;
	int ctlfd;
};

static vector<slavinfo *> *slaves_info = NULL;

static void *registration(void *);

int main() {
	slaves_info = new vector<slavinfo *>();

	pthread_t regthr;
	memset(&regthr, 0, sizeof regthr);
	pthread_create(&regthr, NULL, &registration, NULL);

	while(true); // keep threads alive

	delete slaves_info;
}

void *registration(void *ignored) {
	int single_source_of_slaves = tcpskt(PORT_MASTER_REGISTER, 1);
	while(true) {
		struct sockaddr location;
		socklen_t loclen = sizeof location;
		int heartbeat = accept(single_source_of_slaves, &location, &loclen);
		if(!recvpkt(heartbeat, OPC_HEY, NULL, NULL, NULL)) {
			sendpkt(heartbeat, OPC_FKU, NULL, 0, 0);
			continue;
		}
		int control = socket(AF_INET, SOCK_STREAM, 0);
		if(connect(heartbeat, &location, loclen)) {
			sendpkt(heartbeat, OPC_FKU, NULL, 0, 0);
			continue;
		}
		struct slavinfo *rec = (struct slavinfo *)malloc(sizeof(struct slavinfo));
		*rec = {true, new queue<int>(), heartbeat, control};
		rec->alive = true;
		rec->waiting_clients = new queue<int>();
		rec->supfd = heartbeat;
		rec->ctlfd = control;
		// TODO lock here
		slaves_info->push_back(rec);
		printf("Registered a slave!");
	}

	return NULL;
}
