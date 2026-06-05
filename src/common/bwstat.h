/*
 * bwstat.h
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: bwstat.h,v 1.5 2003/04/06 00:29:37 marius Exp $
 */

/*
 * Modifications Copyright (C) 2026 Marcel Ferrari
 *
 * This modified file is distributed as part of ntrickle under
 * GPL-3.0-or-later. The original BSD copyright and licensing
 * information is retained in this file and in
 * LICENSES/BSD-3-Clause.txt.
 */

#ifndef TRICKLE_BWSTAT
#define TRICKLE_BWSTAT

#include <sys/queue.h>
#include <sys/time.h>
#include <sys/types.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BWSTAT_SEND 0
#define BWSTAT_RECV 1

struct bwstat_data {
    uint64_t bytes;
    uint64_t rate;
    struct timeval tv;

    uint64_t winbytes;
    uint64_t winrate;
    struct timeval wintv;
};

struct bwstat {
    struct bwstat_data data[2];
    uint pts;
    uint lsmooth;
    double tsmooth;
    TAILQ_ENTRY(bwstat) next, qnext;
};

int bwstat_init(uint);
struct bwstat* bwstat_new(void);
struct bwstat* bwstat_free(struct bwstat*);
void bwstat_update(struct bwstat*, size_t, short);
struct timeval* bwstat_getdelay(struct bwstat*, size_t*, uint64_t, short);
struct bwstat* bwstat_gettot(void);

#ifdef __cplusplus
}
#endif

#endif /* TRICKLE_BWSTAT */
