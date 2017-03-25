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
#include "str.h"
#include "util.h"

#define STR_CHUNK_SZ	(64U)

/*
 * Dynamically resizeable strings, with lazy allocation when initialized
 * with a constant string value
 *
 * NOTE: these are not necessairly 0-terminated
 */

str_t *
str_init(str_t *restrict s, sysdem_alloc_t *restrict ops, const char *cstr,
	 size_t cstr_len)
{
	(void) memset(s, 0, sizeof (*s));

	s->str_ops = ops;

	if (cstr != NULL) {
		if (cstr_len == 0)
			cstr_len = strlen(cstr);

		ASSERT3U(strlen(cstr), >=, cstr_len);
		s->str_s = (char *)cstr;
		s->str_len = cstr_len;
		s->str_size = 0;
	}

	return (s);
}

void str_fini(str_t *s)
{
	if (s == NULL)
		return;

	if (s->str_size > 0)
		sysdemfree(s->str_ops, s->str_s, s->str_size);

	(void) memset(s, 0, sizeof (*s));
}

size_t
str_length(const str_t *s)
{
	return s->str_len;
}

static boolean_t
str_reserve(str_t *s, size_t amt)
{
	size_t newlen = s->str_len + amt;

	if (s->str_len + amt <= s->str_size)
		return (B_TRUE);

	size_t newsize = roundup(newlen, STR_CHUNK_SZ);
	void *temp = zalloc(s->str_ops, newsize);

	if (temp == NULL)
		return (B_FALSE);

	(void) memcpy(temp, s->str_s, s->str_len);
	if (s->str_size > 0)
		sysdemfree(s->str_ops, s->str_s, s->str_size);

	s->str_s = temp;
	s->str_size = newsize;

	return (B_TRUE);
}

boolean_t
str_append(str_t *s, const char *cstr, size_t cstrlen)
{
	if (cstr != NULL && cstrlen == 0)
		cstrlen = strlen(cstr);

	str_t src = {
		.str_s = (char *)cstr,
		.str_len = cstrlen,
		.str_ops = s->str_ops
	};

	return (str_append_str(s, &src));
}

boolean_t
str_append_str(str_t *s, str_t *src)
{
	if (src->str_s == NULL || src->str_len == 0)
		return (B_TRUE);

	if (s->str_len == 0 && src->str_size == 0) {
		sysdemfree(s->str_ops, s->str_s, s->str_size);
		s->str_s = src->str_s;
		s->str_len = src->str_len;
		s->str_size = 0;
		return (B_TRUE);
	}

	if (!str_reserve(s, src->str_len))
		return (B_FALSE);

	(void) memcpy(s->str_s + s->str_len, src->str_s, src->str_len);
	s->str_len += src->str_len;
	return (B_TRUE);
}

boolean_t
str_append_c(str_t *s, int c)
{
	if (!str_reserve(s, 1))
		return (B_FALSE);
	s->str_s[s->str_len++] = c;
	return (B_TRUE);
}

boolean_t
str_insert(str_t *s, size_t idx, const char *cstr, size_t cstrlen)
{
	if (cstr == NULL)
		return (B_TRUE);

	if (cstrlen == 0)
		cstrlen = strlen(cstr);

	str_t src = {
		.str_s = (char *)cstr,
		.str_len = cstrlen,
		.str_ops = s->str_ops
	};

	return (str_insert_str(s, idx, &src));
}

boolean_t
str_insert_str(str_t *s, size_t idx, str_t *src)
{
	if (s->str_len == 0 && src->str_size == 0 && idx == 0) {
		sysdemfree(s->str_ops, s->str_s, s->str_size);
		s->str_s = src->str_s;
		s->str_len = src->str_len;
		s->str_size = 0;
		return (B_TRUE);
	}

	if (!str_reserve(s, src->str_len))
		return (B_FALSE);

	/* unlike some programmers, *I* can read manpages */
	(void) memmove(s->str_s + idx + src->str_len, s->str_s + idx,
	    src->str_len);
	(void) memcpy(s->str_s + idx, src->str_s, src->str_len);
	s->str_len += src->str_len;
	return (B_TRUE);
}

str_pair_t *
str_pair_init(str_pair_t *sp, sysdem_alloc_t *ops)
{
	(void) memset(sp, 0, sizeof (*sp));
	str_init(&sp->strp_l, ops, NULL, 0);
	str_init(&sp->strp_r, ops, NULL, 0);
	return (sp);
}

void
str_pair_fini(str_pair_t *sp) {
	str_fini(&sp->strp_l);
	str_fini(&sp->strp_r);
}

/* combine left and right parts and put result into left part */
boolean_t
str_pair_merge(str_pair_t *sp)
{
	/* if right side is empty, don't need to do anything */
	if (str_length(&sp->strp_r) == 0)
		return (B_TRUE);

	/* if left side is empty, just move right to left */
	if (str_length(&sp->strp_l) == 0) {
		str_fini(&sp->strp_l);
		sp->strp_l = sp->strp_r;
		sp->strp_r.str_s = NULL;
		sp->strp_r.str_len = sp->strp_r.str_size = 0;
		return (B_TRUE);
	}

	if (!str_append_str(&sp->strp_l, &sp->strp_r))
		return (B_FALSE);

	str_fini(&sp->strp_r);
	return (B_TRUE);
}
