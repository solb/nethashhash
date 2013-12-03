CPPFLAGS := -std=c++0x -pthread -Wall -Wextra -Wno-unused-parameter ${CPPFLAGS}

all: master slave client
master: common.o
slave: common.o
client: common.o

debug:
	${MAKE} wipe
	CPPFLAGS=-ggdb ${MAKE}

android:
	[ -e jni ] || ln -s ./ jni
	ndk-build
zapconf:
	- rm runslave.conf

clean:
	- rm common.o
	- rm jni
	- rm -r obj/
wipe: clean
	- rm master
	- rm slave
	- rm client
	- rm -r libs/
