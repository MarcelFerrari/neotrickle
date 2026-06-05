/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * state.h — Preload global state and descriptor table
 */

#ifndef TRICKLE_STATE_H
#define TRICKLE_STATE_H

#include "bwstat.h"
#include "message.h"
#include <stdint.h>
#include <sys/queue.h>
#include <sys/types.h>

#define SD_INSELECT 0x01

struct sockdesc {
	int sock;
	int flags;
	struct bwstat *stat;
	struct {
		int flags;
		size_t lastlen;
		size_t selectlen;
	} data[2];
	TAILQ_ENTRY(sockdesc) next;
};

TAILQ_HEAD(sockdeschead, sockdesc);

/* Global state */
extern struct sockdeschead sdhead;
extern uint32_t winsz;
extern int verbose;
extern uint64_t lim[2];
extern char *argv0;
extern double tsmooth;
extern uint lsmooth;
extern uint32_t weight;
extern int trickled;
extern int initialized, initializing;

/* Descriptor table operations */
struct sockdesc *
state_find(int fd);
struct sockdesc *
state_new(int fd);
void
state_remove(int fd);

/* Locking */
void
trickle_lock(void);
void
trickle_unlock(void);

/* Lazy-init guard */
void
trickle_init(void);
void
safe_printv(int level, const char *fmt, ...);

#define INIT                                                                                       \
	do {                                                                                       \
		if (!initialized && !initializing)                                                 \
			trickle_init();                                                            \
	} while (0)

#endif /* TRICKLE_STATE_H */
