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

#include <string.h>
#include "sysdemangle.h"

extern char *cpp_demangle(const char *, sysdem_ops_t *);

char *
sysdemangle(const char *str, sysdem_ops_t *ops)
{
	size_t n = strlen(str);

	if (n < 3)
		return (NULL);

	if (str[0] != '_')
		return (NULL);

	switch (str[1]) {
	case 'Z':
		return (cpp_demangle(str, ops));
	}

	return (NULL);
}

