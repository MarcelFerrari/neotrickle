/*
 * SPDX-FileCopyrightText: 2026 Marcel Ferrari
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * limiter.h — Bandwidth limiting for the preload
 */

#ifndef TRICKLE_LIMITER_H
#define TRICKLE_LIMITER_H

#include "state.h"
#include <sys/types.h>

int
delay(int sock, ssize_t *len, short which);
struct timeval *
getdelay(struct sockdesc *sd, ssize_t *len, short which);
void
update(int sock, ssize_t len, short which);
void
updatesd(struct sockdesc *sd, ssize_t len, short which);

#endif /* TRICKLE_LIMITER_H */
