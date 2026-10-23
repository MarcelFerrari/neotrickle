/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * state.c — Descriptor table, global state, and locking
 */

#ifdef HAVE_CONFIG_H
#include "config.h" /* HAVE_PTHREAD */
#endif

#include "bwstat.h"
#include "ntrickledu.h"
#include "state.h"

#ifdef HAVE_PTHREAD
#include <pthread.h>
#endif

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Libc function pointers (defined in trickle-overload.c) */
#define DECLARE(name, ret, args) extern ret(*libc_##name) args
DECLARE(socket, int, (int, int, int));
DECLARE(close, int, (int));
DECLARE(dup, int, (int));
DECLARE(dup2, int, (int, int));
DECLARE(accept, int, (int, struct sockaddr *, socklen_t *));

/* Global state definitions */
struct sockdeschead sdhead;
uint32_t winsz;
int verbose;
uint64_t lim[2];
char *argv0;
double tsmooth;
uint lsmooth;
uint32_t weight;
int trickled;
int initialized;
int initializing;

/* Locking */
#ifdef HAVE_PTHREAD
static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

void
trickle_lock(void)
{
	pthread_mutex_lock(&global_lock);
}
void
trickle_unlock(void)
{
	pthread_mutex_unlock(&global_lock);
}
#else
void
trickle_lock(void)
{
}
void
trickle_unlock(void)
{
}
#endif

/* Descriptor table */

struct sockdesc *
state_find(int fd)
{
	struct sockdesc *sd;
	TAILQ_FOREACH(sd, &sdhead, next)
	if (sd->sock == fd)
		return sd;
	return NULL;
}

struct sockdesc *
state_new(int fd)
{
	struct sockdesc *sd;
	if ((sd = calloc(1, sizeof(*sd))) == NULL)
		return NULL;
	if ((sd->stat = bwstat_new()) == NULL) {
		free(sd);
		return NULL;
	}
	sd->stat->lsmooth = lsmooth;
	sd->stat->tsmooth = tsmooth;
	sd->sock = fd;
	TAILQ_INSERT_TAIL(&sdhead, sd, next);
	return sd;
}

void
state_remove(int fd)
{
	struct sockdesc *sd, *next;
	for (sd = TAILQ_FIRST(&sdhead); sd != NULL; sd = next) {
		next = TAILQ_NEXT(sd, next);
		if (sd->sock == fd) {
			TAILQ_REMOVE(&sdhead, sd, next);
			bwstat_free(sd->stat);
			free(sd);
			return;
		}
	}
}

/* --- LD_PRELOAD interposition: descriptor-tracking wrappers --- */

int
socket(int domain, int type, int protocol)
{
	int sock;

	trickle_lock();
	INIT;
	trickle_unlock();

	sock = (*libc_socket)(domain, type, protocol);

#ifdef SOCK_NONBLOCK
	type &= ~SOCK_NONBLOCK;
#endif
#ifdef SOCK_CLOEXEC
	type &= ~SOCK_CLOEXEC;
#endif

	if (sock != -1 && (domain == AF_INET || domain == AF_INET6) && type == SOCK_STREAM) {
		struct sockdesc *sd;

		/*
		 * state_new() inserts into the global statistics queue.  Callers
		 * must hold the lock; socket() previously did this unlocked,
		 * which could corrupt the queue when another thread was limiting.
		 */
		trickle_lock();
		sd = state_new(sock);
		trickle_unlock();

		if (sd == NULL) {
			(*libc_close)(sock);
			return (-1);
		}
	}

	return (sock);
}

int
close(int fd)
{
	trickle_lock();
	INIT;

	state_remove(fd);

	if (fd == trickled) {
		ntrickled_close(&trickled);
		trickle_unlock();
		return (0);
	}
	trickle_unlock();

	return ((*libc_close)(fd));
}

int
dup(int oldfd)
{
	int newfd;

	trickle_lock();
	INIT;
	trickle_unlock();

	newfd = (*libc_dup)(oldfd);

	trickle_lock();
	struct sockdesc *sd = state_find(oldfd);
	if (sd != NULL && newfd != -1) {
		// Create a fresh descriptor entry with its own bwstat
		// rather than memcpy'ing the original (which would share
		// the bwstat pointer and TAILQ linkage).  If the original
		// is later closed, the dup'd descriptor remains valid.
		struct sockdesc *nsd = state_new(newfd);
		if (nsd == NULL) {
			trickle_unlock();
			(*libc_close)(newfd);
			return (-1);
		}
		// Carry forward the smoothing parameters and I/O state.
		nsd->stat->tsmooth = sd->stat->tsmooth;
		nsd->stat->lsmooth = sd->stat->lsmooth;
		nsd->stat->pts = sd->stat->pts;
		nsd->data[TRICKLE_SEND].lastlen = sd->data[TRICKLE_SEND].lastlen;
		nsd->data[TRICKLE_RECV].lastlen = sd->data[TRICKLE_RECV].lastlen;
	}
	trickle_unlock();

	return (newfd);
}

int
dup2(int oldfd, int newfd)
{
	trickle_lock();
	INIT;
	trickle_unlock();

	// If newfd is already tracked, remove it first (dup2 closes the
	// target before duplicating).
	trickle_lock();
	state_remove(newfd);
	trickle_unlock();

	int ret = (*libc_dup2)(oldfd, newfd);

	trickle_lock();
	struct sockdesc *sd = state_find(oldfd);
	if (sd != NULL && ret != -1) {
		struct sockdesc *nsd = state_new(newfd);
		if (nsd == NULL) {
			trickle_unlock();
			return (-1);
		}
		nsd->stat->tsmooth = sd->stat->tsmooth;
		nsd->stat->lsmooth = sd->stat->lsmooth;
		nsd->stat->pts = sd->stat->pts;
		nsd->data[TRICKLE_SEND].lastlen = sd->data[TRICKLE_SEND].lastlen;
		nsd->data[TRICKLE_RECV].lastlen = sd->data[TRICKLE_RECV].lastlen;
	}
	trickle_unlock();

	return (ret);
}

int
accept(int sock, struct sockaddr *addr, socklen_t *addrlen)
{
	trickle_lock();
	INIT;
	trickle_unlock();

	int ret = (*libc_accept)(sock, addr, addrlen);

	if (ret != -1) {
		trickle_lock();
		struct sockdesc *sd = state_new(ret);
		trickle_unlock();
		if (sd == NULL)
			return (ret);
	}

	return (ret);
}
