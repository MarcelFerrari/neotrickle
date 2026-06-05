/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wrappers.c — I/O interception wrappers (read/write families)
 */

#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "limiter.h"
#include "state.h"
#include "util.h"

#define DECLARE(name, ret, args) extern ret(*libc_##name) args

DECLARE(read, ssize_t, (int, void *, size_t));
DECLARE(recv, ssize_t, (int, void *, size_t, int));
DECLARE(readv, ssize_t, (int, const struct iovec *, int));
DECLARE(recvfrom, ssize_t, (int, void *, size_t, int, struct sockaddr *, socklen_t *));

DECLARE(write, ssize_t, (int, const void *, size_t));
DECLARE(send, ssize_t, (int, const void *, size_t, int));
DECLARE(writev, ssize_t, (int, const struct iovec *, int));
DECLARE(sendto, ssize_t, (int, const void *, size_t, int, const struct sockaddr *, socklen_t));

#ifdef HAVE_SENDFILE
DECLARE(sendfile, ssize_t, (int, int, off_t *, size_t));
#endif

static ssize_t
size_to_ssize(size_t len)
{
	if (len > (size_t)SSIZE_MAX)
		return (SSIZE_MAX);
	return ((ssize_t)len);
}

/* Read family */

ssize_t
read(int fd, void *buf, size_t nbytes)
{
	ssize_t ret = -1;
	ssize_t xnbytes = size_to_ssize(nbytes);
	int eagain;

	trickle_lock();
	INIT;

	if (!(eagain = delay(fd, &xnbytes, TRICKLE_RECV) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_read)(fd, buf, (size_t)xnbytes);
		trickle_lock();
	}

	update(fd, ret, TRICKLE_RECV);
	trickle_unlock();

	if (eagain) {
		ret = -1;
		errno = EAGAIN;
	}

	return (ret);
}

ssize_t
readv(int fd, const struct iovec *iov, int iovcnt)
{
	size_t total = 0;
	ssize_t len;
	ssize_t ret = -1;
	int i, eagain;

	for (i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;
	len = size_to_ssize(total);

	trickle_lock();
	INIT;

	if (!(eagain = delay(fd, &len, TRICKLE_RECV) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_readv)(fd, iov, iovcnt);
		trickle_lock();
	}

	update(fd, ret, TRICKLE_RECV);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}

#ifndef __FreeBSD__
ssize_t
recv(int sock, void *buf, size_t len, int flags)
{
	ssize_t ret = -1;
	ssize_t xlen = size_to_ssize(len);
	int eagain;

	trickle_lock();
	INIT;

	if (!(eagain = delay(sock, &xlen, TRICKLE_RECV) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_recv)(sock, buf, (size_t)xlen, flags);
		trickle_lock();
	}

	update(sock, ret, TRICKLE_RECV);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}
#endif /* !__FreeBSD__ */

ssize_t
recvfrom(int sock, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
	ssize_t ret = -1;
	ssize_t xlen = size_to_ssize(len);
	int eagain;

	trickle_lock();
	INIT;

	if (!(eagain = delay(sock, &xlen, TRICKLE_RECV) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_recvfrom)(sock, buf, (size_t)xlen, flags, from, fromlen);
		trickle_lock();
	}

	update(sock, ret, TRICKLE_RECV);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}

/* Write family */

ssize_t
write(int fd, const void *buf, size_t len)
{
	ssize_t ret = -1;
	ssize_t xlen = size_to_ssize(len);
	int eagain;

	trickle_lock();
	INIT;

	if (!(eagain = delay(fd, &xlen, TRICKLE_SEND) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_write)(fd, buf, (size_t)xlen);
		trickle_lock();
	}

	update(fd, ret, TRICKLE_SEND);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}

ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
	size_t total = 0;
	ssize_t len;
	ssize_t ret = -1;
	int i, eagain;

	for (i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;
	len = size_to_ssize(total);

	trickle_lock();
	INIT;

	if (!(eagain = delay(fd, &len, TRICKLE_SEND) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_writev)(fd, iov, iovcnt);
		trickle_lock();
	}

	update(fd, ret, TRICKLE_SEND);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}

#ifndef __FreeBSD__
ssize_t
send(int sock, const void *buf, size_t len, int flags)
{
	ssize_t ret = -1;
	ssize_t xlen = size_to_ssize(len);
	int eagain;

	trickle_lock();
	INIT;

	if (!(eagain = delay(sock, &xlen, TRICKLE_SEND) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_send)(sock, buf, (size_t)xlen, flags);
		trickle_lock();
	}

	update(sock, ret, TRICKLE_SEND);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}
#endif /* !__FreeBSD__ */

ssize_t
sendto(int sock, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
	ssize_t ret = -1;
	ssize_t xlen = size_to_ssize(len);
	int eagain;

	trickle_lock();
	INIT;

	if (!(eagain = delay(sock, &xlen, TRICKLE_SEND) == TRICKLE_WOULDBLOCK)) {
		trickle_unlock();
		ret = (*libc_sendto)(sock, buf, (size_t)xlen, flags, to, tolen);
		trickle_lock();
	}

	update(sock, ret, TRICKLE_SEND);
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		ret = -1;
	}

	return (ret);
}

#ifdef HAVE_SENDFILE
ssize_t
sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	ssize_t inbytes = size_to_ssize(count), outbytes = size_to_ssize(count);
	size_t bytes;
	ssize_t ret = 0;
	int eagain = 0;

	trickle_lock();
	INIT;

	if (delay(in_fd, &inbytes, TRICKLE_RECV) == TRICKLE_WOULDBLOCK)
		eagain = 1;
	if (delay(out_fd, &outbytes, TRICKLE_SEND) == TRICKLE_WOULDBLOCK)
		eagain = 1;
	trickle_unlock();

	if (eagain) {
		errno = EAGAIN;
		return (-1);
	}

	bytes = (size_t)MIN(inbytes, outbytes);
	if (bytes > 0)
		ret = (*libc_sendfile)(out_fd, in_fd, offset, bytes);

	return (ret);
}
#endif /* HAVE_SENDFILE */
