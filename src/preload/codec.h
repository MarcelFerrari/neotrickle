/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * codec.h — Manual XDR-compatible message serialization (no librpc dependency)
 */
#ifndef TRICKLE_CODEC_H
#define TRICKLE_CODEC_H

#include <stdint.h>
#include <sys/types.h>

struct msg;

int
codec_encode(const struct msg *m, unsigned char *buf, uint32_t *buflen);
int
codec_decode(struct msg *m, const unsigned char *buf, uint32_t buflen);

#endif /* TRICKLE_CODEC_H */
