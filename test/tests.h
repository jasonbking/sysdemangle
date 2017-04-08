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
#ifndef _TESTS_H
#define _TESTS_H

#include <sys/types.h>

typedef struct test_s {
	const char *mangled;
	const char *demangled;
} test_t;

typedef struct test_list_s {
	const char	*desc;
	test_t		*tests;
	size_t		ntests;
} test_list_t;

typedef struct test_fail_s {
	const char *desc;
	const char **names;
	size_t n;
} test_fail_t;

#endif /* _TESTS_H */
