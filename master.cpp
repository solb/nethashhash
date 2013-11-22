#include "common.h"
#include <cstring>
#include <pthread.h>
#include <queue>
#include <unistd.h>
#include <unordered_map>
#include <vector>

using namespace hashhash;
using std::queue;
using std::unordered_map;
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
static pthread_mutex_t *files_lock = NULL;
static unordered_map<const char *, vector<int> *> *files_deleg = NULL;

static void *registration(void *);
static void *keepalive(void *);

bool proceed() { // TODO remove this crap once the nonleakiness has been formally proven
	pthread_mutex_lock(slaves_lock);
	bool res = slaves_info->size() < 2;
	pthread_mutex_unlock(slaves_lock);
	return res;
}

int main() {
	slaves_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(slaves_lock, NULL);
	slaves_info = new vector<slavinfo *>();
	files_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(files_lock, NULL);
	files_deleg = new unordered_map<const char *, vector<int> *>();

	pthread_t regthr;
	memset(&regthr, 0, sizeof regthr);
	pthread_create(&regthr, NULL, &registration, NULL);
	pthread_t supthr;
	memset(&supthr, 0, sizeof supthr);
	pthread_create(&supthr, NULL, &keepalive, NULL);

	while(proceed()); // keep threads alive TODO handle incoming clients here

	pthread_cancel(regthr);
	pthread_cancel(supthr);
	pthread_join(regthr, NULL);
	pthread_join(supthr, NULL);

	pthread_mutex_lock(slaves_lock);
	while(slaves_info->size()) {
		struct slavinfo *each = slaves_info->back();
		slaves_info->pop_back();
		pthread_mutex_destroy(each->waiting_lock);
		free(each->waiting_lock);
		each->waiting_lock = NULL;
		pthread_cond_destroy(each->waiting_notify);
		free(each->waiting_notify);
		each->waiting_notify = NULL;
		delete each->waiting_clients;
		each->waiting_clients = NULL;
		free(each);
	}
	delete slaves_info;
	pthread_mutex_unlock(slaves_lock);
	pthread_mutex_destroy(slaves_lock);
	free(slaves_lock);
	slaves_lock = NULL;

	pthread_mutex_lock(files_lock);
	for(auto it = files_deleg->begin(); it != files_deleg->end(); ++it) {
		delete it->second;
		it->second = NULL;
	}
	delete files_deleg;
	pthread_mutex_unlock(files_lock);
	pthread_mutex_destroy(files_lock);
	free(files_lock);
	files_lock = NULL;
}

void *registration(void *ignored) {
	int single_source_of_slaves = tcpskt(PORT_MASTER_REGISTER, MAX_REGISTER_BACKLOG);
	while(true) {
		struct sockaddr_in location;
		socklen_t loclen = sizeof location;
		int heartbeat = accept(single_source_of_slaves, (struct sockaddr *)&location, &loclen);
		if(!recvpkt(heartbeat, OPC_HEY, NULL, NULL, NULL, false)) {
			sendpkt(heartbeat, OPC_FKU, NULL, 0, 0);
			continue;
		}
		int control = socket(AF_INET, SOCK_STREAM, 0);
		usleep(10000); // TODO fix this crap
		location.sin_port = htons(PORT_SLAVE_MAIN);
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

		usleep(MASTER_REG_GRACE_PRD); // Give the client's heart a moment to start beating.
		pthread_mutex_lock(slaves_lock);
		slaves_info->push_back(rec);
		printf("Registered a slave!\n");
		pthread_mutex_unlock(slaves_lock);
	}

	return NULL;
}

void *keepalive(void *ignored) {
	vector<int>::size_type threadsize = 0;
	vector<int> slavefds;
	// slaves_lock is a pthread_mutex_t* that exists by slaves_info
	
	while(true) {
		pthread_mutex_lock(slaves_lock);
		threadsize = slaves_info->size();
		pthread_mutex_unlock(slaves_lock);
		
		if(threadsize > slavefds.size()) {
			pthread_mutex_lock(slaves_lock);
			for(auto i = slavefds.size(); i < threadsize; ++i) {
				slavinfo *slave = (*slaves_info)[i];
				slavefds.push_back(slave->supfd);
			}
			pthread_mutex_unlock(slaves_lock);
		}
		
		for(vector<int>::size_type i = 0; i < slavefds.size(); ++i) {
			bool failure = true;
			while(recvpkt(slavefds[i], OPC_SUP, NULL, NULL, NULL, true)) {
				failure = false;
			}
			if(failure)
				printf("Slave %lu is dead!\n", i);
			else
				printf("beat\n");
		}
		
		usleep(2 * SLAVE_KEEPALIVE_TIME);
	}
	
	return NULL;
}
