/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ntrickledu.h — Protocol v3 daemon client (grant-based, 64-bit)
 */

#ifndef TRICKLE_NTRICKLEDU_H
#define TRICKLE_NTRICKLEDU_H

#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>

void
ntrickled_configure(char *, int (*)(int, int, int), ssize_t (*)(int, void *, size_t),
		    ssize_t (*)(int, const void *, size_t), int (*)(int), char *);
void
ntrickled_open(int *);
int
ntrickled_update(short, size_t);
int
ntrickled_poll_grant(void);
void
ntrickled_close(int *);

/* Grant state — read by the preload hot path */
extern uint64_t ntrickled_grant_send;
extern uint64_t ntrickled_grant_recv;

#endif /* TRICKLE_NTRICKLEDU_H */
