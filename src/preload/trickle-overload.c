/*
 * Copyright (c) 2002, 2003 Marius Aamodt Eriksen <marius@monkey.org>
 * All rights reserved.
 *
 * $Id: trickle-overload.c,v 1.38 2004/02/13 06:11:21 marius Exp $
 */

/*
 * Modifications Copyright (C) 2026 Marcel Ferrari
 *
 * Restructured and rewritten for ntrickle: symbol resolution, bootstrap,
 * and diagnostics remain; delay/update logic moved to separate files.
 * This modified file is distributed as part of ntrickle under
 * GPL-3.0-or-later. The original BSD copyright is retained above and the
 * BSD-3-Clause license text is provided in LICENSES/BSD-3-Clause.txt.
 */

/*
 * trickle-overload.c — Preload bootstrap, symbols, and diagnostics
 */

#define _GNU_SOURCE

#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <netinet/in.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if defined(HAVE_TIME_H) && defined(TIME_WITH_SYS_TIME)
#include <time.h>
#endif
#include <pwd.h>
#include <stdarg.h>
#include <string.h>
#include <syslog.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include "bwstat.h"
#include "limiter.h"
#include "message.h"
#include "ntrickledu.h"
#include "state.h"
#include "util.h"

/* --- Symbol resolution --- */

#define DECLARE(name, ret, args) ret(*libc_##name) args

DECLARE(socket, int, (int, int, int));
DECLARE(close, int, (int));
DECLARE(read, ssize_t, (int, void *, size_t));
DECLARE(recv, ssize_t, (int, void *, size_t, int));
DECLARE(readv, ssize_t, (int, const struct iovec *, int));
DECLARE(recvfrom, ssize_t, (int, void *, size_t, int, struct sockaddr *, socklen_t *));
DECLARE(write, ssize_t, (int, const void *, size_t));
DECLARE(send, ssize_t, (int, const void *, size_t, int));
DECLARE(writev, ssize_t, (int, const struct iovec *, int));
DECLARE(sendto, ssize_t, (int, const void *, size_t, int, const struct sockaddr *, socklen_t));
DECLARE(select, int, (int, fd_set *, fd_set *, fd_set *, struct timeval *));
DECLARE(poll, int, (struct pollfd *, int, int));
DECLARE(accept, int, (int, struct sockaddr *, socklen_t *));
DECLARE(dup, int, (int));
DECLARE(dup2, int, (int, int));
#ifdef HAVE_SENDFILE
DECLARE(sendfile, ssize_t, (int, int, off_t *, size_t));
#endif
#ifdef HAVE_SENDFILE64
DECLARE(sendfile64, ssize_t, (int, int, off64_t *, size_t));
#endif

/* Forward declaration — safe_printv is defined later in this file. */
void
safe_printv(int level, const char *fmt, ...);

#define errx(l, fmt, arg...)                                                                       \
	do {                                                                                       \
		safe_printv(0, "[FATAL] " fmt, ##arg);                                             \
		exit(l);                                                                           \
	} while (0)

#ifdef DL_NEED_UNDERSCORE
#define UNDERSCORE "_"
#else
#define UNDERSCORE ""
#endif

#define GETADDR(x)                                                                                 \
	do {                                                                                       \
		if ((libc_##x = dlsym(dh, UNDERSCORE #x)) == NULL)                                 \
			errx(0, "[ntrickle] Failed to get " #x "() address");                      \
	} while (0)

/* --- Bootstrap --- */

void
trickle_init(void)
{
	void *dh;
	char *winszstr, *verbosestr, *recvlimstr, *sendlimstr, *sockname, *tsmoothstr, *lsmoothstr,
		*weightstr;

	initializing = 1;

#ifdef NODLOPEN
	dh = (void *)-1L;
#else
	if ((dh = dlopen(DLOPENLIBC, RTLD_LAZY)) == NULL)
		errx(1, "[ntrickle] Failed to open libc");
#endif

	GETADDR(write);
	GETADDR(socket);
	GETADDR(close);
	GETADDR(read);
	GETADDR(readv);
#ifndef __FreeBSD__
	GETADDR(recv);
#endif
	GETADDR(recvfrom);
	GETADDR(writev);
#ifndef __FreeBSD__
	GETADDR(send);
#endif
	GETADDR(sendto);
	GETADDR(select);
	GETADDR(dup);
	GETADDR(dup2);
	GETADDR(accept);
#ifdef HAVE_SENDFILE
	GETADDR(sendfile);
#endif
#ifdef HAVE_SENDFILE64
	GETADDR(sendfile64);
#endif
	GETADDR(poll);

	if ((winszstr = getenv("NTRICKLE_WINDOW_SIZE")) == NULL)
		errx(1, "[ntrickle] Failed to get window size");
	if ((recvlimstr = getenv("NTRICKLE_DOWNLOAD_LIMIT")) == NULL)
		errx(1, "[ntrickle] Failed to get limit");
	if ((sendlimstr = getenv("NTRICKLE_UPLOAD_LIMIT")) == NULL)
		errx(1, "[ntrickle] Failed to get limit");
	if ((verbosestr = getenv("NTRICKLE_VERBOSE")) == NULL)
		verbosestr = "0";
	if ((argv0 = getenv("NTRICKLE_ARGV")) == NULL)
		errx(1, "[ntrickle] Failed to get argv");
	if ((sockname = getenv("NTRICKLE_SOCKNAME")) == NULL)
		errx(1, "[ntrickle] Failed to get socket name");
	if ((tsmoothstr = getenv("NTRICKLE_TSMOOTH")) == NULL)
		errx(1, "[ntrickle] Failed to get time smoothing parameter");
	if ((lsmoothstr = getenv("NTRICKLE_LSMOOTH")) == NULL)
		errx(1, "[ntrickle] Failed to get length smoothing parameter");
	if ((weightstr = getenv("NTRICKLE_WEIGHT")) == NULL)
		errx(1, "[ntrickle] Failed to get weight");

	winsz = atoi(winszstr) * 1024;
	lim[TRICKLE_RECV] = strtoull(recvlimstr, NULL, 10);
	lim[TRICKLE_SEND] = strtoull(sendlimstr, NULL, 10);
	verbose = atoi(verbosestr);
	if ((tsmooth = strtod(tsmoothstr, (char **)NULL)) <= 0.0)
		errx(1, "[ntrickle] Invalid time smoothing parameter");
	lsmooth = atoi(lsmoothstr) * 1024;
	weight = (uint32_t)strtoul(weightstr, NULL, 10);
	if (weight == 0)
		weight = 1;

	TAILQ_INIT(&sdhead);

	ntrickled_configure(sockname, libc_socket, libc_read, libc_write, libc_close, argv0);
	ntrickled_open(&trickled);

	bwstat_init(winsz);

	safe_printv(1, "[ntrickle] Initialized");

	initialized = 1;
}

/* --- Diagnostics --- */

void
safe_printv(int level, const char *fmt, ...)
{
	va_list ap;
	char str[1024];
	int n;
	const char *tag;

	if (level > verbose)
		return;

	switch (level) {
	case 0:
		tag = "";
		break; // caller provides tag (ERROR/FATAL)
	case 1:
		tag = "[INFO] ";
		break;
	case 2:
		tag = "[INFO] ";
		break;
	case 3:
		tag = "[DEBUG] ";
		break;
	default:
		tag = "[TRACE] ";
		break;
	}

	va_start(ap, fmt);

	n = snprintf(str, sizeof(str), "%s: %s", argv0, tag);
	if (n < 0 || (size_t)n >= sizeof(str)) {
		// Truncated or error — zero the buffer to avoid writing past
		// the end on the subsequent vsnprintf.
		n = (int)(sizeof(str) - 1);
		str[n] = '\0';
	}

	if (fmt != NULL) {
		int n2 = vsnprintf(str + n, sizeof(str) - (size_t)n, fmt, ap);
		if (n2 >= 0)
			n += n2;
		else
			n = (int)(sizeof(str) - 1);
	}

	// Ensure NUL-termination even if vsnprintf filled the buffer.
	str[sizeof(str) - 1] = '\0';

	strncat(str, "\n", sizeof(str) - strlen(str) - 1);

	(*libc_write)(STDERR_FILENO, str, strlen(str));
	va_end(ap);
}
