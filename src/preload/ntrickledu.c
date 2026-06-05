/*
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: trickledu.c,v 1.16 2004/02/13 06:11:21 marius Exp $
 */

/*
 * Modifications Copyright (C) 2026 Marcel Ferrari
 *
 * Rewritten for protocol v3 (grant-based, 64-bit, non-blocking poll).
 * This modified file is distributed as part of ntrickle under
 * GPL-3.0-or-later. The original BSD copyright is retained above and the
 * BSD-3-Clause license text is provided in LICENSES/BSD-3-Clause.txt.
 */

/*
 * ntrickledu.c — Protocol v3 daemon client (grant-based, 64-bit)
 */

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/socket.h>
#include <sys/un.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
#include <time.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "codec.h"
#include "message.h"
#include "ntrickledu.h"
#include "util.h"

extern uint32_t weight;

static void
_ntrickled_open(struct msg *, int *);

#define DECLARE(name, ret, args) static ret(*libc_##name) args

DECLARE(socket, int, (int, int, int));
DECLARE(read, ssize_t, (int, void *, size_t));
DECLARE(write, ssize_t, (int, const void *, size_t));
DECLARE(close, int, (int));

static char *argv0, *sockname;
static int ntrickled_sock = -1, *trickled_ptr;
static pid_t ntrickled_pid = -1;

/* Public grant state — read by the preload hot path without locks */
uint64_t ntrickled_grant_send = 0;
uint64_t ntrickled_grant_recv = 0;

/* Non-blocking read: consume any pending MSG_TYPE_GRANT from the daemon */
int
ntrickled_poll_grant(void)
{
	struct msg msg;

	if (ntrickled_sock == -1)
		return (-1);

	/* Set socket to non-blocking temporarily */
	int flags = fcntl(ntrickled_sock, F_GETFL, 0);
	if (flags == -1)
		return (-1);
	if (fcntl(ntrickled_sock, F_SETFL, flags | O_NONBLOCK) == -1)
		return (-1);

	int ret = -1;
	for (;;) {
		/* Wire framing: [1 byte version] [4 bytes len BE] [payload] */
		uint8_t ver;
		ssize_t n = (*libc_read)(ntrickled_sock, &ver, 1);
		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				ret = 0; /* nothing pending */
			}
			break;
		}
		if (n != 1 || ver != PROTOCOL_VERSION)
			break;

		uint32_t xbuflen;
		n = (*libc_read)(ntrickled_sock, &xbuflen, sizeof(xbuflen));
		if (n == -1) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
		}
		if (n != sizeof(xbuflen))
			break;

		uint32_t buflen = ntohl(xbuflen);
		if (buflen > 2048)
			break;

		u_char buf[2048];
		if (atomicio(libc_read, ntrickled_sock, buf, buflen) != buflen)
			break;

		memset(&msg, 0, sizeof(msg));
		if (codec_decode(&msg, buf, buflen) == -1)
			break;

		if (msg.type == MSG_TYPE_GRANT) {
			ntrickled_grant_send = msg.data.grant.grant[TRICKLE_SEND];
			ntrickled_grant_recv = msg.data.grant.grant[TRICKLE_RECV];
			ret = 0;
			continue; /* drain any additional grants */
		}
		/* Unknown message type — ignore and continue draining */
	}

	/* Restore blocking mode */
	if (flags != -1)
		fcntl(ntrickled_sock, F_SETFL, flags & ~O_NONBLOCK);

	if (ret == -1) {
		ntrickled_close(trickled_ptr);
	}
	return ret;
}

/* Internal send with framing */
static int
send_msg(struct msg *msg)
{
	u_char buf[2048];
	uint32_t buflen = sizeof(buf), xbuflen;

	if (ntrickled_sock != -1 && ntrickled_pid != getpid()) {
		ntrickled_close(trickled_ptr);
		ntrickled_open(trickled_ptr);
	}

	if (ntrickled_sock == -1)
		goto fail;

	if (codec_encode(msg, buf, &buflen) == -1)
		return (-1);

	/* Wire framing: [1 byte version] [4 bytes len BE] [payload] */
	uint8_t version = PROTOCOL_VERSION;
	if (atomicio(libc_write, ntrickled_sock, &version, 1) != 1)
		return (-1);

	xbuflen = htonl(buflen);
	if (atomicio(libc_write, ntrickled_sock, &xbuflen, sizeof(xbuflen)) != sizeof(xbuflen))
		return (-1);

	if (atomicio(libc_write, ntrickled_sock, buf, buflen) == buflen)
		return (0);

fail:
	*trickled_ptr = 0;
	ntrickled_sock = -1;
	return (-1);
}

/* Internal receive with framing */
static int
recv_msg(struct msg *msg)
{
	u_char buf[2048];
	uint32_t buflen, xbuflen;

	if (ntrickled_sock == -1)
		goto fail;

	/* Wire framing: [1 byte version] [4 bytes len BE] [payload] */
	uint8_t version;
	if (atomicio(libc_read, ntrickled_sock, &version, 1) != 1)
		return (-1);
	if (version != PROTOCOL_VERSION)
		return (-1);

	if (atomicio(libc_read, ntrickled_sock, &xbuflen, sizeof(xbuflen)) != sizeof(xbuflen))
		return (-1);
	buflen = ntohl(xbuflen);
	if (buflen > sizeof(buf))
		return (-1);

	if (atomicio(libc_read, ntrickled_sock, buf, buflen) == buflen) {
		if (codec_decode(msg, buf, buflen) == -1)
			return (-1);
		return (0);
	}

fail:
	*trickled_ptr = 0;
	ntrickled_sock = -1;
	return (-1);
}

void
ntrickled_configure(char *xsockname, int (*xlibc_socket)(int, int, int),
		    ssize_t (*xlibc_read)(int, void *, size_t),
		    ssize_t (*xlibc_write)(int, const void *, size_t), int (*xlibc_close)(int),
		    char *xargv0)
{
	sockname = xsockname;
	libc_socket = xlibc_socket;
	libc_write = xlibc_write;
	libc_read = xlibc_read;
	libc_close = xlibc_close;
	argv0 = xargv0;
}

void
ntrickled_open(int *xtrickled)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_TYPE_CONF;
	msg.data.conf.pid = getpid();
	strncpy(msg.data.conf.argv0, argv0, sizeof(msg.data.conf.argv0) - 1);
	msg.data.conf.argv0[sizeof(msg.data.conf.argv0) - 1] = '\0';
	msg.data.conf.uid = geteuid();
	msg.data.conf.gid = getegid();
	msg.data.conf.weight = weight;

	_ntrickled_open(&msg, xtrickled);

	/* Receive the initial grant response */
	if (ntrickled_sock != -1) {
		struct msg resp;
		memset(&resp, 0, sizeof(resp));
		if (recv_msg(&resp) == 0 && resp.type == MSG_TYPE_GRANT) {
			ntrickled_grant_send = resp.data.grant.grant[TRICKLE_SEND];
			ntrickled_grant_recv = resp.data.grant.grant[TRICKLE_RECV];
		}
	}
}

void
ntrickled_close(int *xtrickled)
{
	if (ntrickled_sock != -1) {
		(*libc_close)(ntrickled_sock);
	}
	*xtrickled = 0;
	ntrickled_sock = -1;
	ntrickled_grant_send = 0;
	ntrickled_grant_recv = 0;
}

static void
_ntrickled_open(struct msg *msg, int *xtrickled)
{
	int s;
	struct sockaddr_un xsun;

	trickled_ptr = xtrickled;
	*trickled_ptr = 0;

	if ((s = (*libc_socket)(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return;

	memset(&xsun, 0, sizeof(xsun));
	xsun.sun_family = AF_UNIX;
	strncpy(xsun.sun_path, sockname, sizeof(xsun.sun_path) - 1);
	xsun.sun_path[sizeof(xsun.sun_path) - 1] = '\0';

	if (connect(s, (struct sockaddr *)&xsun, sizeof(xsun)) == -1) {
		(*libc_close)(s);
		return;
	}

	ntrickled_pid = getpid();
	ntrickled_sock = *trickled_ptr = s;

	if (send_msg(msg) == -1) {
		(*libc_close)(s);
		ntrickled_sock = -1;
		*trickled_ptr = 0;
	}
}

int
ntrickled_update(short dir, size_t len)
{
	struct msg msg;

	memset(&msg, 0, sizeof(msg));
	msg.type = MSG_TYPE_UPDATE;
	msg.data.update.len = len;
	msg.data.update.dir = dir;

	return send_msg(&msg);
}
