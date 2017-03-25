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

#include <stdlib.h>
#include <string.h>
#include "sysdemangle.h"
#include "util.h"

void *
zalloc(sysdem_alloc_t *ops, size_t len)
{
	return (ops->zalloc(len));
}

void
sysdemfree(sysdem_alloc_t *ops, void *p, size_t len)
{
	if (p == NULL || len == 0)
		return;

	ops->free(p, len);
}

void *
sysdem_realloc(sysdem_alloc_t *ops, void *p, size_t oldsz, size_t newsz)
{
	if (newsz == oldsz)
		return (p);
	ASSERT3U(newsz, >, oldsz);

	void *temp = zalloc(ops, newsz);

	if (temp == NULL)
		return (NULL);

	(void) memcpy(temp, p, oldsz);
	sysdemfree(ops, p, oldsz);

	return (temp);
}

static void *
def_zalloc(size_t len)
{
	return (calloc(1, len));
}

/*ARGSUSED*/
static void
def_free(void *p, size_t len)
{
	free(p);
}

sysdem_alloc_t *sysdem_alloc_default = &(sysdem_alloc_t){
	.zalloc = def_zalloc,
	.free = def_free
};
