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
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdarg.h>

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

// "Sex appeal", as Sol would say
static const char *const SHL_PS1 = "#hashtable> ";

// Interactive commands (must not share a first character)
static const char *const CMD_SLV = "slaves";
static const char *const CMD_FIL = "files";
static const char *const CMD_GFO = "quit";
static const char *const CMD_HLP = "?";

typedef vector<int>::size_type slave_idx;

struct slavinfo {
	bool alive; // access is atomic
	pthread_mutex_t *waiting_lock;
	pthread_cond_t *waiting_notify;
	queue<int> *waiting_clients; // acquire waiting_lock before reading or writing, then wait on waiting_notify until at head
	int supfd; // should only be used by keepalive thread
	int ctlfd; // only head of waiting_clients may use
	long long howfull; // only head of waiting_clients may write
};

struct filinfo {
	pthread_mutex_t *write_lock; // acquire before changing the value, hold until every slave in holders is consistent and stores the same value
	unordered_set<slave_idx> *holders; // reads are safe, but must be holding write_lock to write
};

static pthread_mutex_t *slaves_lock = NULL;
static vector<struct slavinfo *> *slaves_info = NULL; // acquire slaves_lock before reading or writing
static vector<int>::size_type living_count; // acquire slaves_lock before writing
static pthread_mutex_t *files_lock = NULL;
static unordered_map<const char *, struct filinfo *> *files = NULL; // acquire files_lock before reading or writing

/** Thread functions */
static void *each_client(void *);
static void *rereplicate(void *);
static void *registration(void *);
static void *clientregistration(void *);
static void *keepalive(void *);

/** Communication functions */
bool getfile(const char *, char **, size_t *, const int);
bool putfile(slavinfo *, const char *, const char *, const size_t, const int, bool);

/** Utility functions */
slave_idx bestslave(const function<bool(slave_idx)> &);
void writelog(int, const char *, ...);

/** CLI functions */
static void print_slaves();
static void print_files();
static void print_help();

static const int PRI_SRS = 0;
static const int PRI_INF = 1;
static const int PRI_DBG = 2;

static int logpri = PRI_INF;

int main(int argc, char **argv) {
	// Get debug log priority
	if(argc > 1 && atoi(argv[1])) {
		logpri = atoi(argv[1]);
	}
	
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
				break;
			}
		}
		while(homog(buf, ' '));
		
		// Cleave off the command (first word):
		cmd = strtok(buf, " ");
		len = strlen(cmd);
		
		if(strncmp(cmd, CMD_SLV, len) == 0) {
			print_slaves();
		} else if(strncmp(cmd, CMD_FIL, len) == 0) {
			print_files();
		} else if(strncmp(cmd, CMD_HLP, len) == 0) {
			print_help();
		} else if(strncmp(cmd, CMD_GFO, len) == 0) {
			break;
		} else {
			printf("Unknown command: '%s'\n", cmd);
		}
	}
	while(true);
	
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
	for(auto it = files->begin(); it != files->end(); ++it) {
		pthread_mutex_destroy(it->second->write_lock);
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
		bool inbound = 0; // whether a HRZ message
		if(recvpkt(fd, OPC_PLZ|OPC_HRZ, &payld, &inbound, 0, false)) {
			writelog(PRI_INF, "Received %s packet for key %s\n", inbound ? "HRZ" : "PLZ", payld);
			if(inbound) {
				// We got a HRZ packet
				size_t jsize;
				recvfile(fd, &junk, &jsize);
				// printf("It was %lu bytes long\n", jsize);
				// printf("\tAND IT WAS CARRYING ALL THIS: %s\n", junk);
				
				// Store the file with some slaves
				unordered_map<slave_idx, slavinfo *> slavestorecv;
				
				bool already_stored = false;
				
				pthread_mutex_lock(files_lock);
				if(files->find(payld) != files->end()) {
					already_stored = true;
					writelog(PRI_INF, "File '%s' has already been stored on the following slaves: ", payld);
					// The file exists in the table
					struct filinfo *file_info = (*files)[payld];
					pthread_mutex_lock(slaves_lock);
					for(slave_idx slaveidx : *(file_info->holders)) {
						writelog(PRI_INF, "%lu ", slaveidx);
						slavinfo *slave = (*slaves_info)[slaveidx];
						slavestorecv[slaveidx] = slave;
					}
					pthread_mutex_unlock(slaves_lock);
					writelog(PRI_INF, "\n");
				}
				pthread_mutex_unlock(files_lock);
				
				// If it's a new file, store it with the MIN_STOR_REDUN most ideal slaves
				if(!already_stored) {
					pthread_mutex_lock(slaves_lock);
					auto numslaves = living_count;
					unsigned int numtoget = min(numslaves, MIN_STOR_REDUN);
					writelog(PRI_INF, "Selecting %u best slaves from %lu responsive slaves\n", numtoget, numslaves);
					for(unsigned int i = 0; i < numtoget; ++i) {
						writelog(PRI_DBG, "on iter %u < %u\n", i, numtoget);
						slave_idx bestslaveidx = bestslave([slavestorecv](slave_idx check){return slavestorecv.count(check);});
						slavinfo *bestslave = (*slaves_info)[bestslaveidx];
						if(bestslave == NULL) {
							writelog(PRI_DBG, "Something went very wrong; I selected a null best slave from index %lu!\n", bestslaveidx);
						}
						
						slavestorecv[bestslaveidx] = bestslave;
						writelog(PRI_DBG, "Selecting slave %lu as a best slave\n", bestslaveidx);
					}
					pthread_mutex_unlock(slaves_lock);
				}
				
				writelog(PRI_DBG, "slavestorecv has %lu slaves\n", slavestorecv.size());
				
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
					
					writelog(PRI_INF, "Sending file to slave %lu\n", slaveidx);
					
					if(putfile(slave, payld, junk, jsize, fd, !already_stored)) {
						writelog(PRI_DBG, "Succeeded in sending to slave %lu!\n", slaveidx);
						
						// Lock and update the file map
						pthread_mutex_lock(files_lock);
						(*files)[payld]->holders->insert(slaveidx);
						pthread_mutex_unlock(files_lock);
					} else {
						// TODO handle the case where the transfer was not successful
						writelog(PRI_SRS, "The transfer to slave %lu was not successful\n", slaveidx);
					}
				}
				pthread_mutex_unlock(writeprotect_lock);
				free(junk);
			} else {
				// We got a PLZ packet
				
				// Get the file from the best containing slave
				char *filedata;
				size_t dlen;
				if(getfile(payld, &filedata, &dlen, fd)) {
					// Send the file to the client
					sendfile(fd, payld, filedata, dlen);
				} else {
					writelog(PRI_DBG, "A client's get FAILED!\n");
					sendpkt(fd, OPC_FKU, NULL, 0);
				}
				
				free(payld);
			}
		}
	}

	return NULL;
}

// Gets a file from what it deems to be the best slave (based currently on queue size)
// Accepts: a filename string to request, a pointer to where the data should be stored, a pointer to the length of the data, and a unique ID to add to the slave's queue (client file descriptor is a good choice)
bool getfile(const char *filename, char **databuf, size_t *dlen, const int queueid) {
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
		writelog(PRI_SRS, "No slave is alive from which we may receive file '%s'!\n", filename);
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
	
	sendpkt(bestslave->ctlfd, OPC_PLZ, filename, 0);
	bool blackhole; // we know it's going to be a HRZ, so we ignore this
	
	char *receivedfilename;
	recvpkt(bestslave->ctlfd, OPC_HRZ, &receivedfilename, &blackhole, NULL, false);
	writelog(PRI_INF, "Receiving file '%s' from slave %lu\n", receivedfilename, bestslaveidx);
	recvfile(bestslave->ctlfd, databuf, dlen);
	
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

bool putfile(slavinfo *slave, const char *filename, const char *filedata, const size_t dlen, const int queueid, bool newfile) {
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
	succeeded = sendfile(slave->ctlfd, filename, filedata, dlen);
	if(newfile) // It's a Brand New File (for this slave, that is)
		slave->howfull = slave->howfull + strlen(filedata);
	
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

			pthread_mutex_lock(slaves_lock);
			if(slave_failed) {
				unordered_set<slave_idx> *holders = file_corr->second->holders;
				dest_slavid = bestslave([holders](slave_idx check){return holders->count(check);});
			}
			else
				dest_slavid = failed_slavid; // Propagate to the new node

			struct slavinfo *dest_slavif = (*slaves_info)[dest_slavid];
			pthread_mutex_unlock(slaves_lock);

			if(!slave_failed && !dest_slavif->alive) {
				// We're trying to mirror onto a brand new node that just died on us!
				// Our work here is done: a separate cleanup thread was spawned, so we defer to it.
				pthread_mutex_unlock(file_corr->second->write_lock);
				return NULL;
			}

			char *value = NULL;
			size_t vallen;
			// Our use of the same identifier for both newly-added and failed slaves is threadsafe because the thread that handles the "newly-added" case bails out as soon as it discovers its slave has been lost.
			getfile(file_corr->first, &value, &vallen, -failed_slavid); // Use additive inverse of faild slave ID as our unique queue identifier

			if(!putfile(dest_slavif, file_corr->first, value, vallen, -failed_slavid, true)) // We'll use that same unique ID to mark our place in line
				// TODO release the writelock, repeat this run of the for loop
				writelog(PRI_DBG, "Failed to put the file during cremation; case not handled!");
		}

		pthread_mutex_lock(files_lock);

		struct filinfo *entry = (*files)[file_corr->first];
		entry->holders->erase(failed_slavid);
		if(actually_replicate)
			entry->holders->insert(dest_slavid);
		else if(!entry->holders->size()) { // No more Mr. Nice Guy (i.e. nobody has this file anymore)
			writelog(PRI_SRS, "The last keeper of '%s' has been vanquished!", file_corr->first);
			files->erase(file_corr->first);
			pthread_mutex_destroy(entry->write_lock);
			free(entry->write_lock);
			delete entry->holders;
			free(entry);
			free((char *)file_corr->first);
		}

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
			sendpkt(heartbeat, OPC_FKU, NULL, 0);
			continue;
		}
		int control = socket(AF_INET, SOCK_STREAM, 0);
		usleep(10000); // TODO fix this crap
		location.sin_port = htons(PORT_SLAVE_MAIN);
		if(connect(control, (struct sockaddr *)&location, loclen)) {
			sendpkt(heartbeat, OPC_FKU, NULL, 0);
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
		if(living_count && living_count < MIN_STOR_REDUN) // Slaves are up, but system is degraded
			replicate = slaves_info->size()-1;
		++living_count;

		pthread_mutex_unlock(slaves_lock);

		if(replicate) {
			pthread_t distribute;
			bool *flags = (bool *)malloc(sizeof(bool)+sizeof(slave_idx));
			*flags = false; // No slave failed
			*(slave_idx *)(flags+1) = replicate; // Replicate everything onto me
			pthread_create(&distribute, NULL, &rereplicate, flags);
		}
		
		writelog(PRI_INF, "Registered a slave: %s!\n", inet_ntoa(location.sin_addr));
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
					writelog(PRI_INF, "Slave %lu is dead!\n", i);
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

void print_slaves() {
	vector<slavinfo *> slaves;
	
	pthread_mutex_lock(slaves_lock);
	for(slavinfo *slave : (*slaves_info)) {
		slaves.push_back(slave);
	}
	pthread_mutex_unlock(slaves_lock);
	
	for(slave_idx i = 0; i < slaves.size(); ++i) {
		if(slaves[i]->alive) {
			struct sockaddr_in peeraddr;
			socklen_t peeraddrlen = sizeof(peeraddr);
			getpeername(slaves[i]->ctlfd, (sockaddr *)&peeraddr, &peeraddrlen);
			
			printf("Slave #%lu: %s\n\tCurrently storing: %lld bytes\n", i, inet_ntoa(peeraddr.sin_addr), slaves[i]->howfull);
		}
	}
}

void print_files() {
	unordered_map<const char *, filinfo *> localfiles;
	
	pthread_mutex_lock(files_lock);
	for(auto it = files->begin(); it != files->end(); ++it) {
		localfiles[it->first] = it->second;
	}
	pthread_mutex_unlock(files_lock);
	
	for(auto it = localfiles.begin(); it != localfiles.end(); ++it) {
		writelog(PRI_INF, "Key '%s' is stored on the following slaves: ", it->first);
		
		const char *sep = "";
		unordered_set<slave_idx> localholders = *(it->second->holders);
		for(slave_idx idx : localholders) {
			printf("%s%lu", sep, idx);
			sep = ", ";
		}
		
		printf("\n");
	}
}

void print_help() {
	printf("Commands may be abbreviated.  Commands are:\n\n");
	printf("%s\t\tview slave info\n", CMD_SLV);
	printf("%s\t\tview file info\n", CMD_FIL);
	printf("%s\t\tshut down #hashtable master server\n", CMD_GFO);
	printf("%s\t\tprint help information\n", CMD_HLP);
}

void writelog(int pri, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	
	if(pri <= logpri) {
		printf("\n");
		vprintf(fmt, args);
		printf("\n%s", SHL_PS1); // "restore" the prompt
		fflush(stdout);
	}
	
	va_end(args);
}
