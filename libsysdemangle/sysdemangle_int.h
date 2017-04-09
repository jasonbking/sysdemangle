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
#ifndef _SYSDEMANGLE_INT_H
#define _SYSDEMANGLE_INT_H

#include <stdio.h>
#include "sysdemangle.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifndef __sun

/* compatibility bits */

#include <assert.h>

#define _LITTLE_ENDIAN 1

#define ASSERT(x) assert(x)
#define	VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE)	\
do {						\
	const TYPE __left = (TYPE)(LEFT);	\
	const TYPE __right = (TYPE)(RIGHT);	\
	assert(__left OP __right);		\
} while (0)

#define ASSERT3U(x, y, z) VERIFY3_IMPL(x, y, z, uint64_t)
#define VERIFY(x) assert(x)

typedef enum {
	B_FALSE,
	B_TRUE
	} boolean_t;

#define roundup(x, y) ((((x)+((y)-1))/(y))*(y))
#define ARRAY_SIZE(x) (sizeof (x) / sizeof (x[0]))

#else

#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/isa_defs.h>

#endif /* __sun */


extern sysdem_ops_t *sysdem_ops_default;

char *cpp_demangle(const char *, sysdem_ops_t *);

void *zalloc(sysdem_ops_t *, size_t);
void *xrealloc(sysdem_ops_t *, void *, size_t, size_t);
void xfree(sysdem_ops_t *, void *, size_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYSDEMANGLE_INT_H */
