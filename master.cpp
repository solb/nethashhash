#include "common.h"
#include <cstring>
#include <pthread.h>
#include <queue>
#include <unistd.h>

using namespace hashhash;
using std::queue;
using std::vector;

struct slavinfo {
	bool alive;
	queue<int> *waiting_clients;
	int supfd;
	int ctlfd;
};

static pthread_mutex_t *slaves_lock = NULL;
static vector<slavinfo *> *slaves_info = NULL;

static void *registration(void *);

int main() {
	slaves_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(slaves_lock, NULL);
	slaves_info = new vector<slavinfo *>();

	pthread_t regthr;
	memset(&regthr, 0, sizeof regthr);
	pthread_create(&regthr, NULL, &registration, NULL);

	while(true); // keep threads alive

	pthread_mutex_destroy(slaves_lock);
	free(slaves_lock);
	delete slaves_info;
}

void *registration(void *ignored) {
	int single_source_of_slaves = tcpskt(PORT_MASTER_REGISTER, 1);
	while(true) {
		struct sockaddr_in location;
		socklen_t loclen = sizeof location;
		int heartbeat = accept(single_source_of_slaves, (struct sockaddr *)&location, &loclen);
		if(!recvpkt(heartbeat, OPC_HEY, NULL, NULL, NULL)) {
			sendpkt(heartbeat, OPC_FKU, NULL, 0, 0);
			continue;
		}
		int control = socket(AF_INET, SOCK_STREAM, 0);
		location.sin_port = htons(PORT_SLAVE_MAIN);
		usleep(10000); // TODO fix this crap
		if(connect(control, (struct sockaddr *)&location, loclen)) {
			sendpkt(heartbeat, OPC_FKU, NULL, 0, 0);
			continue;
		}
		struct slavinfo *rec = (struct slavinfo *)malloc(sizeof(struct slavinfo));
		rec->alive = true;
		rec->waiting_clients = new queue<int>();
		rec->supfd = heartbeat;
		rec->ctlfd = control;
		pthread_mutex_lock(slaves_lock);
		slaves_info->push_back(rec);
		pthread_mutex_unlock(slaves_lock);
		printf("Registered a slave!\n");
	}

	return NULL;
}
