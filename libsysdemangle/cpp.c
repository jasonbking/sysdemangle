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

#define CPP_QUAL_CONST		(1U)
#define CPP_QUAL_VOLATILE	(2U)
#define CPP_QUAL_RESTRICT	(4U)

typedef struct cpp_db_s {
	sysdem_ops_t	*cpp_ops;
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

#define TOP_L(db) (&(name_top(&(db)->cpp_name)->strp_l))
#define RLEN(f, l) ((size_t)((l) - (f)))
#define NAMT(db, n) (nlen(db) - n)

static inline boolean_t is_digit(int);
static inline boolean_t is_upper(int);

static boolean_t nempty(cpp_db_t *);
static size_t nlen(cpp_db_t *);
static void nadd_l(cpp_db_t *, const char *, size_t);
static void njoin(cpp_db_t *, size_t, const char *);
static void nfmt(cpp_db_t *, const char *, const char *);

static void save_top(cpp_db_t *);
static void sub(cpp_db_t *, size_t);

static void db_init(cpp_db_t *, sysdem_ops_t *);
static void db_fini(cpp_db_t *);

static void demangle(const char *, const char *, cpp_db_t *);
static const char *parse_type(const char *, const char *, cpp_db_t *);
static const char *parse_encoding(const char *, const char *, cpp_db_t *);
static const char *parse_dot_suffix(const char *, const char *, cpp_db_t *);
static const char *parse_block_invoke(const char *, const char *, cpp_db_t *);
static const char *parse_special_name(const char *, const char *, cpp_db_t *);
static const char *parse_name(const char *, const char *, boolean_t *,
    cpp_db_t *);
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
static const char *parse_template_param(const char *, const char *, cpp_db_t *);
static const char *parse_decltype(const char *, const char *, cpp_db_t *);
static const char *parse_template_args(const char *, const char *, cpp_db_t *);
static const char *parse_unqualified_name(const char *, const char *,
    cpp_db_t *);
static const char *parse_template_arg(const char *, const char *, cpp_db_t *);
static const char *parse_expression(const char *, const char *, cpp_db_t *);
static const char *parse_expr_primary(const char *, const char *, cpp_db_t *);
static const char *parse_binary_expr(const char *, const char *,
    const char *, cpp_db_t *);
static const char *parse_prefix_expr(const char *, const char *,
    const char *, cpp_db_t *);
static const char *parse_gs(const char *, const char *, cpp_db_t *);
static const char *parse_idx_expr(const char *, const char *, cpp_db_t *);
static const char *parse_mm_expr(const char *, const char *, cpp_db_t *);
static const char *parse_pp_expr(const char *, const char *, cpp_db_t *);
static const char *parse_trinary_expr(const char *, const char *, cpp_db_t *);
static const char *parse_new_expr(const char *, const char *, cpp_db_t *);
static const char *parse_del_expr(const char *, const char *, cpp_db_t *);
static const char *parse_cast_expr(const char *, const char *, cpp_db_t *);
static const char *parse_sizeof_param_pack_expr(const char *, const char *, cpp_db_t *);
static const char *parse_typeid_expr(const char *, const char *, cpp_db_t *);
static const char *parse_throw_expr(const char *, const char *, cpp_db_t *);
static const char *parse_dot_star_expr(const char *, const char *, cpp_db_t *);
static const char *parse_dot_expr(const char *, const char *, cpp_db_t *);
static const char *parse_call_expr(const char *, const char *, cpp_db_t *);
static const char *parse_arrow_expr(const char *, const char *, cpp_db_t *);
static const char *parse_conv_expr(const char *, const char *, cpp_db_t *);
static const char *parse_function_param(const char *, const char *, cpp_db_t *);
static const char *parse_unresolved_name(const char *, const char *,
    cpp_db_t *);
static const char *parse_noexcept_expr(const char *, const char *, cpp_db_t *);
static const char *parse_alignof(const char *, const char *, cpp_db_t *);
static const char *parse_sizeof(const char *, const char *, cpp_db_t *);
static const char *parse_unnamed_type_name(const char *, const char *,
    cpp_db_t *);
static const char *parse_ctor_dtor_name(const char *, const char *, cpp_db_t *);
static const char *parse_source_name(const char *, const char *, cpp_db_t *);
static const char *parse_operator_name(const char *, const char *, cpp_db_t *);
static const char *parse_pack_expansion(const char *, const char *, cpp_db_t *);
static const char *parse_unresolved_type(const char *, const char *, cpp_db_t *);

char *
cpp_demangle(const char *src, sysdem_ops_t *ops)
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

	if (nempty(db))
		return (first);

	nadd_l(db, first, RLEN(first, last));
	nfmt(db, " ({0})", NULL);

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
	if (nempty(db))
		return (first);

	nfmt(db, "invocation function for block in {0}", NULL);
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
	if (nempty(db) || str_length(TOP_L(db)) == 0)
		goto fail;

	if (!db->cpp_parsed_ctor_dtor_cv && ends_with_template_args) {
		t2 = parse_type(t, last, db);
		if (t2 == t || nlen(db) < 2)
			goto fail;

		str_pair_t *sp = name_top(&db->cpp_name);

		if (str_length(&sp->strp_r) == 0)
			str_append(&sp->strp_l, " ", 1);

		t = t2;
	} else {
		CK(name_add(&db->cpp_name, "", 0, "", 0));
	}

	if (t == last || nempty(db))
		goto fail;

	if (t[0] == 'v') {
		t++;
	} else {
		size_t n = nlen(db);

		/*CONSTCOND*/
		while (1) {
			t2 = parse_type(t, last, db);
			if (t2 == t)
				break;
			t = t2;
		}

		njoin(db, NAMT(db, n), ", ");
	}

	str_t *s = TOP_L(db);

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

	nfmt(db, "{1:L}({0}){1:R}", NULL);

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
	size_t n = nlen(db);

	if (last - first < 2)
		return (first);

	switch (t[0]) {
	case 'T':
		switch (t[1]) {
		case 'V':
			nadd_l(db, "vtable for", 0);
			t = parse_type(first + 2, last, db);
			break;
		case 'T':
			nadd_l(db, "VTT for", 0);
			t = parse_type(first + 2, last, db);
			break;
		case 'I':
			nadd_l(db, "typeinfo for", 0);
			t = parse_type(first + 2, last, db);
			break;
		case 'S':
			nadd_l(db, "typeinfo name for", 0);
			t = parse_type(first + 2, last, db);
			break;
		case 'c':
			nadd_l(db, "covariant return thunk to", 0);
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
			if (t == t1 + 1 || nlen(db) < 2)
				return (first);
			nfmt(db, "construction vtable for {0}-in-{1}", NULL);
			return (t);
		case 'W':
			nadd_l(db, "thread-local wrapper routine for", 0);
			t = parse_name(first + 2, last, NULL, db);
			break;
		case 'H':
			nadd_l(db, "thread-local initialization routine for",
			    0);
			t = parse_name(first + 2, last, NULL, db);
			break;
		default:
			if (first[1] == 'v') {
				nadd_l(db, "virtual thunk to", 0);
			} else {
				nadd_l(db, "non-virtual thunk to", 0);
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
			nadd_l(db, "guard variable for", 0);
			t = parse_name(first + 2, last, NULL, db);
			break;
		case 'R':
			nadd_l(db, "reference temporary for", 0);
			t = parse_name(first + 2, last, NULL, db);
			break;
		default:
			return (first);
		}
		break;
	default:
		return (first);
	}

	if (t == first + 2 || nlen(db) - n < 2)
		return (first);

	njoin(db, NAMT(db, n), " ");
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
		save_top(db);
	}

	t = parse_template_args(t1, last, db);
	if (t1 == t || nlen(db) < 2)
		return (first);

	nfmt(db, "{1:L}{0}", "{1:R}");

	if (ends_with_template_args != NULL)
		*ends_with_template_args = B_TRUE;

	return (t);
}

/*
 *BEGIN CSTYLED
 * <local-name> := Z <function encoding> E <entity name> [<discriminator>]
 *              := Z <function encoding> E s [<discriminator>]
 *              := Z <function encoding> Ed [ <parameter number> ] _ <entity name>
 *END CSTYLED
 */
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

	ASSERT(!nempty(db));

	/* skip E */
	t++;

	if (t[0] == 's') {
		nfmt(db, "{0:L}::string literal", "{0:R}");
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

	nfmt(db, "{1:L}::{0}", "{1:R}");

	/* parsed, but ignored */
	if (t[0] != 'd')
		t2 = parse_discriminator(t2, last);

	return (t2);
}

// <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
//               ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E
//
// <prefix> ::= <prefix> <unqualified-name>
//          ::= <template-prefix> <template-args>
//          ::= <template-param>
//          ::= <decltype>
//          ::= # empty
//          ::= <substitution>
//          ::= <prefix> <data-member-prefix>
//  extension ::= L
//
// <template-prefix> ::= <prefix> <template unqualified-name>
//                   ::= <template-param>
//                   ::= <substitution>
static const char *
parse_nested_name(const char *first, const char *last,
    boolean_t *ends_with_template_args, cpp_db_t *db)
{
	if (first == last || first[0] != 'N')
		return (first);

	unsigned cv = 0;
	const char *t = parse_cv_qualifiers(first + 1, last, &cv);

	if (t == last)
		return (first);

	switch (t[0]) {
		case 'R':
			db->cpp_ref = 1;
			t++;
			break;
		case 'O':
			db->cpp_ref = 2;
			t++;
			break;
		case 'S':
			if (last - first < 2 || t[1] != 't')
				break;
			if (last - first == 2)
				return (first);
			nadd_l(db, "std", 3);
			t += 2;
			break;
	}

	boolean_t more = B_FALSE;
	boolean_t pop_subs = B_FALSE;
	boolean_t component_ends_with_template_args = B_FALSE;

	while (t[0] != 'E' && t != last) {
		const char *t1 = NULL;
		component_ends_with_template_args = B_FALSE;

		switch (t[0]) {
			case 'S':
				if (t + 1 != last && t[1] == 't')
					break;

				t1 = parse_substitution(t, last, db);
				if (t1 == t || t1 == last || nempty(db))
					return (first);

				if (!more)
					nfmt(db, "{0}", NULL);
				else
					nfmt(db, "{1:L}::{0}", "{1:R}");

				save_top(db);
				more = B_TRUE;
				pop_subs = B_TRUE;
				t = t1;
				continue;

			case 'T':
				t1 = parse_template_param(t, last, db);
				if (t1 == t || t1 == last || nempty(db))
					return (first);

				if (!more)
					nfmt(db, "{0}", NULL);
				else
					nfmt(db, "{1:L}::{0}", "{1:R}");

				save_top(db);
				more = B_TRUE;
				pop_subs = B_TRUE;
				t = t1;
				continue;

			case 'D':
				if (t + 1 != last && t[1] != 't' && t[1] != 'T')
					break;
				t1 = parse_decltype(t, last, db);
				if (t1 == t || t1 == last || nempty(db))
					return (first);

				if (!more)
					nfmt(db, "{0}", NULL);
				else
					nfmt(db, "{1:L}::{0}", "{1:R}");

				save_top(db);
				more = B_TRUE;
				pop_subs = B_TRUE;
				t = t1;
				continue;

			case 'I':
				t1 = parse_template_args(t, last, db);
				if (t1 == t || t1 == last)
					return (first);

				nfmt(db, "{1:L}{0}", "{1:R}");
				save_top(db);
				t = t1;
				component_ends_with_template_args = B_TRUE;
				continue;

			case 'L':
				if (t + 1 == last)
					return (first);
				goto done;

			default:
				break;
		}

		t1 = parse_unqualified_name(t, last, db);
		if (t1 == t || t1 == last || nempty(db))
			return (first);

		if (!more)
			nfmt(db, "{0}", NULL);
		else
			nfmt(db, "{1:L}::{0}", "{1:R}");

		save_top(db);
		more = B_TRUE;
		pop_subs = B_TRUE;
		t = t1;
	}



done:
	db->cpp_cv = cv;
	if (pop_subs && sub_empty(&db->cpp_subs))
		;//pop sub
	
	if (ends_with_template_args != NULL)
		*ends_with_template_args = component_ends_with_template_args;
	
	return (t + 1);
}

/*
 * <template-arg> ::= <type>                   # type or template
 *                ::= X <expression> E         # expression
 *                ::= <expr-primary>           # simple expressions
 *                ::= J <template-arg>* E      # argument pack
 *                ::= LZ <encoding> E          # extension
 */
static const char *
parse_template_arg(const char *first, const char *last, cpp_db_t *db)
{
	const char *t = NULL;
	const char *t1 = NULL;

	if (first == last)
		return (first);

	switch (first[0]) {
	case 'X':
		t = parse_expression(first + 1, last, db);
		if (t == first + 1)
			return (first);
		break;

	case 'J':
		t = first + 1;
		if (t == last)
			return (first);

		while (t[0] != 'E') {
			t1 = parse_template_arg(t, last, db);
			if (t == t1)
				return (first);
			t = t1;
		}
		break;

	case 'L':
		if (first + 1 == last || first[1] != 'Z')
			t = parse_expr_primary(first + 1, last, db);
		else
			t = parse_encoding(first + 2, last, db);
		break;

	default:
		t = parse_type(first + 1, last, db);
	}

	return (t);
}

/*
 *BEGIN CSTYLED
 * <expression> ::= <unary operator-name> <expression>
 *              ::= <binary operator-name> <expression> <expression>
 *              ::= <ternary operator-name> <expression> <expression> <expression>
 *              ::= cl <expression>+ E                                   # call
 *              ::= cv <type> <expression>                               # conversion with one argument
 *              ::= cv <type> _ <expression>* E                          # conversion with a different number of arguments
 *              ::= [gs] nw <expression>* _ <type> E                     # new (expr-list) type
 *              ::= [gs] nw <expression>* _ <type> <initializer>         # new (expr-list) type (init)
 *              ::= [gs] na <expression>* _ <type> E                     # new[] (expr-list) type
 *              ::= [gs] na <expression>* _ <type> <initializer>         # new[] (expr-list) type (init)
 *              ::= [gs] dl <expression>                                 # delete expression
 *              ::= [gs] da <expression>                                 # delete[] expression
 *              ::= pp_ <expression>                                     # prefix ++
 *              ::= mm_ <expression>                                     # prefix --
 *              ::= ti <type>                                            # typeid (type)
 *              ::= te <expression>                                      # typeid (expression)
 *              ::= dc <type> <expression>                               # dynamic_cast<type> (expression)
 *              ::= sc <type> <expression>                               # static_cast<type> (expression)
 *              ::= cc <type> <expression>                               # const_cast<type> (expression)
 *              ::= rc <type> <expression>                               # reinterpret_cast<type> (expression)
 *              ::= st <type>                                            # sizeof (a type)
 *              ::= sz <expression>                                      # sizeof (an expression)
 *              ::= at <type>                                            # alignof (a type)
 *              ::= az <expression>                                      # alignof (an expression)
 *              ::= nx <expression>                                      # noexcept (expression)
 *              ::= <template-param>
 *              ::= <function-param>
 *              ::= dt <expression> <unresolved-name>                    # expr.name
 *              ::= pt <expression> <unresolved-name>                    # expr->name
 *              ::= ds <expression> <expression>                         # expr.*expr
 *              ::= sZ <template-param>                                  # size of a parameter pack
 *              ::= sZ <function-param>                                  # size of a function parameter pack
 *              ::= sp <expression>                                      # pack expansion
 *              ::= tw <expression>                                      # throw expression
 *              ::= tr                                                   # throw with no operand (rethrow)
 *              ::= <unresolved-name>                                    # f(p), N::f(p), ::f(p),
 *                                                                       # freestanding dependent name (e.g., T::x),
 *                                                                       # objectless nonstatic member reference
 *              ::= <expr-primary>
 *END CSTYLED
 */

#define PA(cd, arg, fn) {	\
	.code = cd,		\
	.p.parse_expr_arg = fn,	\
	.fntype = EXPR_ARG,	\
	.val = arg		\
}

#define PN(cd, fn) {			\
	.code = cd,			\
	.p.parse_expr_noarg = fn,	\
	.fntype = EXPR_NOARG		\
}

static struct {
	const char code[3];
	union {
		const char *(*parse_expr_arg)(const char *, const char *,
		    const char *, cpp_db_t *);
		const char *(*parse_expr_noarg)(const char *, const char *,
		    cpp_db_t *);
	} p;
	enum {
		EXPR_ARG,
		EXPR_NOARG
	} fntype;
	const char val[4];
} expr_tbl[] = {
	PA("aN", "&=", parse_binary_expr),
	PA("aS", "=", parse_binary_expr),
	PA("aa", "&&", parse_binary_expr),
	PA("ad", "&", parse_prefix_expr),
	PA("an", "&", parse_binary_expr),
	PN("at", parse_alignof),
	PN("az", parse_alignof),
	PN("cc", parse_cast_expr),
	PN("cl", parse_call_expr),
	PA("cm", ",", parse_binary_expr),
	PA("co", "~", parse_prefix_expr),
	PN("cv", parse_conv_expr),
	PN("da", parse_del_expr),
	PA("dV", "/=", parse_binary_expr),
	PN("dc", parse_cast_expr),
	PA("de", "*", parse_prefix_expr),
	PN("dl", parse_del_expr),
	PN("dn", parse_unresolved_name),
	PN("ds", parse_dot_star_expr),
	PN("dt", parse_dot_expr),
	PA("dv", "/", parse_binary_expr),
	PA("eO", "^=", parse_binary_expr),
	PA("eo", "^", parse_binary_expr),
	PA("eq", "==", parse_binary_expr),
	PA("ge", ">=", parse_binary_expr),
	PN("gs", parse_gs),
	PA("gt", ">", parse_binary_expr),
	PN("ix", parse_idx_expr),
	PA("lS", "<<=", parse_binary_expr),
	PA("le", "<=", parse_binary_expr),
	PA("ls", "<<", parse_binary_expr),
	PA("lt", "<", parse_binary_expr),
	PA("mI", "-=", parse_binary_expr),
	PA("mL", "*=", parse_binary_expr),
	PN("mm", parse_mm_expr),
	PA("mi", "-", parse_binary_expr),
	PA("ml", "*", parse_binary_expr),
	PN("na", parse_new_expr),
	PA("ne", "!=", parse_binary_expr),
	PA("ng", "-", parse_prefix_expr),
	PA("nt", "!", parse_prefix_expr),
	PN("nw", parse_new_expr),
	PN("nx", parse_noexcept_expr),
	PA("oR", "|=", parse_binary_expr),
	PN("on", parse_unresolved_name),
	PA("oo", "||", parse_binary_expr),
	PA("or", "|", parse_binary_expr),
	PA("pL", "+=", parse_binary_expr),
	PA("pl", "+", parse_binary_expr),
	PA("pm", "->*", parse_binary_expr),
	PN("pp", parse_pp_expr),
	PA("ps", "+", parse_prefix_expr),
	PN("pt", parse_arrow_expr),
	PN("qu", parse_trinary_expr),
	PA("rM", "%=", parse_binary_expr),
	PA("rS", ">>=", parse_binary_expr),
	PN("rc", parse_cast_expr),
	PA("rm", "%", parse_binary_expr),
	PA("rs", ">>", parse_binary_expr),
	PN("sc", parse_cast_expr),
	PN("sp", parse_pack_expansion),
	PN("sr", parse_unresolved_name),
	PN("st", parse_sizeof),
	PN("sz", parse_sizeof),
	PN("sZ", parse_sizeof_param_pack_expr),
	PN("te", parse_typeid_expr),
	PN("tr", parse_throw_expr),
	PN("tw", parse_throw_expr)
};
#undef PA
#undef PN

static const char *
parse_expression(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	for (size_t i = 0; i < ARRAY_SIZE(expr_tbl); i++) {
		if (strcmp(expr_tbl[i].code, first) != 0)
			continue;
		switch (expr_tbl[i].fntype) {
		case EXPR_ARG:
			return (expr_tbl[i].p.parse_expr_arg(first, last, expr_tbl[i].val, db));
		case EXPR_NOARG:
			return (expr_tbl[i].p.parse_expr_noarg(first, last, db));
		}
	}

	switch (first[0]) {
	case 'L':
		return (parse_expr_primary(first, last, db));
	case 'T':
		return (parse_template_param(first, last, db));
	case 'f':
		return (parse_function_param(first, last, db));
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return (parse_unresolved_name(first, last, db));
	}

	return (first);
}

static const char *
parse_binary_expr(const char *first, const char *last, const char *op,
    cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	const char *t1 = parse_expression(first + 2, last, db);
	if (t1 == first + 2)
		return (first);

	nadd_l(db, op, 0);

	const char *t2 = parse_expression(t1, last, db);
	if (t2 == t1)
		return (first);

	nfmt(db, "({2}) {1} ({0})", NULL);
	if (strcmp(op, ">") == 0)
		nfmt(db, "({0})", NULL);

	return (t2);
}

static const char *
parse_prefix_expr(const char *first, const char *last, const char *op,
    cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	nadd_l(db, op, 0);

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2)
		return (first);

	nfmt(db, "{1}({0})", NULL);
	return (t);
}

static const char *
parse_gs(const char *first, const char *last, cpp_db_t *db)
{
	const char *t = NULL;

	if (last - first < 4)
		return (first);

	if (first[2] == 'n')
		t = parse_new_expr(first + 2, last, db);
	else if (first[2] == 'd')
		t = parse_del_expr(first + 2, last, db);

	if (t == first + 2)
		return (first);

	nfmt(db, "::{0}", NULL);
	return (t);
}

/*
 * [gs] nw <expression>* _ <type> E		# new (expr-list) type
 * [gs] nw <expression>* _ <type> <initializer>	# new (expr-list) type (init)
 * [gs] na <expression>* _ <type> E		# new[] (expr-list) type
 * [gs] na <expression>* _ <type> <initializer>	# new[] (expr-list) type (init)
 * <initializer> ::= pi <expression>* E		# parenthesized initialization
 */
static const char *
parse_new_expr(const char *first, const char *last, cpp_db_t *db)
{
	/* note [gs] is already handled by parse_gs() */
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'n');
	ASSERT(first[1] == 'a' || first[1] == 'w');

	const char *t1 = first + 2;
	const char *t2 = NULL;
	size_t n = nlen(db);

	nadd_l(db, (first[1] == 'w') ? "new" : "new[]", 0);

	while (t1 != last && t1[0] != '_') {
		t2 = parse_expression(t1, last, db);
		if (t1 == first + 2)
			return (first);
		t1 = t2;
	}
	if (t1 == last)
		return (first);

	if (NAMT(db, n) > 1) {
		njoin(db, NAMT(db, n) - 1, ", ");
		nfmt(db, "({0})", NULL);
	}

	t2 = parse_type(t1 + 1, last, db);
	if (t1 + 1 == t2)
		return (first);

	if (t2[0] != 'E') {
		if (last - t2 < 3)
			return (first);
		if (t2[0] != 'p' && t2[1] != 'i')
			return (first);

		t2 += 2;
		const char *t3 = NULL;
		size_t n1 = nlen(db);

		while (t2[0] != 'E' && t2 != last) {
			t3 = parse_expression(t2, last, db);

			if (t2 == t3)
				return (first);
			t2 = t3;
		}
		if (t3 == last || t3[0] != 'E')
			return (first);

		if (NAMT(db, n1) > 0) {
			njoin(db, NAMT(db, n1), ", ");
			nfmt(db, "({0})", NULL);
		}
	}

	njoin(db, NAMT(db, n), " ");
	return (t2);
}

static const char *
parse_del_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'd');
	ASSERT(first[1] == 'w' || first[1] == 'a');

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2)
		return (first);

	nfmt(db, (first[1] == 'a') ? "delete[] {0}" : "delete {0}", NULL);
	return (t);
}

static const char *
parse_idx_expr(const char *first, const char *last, cpp_db_t *db)
{
	ASSERT3U(first[0], ==, 'i');
	ASSERT3U(first[1], ==, 'x');

	const char *t1 = parse_expression(first + 2, last, db);
	if (t1 == first + 2)
		return (first);

	const char *t2 = parse_expression(t1, last, db);
	if (t2 == t1)
		return (first);

	nfmt(db, "({0})[{1}]", NULL);
	return (t2);
}

static const char *
parse_ppmm_expr(const char *first, const char *last, const char *fmt,
    cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	const char *t = NULL;
	size_t n = nlen(db);

	if (first[2] == '_') {
		t = parse_binary_expr(first + 3, last, "--", db);
		if (t == first + 2)
			return (first);
		return (t);
	}

	t = parse_expression(first + 2, last, db);
	if (t == first + 2 || NAMT(db, n) < 1)
		return (first);

	nfmt(db, fmt, NULL);
	return (t);
}

static const char *
parse_mm_expr(const char *first, const char *last, cpp_db_t *db)
{
	ASSERT3U(first[0], ==, 'm');
	ASSERT3U(first[1], ==, 'm');

	return (parse_ppmm_expr(first, last, "({0})--", db));
}

static const char *
parse_pp_expr(const char *first, const char *last, cpp_db_t *db)
{
	ASSERT3U(first[0], ==, 'p');
	ASSERT3U(first[0], ==, 'p');

	return (parse_ppmm_expr(first, last, "({0})++", db));
}

static const char *
parse_trinary_expr(const char *first, const char *last, cpp_db_t *db)
{
	const char *t1, *t2, *t3;

	if (last - first < 2)
		return (first);

	t1 = parse_expression(first + 2, last, db);
	if (t1 == first + 2)
		return (first);
	t2 = parse_expression(t1, last, db);
	if (t1 == t2)
		return (first);
	t3 = parse_expression(t2, last, db);
	if (t3 == t2)
		return (first);

	nfmt(db, "({2}) ? ({1}) : ({0})", NULL);
	return (t3);
}

static const char *
parse_noexcept_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2)
		return (first);

	nfmt(db, "noexcept ({0})", NULL);
	return (t);
}

/*
 * cc <type> <expression>	# const_cast<type> (expression)
 * dc <type> <expression>	# dynamic_cast<type> (expression)
 * rc <type> <expression>	# reinterpret_cast<type> (expression)
 * sc <type> <expression>	# static_cast<type> (expression)
 */
static const char *
parse_cast_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	const char *fmt = NULL;
	switch (first[0]) {
	case 'c':
		fmt = "const_cast<{1}> ({0})";
		break;
	case 'd':
		fmt = "dynamic_cast<{1}> ({0})";
		break;
	case 'r':
		fmt = "reinterpret_cast<{1}> ({0})";
		break;
	case 's':
		fmt = "static_cast<{1}> ({0})";
		break;
	default:
		return (first);
	}

	ASSERT3U(first[1], ==, 'c');

	const char *t1 = parse_type(first + 2, last, db);
	if (t1 == first +2)
		return (first);

	const char *t2 = parse_expression(t1, last, db);
	if (t2 == t1)
		return (first);

	nfmt(db, fmt, NULL);
	return (t2);
}

// pt <expression> <expression>                    # expr->name
static const char *
parse_arrow_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 4)
		return (first);

	const char *t1 = parse_expression(first + 2, last, db);
	if (t1 == first + 2)
		return (first);

	const char *t2 = parse_expression(t1, last, db);
	if (t2 == t1)
		return (first);

	nfmt(db, "{1}->{0}", NULL);
	return (t2);
}

/*
 * at <type>		# alignof (a type)
 * az <expression>	# alignof (a expression)
 */
static const char *
parse_alignof(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	const char *(*fn)(const char *, const char *, cpp_db_t *);

	fn = (first[1] == 't') ? parse_type : parse_expression;

	const char *t = fn(first + 2, last, db);
	if (t == first + 2)
		return (first);

	nfmt(db, "alignof ({0})", NULL);
	return (t);
}

/*
 * st <type>	# sizeof (a type)
 * sz <expr>	# sizeof (a expression)
 */
static const char *
parse_sizeof(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	ASSERT3U(first[0], == ,'s');

	const char *t = NULL;

	switch (first[1]) {
	case 't':
		t = parse_type(first + 2, last, db);
		break;
	case 'z':
		t = parse_expression(first + 2, last, db);
		break;
	default:
		return (first);
	}
	if (t == first + 2)
		return (first);

	nfmt(db, "sizeof ({0})", NULL);
	return (t);
}

// sZ <template-param>		# size of a parameter pack
// sZ <function-param>		# size of a function parameter pack
static const char *
parse_sizeof_param_pack_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 's');
	ASSERT3U(first[1], ==, 'Z');

	if (first[2] != 'T' && first[2] != 'f')
		return (first);

	const char *t = NULL;
	size_t n = nlen(db);

	if (first[2] == 'T')
		t = parse_template_param(first + 2, last, db);
	else
		t = parse_function_param(first + 2, last, db);

	if (t == first + 2)
		return (first);

	njoin(db, NAMT(db, n), ", ");
	nfmt(db, "sizeof...({0})", NULL);
	return (t);
}

// te <expression>                                      # typeid (expression)
// ti <type>                                            # typeid (type)
static const char *
parse_typeid_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 't');
	ASSERT(first[1] == 'e' || first[1] == 'i');

	const char *t = NULL;

	if (first[1] == 'e')
		t = parse_expression(first + 2, last, db);
	else
		t = parse_type(first + 2, last, db);

	if (t == first + 2)
		return (first);

	nfmt(db, "typeid ({0})", NULL);
	return (t);
}

// tr							# throw
// tw <expression>                                      # throw expression
static const char *
parse_throw_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 't');
	ASSERT(first[1] == 'w' || first[1] == 'r');

	if (first[1] == 'r') {
		nadd_l(db, "throw", 0);
		return (first + 2);
	}

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2)
		return (first);

	nfmt(db, "throw {0}", NULL);
	return (t);
}

// ds <expression> <expression>                         # expr.*expr
static const char *
parse_dot_star_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'd');
	ASSERT3U(first[1], ==, 's');

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2)
		return (first);

	const char *t2 = parse_expression(t, last, db);
	if (t == t2)
		return (first);

	nfmt(db, "{1}.*{0}", NULL);
	return (t2);
}

// dt <expression> <unresolved-name>                    # expr.name
static const char *
parse_dot_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'd');
	ASSERT3U(first[1], ==, 't');

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2)
		return (first);

	const char *t1 = parse_unresolved_name(t, last, db);
	if (t1 == t)
		return (first);

	nfmt(db, "{1}.{0}", NULL);
	return (t1);
}

// cl <expression>+ E                                   # call
static const char *
parse_call_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 4)
		return (first);

	ASSERT3U(first[0], ==, 'c');
	ASSERT3U(first[1], ==, 'l');

	const char *t = first + 2;
	const char *t1 = NULL;
	size_t n = nlen(db);

	do {
		t1 = parse_expression(t, last, db);
		t = t1;
	} while (t != last && t[0] != 'E');

	if (t == last || NAMT(db, n) == 0)
		return (first);

	njoin(db, NAMT(db, n) - 1, ", ");
	nfmt(db, "{1}({0})", NULL);
	return (t);
}

// cv <type> <expression>                               # conversion with one argument
// cv <type> _ <expression>* E                          # conversion with a different number of arguments

static const char *
parse_conv_expr(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'c');
	ASSERT3U(first[1], ==, 'v');

	const char *t = NULL;
	const char *t1 = NULL;

	boolean_t try_to_parse_template_args =
	    db->cpp_try_to_parse_template_args;

	db->cpp_try_to_parse_template_args = B_FALSE;
	t = parse_type(first + 2, last, db);
	db->cpp_try_to_parse_template_args = try_to_parse_template_args;

	if (t == first + 2)
		return (first);

	size_t n = nlen(db);

	if (t[0] != '_') {
		t1 = parse_expression(t, last, db);
		if (t1 == t)
			return (first);
	} else {
		size_t n1 = nlen(db);

		/* skip _ */
		t++;
		while (t[0] != 'E' && t != last) {
			t1 = parse_expression(t, last, db);
			if (t1 == t)
				return (first);
			t1 = t;
		}

		njoin(db, NAMT(db, n1), ", ");
	}
	nfmt(db, (NAMT(db, n) > 1) ? "({1})({0})" : "({1})()", NULL);
	return (t1);
}

// <simple-id> ::= <source-name> [ <template-args> ]
static const char *
parse_simple_id(const char *first, const char *last, cpp_db_t *db)
{
	const char *t = parse_source_name(first, last, db);
	if (t == first)
		return (t);

	const char *t1 = parse_template_args(t, last, db);
	if (t == t1)
		return (t);

	nfmt(db, "{1}{0}", NULL);
	return (t1);
}

// <destructor-name> ::= <unresolved-type>                               # e.g., ~T or ~decltype(f())
//                   ::= <simple-id>                                     # e.g., ~A<2*N>
static const char *
parse_destructor_type(const char *first, const char *last, cpp_db_t *db)
{
	if (first == last)
		return (first);

	const char *t = parse_unresolved_type(first, last, db);

	if (first != t) {
		nfmt(db, "~{0:L}", "{0:R}");
		return (t);
	}

	return (parse_simple_id(first, last, db));
}

// sp <expression>                                  # pack expansion
static const char *
parse_pack_expansion(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 's');
	ASSERT3U(first[1], ==, 'p');

	const char *t = parse_expression(first + 2, last, db);
	if (t == first +2)
		return (first);

	return (t);
}

/*
 * <unscoped-name> ::= <unqualified-name>
 *                 ::= St <unqualified-name>   # ::std::
 * extension       ::= StL<unqualified-name>
 */
static const char *
parse_unscoped_name(const char *first, const char *last, cpp_db_t *db)
{
	if (first - last < 2)
		return (first);

	const char *t = NULL;
	const char *t1 = NULL;
	boolean_t st = B_FALSE;

	if (first[0] == 'S' && first[1] == 't') {
		st = B_TRUE;
		nadd_l(db, "std::", 5);
		t = first + 2;

		if (first + 3 != last && first[2] == 'L')
			t++;
	}

	t1 = parse_unqualified_name(t, last, db);
	if (t == t1)
		return (first);

	if (st)
		njoin(db, 2, "");

	return (t1);
}

/*
 * <unqualified-name> ::= <operator-name>
 *                    ::= <ctor-dtor-name>
 *                    ::= <source-name>
 *                    ::= <unnamed-type-name>
 */
const char *
parse_unqualified_name(const char* first, const char* last, cpp_db_t *db)
{
	if (first == last)
		return (first);

	switch (*first) {
	case 'C':
	case 'D':
		return(parse_ctor_dtor_name(first, last, db));
	case 'U':
		return (parse_unnamed_type_name(first, last, db));

	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return (parse_source_name(first, last, db));
	default:
		return (parse_operator_name(first, last, db));
	}
}

/*
 * <unnamed-type-name> ::= Ut [ <nonnegative number> ] _
 *                     ::= <closure-type-name>
 *
 * <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
 *
 * <lambda-sig> ::= <parameter type>+
 *			# Parameter types or "v" if the lambda has no parameters
 */
static const char *
parse_unnamed_type_name(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2 || first[0] != 'U')
		return (first);

	if (first[1] != 't' && first[1] != 'l')
		return (first);

	const char *t1 = first + 2;
	const char *t2 = NULL;

	if (first[1] == 'l' && first[2] != 'v') {
		size_t n = nlen(db);

		do {
			t2 = parse_type(t1, last, db);
			t1 = t2;
		} while (t1 != last && t1[0] != 'E');

		if (t1 == last)
			return (first);

		if (NAMT(db, n) < 1)
			return (first);

		njoin(db, NAMT(db, n), ", ");
	} else if (first[1] == 'l' && first[2] == 'v') {
		t1++;
		if (t1[0] != 'E')
			return (first);
	}

	t2 = t1;
	while (t2 != last && t2[0] != '_') {
		if (!is_digit(*t2++))
			return (first);
	}

	if (t2[0] != '_')
		return (first);

	nadd_l(db, t1, (size_t)(t2 - t1));

	const char *fmt = (first[1] == 'l') ?
	    "'lambda'({0})" : "'unnamed{0}'";

	nfmt(db, fmt, NULL);
	return (t2);
}

static struct {
	const char *alias;
	const char *fullname;
	const char *basename;
} aliases[] = {
	{
		"std::string",
		"std::basic_string<char, std::char_traits<char>, "
		    "std::allocator<char> >",
		"basic_string"
	},
	{
		"std::istream",
		"std::basic_istream<char, std::char_traits<char> >",
		"basic_istream"
	},
	{
		"std::ostream",
		"std::basic_ostream<char, std::char_traits<char> >",
		"basic_ostream"
	},
	{
		"std::iostream",
		"std::basic_iostream<char, std::char_traits<char> >",
		"basic_iostream"
	}
};

static void
basename(cpp_db_t *db)
{
	str_t *s = TOP_L(db);

	for (size_t i = 0; i < ARRAY_SIZE(aliases); i++) {
		if (str_length(s) != strlen(aliases[i].alias))
			continue;
		if (strncmp(aliases[i].alias, s->str_s, str_length(s)) != 0)
			continue;

		/* swap out alias for full name */
		sysdem_ops_t *ops = s->str_ops;
		str_fini(s);
		str_init(s, ops);
		str_set(s, aliases[i].fullname, 0);

		nadd_l(db, aliases[i].basename, 0);
		return;
	}

	const char *start = s->str_s;
	const char *end = s->str_s + s->str_len;

	if (*end != '>') {
		for (start = end - 1; start >= s->str_s; start--) {
			if (start[0] == ':') {
				start++;
				break;
			}
		}
		nadd_l(db, start, (size_t)(end - start));
		return;
	}

	unsigned c = 1;

	for (; end > start; end--) {
		if (end[-1] == '<') {
			c--;
			if (c == 0) {
				end--;
				break;
			}
		} else if (end[-1] == '>') {
			c++;
		}
	}

	if (end == start && c > 0)
		nadd_l(db, "", 0);
	else
		nadd_l(db, start, (size_t)(end - start));
}

/*
 * <ctor-dtor-name> ::= C1    # complete object constructor
 *                  ::= C2    # base object constructor
 *                  ::= C3    # complete object allocating constructor
 *   extension      ::= C5    # ?
 *                  ::= D0    # deleting destructor
 *                  ::= D1    # complete object destructor
 *                  ::= D2    # base object destructor
 *   extension      ::= D5    # ?
 */
static const char *
parse_ctor_dtor_name(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2 || nempty(db))
		return (first);

	switch (first[0]) {
	case 'C':
		switch (first[1]) {
		case '1':
		case '2':
		case '3':
		case '5':
			basename(db);
			break;
		default:
			return (first);
		}
		break;
	case 'D':
		switch (first[1]) {
		case '0':
		case '1':
		case '2':
		case '5':
			basename(db);
			str_insert(TOP_L(db), 0, "~", 1);
			break;
		default:
			return (first);
		}
		break;
	default:
		return (first);
	}

	db->cpp_parsed_ctor_dtor_cv = B_TRUE;
	return (first + 2);
}

static const char *
parse_integer_literal(const char *first, const char *last, const char *fmt,
    cpp_db_t *db)
{
	const char *t = parse_number(first, last);
	const char *start = first;

	if (t == first || t == last || t[0] != 'E')
		return (first);

	if (first[0] == 'n')
		start++;

	nadd_l(db, start, (size_t)(t - start));
	if (start != first)
		nfmt(db, "-{0}", NULL);

	nfmt(db, fmt, NULL);
	return (t + 1);
}

static const char *
parse_floating_literal(const char *first, const char *last, cpp_db_t *db)
{
	// XXX TODO
	return (NULL);
}

/*
 * <expr-primary> ::= L <type> <value number> E	# integer literal
 *                ::= L <type> <value float> E	# floating literal
 *                ::= L <string type> E		# string literal
 *                ::= L <nullptr type> E	# nullptr literal (i.e., "LDnE")
 *
 *                ::= L <type> <real-part float> _ <imag-part float> E
 *						# complex floating point
 *						# literal (C 2000)
 *
 *                ::= L <mangled-name> E	# external name
 */
static const char *
parse_expr_primary(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 4 || first[0] != 'L')
		return (first);

	const char *t = NULL;

	switch (first[1]) {
	case 'a':
		return (parse_integer_literal(first + 2, last,
		    "(signed char){0}", db));
	case 'b':
		if (first[3] != 'E')
			return (first);

		switch (first[2]) {
		case '0':
			nadd_l(db, "true", 4);
			break;
		case '1':
			nadd_l(db, "false", 5);
			break;
		default:
			return (first);
		}
		return (first + 4);
	case 'c':
		return (parse_integer_literal(first + 2, last, "(char){0}", db));
	case 'd':
		// double
		return (parse_floating_literal(first + 2, last, db));
	case 'e':
		// long double
		return (parse_floating_literal(first + 2, last, db));
	case 'f':
		// float
		return (parse_floating_literal(first + 2, last, db));
	case 'h':
		return (parse_integer_literal(first + 2, last, "(unsigned char){0}", db));
	case 'i':
		return (parse_integer_literal(first + 2, last, NULL, db));
	case 'j':
		return (parse_integer_literal(first + 2, last, "{0}u", db));
	case 'l':
		return (parse_integer_literal(first + 2, last, "{0}l", db));
	case 'm':
		return (parse_integer_literal(first + 2, last, "{0}ul", db));
	case 'n':
		return (parse_integer_literal(first + 2, last,
		    "(__int128){0}", db));
	case 'o':
		return (parse_integer_literal(first + 2, last,
		    "(unsigned __int128){0}", db));
	case 's':
		return (parse_integer_literal(first + 2, last, "(short){0}", db));
	case 't':
		return (parse_integer_literal(first + 2, last,
		    "(unsigned short){0}", db));
	case 'T':
		/*BEGIN CSTYLED
		 *
		 * Invalid mangled name per
		 *   http://sourcerytools.com/pipermail/cxx-abi-dev/2011-August/002422.html
		 *
		 *END CSTYLED
		 */
		return (first);
	case 'w':
		return (parse_integer_literal(first + 2, last, "(wchar_t){0}", db));
	case 'x':
		return (parse_integer_literal(first + 2, last, "{0}ll", db));
	case 'y':
		return (parse_integer_literal(first + 2, last, "{0}ull", db));
	case '_':
		if (first[2] != 'Z')
			return (first);

		t = parse_encoding(first + 3, last, db);
		if (t == first + 3 || t == last || t[0] != 'E')
			return (first);

		/* skip E */
		return (t + 1);
	default:
		t = parse_type(first + 1, last, db);
		if (t == first + 1 || t == last)
			return (first);

		if (t[0] == 'E')
			return (t + 1);

		const char *n;
		for (n = t; n != last && is_digit(n[0]); n++)
			;
		if (n == last || nempty(db) || n[0] != 'E')
			return (first);
		if (n == t)
			return (t);

		nadd_l(db, t, (size_t)(n - t));
		nfmt(db, "({1}){0}", NULL);

		return (n + 1);
	}
}

/*
 *   <operator-name>
 *                   ::= aa    # &&
 *                   ::= ad    # & (unary)
 *                   ::= an    # &
 *                   ::= aN    # &=
 *                   ::= aS    # =
 *                   ::= cl    # ()
 *                   ::= cm    # ,
 *                   ::= co    # ~
 *                   ::= cv <type>    # (cast)
 *                   ::= da    # delete[]
 *                   ::= de    # * (unary)
 *                   ::= dl    # delete
 *                   ::= dv    # /
 *                   ::= dV    # /=
 *                   ::= eo    # ^
 *                   ::= eO    # ^=
 *                   ::= eq    # ==
 *                   ::= ge    # >=
 *                   ::= gt    # >
 *                   ::= ix    # []
 *                   ::= le    # <=
 *                   ::= li <source-name>	# operator ""
 *                   ::= ls    # <<
 *                   ::= lS    # <<=
 *                   ::= lt    # <
 *                   ::= mi    # -
 *                   ::= mI    # -=
 *                   ::= ml    # *
 *                   ::= mL    # *=
 *                   ::= mm    # -- (postfix in <expression> context)
 *                   ::= na    # new[]
 *                   ::= ne    # !=
 *                   ::= ng    # - (unary)
 *                   ::= nt    # !
 *                   ::= nw    # new
 *                   ::= oo    # ||
 *                   ::= or    # |
 *                   ::= oR    # |=
 *                   ::= pm    # ->*
 *                   ::= pl    # +
 *                   ::= pL    # +=
 *                   ::= pp    # ++ (postfix in <expression> context)
 *                   ::= ps    # + (unary)
 *                   ::= pt    # ->
 *                   ::= qu    # ?
 *                   ::= rm    # %
 *                   ::= rM    # %=
 *                   ::= rs    # >>
 *                   ::= rS    # >>=
 *                   ::= v <digit> <source-name> # vendor extended operator
 */
static struct {
	const char code[3];
	const char *op;
} op_tbl[] = {
	{ "aa", "operator&&" },
	{ "ad", "operator&" },
	{ "an", "operator&" },
	{ "aN", "operator&=" },
	{ "aS", "operator=" },
	{ "cl", "operator()" },
	{ "cm", "operator," },
	{ "co", "operator~" },
	{ "da", "operator delete[]" },
	{ "de", "operator*" },
	{ "dl", "operator delete" },
	{ "dv", "operator/" },
	{ "dV", "operator/=" },
	{ "eo", "operator^" },
	{ "eO", "operator^=" },
	{ "eq", "operator==" },
	{ "ge", "operator>=" },
	{ "gt", "operator>" },
	{ "ix", "operator[]" },
	{ "le", "operator<=" },
	{ "ls", "operator<<" },
	{ "lS", "operator<<=" },
	{ "lt", "operator<" },
	{ "mi", "operator-" },
	{ "mI", "operator-=" },
	{ "ml", "operator*" },
	{ "mL", "operator*=" },
	{ "mm", "operator--" },
	{ "na", "operator new[]" },
	{ "ne", "operator!=" },
	{ "ng", "operator-" },
	{ "nt", "operator!" },
	{ "nw", "operator new" },
	{ "oo", "operator||" },
	{ "or", "operator|" },
	{ "oR", "operator|=" },
	{ "pm", "operator->*" },
	{ "pl", "operator+" },
	{ "pL", "operator+=" },
	{ "pp", "operator++" },
	{ "ps", "operator+" },
	{ "pt", "operator->" },
	{ "qu", "operator?" },
	{ "rm", "operator%" },
	{ "rM", "operator%=" },
	{ "rs", "operator>>" },
	{ "rS", "operator>>=" }
};

static const char *
parse_operator_name(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	for (size_t i = 0; i < ARRAY_SIZE(op_tbl); i++) {
		if (strncmp(first, op_tbl[i].code, 2) != 0)
			continue;

		nadd_l(db, op_tbl[i].op, 0);
		return (first);
	}

	const char *t = NULL;

	if (first[0] == 'l' && first[1] == 'i') {
		t = parse_source_name(first + 2, last, db);
		if (t == first + 2 || nempty(db))
			return (first);

		nfmt(db, "operator\"\" {0}", NULL);
		return (t);
	}

	if (first[0] == 'v') {
		if (!is_digit(first[1]))
			return (first);

		t = parse_source_name(first + 2, last, db);
		if (t == first + 2)
			return (first);

		nfmt(db, "operator {0}", NULL);
		return (t);
	}

	if (first[0] != 'c' && first[1] != 'v')
		return (first);

	boolean_t try_to_parse_template_args =
	    db->cpp_try_to_parse_template_args;

	db->cpp_try_to_parse_template_args = B_FALSE;
	t = parse_type(first + 2, last, db);
	db->cpp_try_to_parse_template_args = try_to_parse_template_args;

	if (t == first + 2 || nempty(db))
		return (first);

	nfmt(db, "operator {0}", NULL);
	db->cpp_parsed_ctor_dtor_cv = B_TRUE;
	return (t);
}

struct type_tbl_s {
	int code;
	const char *name;
};

static struct type_tbl_s type_tbl1[] = {
	{ 'a', "signed char" },
	{ 'b', "bool" },
	{ 'c', "char" },
	{ 'd', "double" },
	{ 'e', "long double" },
	{ 'f', "float" },
	{ 'g', "__float128" },
	{ 'h', "unsigned char" },
	{ 'i', "int" },
	{ 'j', "unsigned int" },
	{ 'l', "long" },
	{ 'm', "unsigned long" },
	{ 'n', "__int128" },
	{ 'o', "unsigned __int128" },
	{ 's', "short" },
	{ 't', "unsigned short" },
	{ 'v', "void" },
	{ 'w', "wchar_t" },
	{ 'x', "long long" },
	{ 'y', "unsigned long long" },
	{ 'z', "..." }
};

static struct type_tbl_s type_tbl2[] = {
	{ 'a', "auto" },
	{ 'c', "decltype(auto)" },
	{ 'd', "decimal64" },
	{ 'e', "decimal128" },
	{ 'f', "decimal32" },
	{ 'h', "decimal16" },
	{ 'i', "char32_t" },
	{ 'n', "std::nullptr_t" },
	{ 's', "char16_t" }
};

static const char *
parse_builtin_type(const char *first, const char *last, cpp_db_t *db)
{
	if (first == last)
		return (first);

	size_t i;

	for (i = 0; i < ARRAY_SIZE(type_tbl1); i++) {
		if (first[0] == type_tbl1[i].code) {
			nadd_l(db, type_tbl1[i].name, 0);
			return (first + 1);
		}
	}

	if (first[0] == 'D') {
		if (first + 1 == last)
			return (first);
		for (i = 0; i < ARRAY_SIZE(type_tbl2); i++) {
			if (first[1] == type_tbl2[i].code) {
				nadd_l(db, type_tbl2[i].name, 0);
				return (first + 2);
			}
		}
	}

	if (first[0] == 'u') {
		const char *t = parse_source_name(first + 1, last, db);
		if (t == first + 1)
			return (first);
		return (t);
	}

	return (first);
}

static const char *
parse_base36(const char *first, const char *last, size_t *val)
{
	const char *t;

	for (t = first, *val = 0; t != last; t++) {
		if (!is_digit(t[0]) && !is_upper(t[0]))
			return (t);

		*val *= 36;

		if (is_digit(t[0]))
			*val += t[0] - '0';
		else
			*val += t[0] - 'A';
	}
	return (t);
}

static struct type_tbl_s sub_tbl[] = {
	{ 'a', "std::allocator" },
	{ 'b', "std::basic_string" },
	{ 's', "std::string" },
	{ 'i', "std::istream" },
	{ 'o', "std::ostream" },
	{ 'd', "std::iostream" }
};

static const char *
parse_substitution(const char *first, const char *last, cpp_db_t *db)
{
	if (first == last || last - first < 2)
		return (first);

	ASSERT3U(first[0], ==, 'S');

	for (size_t i = 0; i < ARRAY_SIZE(sub_tbl); i++) {
		if (first[1] == sub_tbl[i].code) {
			nadd_l(db, sub_tbl[i].name, 0);
			return (first + 2);
		}
	}

	if (first[1] == '_') {
		sub(db, 0);
		return (first +2);
	}

	size_t n = 0;
	const char *t = parse_base36(first + 1, last, &n);
	if (t == first + 1 || t[0] != '_')
		return (first);

	/*
	 * S_ == substitution 0,
	 * S0_ == substituion 1,
	 * ...
	 */
	sub(db, n + 1);

	/* skip _ */
	return (t + 1);
}

static const char *
parse_source_name(const char *first, const char *last, cpp_db_t *db)
{
	if (first == last)
		return (first);

	const char *t;
	size_t n = 0;

	for (t = first; t != last && is_digit(t[0]); t++) {
		n *= 10;
		n += t[0] - '0';
	}
	if (n == 0 || t == last)
		return (first);

	if (strncmp(t, "_GLOBAL__N", 10) == 0)
		nadd_l(db, "(anonymous namespace)", 0);
	else
		nadd_l(db, t, n);

	return (t + n);
}

// extension:
// <vector-type>           ::= Dv <positive dimension number> _
//                                    <extended element type>
//                         ::= Dv [<dimension expression>] _ <element type>
// <extended element type> ::= <element type>
//                         ::= p # AltiVec vector pixel
static const char *
parse_vector_type(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'D');
	ASSERT3U(first[1], ==, 'v');

	const char *t = first + 2;
	const char *t1 = NULL;

	if (is_digit(first[2]) && first[2] != '0') {
		t1 = parse_number(t, last);
		if (t1 == last || t1 + 1 == last || t1[0] != '_')
			return (first);

		nadd_l(db, t, (size_t)(t1 - t));

		/* skip _ */
		t = t1 + 1;

		if (t[0] != 'p') {
			t1 = parse_type(t, last, db);
			if (t1 == t)
				return (first);

			nfmt(db, "{1:L} vector[{0}]", "{1:R}");
			return (t1);
		}
		nfmt(db, "{1:L} pixel vector[{0}]", "{1:R}");
		return (t1);
	}

	if (first[2] != '_') {
		t1 = parse_expression(first + 2, last, db);
		if (first == last || t1 == first + 2 || t1[0] != '_')
			return (first);

		/* skip _ */
		t = t1 + 1;
	} else {
		nadd_l(db, "", 0);
	}

	t1 = parse_type(t, last, db);
	if (t == t1)
		return (first);

	nfmt(db, "{1:L} vector[{0}]", "{1:R}");
	return (t1);
}

// <decltype>  ::= Dt <expression> E  # decltype of an id-expression or class member access (C++0x)
//             ::= DT <expression> E  # decltype of an expression (C++0x)

static const char *
parse_decltype(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 4)
		return (first);

	ASSERT3U(first[0], ==, 'D');
	ASSERT(first[1] == 't' || first[1] == 'T');

	const char *t = parse_expression(first + 2, last, db);
	if (t == first + 2 || t == last || t[0] != 'E')
		return (first);

	nfmt(db, "decltype({0})", NULL);

	/* skip E */
	return (t + 1);
}

// <array-type> ::= A <positive dimension number> _ <element type>
//              ::= A [<dimension expression>] _ <element type>
static const char *
parse_array_type(const char *first, const char *last, cpp_db_t *db)
{
	ASSERT3U(first[0], ==, 'A');

	if (last - first < 3)
		return (first);

	const char *t = first + 1;
	const char *t1 = NULL;

	if (t[0] != '_') {
		if (is_digit(t[0]) && t[0] != '0') {
			t1 = parse_number(t, last);
			if (t1 == last)
				return (first);

			nadd_l(db, t, (size_t)(t1 - t));
		} else {
			t1 = parse_expression(t, last, db);
			if (t1 == last || t == t1)
				return (first);
		}

		if (t1[0] != '_')
			return (first);

		t = t1;
	} else {
		nadd_l(db, "", 0);
	}

	ASSERT3U(t[0], ==, '_');

	t1 = parse_type(t + 1, last, db);
	if (t1 == t + 1)
		return (first);

	/*
	 * if we have  " [xxx]" already, want new result to be
	 * " [yyy][xxx]"
	 */
	str_t *r = &name_top(&db->cpp_name)->strp_r;
	if (r->str_len > 1 || r->str_s[0] == ' ' || r->str_s[1] == '[')
		str_erase(r, 0, 1);

	nfmt(db, "{1:L}", " [{0}]{1:R}");
	return (t1);
}

// <pointer-to-member-type> ::= M <class type> <member type>
static const char *
parse_pointer_to_member_type(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 3)
		return (first);

	ASSERT3U(first[0], ==, 'M');

	const char *t1 = first + 1;
	const char *t2 = NULL;

	t2 = parse_type(t1, last, db);
	if (t1 == t2)
		return (first);

	t1 = t2;
	t2 = parse_type(t1, last, db);
	if (t1 == t2)
		return (first);

	str_pair_t *func = name_top(&db->cpp_name);

	if (str_length(&func->strp_r) > 0 && func->strp_r.str_s[0] == '(')
		nfmt(db, "{0:L}({1}::*", "){0:R}");
	else
		nfmt(db, "{0:L} {1}::*", "{0:R}");

	return (t2);
}

//  <ref-qualifier> ::= R                   # & ref-qualifier
//  <ref-qualifier> ::= O                   # && ref-qualifier

// <function-type> ::= F [Y] <bare-function-type> [<ref-qualifier>] E
static const char *
parse_function_type(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	ASSERT3U(first[0], ==, 'F');

	const char *t = first + 1;

	/* extern "C" */
	if (t[0] == 'Y')
		t++;

	const char *t1 = parse_type(t, last, db);
	if (t1 == t)
		return (first);

	size_t n = nlen(db);
	int ref_qual = 0;

	while (t != last && t[0] != 'E') {
		if (t[0] == 'v') {
			t++;
			continue;
		}

		if (t[0] == 'R' && t + 1 != last && t[1] == 'E') {
			ref_qual = 1;
			t++;
			continue;
		}

		if (t[0] == 'O' && t + 1 != last && t[1] == 'E') {
			ref_qual = 2;
			t++;
			continue;
		}


		t1 = parse_type(t, last, db);
		if (t1 == t || t == last)
			return (first);

		t = t1;
	}

	if (NAMT(db, n) > 0)
		njoin(db, NAMT(db, n), ", ");
	else
		nadd_l(db, "", 0);

	nfmt(db, "({0})", NULL);

	switch (ref_qual) {
	case 1:
		nfmt(db, "{0} &", NULL);
		break;
	case 2:
		nfmt(db, "{0} &&", NULL);
		break;
	}

	nfmt(db, "{1:L} ", "{0}{1:R}");
	return (t);
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
		*cv |= CPP_QUAL_RESTRICT;
		first++;
	}
	if (first != last && first[0] == 'V') {
		*cv |= CPP_QUAL_VOLATILE;
		first++;
	}
	if (first != last && first[0] == 'K') {
		*cv |= CPP_QUAL_CONST;
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

	if (first == last || (first[0] != 'n' && !is_digit(first[0])))
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

static inline boolean_t
is_upper(int c)
{
	if (c < 'A' || c > 'Z')
		return (B_FALSE);
	return (B_TRUE);
}

static boolean_t
nempty(cpp_db_t *db)
{
	return (name_empty(&db->cpp_name));
}

static size_t
nlen(cpp_db_t *db)
{
	return (name_len(&db->cpp_name));
}

static void
nadd_l(cpp_db_t *db, const char *s, size_t len)
{
	CK(name_add(&db->cpp_name, s, len, NULL, 0));
}

static void
njoin(cpp_db_t *db, size_t amt, const char *sep)
{
	name_t *nm = &db->cpp_name;

	CK(name_join(nm, amt, sep));
}

static void
nfmt(cpp_db_t *db, const char *fmt_l, const char *fmt_r)
{
	CK(name_fmt(&db->cpp_name, fmt_l, fmt_r));
}

static void
save_top(cpp_db_t *db)
{
	CK(sub_save(&db->cpp_subs, &db->cpp_name, 1));
}

static void
sub(cpp_db_t *db, size_t n)
{
	CK(sub_substitute(&db->cpp_subs, n, &db->cpp_name));
}

static void
db_init(cpp_db_t *db, sysdem_ops_t *ops)
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
