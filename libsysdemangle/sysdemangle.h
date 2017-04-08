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

#ifndef _SYSDEMANGLE_H
#define	_SYSDEMANGLE_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __sun
#include <assert.h>

#define _LITTLE_ENDIAN 1

#define ASSERT(x) assert(x)
#define	VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE) \
	do {							\
		const TYPE __left = (TYPE)(LEFT);		\
		const TYPE __right = (TYPE)(RIGHT);		\
		assert(__left OP __right);			\
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

typedef enum sysdem_lang_e {
	SYSDEM_LANG_AUTO,
	SYSDEM_LANG_CPP
} sysdem_lang_t;

typedef struct sysdem_alloc_s {
	void *(*alloc)(size_t);
	void (*free)(void *, size_t);
} sysdem_ops_t;
	
char *sysdemangle(const char *, sysdem_lang_t, sysdem_ops_t *, char **);

#ifdef __cplusplus
}
#endif

#endif /* _SYSDEMANGLE_H */
