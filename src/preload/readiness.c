/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * readiness.c — select() and poll() wrappers
 */

#include <poll.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/types.h>

#include "limiter.h"
#include "state.h"
#include "util.h"

#define DECLARE(name, ret, args) extern ret(*libc_##name) args
DECLARE(select, int, (int, fd_set *, fd_set *, fd_set *, struct timeval *));
DECLARE(poll, int, (struct pollfd *, int, int));

#ifndef INFTIM
#define INFTIM -1
#endif

#define POLL_WRMASK (POLLOUT | POLLWRNORM | POLLWRBAND)
#define POLL_RDMASK (POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND)

struct delay {
	struct sockdesc *sd;
	struct timeval delaytv;
	struct timeval abstv;
	short which;
	short pollevents;
	int pollidx;
	TAILQ_ENTRY(delay) next;
};

TAILQ_HEAD(delayhead, delay);

static struct delay *
select_delay(struct delayhead *dhead, struct sockdesc *sd, short which)
{
	ssize_t len = -1;
	struct timeval *delaytv;
	struct delay *d, *_d;

	/*
	 * Compute the delay BEFORE the zero-byte time update so that the
	 * rate used for the decision reflects actual I/O, not an
	 * artificially lowered value.  The zero-update still runs
	 * afterwards so the next poll/select cycle sees fresh timing.
	 */
	delaytv = getdelay(sd, &len, which);
	updatesd(sd, 0, which);

	if (delaytv == NULL)
		return (NULL);

	safe_printv(3,
		    "[ntrickle] Delaying socket (%s) %d "
		    "by %ld seconds %ld microseconds",
		    which == 0 ? "write" : "read", sd->sock, delaytv->tv_sec, delaytv->tv_usec);

	if ((d = calloc(1, sizeof(*d))) == NULL)
		return (NULL);

	gettimeofday(&d->abstv, NULL);
	d->delaytv = *delaytv;
	d->which = which;
	d->sd = sd;
	sd->data[which].selectlen = len;

	if (TAILQ_EMPTY(dhead))
		TAILQ_INSERT_HEAD(dhead, d, next);
	else {
		TAILQ_FOREACH(_d, dhead, next)
		if (timercmp(&d->delaytv, &_d->delaytv, <)) {
			TAILQ_INSERT_BEFORE(_d, d, next);
			break;
		}
		if (_d == NULL)
			TAILQ_INSERT_TAIL(dhead, d, next);
	}

	return (d);
}

static struct delay *
select_shift(struct delayhead *dhead, struct timeval *difftv, struct timeval **delaytv)
{
	struct delay *d;
	struct sockdesc *sd;

	TAILQ_FOREACH(d, dhead, next)
	{
		if (timercmp(&d->delaytv, difftv, >))
			break;
		sd = d->sd;
		updatesd(sd, 0, d->which);
		SET(sd->data[d->which].flags, SD_INSELECT);
	}

	if (d != NULL)
		timersub(&d->delaytv, difftv, *delaytv);
	else
		*delaytv = NULL;

	if (*delaytv != NULL && ((*delaytv)->tv_sec < 0 || (*delaytv)->tv_usec < 0))
		timerclear(*delaytv);

	return (d);
}

int
select(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *__timeout)
{
	struct sockdesc *sd;
	fd_set *fdsets[] = {wfds, rfds}, *fds;
	struct timeval *delaytv, _delaytv, *selecttv = NULL, *timeout = NULL, _timeout, inittv,
					   curtv, difftv;
	short which;
	struct delayhead dhead;
	struct delay *d, *_d;
	int ret;

	TAILQ_INIT(&dhead);

	if (__timeout != NULL) {
		_timeout = *__timeout;
		timeout = &_timeout;
	}

	trickle_lock();
	INIT;

	for (which = 0; which < 2; which++)
		TAILQ_FOREACH(sd, &sdhead, next)
	if ((fds = fdsets[which]) != NULL && FD_ISSET(sd->sock, fds) &&
	    select_delay(&dhead, sd, which)) {
		FD_CLR(sd->sock, fds);
	}

	gettimeofday(&inittv, NULL);
	curtv = inittv;
	d = TAILQ_FIRST(&dhead);
	if (d != NULL) {
		_delaytv = d->delaytv;
		delaytv = &_delaytv;
	} else
		delaytv = NULL;
	timersub(&curtv, &inittv, &difftv);
again:
	selecttv = NULL;

	if (delaytv != NULL)
		selecttv = delaytv;

	if (timeout != NULL) {
		timersub(timeout, &difftv, timeout);
		if (timeout->tv_sec < 0 || timeout->tv_usec < 0)
			timerclear(timeout);
		if (delaytv != NULL && timercmp(timeout, delaytv, <))
			selecttv = timeout;
		else if (delaytv == NULL)
			selecttv = timeout;
	}

	trickle_unlock();
	ret = (*libc_select)(nfds, rfds, wfds, efds, selecttv);
	trickle_lock();

	if (ret == 0 && delaytv != NULL && selecttv == delaytv) {
		gettimeofday(&curtv, NULL);
		timersub(&curtv, &inittv, &difftv);
		_d = select_shift(&dhead, &difftv, &delaytv);
		while ((d = TAILQ_FIRST(&dhead)) != _d) {
			FD_SET(d->sd->sock, fdsets[d->which]);
			TAILQ_REMOVE(&dhead, d, next);
			free(d);
		}
		goto again;
	}

	while ((d = TAILQ_FIRST(&dhead)) != NULL) {
		CLR(d->sd->data[d->which].flags, SD_INSELECT);
		TAILQ_REMOVE(&dhead, d, next);
		free(d);
	}
	trickle_unlock();

	return (ret);
}

#if defined(__linux__) || (defined(__svr4__) && defined(__sun__)) || defined(__OpenBSD__)
int
poll(struct pollfd *fds, nfds_t nfds, int __timeout)
#elif defined(__FreeBSD__)
int
poll(struct pollfd *fds, unsigned int nfds, int __timeout)
#else
int
poll(struct pollfd *fds, int nfds, int __timeout)
#endif
{
	struct pollfd *pfd;
	int polltimeout, ret;
	nfds_t i;
	struct sockdesc *sd;
	struct delay *d, *_d;
	struct timeval inittv, curtv, _timeout, *timeout = NULL, *delaytv, *polltv, difftv;
	struct delayhead dhead;

	if (__timeout != INFTIM) {
		_timeout.tv_sec = __timeout / 1000;
		_timeout.tv_usec = (__timeout % 1000) * 100;
		timeout = &_timeout;
	}

	TAILQ_INIT(&dhead);

	trickle_lock();
	INIT;

	for (i = 0; i < nfds; i++) {
		pfd = &fds[i];
		sd = state_find(pfd->fd);
		if (sd == NULL)
			continue;

		if (pfd->events & POLL_RDMASK &&
		    (d = select_delay(&dhead, sd, TRICKLE_RECV)) != NULL) {
			d->pollevents = pfd->events & POLL_RDMASK;
			d->pollidx = i;
			pfd->events &= ~POLL_RDMASK;
		}

		if (pfd->events & POLL_WRMASK &&
		    (d = select_delay(&dhead, sd, TRICKLE_SEND)) != NULL) {
			d->pollevents = pfd->events & POLL_WRMASK;
			d->pollidx = i;
			pfd->events &= ~POLL_WRMASK;
		}
	}

	gettimeofday(&inittv, NULL);
	curtv = inittv;
	d = TAILQ_FIRST(&dhead);
	delaytv = d != NULL ? &d->delaytv : NULL;
again:
	timersub(&curtv, &inittv, &difftv);
	polltv = NULL;

	if (delaytv != NULL)
		polltv = delaytv;

	if (timeout != NULL) {
		timersub(timeout, &difftv, timeout);
		if (delaytv != NULL && timercmp(timeout, delaytv, <))
			polltv = timeout;
		else if (delaytv == NULL)
			polltv = timeout;
	}

	if (polltv != NULL) {
		polltimeout = polltv->tv_sec * 1000 + polltv->tv_usec / 1000;
		safe_printv(3,
			    "[ntrickle] poll delay %ld.%06lds -> "
			    "timeout=%dms (nfds=%d, delayq=%s)",
			    polltv->tv_sec, polltv->tv_usec, polltimeout, (int)nfds,
			    delaytv ? "present" : "empty");
	} else
		polltimeout = INFTIM;

	trickle_unlock();
	ret = (*libc_poll)((struct pollfd *)fds, (int)nfds, (int)polltimeout);
	trickle_lock();
	if (ret == 0 && delaytv != NULL && polltv == delaytv) {
		_d = select_shift(&dhead, &difftv, &delaytv);
		while ((d = TAILQ_FIRST(&dhead)) != NULL && d != _d) {
			fds[d->pollidx].events |= d->pollevents;
			TAILQ_REMOVE(&dhead, d, next);
			free(d);
		}
		gettimeofday(&curtv, NULL);
		goto again;
	}

	while ((d = TAILQ_FIRST(&dhead)) != NULL) {
		CLR(d->sd->data[d->which].flags, SD_INSELECT);
		TAILQ_REMOVE(&dhead, d, next);
		free(d);
	}
	trickle_unlock();
	return (ret);
}
