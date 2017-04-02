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

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "sysdemangle.h"
#include "tests.h"

extern test_list_t *gcc_libstdc;
extern test_list_t *llvm_pass_list;

static uint64_t total;
static uint64_t success;

static void
run_test_list(test_list_t *tl)
{
	uint64_t l_total = 0;
	uint64_t l_success = 0;

	(void) printf("# Test: %s\n", tl->desc);

	for (size_t i = 0; i < tl->ntests; i++) {
		char *result = sysdemangle(tl->tests[i].mangled, NULL);

		if (result == NULL ||
		    strcmp(result, tl->tests[i].demangled) != 0) {
			(void) printf("%zu failed:\n", i + 1);
			(void) printf("     mangled named: %s\n",
			    tl->tests[i].mangled);
			(void) printf("  demangled result: ");
			if (result != NULL) {
				(void) printf("%s\n", result);
			} else {
				(void) printf("error: %s\n", strerror(errno));
			}
			(void) fputc('\n', stdout);
		} else {
			l_success++;
		}

		free(result);
		l_total++;
	}

	(void) printf("# Result: %" PRIu64 "/%" PRIu64 "\n\n",
	    l_success, l_total);

	total += l_total;
	success += l_success;
}

int
main(int argc, const char * argv[]) {
	run_test_list(gcc_libstdc);
	run_test_list(llvm_pass_list);

	(void) printf("Total: %" PRIu64 "/%" PRIu64 "\n", success, total);
	return ((success == total) ? 0 : 1);
}
