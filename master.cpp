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
	pthread_mutex_t *waiting_lock;
	pthread_cond_t *waiting_notify;
	queue<int> *waiting_clients;
	int supfd;
	int ctlfd;
};

static pthread_mutex_t *slaves_lock = NULL;
static vector<slavinfo *> *slaves_info = NULL;

static void *registration(void *);

bool proceed() { // TODO remove this crap once the nonleakiness has been formally proven
	pthread_mutex_lock(slaves_lock);
	bool res = !slaves_info->size();
	pthread_mutex_unlock(slaves_lock);
	return res;
}

int main() {
	slaves_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(slaves_lock, NULL);
	slaves_info = new vector<slavinfo *>();

	pthread_t regthr;
	memset(&regthr, 0, sizeof regthr);
	pthread_create(&regthr, NULL, &registration, NULL);

	while(proceed()); // keep threads alive

	pthread_mutex_lock(slaves_lock);
	while(slaves_info->size()) {
		struct slavinfo *each = slaves_info->back();
		slaves_info->pop_back();
		pthread_mutex_destroy(each->waiting_lock);
		free(each->waiting_lock);
		pthread_cond_destroy(each->waiting_notify);
		free(each->waiting_notify);
		delete each->waiting_clients;
		free(each);
	}
	delete slaves_info;
	pthread_mutex_unlock(slaves_lock);

	pthread_mutex_destroy(slaves_lock);
	free(slaves_lock);
}

void *registration(void *ignored) {
	int single_source_of_slaves = tcpskt(PORT_MASTER_REGISTER, 1);
	while(true) {
		struct sockaddr_in location;
		socklen_t loclen = sizeof location;
		int heartbeat = accept(single_source_of_slaves, (struct sockaddr *)&location, &loclen);
		if(!recvpkt(heartbeat, OPC_HEY, NULL, NULL, NULL, false)) {
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
		rec->waiting_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
		pthread_mutex_init(rec->waiting_lock, NULL);
		rec->waiting_notify = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
		pthread_cond_init(rec->waiting_notify, NULL);
		rec->waiting_clients = new queue<int>();
		rec->supfd = heartbeat;
		rec->ctlfd = control;

		pthread_mutex_lock(slaves_lock);
		slaves_info->push_back(rec);
		printf("Registered a slave!\n");
		pthread_mutex_unlock(slaves_lock);
	}

	return NULL;
}

void *keepalive(void *ignored) {
	int threadsize = 0, oldthreadsize = 0;
	vector<int> slavefds;
	// slaves_lock is a pthread_mutex_t* that exists by slaves_info
	
	for(slavinfo *slave : *slaves_info) {
		int slavefd = slave->supfd;
		slavefds.push_back(slavefd);
	}
	
	while(true) {
		pthread_mutex_lock(slaves_lock);
		threadsize = slaves_info->size();
		pthread_mutex_unlock(slaves_lock);
		
		if(threadsize != oldthreadsize) {
			pthread_mutex_lock(slaves_lock);
			for(int i = oldthreadsize; i < threadsize; ++i) {
				slavinfo *slave = (*slaves_info)[i];
				slavefds.push_back(slave->supfd);
			}
			pthread_mutex_unlock(slaves_lock);
		}
		
		for(vector<int>::size_type i = 0; i < slavefds.size(); ++i) {
			int slavefd = slavefds[i];
			char *pkt = NULL;
			if(!recvpkt(slavefd, OPC_SUP, &pkt, NULL, NULL, true)) {
				fprintf(stderr, "Slave %lu is dead!\n", i);
			}
		}
		
		usleep(2 * SLAVE_KEEPALIVE_TIME);
	}
	
	return NULL;
}
