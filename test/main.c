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
extern test_fail_t *llvm_fail;

static uint64_t total;
static uint64_t success;

static void
run_test_list(test_list_t *tl)
{
	char *buf = NULL;
	uint64_t l_total = 0;
	uint64_t l_success = 0;

	(void) printf("# Test: %s\n", tl->desc);

	for (size_t i = 0; i < tl->ntests; i++) {
		char *result = sysdemangle(tl->tests[i].mangled,
		    SYSDEM_LANG_CPP, NULL, &buf);

		if (result == NULL ||
		    strcmp(result, tl->tests[i].demangled) != 0) {
			(void) printf("%zu failed:\n", i + 1);
			(void) printf("      mangled name: %s\n",
			    tl->tests[i].mangled);
			(void) printf("  demangled result: ");
			if (result != NULL) {
				(void) printf("%s\n", result);
			} else {
				(void) printf("error: %s\n", strerror(errno));
			}
			(void) printf("          expected: %s\n",
			    tl->tests[i].demangled);
			if (buf != NULL)
				(void) printf("%s\n", buf);
		} else {
			l_success++;
		}

		free(result);
		free(buf);
		buf = NULL;
		l_total++;
	}

	(void) printf("# Result: %" PRIu64 "/%" PRIu64 "\n\n",
	    l_success, l_total);

	total += l_total;
	success += l_success;
}

static void
run_fail(test_fail_t *fail)
{
	(void) printf("# %s\n", fail->desc);

	for (size_t i = 0; i < fail->n; i++) {
		errno = 0;

		(void) printf("Fail test %zu: ", i);
		char *res = sysdemangle(fail->names[i], SYSDEM_LANG_CPP,
		    NULL, NULL);

		if (res != NULL) {
			(void) printf("FAIL\n\t%s\n", fail->names[i]);
		} else {
			(void) printf("PASS\n");
		}
	}
}

int
main(int argc, const char * argv[]) {
	run_test_list(gcc_libstdc);
	run_test_list(llvm_pass_list);

	run_fail(llvm_fail);

	(void) printf("Total: %" PRIu64 "/%" PRIu64 "\n", success, total);
	return ((success == total) ? 0 : 1);
}
