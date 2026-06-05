/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * limiter.c — Bandwidth limiting hot path
 */

#include "limiter.h"
#include "bwstat.h"
#include "ntrickledu.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

int
delay(int sock, ssize_t *len, short which)
{
	struct sockdesc *sd;
	struct timeval *tv;
	struct timespec ts, rm;

	sd = state_find(sock);
	if (sd == NULL)
		return (-1);

	if (ISSET(sd->data[which].flags, SD_INSELECT)) {
		if (*len > 0 && (size_t)*len > sd->data[which].selectlen)
			*len = (ssize_t)sd->data[which].selectlen;
		CLR(sd->data[which].flags, SD_INSELECT);
		return (0);
	}

	if ((tv = getdelay(sd, len, which)) != NULL) {
		TIMEVAL_TO_TIMESPEC(tv, &ts);

		safe_printv(3, "[ntrickle] Delaying %lds%ldus", tv->tv_sec, tv->tv_usec);

		{
			double delay_s = tv->tv_sec + tv->tv_usec / 1000000.0;
			double rate_bps = (delay_s > 0.0) ? (double)(ssize_t)*len / delay_s : 0.0;
			safe_printv(3,
				    "[ntrickle] delay %s: "
				    "%ld.%06lds for %zd bytes → %.0f B/s "
				    "(limit=%lu grant=%lu)",
				    (which == TRICKLE_SEND) ? "SEND" : "RECV", tv->tv_sec,
				    tv->tv_usec, (ssize_t)*len, rate_bps, (unsigned long)lim[which],
				    (unsigned long)(trickled ? ((which == TRICKLE_SEND)
									? ntrickled_grant_send
									: ntrickled_grant_recv)
							     : 0));
		}

		// In daemon mode, return EAGAIN on non-blocking sockets so the
		// application can retry after the daemon updates the grant.  In
		// standalone mode there is no external grant source — sleep
		// for the computed delay so the limit is actually enforced.
		if (ISSET(sd->flags, TRICKLE_NONBLOCK) && trickled)
			return (TRICKLE_WOULDBLOCK);

		while (nanosleep(&ts, &rm) == -1 && errno == EINTR)
			ts = rm;
	}

	return (0);
}

struct timeval *
getdelay(struct sockdesc *sd, ssize_t *len, short which)
{
	uint64_t xlim = lim[which];

	if (*len < 0)
		*len = sd->data[which].lastlen;

	if (trickled) {
		uint64_t grant =
			(which == TRICKLE_SEND) ? ntrickled_grant_send : ntrickled_grant_recv;
		if (grant > 0)
			xlim = grant;

		static int poll_counter = 0;
		if (++poll_counter >= 100) {
			poll_counter = 0;
			ntrickled_poll_grant();
		}
	}

	if (xlim == 0)
		return (NULL);

	{
		size_t xlen = (size_t)(*len > 0 ? *len : 0);
		struct timeval *tv = bwstat_getdelay(sd->stat, &xlen, xlim, which);
		*len = (ssize_t)xlen;
		return tv;
	}
}

void
update(int sock, ssize_t len, short which)
{
	struct sockdesc *sd;

	sd = state_find(sock);
	if (sd == NULL)
		return;

	updatesd(sd, len, which);
}

void
updatesd(struct sockdesc *sd, ssize_t len, short which)
{
	struct bwstat_data *bsd;
	int ret;

	if (len < 0)
		len = 0;

	if ((ret = fcntl(sd->sock, F_GETFL, 0)) != -1) {
		if (ret & O_NONBLOCK)
			SET(sd->flags, TRICKLE_NONBLOCK);
		else
			CLR(sd->flags, TRICKLE_NONBLOCK);
	}

	if (len > 0)
		sd->data[which].lastlen = len;

	if (trickled)
		ntrickled_update(which, len);

	bwstat_update(sd->stat, len, which);

	bsd = &sd->stat->data[which];

	safe_printv(3, "[ntrickle] avg: %lu.%lu KB/s; win: %lu.%lu KB/s",
		    (unsigned long)(bsd->rate / 1024),
		    (unsigned long)((bsd->rate % 1024) * 100 / 1024),
		    (unsigned long)(bsd->winrate / 1024),
		    (unsigned long)((bsd->winrate % 1024) * 100 / 1024));
}
