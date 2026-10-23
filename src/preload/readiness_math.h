/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * readiness_math.h — Pure timeout arithmetic for the select/poll wrappers.
 *
 * Kept separate so the arithmetic can be unit-tested without loading the
 * LD_PRELOAD library.
 */

#ifndef NTRICKLE_READINESS_MATH_H
#define NTRICKLE_READINESS_MATH_H

#include <limits.h>
#include <sys/time.h>

/* Convert a poll(2) timeout in milliseconds to a timeval. */
static inline void
ntrickle_poll_timeout_to_tv(int timeout_ms, struct timeval *tv)
{
	tv->tv_sec = timeout_ms / 1000;
	tv->tv_usec = (timeout_ms % 1000) * 1000;
}

/*
 * Compute initial - elapsed and clamp negative results to zero.  The result
 * is always a non-negative timeval, even if a broken clock made elapsed
 * negative or larger than initial.
 */
static inline void
ntrickle_remaining_timeout(const struct timeval *initial, const struct timeval *elapsed,
			   struct timeval *remaining)
{
	long sec = initial->tv_sec - elapsed->tv_sec;
	long usec = initial->tv_usec - elapsed->tv_usec;

	if (usec < 0) {
		--sec;
		usec += 1000000;
	}
	if (sec < 0) {
		sec = 0;
		usec = 0;
	}

	remaining->tv_sec = sec;
	remaining->tv_usec = usec;
}

/* Reduce remaining to cap when remaining is larger. */
static inline void
ntrickle_cap_timeout(struct timeval *remaining, const struct timeval *cap)
{
	if (remaining->tv_sec > cap->tv_sec ||
	    (remaining->tv_sec == cap->tv_sec && remaining->tv_usec > cap->tv_usec)) {
		*remaining = *cap;
	}
}

/* Convert a timeval back to a poll(2) millisecond timeout, saturating at INT_MAX. */
static inline int
ntrickle_timeout_to_poll_ms(const struct timeval *tv)
{
	long ms;

	if (tv->tv_sec < 0)
		return 0;
	if (tv->tv_sec > INT_MAX / 1000)
		return INT_MAX;

	ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
	if (ms > INT_MAX)
		return INT_MAX;
	return (int)ms;
}

#endif /* NTRICKLE_READINESS_MATH_H */
