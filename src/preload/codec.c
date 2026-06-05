/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * codec.c — Manual XDR-compatible message serialization (protocol v3, 64-bit).
 *
 * Produces the identical wire format as the daemon's MessageCodec (C++).
 * Verified by test_message_codec cross-test.
 */

#include "codec.h"
#include "message.h"

#include <arpa/inet.h>
#include <string.h>

/* Wire sizes for each message type (XDR payload only, no framing) */
#define CODEC_CONF_SIZE 296  /* 8 header + 288 data */
#define CODEC_GRANT_SIZE 24  /* 8 header + 16 data */
#define CODEC_UPDATE_SIZE 16 /* 8 header + 8 data */

static void
write_u32(unsigned char *out, uint32_t pos, uint32_t val)
{
	uint32_t be = htonl(val);
	memcpy(out + pos, &be, 4);
}

static uint32_t
read_u32(const unsigned char *in, uint32_t pos)
{
	uint32_t be;
	memcpy(&be, in + pos, 4);
	return ntohl(be);
}

static void
write_u64(unsigned char *out, uint32_t pos, uint64_t val)
{
	/* XDR hyper: big-endian 64-bit: high 4 bytes first, then low 4 bytes */
	uint32_t hi = htonl((uint32_t)(val >> 32));
	uint32_t lo = htonl((uint32_t)(val & 0xFFFFFFFFULL));
	memcpy(out + pos, &hi, 4);
	memcpy(out + pos + 4, &lo, 4);
}

static uint64_t
read_u64(const unsigned char *in, uint32_t pos)
{
	uint32_t hi, lo;
	memcpy(&hi, in + pos, 4);
	memcpy(&lo, in + pos + 4, 4);
	return ((uint64_t)ntohl(hi) << 32) | ntohl(lo);
}

static void
write_i32(unsigned char *out, uint32_t pos, int32_t val)
{
	write_u32(out, pos, (uint32_t)val);
}

static int32_t
read_i32(const unsigned char *in, uint32_t pos)
{
	return (int32_t)read_u32(in, pos);
}

static void
write_short(unsigned char *out, uint32_t pos, short val)
{
	/* XDR short is encoded as 4-byte int */
	write_i32(out, pos, (int32_t)val);
}

static short
read_short(const unsigned char *in, uint32_t pos)
{
	return (short)read_i32(in, pos);
}

int
codec_encode(const struct msg *m, unsigned char *buf, uint32_t *buflen)
{
	uint32_t size;
	unsigned char *p = buf;

	switch (m->type) {
	case MSG_TYPE_CONF:
		size = CODEC_CONF_SIZE;
		if (*buflen < size)
			return -1;
		write_short(p, 0, m->status);
		write_i32(p, 4, (int32_t)m->type);
		write_u64(p, 8, m->data.conf.lim[0]);
		write_u64(p, 16, m->data.conf.lim[1]);
		write_i32(p, 24, m->data.conf.pid);
		memcpy(p + 28, m->data.conf.argv0, 256);
		write_u32(p, 284, (uint32_t)m->data.conf.uid);
		write_u32(p, 288, (uint32_t)m->data.conf.gid);
		write_u32(p, 292, m->data.conf.weight);
		break;

	case MSG_TYPE_GRANT:
		size = CODEC_GRANT_SIZE;
		if (*buflen < size)
			return -1;
		write_short(p, 0, m->status);
		write_i32(p, 4, (int32_t)m->type);
		write_u64(p, 8, m->data.grant.grant[TRICKLE_SEND]);
		write_u64(p, 16, m->data.grant.grant[TRICKLE_RECV]);
		break;

	case MSG_TYPE_UPDATE:
		size = CODEC_UPDATE_SIZE;
		if (*buflen < size)
			return -1;
		write_short(p, 0, m->status);
		write_i32(p, 4, (int32_t)m->type);
		write_u32(p, 8, (uint32_t)m->data.update.len);
		write_short(p, 12, m->data.update.dir);
		break;

	default:
		return -1;
	}

	*buflen = size;
	return 0;
}

int
codec_decode(struct msg *m, const unsigned char *buf, uint32_t buflen)
{
	memset(m, 0, sizeof(*m));

	m->status = read_short(buf, 0);
	m->type = (enum msgtype)read_i32(buf, 4);

	switch (m->type) {
	case MSG_TYPE_CONF:
		if (buflen < CODEC_CONF_SIZE)
			return -1;
		m->data.conf.lim[0] = read_u64(buf, 8);
		m->data.conf.lim[1] = read_u64(buf, 16);
		m->data.conf.pid = (pid_t)read_i32(buf, 24);
		memcpy(m->data.conf.argv0, buf + 28, 256);
		m->data.conf.uid = (uid_t)read_u32(buf, 284);
		m->data.conf.gid = (gid_t)read_u32(buf, 288);
		m->data.conf.weight = read_u32(buf, 292);
		break;

	case MSG_TYPE_GRANT:
		if (buflen < CODEC_GRANT_SIZE)
			return -1;
		m->data.grant.grant[TRICKLE_SEND] = read_u64(buf, 8);
		m->data.grant.grant[TRICKLE_RECV] = read_u64(buf, 16);
		break;

	case MSG_TYPE_UPDATE:
		if (buflen < CODEC_UPDATE_SIZE)
			return -1;
		m->data.update.len = (size_t)read_u32(buf, 8);
		m->data.update.dir = read_short(buf, 12);
		break;

	default:
		return -1;
	}

	return 0;
}
