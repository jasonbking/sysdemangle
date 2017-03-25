/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2017 Jason King
 */
#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>
#include "sysdemangle.h"

#ifdef __cplusplus
extern "C" {
#endif

void *zalloc(sysdem_alloc_t *, size_t);
void *sysdem_realloc(sysdem_alloc_t *, void *, size_t, size_t);
void sysdemfree(sysdem_alloc_t *, void *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* _UTIL_H */
