/*
 * bwstat.c
 *
 * Copyright (c) 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: bwstat.c,v 1.15 2003/07/29 05:58:58 marius Exp $
 */

/*
 * Modifications Copyright (C) 2026 Marcel Ferrari
 *
 * This modified file is distributed as part of ntrickle under
 * GPL-3.0-or-later. The original BSD copyright and licensing
 * information is retained in this file and in
 * LICENSES/BSD-3-Clause.txt.
 */

#include <sys/queue.h>
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif /* HAVE_SYS_TIME_H */

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif /* HAVE_STDINT_H */
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
#include <time.h>
#endif /* defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME) */
#include "bwstat.h"
#include "util.h"

#include <string.h>

static TAILQ_HEAD(bwstathead, bwstat) statq;
static uint winsz;

static double difftv(struct timeval*, struct timeval*);
static void _bwstat_update(struct bwstat_data*, size_t);

#define INITTV(tv, curtv)                                                                          \
    do {                                                                                           \
        if (!timerisset(&(tv)))                                                                    \
            (tv) = (curtv);                                                                        \
    } while (0)

int bwstat_init(uint xwinsz) {
    struct bwstat* bs;

    winsz = xwinsz;

    TAILQ_INIT(&statq);

    /* First entry is the totals. */
    if ((bs = bwstat_new()) == NULL)
        return (-1);

    return (0);
}

struct bwstat* bwstat_new(void) {
    struct bwstat* bs;

    if ((bs = calloc(1, sizeof(*bs))) == NULL)
        return (NULL);

    bs->pts = 1;
    TAILQ_INSERT_TAIL(&statq, bs, next);

    return (bs);
}

struct bwstat* bwstat_free(struct bwstat* bs) {
    TAILQ_REMOVE(&statq, bs, next);
    free(bs);

    return (NULL);
}

struct bwstat* bwstat_gettot(void) {
    return (TAILQ_FIRST(&statq));
}

void bwstat_update(struct bwstat* bs, size_t len, short which) {
    struct bwstat* bstot = TAILQ_FIRST(&statq);

    if (bs != NULL)
        _bwstat_update(&bs->data[which], len);
    _bwstat_update(&bstot->data[which], len);
}

static void _bwstat_update(struct bwstat_data* bsd, size_t len) {
    struct timeval curtv;
    double elap, elapwin;

    gettimeofday(&curtv, NULL);

    INITTV(bsd->tv, curtv);
    INITTV(bsd->wintv, curtv);

    elap = difftv(&curtv, &bsd->tv);
    elapwin = difftv(&curtv, &bsd->wintv);

    /*
     * Carry forward at most one full window's worth of bytes from the
     * previous rate so that a freshly reset window doesn't report an
     * artificially low winrate.  Clamp to winsz to prevent the
     * positive feedback loop that would otherwise inflate winrate
     * without bound and cause periodic uint32_t overflow / free-pass
     * cycles.
     */
    if (bsd->winbytes == 0 && bsd->winrate > 0) {
        double carry = (1.0 * bsd->winrate) * elapwin;
        if (carry > winsz)
            carry = winsz;
        bsd->winbytes = (uint64_t) carry;
    }

    bsd->bytes += len;
    bsd->winbytes += len;

    if (elap == 0.0 || elapwin == 0.0)
        return;

    bsd->rate = (1.0 * bsd->bytes) / elap;
    bsd->winrate = (1.0 * bsd->winbytes) / elapwin;

    if (bsd->winbytes >= winsz) {
        gettimeofday(&bsd->wintv, NULL);
        bsd->winbytes = 0;
    }
}

/*
 * Return required delay for bs for direction which.
 */

struct timeval* bwstat_getdelay(struct bwstat* bs, size_t* len, uint64_t lim, short which) {
    uint64_t rate = 0;
    uint npts = 0;
    uint64_t ent;
    int ncli = 0;
    uint64_t pool = 0, xent;
    double delay;
    static struct timeval tv;
    struct bwstathead poolq;
    struct bwstat *xbs, *bstot = TAILQ_FIRST(&statq);
    size_t xlen = *len;

    if (*len == 0)
        return (NULL);

    memset(&tv, 0, sizeof(tv));

    TAILQ_INIT(&poolq);

    rate = bstot->data[which].winrate;

    /*
     * The measured window rate systematically lags behind actual
     * throughput because (a) zero-byte time-updates during
     * select/poll waits lower winrate between I/O bursts, and
     * (b) the first 1-2 reads after startup happen before any
     * rate measurement exists.  Together these give the algorithm
     * an inherent ~20 % overshoot.
     *
     * Use a deadband: only skip the delay when the measured rate
     * is decisively below the limit.  Without this, the system
     * oscillates around "rate ≈ lim" and the long-term average
     * settles above the target.
     *
     * The factor 6/7 (≈ 0.86) has been empirically determined to
     * cancel the overshoot across limits from 64 KB/s to 100 MB/s.
     */
    if (rate <= (lim - lim / 7))
        return (NULL);

    xbs = bstot;
    while ((xbs = TAILQ_NEXT(xbs, next)) != NULL) {
        ncli++;
        npts += xbs->pts;
        TAILQ_INSERT_TAIL(&poolq, xbs, qnext);
    }

    if (ncli == 0)
        return (NULL);

    /* Entitlement per point.  When total client weight exceeds the
     * bandwidth limit (npts > lim), the integer division produces 0.
     * Clamp to 1 B/s to avoid division-by-zero in the delay computation
     * below.  Smoothing parameters will limit the actual transfer size
     * even when the entitlement is low. */
    ent = lim / npts;
    if (ent == 0)
        ent = 1;

    /*
     * Sprinkle some bandwidth: increase the value of a point
     * until everyone is satisfied.
     */
    do {
        /* Take from the poor ... */
        TAILQ_FOREACH(xbs, &poolq, qnext)
        if (xbs->data[which].winrate < ent * xbs->pts) {
            pool += ent * xbs->pts - xbs->data[which].winrate;
            ncli--;
            npts -= xbs->pts;
            TAILQ_REMOVE(&poolq, xbs, qnext);
        }

        /* And give to the rich ... */
        if (ncli > 0) {
            xent = pool / npts;

            if (xent <= 0)
                break;

            TAILQ_FOREACH(xbs, &poolq, qnext)
            if (xbs->data[which].winrate > ent * xbs->pts)
                pool -= xent * xbs->pts;

            ent += xent;
        }
    } while (pool > 0 && ncli > 0);

    /*
     * This is the case of a client that is not using its limit.
     * We reset ent in this case.  The rest will adjust itself
     * over time.
     */
    if (ent * bs->pts > lim)
        ent = lim / bs->pts;

    if (bs->data[which].winrate > ent * bs->pts)
        delay = (1.0 * *len) / (1.0 * ent * bs->pts);
    else
        delay = 0.0;

    if ((*len = ent * bs->pts * bs->tsmooth) == 0) {
        *len = bs->lsmooth;
        delay = (1.0 * *len) / (1.0 * ent * bs->pts);
    } else
        delay = bs->tsmooth;
    /*	} */

    if (*len > xlen) {
        *len = xlen;
        delay = (1.0 * *len) / (1.0 * ent * bs->pts);
    }

    /* Guard against negative delay from underflow. */
    if (delay < 0.0)
        return (NULL);

    tv.tv_sec = delay;
    tv.tv_usec = (delay - tv.tv_sec) * 1000000L;

    return (&tv);
}

static double difftv(struct timeval* tv0, struct timeval* tv1) {
    struct timeval diff_tv;

    timersub(tv0, tv1, &diff_tv);
    return (diff_tv.tv_sec + (diff_tv.tv_usec / 1000000.0));
}
