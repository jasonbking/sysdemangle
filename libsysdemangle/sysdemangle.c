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
#include <errno.h>
#include "sysdemangle.h"
#include "util.h"

extern char *cpp_demangle(const char *, sysdem_ops_t *, char **);

static sysdem_lang_t
detect_lang(const char *str)
{
	size_t n = strlen(str);

	if (n < 3 || str[0] != '_')
		return (SYSDEM_LANG_AUTO);

	switch (str[1]) {
	case 'Z':
		return (SYSDEM_LANG_CPP);

	case '_':
		break;

	default:
		return (SYSDEM_LANG_AUTO);
	}

	/* why they use ___Z sometimes is puzzling.. *sigh* */
	if (str[2] == '_' && str[3] == 'Z')
		return (SYSDEM_LANG_CPP);

	return (SYSDEM_LANG_AUTO);
}

char *
sysdemangle(const char *str, sysdem_lang_t lang, sysdem_ops_t *ops, char **dbg)
{

	if (ops == NULL)
		ops = sysdem_ops_default;

	if (lang == SYSDEM_LANG_AUTO) {
		lang = detect_lang(str);
		if (lang == SYSDEM_LANG_AUTO) {
			errno = ENOSYS;
			return (NULL);
		}
	}

	switch (lang) {
	case SYSDEM_LANG_CPP:
		return (cpp_demangle(str, ops, dbg));

	default:
		break;
	}

	/* XXX: better return value? */
	errno = ENOSYS;
	return (NULL);
}

