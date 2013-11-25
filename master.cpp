#include "common.h"
#include <cstring>
#include <pthread.h>
#include <queue>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <utility>

using namespace hashhash;
using std::min;
using std::queue;
using std::unordered_map;
using std::unordered_set;
using std::vector;
using std::pair;

typedef vector<int>::size_type slave_idx;

struct slavinfo {
	bool alive;
	pthread_mutex_t *waiting_lock;
	pthread_cond_t *waiting_notify;
	queue<int> *waiting_clients;
	int supfd;
	int ctlfd;
	long long howfull;
};

static pthread_mutex_t *slaves_lock = NULL;
static vector<slavinfo *> *slaves_info = NULL;
static pthread_mutex_t *files_lock = NULL;
static unordered_map<const char *, unordered_set<slave_idx> *> *files = NULL;

static void *each_client(void *);
static void *bury_slave(void *);
static void *registration(void *);
static void *keepalive(void *);
slave_idx bestslave(unordered_map<slave_idx, slavinfo *>);

int main() {
	slaves_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(slaves_lock, NULL);
	slaves_info = new vector<slavinfo *>();
	files_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(files_lock, NULL);
	files = new unordered_map<const char *, unordered_set<slave_idx> *>();

	pthread_t regthr;
	memset(&regthr, 0, sizeof regthr);
	pthread_create(&regthr, NULL, &registration, NULL);
	pthread_t supthr;
	memset(&supthr, 0, sizeof supthr);
	pthread_create(&supthr, NULL, &keepalive, NULL);

	int single_source_of_clients = tcpskt(PORT_MASTER_CLIENTS, MAX_MASTER_BACKLOG);
	queue<pthread_t *> connected_clients;
	while(true) {
		int *particular_client = (int *)malloc(sizeof(int));
		*particular_client = accept(single_source_of_clients, NULL, NULL);
		if(*particular_client >= 0) {
			pthread_t *particular_thread = (pthread_t *)malloc(sizeof(pthread_t));
			memset(particular_thread, 0, sizeof(pthread_t));
			pthread_create(particular_thread, NULL, &each_client, particular_client);
			connected_clients.push(particular_thread);
		}
	}

	pthread_cancel(regthr);
	pthread_cancel(supthr);
	pthread_join(regthr, NULL);
	pthread_join(supthr, NULL);

	while(connected_clients.size()) {
		pthread_cancel(*connected_clients.front());
		pthread_join(*connected_clients.front(), NULL);
		free(connected_clients.front());
		connected_clients.pop();
	}

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
	for(auto it = files->begin(); it != files->end(); ++it) {
		free((char *)it->first);
		delete it->second;
		it->second = NULL;
	}
	delete files;
	pthread_mutex_unlock(files_lock);
	pthread_mutex_destroy(files_lock);
	free(files_lock);
	files_lock = NULL;
}

// Selects the most ideal slave from the slave vector
// Uses a map to check if a slave has been selected already; a null map implies you are only selecting the one true best slave
// Assumes that you ALREADY hold the slaves_lock
// Accepts: a pointer to a map of slaves already chosen, or null
// Returns: the one true best slave not already in the map
slave_idx bestslave(unordered_map<slave_idx, slavinfo *> *slavemap) {
	// Select the most ideal slave
	// Current metric is just fullness, but perhaps we can incorporate request queue size later
	slave_idx bestslaveidx = 0;
	long long bestfullness = -1;
	for(slave_idx s = 0; s < slaves_info->size(); ++s) {
		slavinfo *slave = (*slaves_info)[s];
		
		bool inBestSlaves = false;
		
		if(slavemap != NULL) {
			inBestSlaves = (slavemap->find(s) != slavemap->end());
		}
		
		if(!inBestSlaves && slave->alive && (slave->howfull < bestfullness || bestfullness == -1)) {
			bestslaveidx = s;
			bestfullness = slave->howfull;
		}
	}
	
	return bestslaveidx;
}

void *each_client(void *f) {
	int fd = *(int *)f;
	free(f);

	while(true) {
		char *payld = NULL;
		char *junk = NULL;
		uint16_t hrzcnt = 0; // sentinel for not a HRZ
		if(recvpkt(fd, OPC_PLZ|OPC_HRZ, &payld, &hrzcnt, 0, false)) {
			printf("YAY I GOT A %s LABELED %s\n", hrzcnt ? "HRZ" : "PLZ", payld);
			if(hrzcnt) {
				// We got a HRZ packet
				unsigned int jsize;
				recvfile(fd, hrzcnt, &junk, &jsize);
				printf("\tAND IT WAS CARRYING ALL THIS: %s\n", junk);
				
				// Store the file with some slaves
				
				unordered_map<slave_idx, slavinfo *> bestslaves;
				
				// Find the MIN_STOR_REDUN most ideal slaves
				pthread_mutex_lock(slaves_lock);
				auto numslaves = slaves_info->size();
				unsigned int numtoget = min(numslaves, MIN_STOR_REDUN);
				printf("Selecting %u best slaves from %lu total slaves\n", numtoget, numslaves);
				for(unsigned int i = 0; i < numtoget; ++i) {
					printf("on iter %u < %u\n", i, numtoget);
					slave_idx bestslaveidx = bestslave(&bestslaves);
					slavinfo *bestslave = (*slaves_info)[bestslaveidx];
					if(bestslave == NULL) {
						fprintf(stderr, "Something went very wrong; I selected a null best slave from index %lu!\n", bestslaveidx);
					}
					
					bestslaves[bestslaveidx] = bestslave;
					printf("Selecting slave %lu as a best slave\n", bestslaveidx);
				}
				pthread_mutex_unlock(slaves_lock);
				
				printf("bestslaves has %lu slaves\n", bestslaves.size());
				
				for(pair<slave_idx, slavinfo *> entry: bestslaves) {
					slave_idx slaveidx = entry.first;
					slavinfo *slave = entry.second;
					// Lock on the slave's queue
					pthread_mutex_lock(slave->waiting_lock);
					// Add ourselves to the slave's queue
					slave->waiting_clients->push(fd);
					// Wait while we're not first in the slave's queue
					while(slave->waiting_clients->front() != fd) {
						pthread_cond_wait(slave->waiting_notify, slave->waiting_lock);
					}
					
					pthread_mutex_unlock(slave->waiting_lock);
					
					// Send the file to the slave; this is the moment we've all been waiting for!
					printf("Sending file to slave %lu\n", slaveidx);
					sendfile(slave->ctlfd, payld, junk);
					
					// Lock and pop ourselves off the queue
					pthread_mutex_lock(slave->waiting_lock);
					slave->waiting_clients->pop();
					
					// Unlock just in case broadcast doesn't
					pthread_mutex_unlock(slave->waiting_lock);
					
					// Notify all others waiting on the slave
					pthread_cond_broadcast(slave->waiting_notify);
					
					// Lock and update the file map
					pthread_mutex_lock(files_lock);
					
					if(files->find(payld) == files->end()) {
						// The file doesn't exist in the table yet
						unordered_set<slave_idx> *file_entry;
						file_entry = new unordered_set<slave_idx>();
						(*files)[payld] = file_entry;
					}
					(*files)[payld]->insert(slaveidx); // TODO won't this make duplicate entries?
					
					pthread_mutex_unlock(files_lock);
				}
				
				free(junk);
			} else {
				// We got a PLZ packet
				pthread_mutex_lock(files_lock);
				// TODO: check if it's there
				if(files->find(payld) == files->end())
					printf("Very, very wrong!\n");
				unordered_set<slave_idx> *containing_slaves = (*files)[payld];
				pthread_mutex_unlock(files_lock);
				
				slave_idx bestslaveidx = -1;
				slave_idx bestqueuesize = 0;
				bool sentinel = true;
				for(slave_idx slaveidx : *containing_slaves) {
					pthread_mutex_lock(slaves_lock);
					slavinfo *slave = (*slaves_info)[slaveidx];
					if(slave->alive) {
						pthread_mutex_lock(slave->waiting_lock);
						slave_idx queuesize = slave->waiting_clients->size();
						pthread_mutex_unlock(slave->waiting_lock);
						if(queuesize < bestqueuesize || sentinel) {
							sentinel = false;
							bestslaveidx = slaveidx;
							bestqueuesize = queuesize;
						}
					}
					pthread_mutex_unlock(slaves_lock);
				}
				
				if(bestslaveidx == (slave_idx)-1) {
					// TODO: No slave is alive
					printf("No slave is alive from which we may receive file '%s'!\n", payld);
					continue;
				}
				
				char *filename;
				char *filedata;
				unsigned int dlen;
				
				pthread_mutex_lock(slaves_lock);
				
				slavinfo *bestslave = (*slaves_info)[bestslaveidx];
				// Lock on the slave's queue
				pthread_mutex_lock(bestslave->waiting_lock);
				// Add ourselves to the slave's queue
				bestslave->waiting_clients->push(fd);
				// Wait while we're not first in the slave's queue
				while(bestslave->waiting_clients->front() != fd) {
					pthread_cond_wait(bestslave->waiting_notify, bestslave->waiting_lock);
				}
				
				pthread_mutex_unlock(bestslave->waiting_lock);
				
				// Send the file to the slave; this is the moment we've all been waiting for!
				// sendfile(bestslave->ctlfd, payld, junk);
				sendpkt(bestslave->ctlfd, OPC_PLZ, payld, 0, 0);
				uint16_t numpkts = -1;
				recvpkt(bestslave->ctlfd, OPC_HRZ, &filename, &numpkts, NULL, false);
				printf("Receiving file '%s' from slave %lu\n", filename, bestslaveidx);
				recvfile(bestslave->ctlfd, numpkts, &filedata, &dlen);
				
				// Lock and pop ourselves off the queue
				pthread_mutex_lock(bestslave->waiting_lock);
				bestslave->waiting_clients->pop();
				
				// Unlock just in case broadcast doesn't
				pthread_mutex_unlock(bestslave->waiting_lock);
				
				// Notify all others waiting on the slave
				pthread_cond_broadcast(bestslave->waiting_notify);
				
				pthread_mutex_unlock(slaves_lock);
				
				// Now we have the file from the slave
				
				sendfile(fd, filename, filedata);
				
				
				free(payld);
			}
		}
	}

	return NULL;
}

void *bury_slave(void *i) {
	int slavid = *(int *)i;
	free(i);
	pthread_detach(pthread_self());

	pthread_mutex_lock(slaves_lock);
	(*slaves_info)[slavid]->alive = false;
	pthread_mutex_unlock(slaves_lock);
	// TODO make an additional backup of all data from the failed node

	return NULL;
}

void *registration(void *ignored) {
	int single_source_of_slaves = tcpskt(PORT_MASTER_REGISTER, MAX_MASTER_BACKLOG);
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
		rec->howfull = 0;

		usleep(SLAVE_KEEPALIVE_TIME); // Give the client's heart a moment to start beating.
		pthread_mutex_lock(slaves_lock);
		slaves_info->push_back(rec);
		printf("Registered a slave!\n");
		pthread_mutex_unlock(slaves_lock);
	}

	return NULL;
}

void *keepalive(void *ignored) {
	slave_idx threadsize = 0;
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
		
		for(slave_idx i = 0; i < slavefds.size(); ++i) {
			if(slavefds[i]) {
				bool failure = true;
				while(recvpkt(slavefds[i], OPC_SUP, NULL, NULL, NULL, true)) {
					failure = false;
				}
				if(failure) {
					printf("Slave %lu is dead!\n", i);
					slavefds[i] = 0; // Let 0 be a sentinel that means, "He's dead, Jim."
					pthread_t cleaner;
					int *killslav = (int *)malloc(sizeof(int));
					*killslav = i;
					pthread_create(&cleaner, NULL, &bury_slave, killslav);
				}
				else {
					// printf("beat\n");
				}
			}
		}
		
		usleep(2 * SLAVE_KEEPALIVE_TIME);
	}
	
	return NULL;
}
