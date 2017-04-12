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
#include "sysdemangle_int.h"
#include "tests.h"

extern test_list_t *gcc_libstdc;
extern test_list_t *llvm_pass_list;
extern test_fail_t *llvm_fail;
extern test_fp_t *llvm_fp;

static uint64_t total;
static uint64_t success;

static void
run_test_list(test_list_t *tl)
{
	uint64_t l_total = 0;
	uint64_t l_success = 0;

	(void) printf("# Test: %s\n", tl->desc);

	for (size_t i = 0; i < tl->ntests; i++) {
		char *result = sysdemangle(tl->tests[i].mangled,
		    SYSDEM_LANG_CPP, NULL);

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

static void
run_fail(test_fail_t *fail)
{
	uint64_t l_total = 0;
	uint64_t l_success = 0;

	(void) printf("# %s\n", fail->desc);

	for (size_t i = 0; i < fail->n; i++) {
		errno = 0;

		(void) printf("Fail test %zu: ", i);
		char *res = sysdemangle(fail->names[i], SYSDEM_LANG_CPP, NULL);

		if (res != NULL) {
			(void) printf("FAIL\n\t%s\n", fail->names[i]);
		} else {
			(void) printf("PASS\n");
			l_success++;
		}

		l_total++;
		free(res);
	}

	total += l_total;
	success += l_success;
}

static void
run_fp(test_fp_t *fp)
{
	uint64_t l_total = 0;
	uint64_t l_success = 0;

	(void) printf("# %s\n", fp->desc);

	for (size_t i = 0; i < fp->n; i++) {
		errno = 0;

		char *res = sysdemangle(fp->cases[i].mangled, SYSDEM_LANG_CPP,
		    NULL);

		l_total++;

		if (res == NULL) {
			(void) printf("  mangled: %s\n", fp->cases[i].mangled);
			(void) printf("  FAILED\n");
			continue;
		}

		boolean_t ok = B_FALSE;
		for (size_t j = 0; j < 4; j++) {
			if (strcmp(res, fp->cases[i].demangled[j]) == 0) {
				ok = B_TRUE;
				break;
			}
		}

		if (ok) {
			l_success++;
			continue;
		}

		(void) printf("   mangled: %s\n", fp->cases[i].mangled);
		for (size_t j = 0; j < 4; j++) {
			(void) printf("   exp[%zu]: %s\n", j,
			    fp->cases[i].demangled[j]);
		}
		(void) fputc('\n', stdout);
	}

	total += l_total;
	success += l_success;
}

int
main(int argc, const char * argv[]) {
	run_test_list(gcc_libstdc);
	run_test_list(llvm_pass_list);

	run_fail(llvm_fail);
	run_fp(llvm_fp);

	(void) printf("Total: %" PRIu64 "/%" PRIu64 "\n", success, total);
	return ((success == total) ? 0 : 1);
}
