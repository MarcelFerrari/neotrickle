/*
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: message.h,v 1.6 2003/05/02 06:33:28 marius Exp $
 */

/*
 * Modifications Copyright (C) 2026 Marcel Ferrari
 *
 * Substantially rewritten for protocol v3 (grant-based instead of
 * delay-based, 64-bit limits). This modified file is distributed as part of ntrickle
 * under GPL-3.0-or-later. The original BSD copyright is retained above
 * and the BSD-3-Clause license text is provided in
 * LICENSES/BSD-3-Clause.txt.
 */

/*
 * message.h — Protocol v3: grant-based (64-bit limits)
 *
 * Wire format:
 *   [1 byte: version] [4 bytes payload len BE] [payload: serialized struct msg]
 *
 * Three message types:
 *   MSG_TYPE_CONF   (preload → daemon): registration
 *   MSG_TYPE_GRANT  (daemon → preload): rate grant
 *   MSG_TYPE_UPDATE (preload → daemon): fire-and-forget byte counts
 */

#ifndef TRICKLE_MESSAGE_H
#define TRICKLE_MESSAGE_H

#include <stdint.h>

/* Protocol constants */
#define PROTOCOL_VERSION 3
#define PROTOCOL_MAX_MSG_SIZE 2048

/* Direction constants */
#define TRICKLE_SEND 0
#define TRICKLE_RECV 1

/* Flag for non-blocking socket descriptor state */
#define TRICKLE_NONBLOCK 0x1

/*
 * Return-value sentinel from delay(): the I/O would exceed the bandwidth
 * limit on a non-blocking socket.  Callers must set errno = EAGAIN and
 * return -1 instead of performing the real read/write.
 *
 * Must NOT collide with TRICKLE_SEND (0) or TRICKLE_RECV (1), which are
 * direction constants used as array indices.
 */
#define TRICKLE_WOULDBLOCK 2

/* Compile-time guard: TRICKLE_WOULDBLOCK must not alias a direction constant */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(TRICKLE_WOULDBLOCK != TRICKLE_SEND && TRICKLE_WOULDBLOCK != TRICKLE_RECV,
               "TRICKLE_WOULDBLOCK collides with a direction constant");
#elif defined(__cplusplus) && __cplusplus >= 201103L
static_assert(TRICKLE_WOULDBLOCK != TRICKLE_SEND && TRICKLE_WOULDBLOCK != TRICKLE_RECV,
              "TRICKLE_WOULDBLOCK collides with a direction constant");
#endif

struct msg_conf {
    uint64_t lim[2]; // bytes/sec (self-reported, advisory)
    pid_t pid;
    char argv0[256];
    uid_t uid;
    gid_t gid;
    uint32_t weight;
};

struct msg_grant {
    uint64_t grant[2]; // [TRICKLE_SEND, TRICKLE_RECV] bytes/sec
};

struct msg_update {
    size_t len;
    short dir;
};

enum msgtype {
    MSG_TYPE_CONF = 1,
    MSG_TYPE_UPDATE = 2,
    MSG_TYPE_GRANT = 9,
};

struct msg {
    enum msgtype type;
    short status;
    union {
        struct msg_conf conf;
        struct msg_grant grant;
        struct msg_update update;
    } data;
};

#endif /* TRICKLE_MESSAGE_H */
