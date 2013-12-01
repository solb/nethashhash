#include "common.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <pthread.h>
#include <queue>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace hashhash;
using std::copy_if;
using std::function;
using std::inserter;
using std::map;
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

struct filinfo {
	pthread_mutex_t *write_lock;
	unordered_set<slave_idx> *holders;
};

static pthread_mutex_t *slaves_lock = NULL;
static vector<struct slavinfo *> *slaves_info = NULL;
static vector<int>::size_type living_count;
static pthread_mutex_t *files_lock = NULL;
static unordered_map<const char *, struct filinfo *> *files = NULL;

static void *each_client(void *);
static void *rereplicate(void *);
static void *registration(void *);
static void *clientregistration(void *);
static void *keepalive(void *);

bool getfile(const char *, char **, unsigned int *, const int);
bool putfile(slavinfo *, const char *, const char *, const int, bool);
slave_idx bestslave(const function<bool(slave_idx)> &);

int main() {
	slaves_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(slaves_lock, NULL);
	slaves_info = new vector<slavinfo *>();
	living_count = 0;
	files_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(files_lock, NULL);
	files = new unordered_map<const char *, struct filinfo *>();

	pthread_t regthr;
	memset(&regthr, 0, sizeof regthr);
	pthread_create(&regthr, NULL, &registration, NULL);
	pthread_t supthr;
	memset(&supthr, 0, sizeof supthr);
	pthread_create(&supthr, NULL, &keepalive, NULL);
	
	queue<pthread_t *> connected_clients;
	pthread_t clientregthr;
	memset(&clientregthr, 0, sizeof clientregthr);
	pthread_create(&clientregthr, NULL, &clientregistration, &connected_clients);
	
	while(true); // CLI soon
	
	pthread_cancel(regthr);
	pthread_cancel(supthr);
	pthread_cancel(clientregthr);
	pthread_join(regthr, NULL);
	pthread_join(supthr, NULL);
	pthread_join(clientregthr, NULL);

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
	for(auto it = files->begin(); it != files->end(); ++it) { // TODO FIX
		free(it->second->write_lock);
		delete it->second->holders;
		free(it->second);
		free((char *)it->first);
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
// Accepts: a lambda expression that returns whether a particular slave has already been chosen
// Returns: the one true best slave not already in the map
slave_idx bestslave(const function<bool(slave_idx)> &redundant) {
	// Select the most ideal slave
	// Current metric is just fullness, but perhaps we can incorporate request queue size later
	slave_idx bestslaveidx = 0;
	long long bestfullness = -1;
	for(slave_idx s = 0; s < slaves_info->size(); ++s) {
		slavinfo *slave = (*slaves_info)[s];
		
		if(!redundant(s) && slave->alive && (slave->howfull < bestfullness || bestfullness == -1)) {
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
				// NB: we should pick the best slave ourselves and get its slavinfo pointer to give to putfile()
				// Then we should update the file table ourselves
				unordered_map<slave_idx, slavinfo *> slavestorecv;
				
				bool already_stored = false;
				
				pthread_mutex_lock(files_lock);
				if(files->find(payld) != files->end()) {
					already_stored = true;
					printf("File '%s' has already been stored on the following slaves: ", payld);
					// The file exists in the table
					struct filinfo *file_info = (*files)[payld];
					pthread_mutex_lock(slaves_lock);
					for(slave_idx slaveidx : *(file_info->holders)) {
						printf("%lu ", slaveidx);
						slavinfo *slave = (*slaves_info)[slaveidx];
						slavestorecv[slaveidx] = slave;
					}
					pthread_mutex_unlock(slaves_lock);
					printf("\n");
				}
				pthread_mutex_unlock(files_lock);
				
				// If it's a new file, store it with the MIN_STOR_REDUN most ideal slaves
				if(!already_stored) {
					pthread_mutex_lock(slaves_lock);
					auto numslaves = living_count;
					unsigned int numtoget = min(numslaves, MIN_STOR_REDUN);
					printf("Selecting %u best slaves from %lu responsive slaves\n", numtoget, numslaves);
					for(unsigned int i = 0; i < numtoget; ++i) {
						printf("on iter %u < %u\n", i, numtoget);
						slave_idx bestslaveidx = bestslave([slavestorecv](slave_idx check){return slavestorecv.count(check);});
						slavinfo *bestslave = (*slaves_info)[bestslaveidx];
						if(bestslave == NULL) {
							fprintf(stderr, "Something went very wrong; I selected a null best slave from index %lu!\n", bestslaveidx);
						}
						
						slavestorecv[bestslaveidx] = bestslave;
						printf("Selecting slave %lu as a best slave\n", bestslaveidx);
					}
					pthread_mutex_unlock(slaves_lock);
				}
				
				printf("slavestorecv has %lu slaves\n", slavestorecv.size());
				
				// Lock on the files so we can get the write protect lock, and check again if we're a new file
				pthread_mutex_lock(files_lock);
				if(!already_stored) {
					// The file doesn't exist in the table yet
					struct filinfo *file_entry = (struct filinfo *)malloc(sizeof(struct filinfo));
					file_entry->write_lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
					pthread_mutex_init(file_entry->write_lock, NULL);
					file_entry->holders = new unordered_set<slave_idx>();
					(*files)[payld] = file_entry;
				}
				
				// We need to grab this either way
				pthread_mutex_t *writeprotect_lock = (*files)[payld]->write_lock;
				pthread_mutex_unlock(files_lock);

				pthread_mutex_lock(writeprotect_lock);
				for(pair<slave_idx, slavinfo *> entry: slavestorecv) {
					slave_idx slaveidx = entry.first;
					slavinfo *slave = entry.second;
					
					printf("Sending file to slave %lu\n", slaveidx);
					
					if(putfile(slave, payld, junk, fd, !already_stored)) {
						printf("Succeeded in sending to slave %lu!\n", slaveidx);
						
						// Lock and update the file map
						pthread_mutex_lock(files_lock);
						(*files)[payld]->holders->insert(slaveidx);
						pthread_mutex_unlock(files_lock);
					} else {
						// TODO handle the case where the transfer was not successful
						printf("The transfer to slave %lu was not successful\n", slaveidx);
					}
				}
				pthread_mutex_unlock(writeprotect_lock);
				free(junk);
			} else {
				// We got a PLZ packet
				
				// Get the file from the best containing slave
				char *filedata;
				unsigned int dlen;
				if(getfile(payld, &filedata, &dlen, fd)) {
					// Send the file to the client
					sendfile(fd, payld, filedata);
				} else {
					fprintf(stderr, "A client's get FAILED!\n");
					sendpkt(fd, OPC_FKU, NULL, 0, 0);
				}
				
				free(payld);
			}
		}
	}

	return NULL;
}

// Gets a file from what it deems to be the best slave (based currently on queue size)
// Accepts: a filename string to request, a pointer to where the data should be stored, a pointer to the length of the data, and a unique ID to add to the slave's queue (client file descriptor is a good choice)
bool getfile(const char *filename, char **databuf, unsigned int *dlen, const int queueid) {
	pthread_mutex_lock(files_lock);
	if(!files->count(filename)) {
		pthread_mutex_unlock(files_lock);
		return false;
	}
	unordered_set<slave_idx> *containing_slaves = (*files)[filename]->holders;
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
		fprintf(stderr, "No slave is alive from which we may receive file '%s'!\n", filename);
		return false;
	}
	
	pthread_mutex_lock(slaves_lock);
	
	slavinfo *bestslave = (*slaves_info)[bestslaveidx];
	// Lock on the slave's queue
	pthread_mutex_lock(bestslave->waiting_lock);
	// Add ourselves to the slave's queue
	bestslave->waiting_clients->push(queueid);
	// Wait while we're not first in the slave's queue
	while(bestslave->waiting_clients->front() != queueid) {
		pthread_cond_wait(bestslave->waiting_notify, bestslave->waiting_lock);
	}
	
	pthread_mutex_unlock(bestslave->waiting_lock);
	
	sendpkt(bestslave->ctlfd, OPC_PLZ, filename, 0, 0);
	uint16_t numpkts = -1;
	
	char *receivedfilename;
	recvpkt(bestslave->ctlfd, OPC_HRZ, &receivedfilename, &numpkts, NULL, false);
	printf("Receiving file '%s' from slave %lu\n", receivedfilename, bestslaveidx);
	recvfile(bestslave->ctlfd, numpkts, databuf, dlen);
	
	// Lock and pop ourselves off the queue
	pthread_mutex_lock(bestslave->waiting_lock);
	bestslave->waiting_clients->pop();
	
	// Unlock just in case broadcast doesn't
	pthread_mutex_unlock(bestslave->waiting_lock);
	
	// Notify all others waiting on the slave
	pthread_cond_broadcast(bestslave->waiting_notify);
	
	pthread_mutex_unlock(slaves_lock);
	
	return true;
}

bool putfile(slavinfo *slave, const char *filename, const char *filedata, const int queueid, bool newfile) {
	bool succeeded = true;
	
	// Lock on the slave's queue
	pthread_mutex_lock(slave->waiting_lock);
	// Add ourselves to the slave's queue
	slave->waiting_clients->push(queueid);
	// Wait while we're not first in the slave's queue
	while(slave->waiting_clients->front() != queueid) {
		pthread_cond_wait(slave->waiting_notify, slave->waiting_lock);
	}
	
	pthread_mutex_unlock(slave->waiting_lock);
	
	// Send the file to the slave; this is the moment we've all been waiting for!
	succeeded = sendfile(slave->ctlfd, filename, filedata);
	if(newfile) // It's a Brand New File (for this slave, that is)
		++slave->howfull;
	
	// Lock and pop ourselves off the queue
	pthread_mutex_lock(slave->waiting_lock);
	slave->waiting_clients->pop();
	
	// Unlock just in case broadcast doesn't
	pthread_mutex_unlock(slave->waiting_lock);
	
	// Notify all others waiting on the slave
	pthread_cond_broadcast(slave->waiting_notify);
	
	return succeeded;
}

// 3 modes:
//   registering?	replicate *all*
//   burying?
//     healthy?		replicate selectively
//     degrading?	wipe
void *rereplicate(void *i) {
	bool slave_failed = *(bool *)i;
	slave_idx failed_slavid = *(slave_idx *)((bool *)i+1);
	free(i);
	pthread_detach(pthread_self());

	map<const char *, struct filinfo *> *files_local = new map<const char *, struct filinfo *>();
	bool actually_replicate = true;

	pthread_mutex_lock(slaves_lock);

	if(slave_failed) {
		copy_if(files->begin(), files->end(), inserter(*files_local, files_local->begin()), [failed_slavid](const pair<const char *, struct filinfo *> &it){return it.second->holders->count(failed_slavid);});
		if(living_count < MIN_STOR_REDUN) actually_replicate = false; // All nodes are already identical, so replicating is pointless
	}
	else
		copy(files->begin(), files->end(), inserter(*files_local, files_local->begin()));

	pthread_mutex_unlock(slaves_lock);

	for(auto file_corr = files_local->begin(); file_corr != files_local->end(); ++file_corr) {
		slave_idx dest_slavid = -1;
		if(actually_replicate) {
			pthread_mutex_lock(file_corr->second->write_lock);

			if(slave_failed) {
				pthread_mutex_lock(slaves_lock);
				unordered_set<slave_idx> *holders = file_corr->second->holders;
				dest_slavid = bestslave([holders](slave_idx check){return holders->count(check);});
			}
			else
				dest_slavid = failed_slavid; // Propagate to the new node

			struct slavinfo *dest_slavif = (*slaves_info)[dest_slavid];
			pthread_mutex_unlock(slaves_lock);

			char *value = NULL;
			unsigned int vallen;
			// TODO WHOOPS! Imagine a slave comes up leaving us in degraded mode, then fails before mirroring is finnished: Now the cleanup thread comes in with the same queue identifier and thread safety is broken!
			getfile(file_corr->first, &value, &vallen, -failed_slavid); // Use additive inverse of faild slave ID as our unique queue identifier

			if(!putfile(dest_slavif, file_corr->first, value, -failed_slavid, true)) // We'll use that same unique ID to mark our place in line
				// TODO release the writelock, repeat this run of the for loop
				printf("Failed to put the file during cremation; case not handled!");
		}

		pthread_mutex_lock(files_lock);

		(*files)[file_corr->first]->holders->erase(failed_slavid);
		if(actually_replicate)
			(*files)[file_corr->first]->holders->insert(dest_slavid);

		pthread_mutex_unlock(files_lock);

		if(actually_replicate)
			pthread_mutex_unlock(file_corr->second->write_lock);
	}

	delete files_local;
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

		slave_idx replicate = 0; // 0 is a sentinel meaning not to (no need when first slave comes up)

		pthread_mutex_lock(slaves_lock);

		slaves_info->push_back(rec);
		++living_count;
		if(living_count <= MIN_STOR_REDUN)
			replicate = slaves_info->size()-1;

		pthread_mutex_unlock(slaves_lock);

		if(replicate) {
			pthread_t distribute;
			bool *flags = (bool *)malloc(sizeof(bool)+sizeof(slave_idx));
			*flags = false; // No slave failed
			*(slave_idx *)(flags+1) = replicate; // Replicate everything onto me
			pthread_create(&distribute, NULL, &rereplicate, flags);
		}

		printf("Registered a slave!\n");
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
			if(slavefds[i]) { // Only ping the slave if it's alive.
				bool failure = true;
				while(recvpkt(slavefds[i], OPC_SUP, NULL, NULL, NULL, true)) {
					failure = false;
				}
				if(failure) {
					printf("Slave %lu is dead!\n", i);
					slavefds[i] = 0; // Let 0 be a sentinel that means, "He's dead, Jim."

					pthread_mutex_lock(slaves_lock);
					(*slaves_info)[i]->alive = false;
					--living_count;
					pthread_mutex_unlock(slaves_lock);

					pthread_t cleaner;
					bool *flags = (bool *)malloc(sizeof(bool)+sizeof(slave_idx));
					*flags = true; // a slave failed
					*(slave_idx *)(flags+1) = i; // which slave failed
					pthread_create(&cleaner, NULL, &rereplicate, flags);
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

void *clientregistration(void *clientqueue) {
	int single_source_of_clients = tcpskt(PORT_MASTER_CLIENTS, MAX_MASTER_BACKLOG);
	queue<pthread_t *> connected_clients = *(queue<pthread_t *> *)clientqueue;
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
	
	return NULL;
}
