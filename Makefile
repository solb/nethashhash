CPPFLAGS := -std=c++0x -Wall -Wextra ${CPPFLAGS}

all: master client
master: master.cpp common.o
client: client.cpp common.o
clean:
	- rm common.o
wipe: clean
	- rm master
	- rm client
