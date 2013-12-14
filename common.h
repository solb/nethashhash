/*
 * Copyright (C) 2013 Sol Boucher and Lane Lawley
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with it.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <errno.h>
#include <functional>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

namespace std {
	template <>
	struct equal_to<const char *> {
		bool operator()(const char *l, const char *r) const {
			return !strcmp(l, r);
		}
	};

	template <>
	struct hash<const char *> {
		size_t operator()(const char *val) const {
			hash<string> hasher;
			return hasher(string(val));
		}
	};
}

namespace hashhash {
	const int PORT_MASTER_CLIENTS = 1030;
	const int PORT_MASTER_REGISTER = 1031;
	const int PORT_MASTER_HEARTBEAT = 1032;
	const int PORT_SLAVE_MAIN = 1033;

	const int MAX_MASTER_BACKLOG = 1;

	const int MAX_PACKET_LEN = 512;
	
	const int SLAVE_KEEPALIVE_TIME = 500000;
	const int MASTER_REG_GRACE_PRD = 500000;

	const unsigned long MIN_STOR_REDUN = 2;

	const uint8_t OPC_PLZ = 1;
	const uint8_t OPC_HRZ = 2;
	const uint8_t OPC_STF = 4;
	const uint8_t OPC_HEY = 8;
	const uint8_t OPC_BYE = 16;
	const uint8_t OPC_THX = 32;
	const uint8_t OPC_FKU = 64;
	const uint8_t OPC_SUP = 128;

	const int RETVAL_INVALID_ARG = 1;
	const int RETVAL_CONN_FAILED = 2;

	int tcpskt(int, int);
	bool rslvconn(int *, const char *, in_port_t);
	bool recvpkt(int, uint16_t, char **, bool *, uint16_t *, bool);
	bool recvfile(int, char **, size_t *);
	bool sendpkt(int, uint8_t, const char *, int);
	bool sendfile(int, const char *, const char*, size_t);
	
	bool readin(char **, size_t *);
	bool homog(const char *, char);

	void handle_error(const char *);
	
	unsigned long min(unsigned long, unsigned long);
}

#endif
