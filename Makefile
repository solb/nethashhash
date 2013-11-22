CPPFLAGS := -std=c++0x -pthread -Wall -Wextra -Wno-unused-parameter ${CPPFLAGS}

all: master slave client
master: master.cpp common.o
slave: slave.cpp common.o
client: client.cpp common.o

debug:
	${MAKE} wipe
	CPPFLAGS=-ggdb ${MAKE}

clean:
	- rm common.o
wipe: clean
	- rm master
	- rm slave
	- rm client
