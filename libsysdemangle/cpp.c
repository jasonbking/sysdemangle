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
#define RLEN(f, l) ((size_t)(l) - (f))
#define NAMT(db, n) (nlen(db) - n)

static inline boolean_t is_digit(int);

static boolean_t nempty(cpp_db_t *);
static size_t nlen(cpp_db_t *);
static void nadd_l(cpp_db_t *, const char *, size_t);
static void njoin(cpp_db_t *, size_t, const char *);
static void nfmt(cpp_db_t *, const char *, const char *);

static void save_top(cpp_db_t *);

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
static const char *parse_template_param(const char *, const char *, cpp_db_t *);
static const char *parse_decltype(const char *, const char *, cpp_db_t *);
static const char *parse_template_args(const char *, const char *, cpp_db_t *);
static const char *parse_unqualified_name(const char *, const char *,
    cpp_db_t *);
static const char *parse_template_arg(const char *, const char *, cpp_db_t *);
static const char *parse_expr_primary(const char *, const char *, cpp_db_t *);
static const char *parse_type(const char *, const char *, cpp_db_t *);
static const char *parse_expr(const char *, const char *, cpp_db_t *);
static const char *parse_binary_expr(const char *, const char *,
    const char *, cpp_db_t *);
static const char *parse_prefix_expr(const char *, const char *,
    const char *, cpp_db_t *);
static const char *parse_gs(const char *, const char *, cpp_db_t *);
static const char *parse_idx_expr(const char *, const char *, cpp_db_t *);
static const char *parse_mm_expr(const char *, const char *, cpp_db_t *);
static const char *parse_pp_expr(const char *, const char *, cpp_db_t *);
static const char *parse_trinary_expr(const char *, const char *, cpp_db_t *);
static const char *parse_function_param(const char *, const char *, cpp_db_t *);
static const char *parse_unresolved_name(const char *, const char *,
    cpp_db_t *);
static const char *parse_noexcept_expr(const char *, const char *, cpp_db_t *);
static const char *parse_alignof(const char *, const char *, cpp_db_t *);

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
	nfmt(db, " ({0})");

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

	nfmt(db, "invocation function for block in {0}");
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
		goto error;

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

static const char *
parse_nested_name(const char *first, const char *last,
    boolean_t *ends_with_template_args, cpp_db_t *db)
{
	// XXX: TODO
	return (NULL);
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
		t = parse_expr(first + 1, last, db);
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
			t = parse_expr_primary(first + 1, last, db));
		else
			t = parse_encoding(first + 2, last, db);
		break;

	default:
		t = parse_type(first + 1, last, db);
	}

	return (t);
}

/*BEGIN CSTYLED*/
/*
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
 */
/*END CSTYLED*/

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

static struct expr_dispatch_s {
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
	PN("cc", parse_const_cast_expr),
	PN("cl", parse_call_expr),
	PA("cm", ",", parse_binary_expr),
	PA("co", "~", parse_prefix_expr),
	PN("cv", parse_conv_expr),
	PN("da", parse_del_expr),
	PA("dV", "/=", parse_binary_expr),
	PN("dc", parse_dynamic_cast_expr),
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
	PA("rS", ">>=", parse_binary_expr)
	PN("rc", parse_reinterpret_cast_expr),
	PA("rm", "%", parse_binary_expr),
	PA("rs", ">>", parse_binary_expr),
	PN("sc", parse_static_cast_expr),
	PN("sp", parse_pack_expansion),
	PN("sr", parse_unresolved_name),
	PN("st", parse_sizeof_type_expr),
	PN("sz", parse_sizeof_expr_expr),
	PN("te", parse_typeid_expr),
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

	const char *t = parse_expression(first + 2);
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

	name_fmt(db, "({0})[{1}]", NULL);
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
 * at <type>		# alignof (a type)
 * az <expression>	# alignof (a expression)
 */
static const char *
parse_alignof(const char *first, const char *last, cpp_db_t *db)
{
	if (last - first < 2)
		return (first);

	const char *(*)(const char *, const char *, cpp_db_t *) fn;

	fn = (first[1] == 't') ? parse_type : parse_expression;

	const char *t = fn(first + 2, last, db);
	if (t == first + 2)
		return (first);

	nfmt(db, "alignof ({0})", NULL);
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
		return (parse_operator_name(first, last, db))
	}
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
