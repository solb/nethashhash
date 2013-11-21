CPPFLAGS := -std=c++0x -Wall -Wextra ${CPPFLAGS}

all: master slave client
master: master.cpp common.o
slave: slave.cpp common.o
client: client.cpp common.o
# fuck you
clean:
	- rm common.o
wipe: clean
	- rm master
	- rm slave
	- rm client
