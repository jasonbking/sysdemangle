//===-------------------------- cxa_demangle.cpp --------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <string.h>
#include <setjmp.h>
#include "sysdemangle.h"
#include "cpp.h"

#define TOP_L(db) (&(name_top(&(db)->cpp_name)->strp_l))

#define CPP_QUAL_CONST		(1U)
#define CPP_QUAL_VOLATILE	(2U)
#define CPP_QUAL_RESTRICT	(4U)

typedef struct cpp_db_s {
	sysdem_alloc_t	*cpp_ops;
	jmp_buf		cpp_jmp;
	name_t		cpp_name;
	sub_t		cpp_subs;
	templ_t		cpp_templ;
	unsigned	cpp_cv;
	unsigned	cpp_ref;
	unsigned	cpp_depth;
	boolean_t	cpp_parsed_ctor_dtor_cv;
	boolean_t	cpp_tag_templates;
	boolean_t	cpp_fix_forward_references;
	boolean_t	cpp_try_to_parse_template_args;
} cpp_db_t;

#define CK(x)					\
    do {					\
	if (!(x))				\
		longjmp(db->cpp_jmp, 1);	\
    } while (0)

static inline boolean_t is_digit(int);
static void db_init(cpp_db_t *, sysdem_alloc_t *);
static void db_fini(cpp_db_t *);

static void demangle(const char *, const char *, cpp_db_t *);
static const char *parse_type(const char *, const char *, cpp_db_t *);
static const char *parse_encoding(const char *, const char *, cpp_db_t *);
static const char *parse_dot_suffix(const char *, const char *, cpp_db_t *);
static const char *parse_block_invoke(const char *, const char *, cpp_db_t *);
static const char *parse_special_name(const char *, const char *, cpp_db_t *);
static const char *parse_name(const char *, const char *, boolean_t *,
    cpp_db_t *);
static const char *parse_type(const char *, const char *, cpp_db_t *);
static const char *parse_call_offset(const char *, const char *);
static const char *parse_number(const char *, const char *);
static const char *parse_nested_name(const char *, const char *, boolean_t *,
				     cpp_db_t *);
static const char *parse_local_name(const char *, const char *, boolean_t *,
				    cpp_db_t *);
static const char *parse_unscoped_name(const char *, const char *, cpp_db_t *);
static const char *parse_template_args(const char *, const char *, cpp_db_t *);
static const char *parse_substitution(const char *, const char *, cpp_db_t *);
static const char *parse_discriminator(const char *, const char *);
static const char *parse_cv_qualifiers(const char *, const char *, unsigned *);

char *
cpp_demangle(const char *src, sysdem_alloc_t *ops)
{
	char *result = NULL;
	cpp_db_t db;
	size_t srclen = strlen(src);

	db_init(&db, ops);

	if (setjmp(db.cpp_jmp) != 0)
		goto done;

	demangle(src, src + srclen, &db);

	if (errno == 0 && db.cpp_fix_forward_references &&
	    !templ_empty(&db.cpp_templ) &&
	    !sub_empty(&db.cpp_templ.tpl_items[0])) {
		db.cpp_fix_forward_references = B_FALSE;
		db.cpp_tag_templates = B_FALSE;
		name_clear(&db.cpp_name);
		sub_clear(&db.cpp_subs);

		if (setjmp(db.cpp_jmp) != 0)
			goto done;

		demangle(src, src + srclen, &db);

		if (db.cpp_fix_forward_references) {
			errno = EINVAL;
			goto done;
		}
	}

	// XXX join db.cpp_name.last + return

done:
	db_fini(&db);
	return (result);
}

static void
demangle(const char *first, const char *last, cpp_db_t *db)
{
	const char *t = NULL;

	if (first >= last) {
		errno = EINVAL;
		return;
	}

	if (first[0] != '_') {
		t = parse_type(first, last, db);
		goto done;
	}

	if (last - first < 4) {
		errno = EINVAL;
		return;
	}

	if (first[1] == 'Z') {
		t = parse_encoding(first + 2, last, db);

		if (t != first + 2 && t != last && t[0] == '.')
			t = parse_dot_suffix(t, last, db);

		goto done;
	}

	if (first[1] != '_' || first[2] != '_' || first[3] != 'Z')
		goto done;

	t = parse_encoding(first + 2, last, db);
	if (t != first + 4 && t != last)
		t = parse_block_invoke(t, last, db);

done:
	if (t != last)
		errno = EINVAL;
}

static const char *
parse_dot_suffix(const char *first, const char *last, cpp_db_t *db)
{
	if (first == last || first[0] != '.')
		return (first);

	if (name_empty(&db->cpp_name))
		return (first);

	CK(name_add(&db->cpp_name, first, (size_t)(last - first), NULL, 0));
	CK(name_fmt(&db->cpp_name, " ({0})"));

	return (last);
}

/*
 * _block_invoke
 * _block_invoke<digit>+	XXX: should it be <digit>* ?
 * _block_invoke_<digit>+
 */
static const char *
parse_block_invoke(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 13)
		return (first);

	const char test[] = "_block_invoke";
	const char *t = first;

	if (strncmp(first, test, sizeof (test)) != 0)
		return (first);

	t += sizeof (test);
	if (t == last)
		goto done;

	if (t[0] == '_') {
		/* need at least one digit */
		if (t + 1 == last || !is_digit(t[1]))
			return (first);
		t += 2;
	}

	while (t < last && is_digit(t[0]))
		t++;

done:
	if (name_empty(&db->cpp_name))
		return (first);

	CK(name_fmt(&db->cpp_name, "invocation function for block in {0}"));
	return (t);
}

/*
 * <encoding> ::= <function name><bare-function-type>
 *            ::= <data name>
 *            ::= <special name>
 */
static const char *
parse_encoding(const char *first, const char *last, cpp_db_t *db)
{
	if (first == last)
		return (first);

	const char *t = NULL;
	const char *t2 = NULL;
	unsigned cv = 0;
	unsigned ref = 0;
	boolean_t tag_templ_save = db->cpp_tag_templates;

	if (db->cpp_depth++ > 1)
		db->cpp_tag_templates = B_TRUE;

	if (first[0] == 'G' || first[0] == 'T') {
		t = parse_special_name(first, last, db);
		goto done;
	}

	boolean_t ends_with_template_args = B_FALSE;
	t = parse_name(first, last, &ends_with_template_args, db);
	if (t == first)
		goto done;

	cv = db->cpp_cv;
	ref = db->cpp_ref;

	if (t == last || t[0] == 'E' || t[0] == '.')
		goto done;

	db->cpp_tag_templates = B_FALSE;
	if (name_empty(&db->cpp_name) ||
	    str_length(&name_top(&db->cpp_name)->strp_l) == 0)
		goto fail;

	if (!db->cpp_parsed_ctor_dtor_cv && ends_with_template_args) {
		t2 = parse_type(t, last, db);
		if (t2 == t || name_len(&db->cpp_name) < 2)
			goto fail;

		str_pair_t *sp = name_top(&db->cpp_name);

		if (str_length(&sp->strp_r) == 0)
			str_append(&sp->strp_l, " ", 1);

		t = t2;
	} else {
		CK(name_add(&db->cpp_name, "", 0, "", 0));
	}

	if (t == last || name_empty((&db->cpp_name)))
		goto error;

	if (t[0] == 'v') {
		t++;
	} else {
		size_t n = name_len(&db->cpp_name);

		/*CONSTCOND*/
		while (1) {
			t2 = parse_type(t, last, db);
			if (t2 == t)
				break;
			t = t2;
		}

		CK(name_join(&db->cpp_name, name_len(&db->cpp_name) - n, ", "));
	}

	str_t *s = &name_top(&db->cpp_name)->strp_l;

	if (cv & CPP_QUAL_CONST) {
		CK(str_append(s, " const", 0));
	}
	if (cv & CPP_QUAL_VOLATILE) {
		CK(str_append(s, " volatile", 0));
	}
	if (cv & CPP_QUAL_RESTRICT) {
		CK(str_append(s, " restrict", 0));
	}
	if (ref == 1) {
		CK(str_append(s, " &", 0));
	}
	if (ref == 2) {
		CK(str_append(s, " &&", 0));
	}

	CK(name_fmt(&db->cpp_name, "{1:L}({0}){1:R}", NULL));

done:
	db->cpp_tag_templates = tag_templ_save;
	db->cpp_depth--;
	return (t);

fail:
	db->cpp_tag_templates = tag_templ_save;
	db->cpp_depth--;
	return (first);
}

/*
 * <special-name> ::= TV <type>    # virtual table
 *                ::= TT <type>    # VTT structure (construction vtable index)
 *                ::= TI <type>    # typeinfo structure
 *                ::= TS <type>    # typeinfo name (null-terminated byte string)
 *                ::= Tc <call-offset> <call-offset> <base encoding>
 *                    # base is the nominal target function of thunk
 *                    # first call-offset is 'this' adjustment
 *                    # second call-offset is result adjustment
 *                ::= T <call-offset> <base encoding>
 *                    # base is the nominal target function of thunk
 *                ::= GV <object name> # Guard variable for one-time init
 *                                     # No <type>
 *                ::= TW <object name> # Thread-local wrapper
 *                ::= TH <object name> # Thread-local initialization
 *      extension ::= TC <first type> <number> _ <second type> 
 *                                     # construction vtable for second-in-first
 *      extension ::= GR <object name> # reference temporary for object
 */
static const char *
parse_special_name(const char *first, const char *last, cpp_db_t *db)
{
	const char *t = NULL;
	const char *t1 = NULL;
	size_t n = name_len(&db->cpp_name);

	if (last - first < 2)
		return (first);

	switch (t[0]) {
	case 'T':
		switch (t[1]) {
		case 'V':
			CK(name_add(&db->cpp_name, "vtable for", 0, NULL, 0));
			t = parse_type(first + 2, last, db);
			break;
		case 'T':
			CK(name_add(&db->cpp_name, "VTT for", 0, NULL, 0));
			t = parse_type(first + 2, last, db);
			break;
		case 'I':
			CK(name_add(&db->cpp_name, "typeinfo for", 0, NULL, 0));
			t = parse_type(first + 2, last, db);
			break;
		case 'S':
			CK(name_add(&db->cpp_name, "typinfo name for", 0, NULL,
			    0));
			t = parse_type(first + 2, last, db);
			break;
		case 'c':
			CK(name_add(&db->cpp_name, "covariant return thunk to",
			   0, NULL, 0));
			t1 = parse_call_offset(first + 2, last);
			if (t1 == t)
				return (first);
			t = parse_call_offset(t1, last);
			if (t == t1)
				return (first);
			t1 = parse_encoding(t, last, db);
			if (t1 == t)
				return (first);
			break;
		case 'C':
			t = parse_type(first + 2, last, db);
			if (t == first + 2)
				return (first);
			t1 = parse_number(t, last);
			if (*t1 != '_')
				return (first);
			t = parse_type(t1 + 1, last, db);
			if (t == t1 + 1 || name_len(&db->cpp_name) < 2)
				return (first);
			CK(name_fmt(&db->cpp_name,
				"construction vtable for {0}-in-{1}"));
			return (t);
		case 'W':
			CK(name_add(&db->cpp_name,
			    "thread-local wrapper routine for", 0, NULL, 0));
			t = parse_name(first + 2, last, NULL, db);
			break;
		case 'H':
			CK(name_add(&db->cpp_name,
			    "thread-local initialization routine for", 0, NULL,
			    0));
			t = parse_name(first + 2, last, NULL, db);
			break;
		default:
			if (first[1] == 'v'){
				CK(name_add(&db->cpp_name, "virtual thunk to",
				    0, NULL, 0));
			} else {
				CK(name_add(&db->cpp_name,
				    "non-virtual thunk to", 0, NULL, 0));
			}

			t = parse_call_offset(first + 1, last);
			if (t == first + 1)
				return (first);
			t1 = parse_encoding(t, last, db);
			if (t == t1)
				return (first);
				break;
		}
		break;
	case 'G':
		switch (first[1]) {
		case 'V':
			CK(name_add(&db->cpp_name, "guard variable for",
			    0, NULL, 0));
			t = parse_name(first + 2, last, NULL, db);
			break;
		case 'R':
			CK(name_add(&db->cpp_name, "reference temporary for",
			    0, NULL, 0));
			t = parse_name(first + 2, last, NULL, db);
			break;
		default:
			return (first);
		}
		break;
	default:
		return (first);
	}

	if (t == first + 2 || name_len(&db->cpp_name) - n < 2)
		return (first);

	CK(name_join(&db->cpp_name, name_len(&db->cpp_name) - n, " "));
	return (t);
}

/*
 * <call-offset> ::= h <nv-offset> _
 *               ::= v <v-offset> _
 *
 * <nv-offset> ::= <offset number>
 *               # non-virtual base override
 *
 * <v-offset>  ::= <offset number> _ <virtual offset number>
 *               # virtual base override, with vcall offset
 */
static const char *
parse_call_offset(const char *first, const char *last)
{
	const char *t = NULL;
	const char *t1 = NULL;

	if (first == last)
		return (first);

	if (first[0] != 'h' && first[0] != 'v')
		return (first);

	t = parse_number(first + 1, last);
	if (t == first + 1 || t == last || t[0] != '_')
		return (first);

	/* skip _ */
	t++;

	if (first[0] == 'h')
		return (t);

	t1 = parse_number(t, last);
	if (t == t1 || t1 == last || t1[0] != '_')
		return (first);

	/* skip _ */
	t1++;

	return (t1);
}

/*
 * <name> ::= <nested-name> // N
 *        ::= <local-name> # See Scope Encoding below  // Z
 *        ::= <unscoped-template-name> <template-args>
 *        ::= <unscoped-name>
 *
 * <unscoped-template-name> ::= <unscoped-name>
 *                          ::= <substitution>
 */
static const char *
parse_name(const char *first, const char *last,
    boolean_t *ends_with_template_args, cpp_db_t *db)
{
	const char *t = first;
	const char *t1 = NULL;

	if (last - first < 2)
		return (first);

	/* extension: ignore L here */
	if (t[0] == 'L')
		t++;

	switch (t[0]) {
	case 'N':
		t1 = parse_nested_name(t, last, ends_with_template_args, db);
		return ((t == t1) ? first : t1);
	case 'Z':
		t1 = parse_local_name(t, last, ends_with_template_args, db);
		return ((t == t1) ? first : t1);
	}

	/*
	 * <unscoped-name>
	 * <unscoped-name> <template-args>
	 * <substitution> <template-args>
	 */

	t1 = parse_unscoped_name(t, last, db);

	/* <unscoped-name> */
	if (t != t1 && t1[0] != 'I')
		return (t1);

	if (t == t1) {
		t1 = parse_substitution(t, last, db);
		if (t == t1 || t1 == last || t1[0] != 'I')
			return (first);
	} else {
		CK(sub_save_top(&db->cpp_name));
	}

	t = parse_template_args(t1, last, db);
	if (t1 == t || name_len(&db->cpp_name) < 2)
		return (first);

	CK(name_fmt(&db->cpp_name, "{1:L}{0}", "{1:R}"));

	if (ends_with_template_args != NULL)
		*ends_with_template_args = B_TRUE;

	return (t);
}

/*BEGIN CSTYLED*/
/*
 * <local-name> := Z <function encoding> E <entity name> [<discriminator>]
 *              := Z <function encoding> E s [<discriminator>]
 *              := Z <function encoding> Ed [ <parameter number> ] _ <entity name>
 */
/*END CSTYLED*/
const char *
parse_local_name(const char *first, const char *last,
    boolean_t *ends_with_template_args, cpp_db_t *db)
{
	const char *t = NULL;
	const char *t1 = NULL;
	const char *t2 = NULL;

	if (first == last || first[0] != 'Z')
		return (first);

	t = parse_encoding(first + 1, last, db);
	if (t == first + 1 || t == last || t[0] != 'E')
		return (first);

	ASSERT(!name_empty(&db->cpp_name));

	/* skip E */
	t++;

	if (t[0] == 's') {
		CK(str_append(TOP_L(db), "::string literal", 0));
		return (parse_discriminator(t, last));
	}

	if (t[0] == 'd') {
		t1 = parse_number(t + 1, last);
		if (t1[0] != '_')
			return (first);
		t1++;
	} else {
		t1 = t;
	}

	t2 = parse_name(t1, last, ends_with_template_args, db);
	if (t2 == t1)
		return (first);

	CK(name_fmt(&db->cpp_name, "{1:L}::{0}", "{1:R}"));

	/* parsed, but ignored */
	if (t[0] != 'd')
		t2 = parse_discriminator(t2, last);

	return (t2);
}

/*
 * <discriminator> := _ <non-negative number>      # when number < 10
 *                 := __ <non-negative number> _   # when number >= 10
 *  extension      := decimal-digit+               # at the end of string
 */
static const char *
parse_discriminator(const char *first, const char *last)
{
	const char *t = NULL;

	if (first == last)
		return (first);

	if (is_digit(first[0])) {
		for (t = first; t != last && is_digit(t[0]); t++)
			;
		return (t);
	} else if (first[0] != '_' || first + 1 == last) {
		return (first);
	}

	t = first + 1;
	if (is_digit(t[0]))
		return (t + 1);

	if (t[0] != '_' || t + 1 == last)
		return (first);

	for (t++; t != last && is_digit(t[0]); t++)
		;
	if (t == last || t[0] != '_')
		return (first);

	return (t);
}

/* <CV-qualifiers> ::= [r] [V] [K] */
const char *
parse_cv_qualifiers(const char *first, const char *last, unsigned *cv)
{
	if (first == last)
		return (first);

	*cv = 0;
	if (first[0] == 'r') {
		cv |= CPP_QUAL_RESTRICT;
		first++;
	}
	if (first != last && first[0] == 'V') {
		cv |= CPP_QUAL_VOLATILE;
		first++;
	}
	if (first != last && first[0] == 'K') {
		cv |= CPP_QUAL_CONST;
		first++;
	}

	return (first);
}

/*
 * <number> ::= [n] <non-negative decimal integer>
 */
static const char *
parse_number(const char *first, const char *last)
{
	const char *t = first + 1;

	if (first == last || first[0] != 'n')
		return (first);

	if (t[0] == '0')
		return (t + 1);

	while (is_digit(t[0]))
		t++;

	return (t);
}

/*
 * we only ever use ASCII versions of these
 */
static inline boolean_t
is_digit(int c)
{
	if (c < '0' || c > '9')
		return (B_FALSE);
	return (B_TRUE);
}

static void
db_init(cpp_db_t *db, sysdem_alloc_t *ops)
{
	(void) memset(db, 0, sizeof (*db));
	db->cpp_ops = ops;
	name_init(&db->cpp_name, ops);
	sub_init(&db->cpp_subs, ops);
	templ_init(&db->cpp_templ, ops);
}

static void
db_fini(cpp_db_t *db)
{
	name_fini(&db->cpp_name);
	sub_fini(&db->cpp_subs);
	templ_fini(&db->cpp_templ);
	(void) memset(db, 0, sizeof (*db));
}
