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
#include <stdlib.h>
#include "util.h"
#include "cpp.h"

#define CHUNK_SIZE  (8U)

/*
 * A name_t is essentially a stack of str_pair_t's.  Generally, the parsing
 * code will push (via name_add() or the like) portions of the demangled
 * name into a name_t, and periodically combine them via name_join().
 *
 * As such it should be noted that since items are added at the end of
 * name_t->nm_items, the numbering of name_at() starts at the end
 * of name_t->nm_items, i.e. name_at(n, 0) == &n->nm_items[n->nm_len - 1].
 *
 * It should also be noted that for name_t's, adding is a move operation in
 * that it takes ownership of the passed in string/str_t/etc
 */

void
name_init(name_t *n, sysdem_ops_t *ops)
{
	(void) memset(n, 0, sizeof (*n));
	n->nm_ops = (ops != NULL) ? ops : sysdem_ops_default;
}

void
name_fini(name_t *n)
{
	if (n == NULL)
		return;

	name_clear(n);
	xfree(n->nm_ops, n->nm_items, n->nm_size);
	n->nm_items = NULL;
	n->nm_size = 0;
}

size_t
name_len(const name_t *n)
{
	return (n->nm_len);
}

boolean_t
name_empty(const name_t *n)
{
	return (name_len(n) == 0 ? B_TRUE : B_FALSE);
}

void
name_clear(name_t *n)
{
	if (n == NULL)
		return;

	for (size_t i = 0; i < n->nm_len; i++) {
		str_pair_t *sp = &n->nm_items[i];
		sysdem_ops_t *ops = sp->strp_l.str_ops;

		str_pair_fini(sp);
		str_pair_init(sp, ops);
	}

	n->nm_len = 0;
}

static boolean_t
name_reserve(name_t *n, size_t amt)
{
	size_t newlen = n->nm_len + amt;

	if (newlen < n->nm_size)
		return (B_TRUE);

	size_t newsize = roundup(newlen, CHUNK_SIZE);
	void *temp = xrealloc(n->nm_ops, n->nm_items,
	    n->nm_size * sizeof (str_pair_t), newsize * sizeof (str_pair_t));

	if (temp == NULL)
		return (B_FALSE);

	n->nm_items = temp;
	n->nm_size = newsize;
	return (B_TRUE);
}

boolean_t
name_add(name_t *n, const char *l, size_t l_len, const char *r, size_t r_len)
{
	str_t sl = { 0 };
	str_t sr = { 0 };

	str_init(&sl, n->nm_ops);
	str_init(&sr, n->nm_ops);
	str_set(&sl, l, l_len);
	str_set(&sr, r, r_len);
	return (name_add_str(n, &sl, &sr));
}

boolean_t
name_add_str(name_t *n, str_t *l, str_t *r)
{
	str_pair_t sp;

	str_pair_init(&sp, n->nm_ops);

	if (!name_reserve(n, 1))
		return (B_FALSE);

	if (l != NULL) {
		sp.strp_l = *l;
		(void) memset(l, 0, sizeof (*l));
	}

	if (r != NULL) {
		sp.strp_r = *r;
		(void) memset(r, 0, sizeof (*r));
	}

	n->nm_items[n->nm_len++] = sp;

	return (B_TRUE);
}

str_pair_t *
name_at(name_t *n, size_t idx)
{
	if (n->nm_len == 0)
		return (NULL);

	ASSERT3U(idx, <=, n->nm_len);
	return (&n->nm_items[n->nm_len - idx - 1]);
}

str_pair_t *
name_top(name_t *n)
{
	return (name_at(n, 0));
}

str_pair_t *
name_pop(name_t *n, str_pair_t *sp)
{
	if (n->nm_len == 0)
		return (NULL);

	str_pair_t *top = name_top(n);

	if (sp != NULL) {
		*sp = *top;
		(void) memset(top, 0, sizeof (*top));
	} else {
		str_pair_fini(top);
	}

	n->nm_len--;
	return (sp);
}

boolean_t
name_join(name_t *n, size_t amt, const char *sep)
{
	str_pair_t *sp = NULL;
	str_t res = { 0 };
	size_t seplen = strlen(sep);

	ASSERT3U(amt, <=, n->nm_len);

	/* a join of 0 elements just implies merging the top str_pair_t */
	if (amt == 0) {
		if (n->nm_len > 0) {
			return (str_pair_merge(name_top(n)));
		}
		return (B_TRUE);
	}

	(void) str_init(&res, n->nm_ops);

	sp = name_at(n, n->nm_len - amt);
	for (size_t i = 0; i < amt; i++) {
		if (i > 0) {
			if (!str_append(&res, sep, seplen))
				goto error;
		}

		if (!str_append_str(&res, &sp->strp_l))
			goto error;
		if (!str_append_str(&res, &sp->strp_r))
			goto error;

		sp++;
	}

	for (size_t i = 0; i < amt; i++)
		(void) name_pop(n, NULL);

	/* since we've removed at least 1 entry, this should always succeed */
	VERIFY(name_add_str(n, &res, NULL));
	return (B_TRUE);

error:
	str_fini(&res);
	return (B_FALSE);
}

static boolean_t
name_fmt_s(name_t *n, str_t *s, const char *fmt, long *maxp)
{
	const char *p;
	long max = -1;

	if (fmt == NULL)
		return (B_TRUE);

	for (p = fmt; *p != '\0'; p++) {
		if (*p != '{') {
			str_append_c(s, *p);
			continue;
		}

		errno = 0;
		char *q = NULL;
		long val = strtol(p + 1, &q, 10);

		ASSERT(val != 0 || errno == 0);
		ASSERT3U(val, <, n->nm_len);

		str_pair_t *sp = name_at(n, val);

		if (val > max)
			max = val;

		switch (q[0]) {
		case '}':
			if (!str_append_str(s, &sp->strp_l))
				return (B_FALSE);
			if (!str_append_str(s, &sp->strp_r))
				return (B_FALSE);
			p = q;
			continue;
		case ':':
			switch (q[1]) {
			case 'L':
				if (!str_append_str(s, &sp->strp_l))
					return (B_FALSE);
				break;
			case 'R':
				if (!str_append_str(s, &sp->strp_r))
					return (B_FALSE);
				break;
			}

			p = q + 2;
			VERIFY(*p == '}');
			break;
		}
	}

	if (*maxp < max)
		*maxp = max;

	return (B_TRUE);
}

/*
 * replace a number of elements in the name stack with a formatted string
 * for format is a plain string with optional {nnn} or {nnn:L|R} substitutions
 * where nnn is the stack position of an element and it's contents (both
 * left and right pieces) are inserted.  Optionally, only the left or
 * right piece can specified using :L|R e.g. {2:L}{3}{2:R} would insert
 * the left piece of element 2, all of element 3, then the right piece of
 * element 2.
 *
 * Once complete, all elements up to the deepest one references are popped
 * off the stack, and the resulting formatted string is pushed into n.
 *
 * This could be done as a sequence of push & pops, but this makes the
 * intended output far clearer to see.
 */
boolean_t
name_fmt(name_t *n, const char *fmt_l, const char *fmt_r)
{
	str_pair_t res;
	long max = -1;

	(void) str_pair_init(&res, n->nm_ops);

	if (!name_reserve(n, 1))
		return (B_FALSE);

	if (!name_fmt_s(n, &res.strp_l, fmt_l, &max))
		goto error;
	if (!name_fmt_s(n, &res.strp_r, fmt_r, &max))
		goto error;

	if (max >= 0) {
		for (size_t i = 0; i <= max; i++)
			(void) name_pop(n, NULL);
	}

	n->nm_items[n->nm_len++] = res;
	return (B_TRUE);

error:
	str_pair_fini(&res);
	return (B_FALSE);
}

/*
 * The substitution list is a list of name_t's that get added as the
 * demangled name is parsed.  Adding a name_t to the substitution list
 * is a copy operation, and likewise inserting a substitution into a name_t
 * is also a copy operation.
 */
void
sub_init(sub_t *sub, sysdem_ops_t *ops)
{
	(void) memset(sub, 0, sizeof (*sub));
	sub->sub_ops = (ops != NULL) ? ops : sysdem_ops_default;
}

void
sub_fini(sub_t *sub)
{
	if (sub == NULL)
		return;

	sub_clear(sub);
	xfree(sub->sub_ops, sub->sub_items, sub->sub_size);
	sub->sub_items = NULL;
	sub->sub_size = 0;
}

void
sub_clear(sub_t *sub)
{
	for (size_t i = 0; i < sub->sub_len; i++) {
		sysdem_ops_t *ops = sub->sub_items[i].nm_ops;
		name_fini(&sub->sub_items[i]);
		name_init(&sub->sub_items[i], ops);
	}
	sub->sub_len = 0;
}

boolean_t
sub_empty(const sub_t *sub)
{
	return ((sub->sub_len == 0) ? B_TRUE : B_FALSE);
}

static boolean_t
sub_reserve(sub_t *sub, size_t amt)
{
	if (sub->sub_len + amt < sub->sub_size)
		return (B_TRUE);

	size_t newsize = roundup(sub->sub_size, CHUNK_SIZE);
	void *temp = xrealloc(sub->sub_ops, sub->sub_items,
	    sub->sub_size * sizeof (name_t), newsize * sizeof (name_t));

	if (temp == NULL)
		return (B_FALSE);

	for (size_t i = sub->sub_len; i < newsize; i++)
		name_init(&sub->sub_items[i], sub->sub_ops);

	sub->sub_items = temp;
	sub->sub_size = newsize;

	return (B_TRUE);
}

/* save the element of n (up to depth elements deep) as a substitution */
boolean_t
sub_save(sub_t *sub, const name_t *n, size_t depth)
{
	if (!sub_reserve(sub, 1))
		return (B_FALSE);

	name_t *dest = &sub->sub_items[sub->sub_len];
	if (!name_reserve(dest, depth))
		return (B_FALSE);

	const str_pair_t *src_sp = name_at((name_t *)n, depth);

	for (size_t i = 0; i < depth; i++, src_sp++) {
		str_pair_t copy;
		str_pair_init(&copy, n->nm_ops);
		if (!str_pair_copy(src_sp, &copy)) {
			str_pair_fini(&copy);
			name_fini(dest);
			return (B_FALSE);
		}

		VERIFY(name_add_str(dest, &copy.strp_l, &copy.strp_r));
	}

	return (B_TRUE);
}

/* push substitution idx onto n */
boolean_t
sub_substitute(const sub_t *sub, size_t idx, name_t *n)
{
	name_t *src = &sub->sub_items[idx];

	for (size_t i = 0; i < src->nm_len; i++) {
		if (!name_add_str(n, &src->nm_items[i].strp_l,
		    &src->nm_items[i].strp_r))
			return (B_FALSE);
	}
	
	return (B_TRUE);
}

static boolean_t
templ_reserve(templ_t *tpl, size_t n)
{
	if (tpl->tpl_len + n < tpl->tpl_size)
		return (B_TRUE);

	size_t newsize = tpl->tpl_size + CHUNK_SIZE;
	void *temp = xrealloc(tpl->tpl_ops, tpl->tpl_items,
	    tpl->tpl_size * sizeof (sub_t), newsize * sizeof (sub_t));

	if (temp == NULL)
		return (B_FALSE);

	for (size_t i = tpl->tpl_size; i < newsize; i++)
		sub_init(&tpl->tpl_items[i], tpl->tpl_ops);

	tpl->tpl_items = temp;
	tpl->tpl_size = newsize;
	return (B_TRUE);
}

void
templ_init(templ_t *tpl, sysdem_ops_t *ops)
{
	(void) memset(tpl, 0, sizeof (*tpl));
	tpl->tpl_ops = ops;
}

void
templ_fini(templ_t *tpl)
{
	if (tpl == NULL)
		return;

	for (size_t i = 0; i < tpl->tpl_len; i++)
		sub_fini(&tpl->tpl_items[i]);

	xfree(tpl->tpl_ops, tpl->tpl_items, tpl->tpl_size * sizeof (sub_t));
	sysdem_ops_t *ops = tpl->tpl_ops;
	(void) memset(tpl, 0, sizeof (*tpl));
	tpl->tpl_ops = ops;
}

boolean_t
templ_push(templ_t *tpl)
{
	if (!templ_reserve(tpl, 1))
		return (B_FALSE);

	tpl->tpl_len++;
	return (B_TRUE);
}

void
templ_pop(templ_t *tpl)
{
	ASSERT(!templ_empty(tpl));
	sub_fini(templ_top(tpl));
	tpl->tpl_len--;
}

sub_t *
templ_top(templ_t *tpl)
{
	return (&tpl->tpl_items[tpl->tpl_len - 1]);
}

boolean_t
templ_empty(const templ_t *tpl)
{
	return ((tpl->tpl_len == 0) ? B_TRUE : B_FALSE);
}

boolean_t
templ_top_empty(const templ_t *tpl)
{
	const sub_t *sub = templ_top((templ_t *)tpl);
	return (sub_empty(sub));
}

boolean_t
templ_sub(const templ_t *tpl, size_t idx, name_t *n)
{
	const sub_t *sub = &tpl->tpl_items[tpl->tpl_len - 1];

	return (sub_substitute(sub, idx, n));
}

boolean_t
templ_save(const name_t *n, size_t amt, templ_t *tpl)
{
	sub_t *s = templ_top(tpl);
	return (sub_save(s, n, amt));
}

