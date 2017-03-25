//
//  cpp.h
//  libsysdemangle
//
//  Created by Jason King on 3/23/17.
//  Copyright © 2017 Jason King. All rights reserved.
//

#ifndef cpp_h
#define cpp_h

#include <stdio.h>
#include "sysdemangle.h"
#include "str.h"

typedef struct name_s {
	str_pair_t	*nm_items;
	sysdem_alloc_t	*nm_ops;
	size_t		nm_len;
	size_t		nm_size;
} name_t;

void name_clear(name_t *);
void name_init(name_t *, sysdem_alloc_t *);
void name_fini(name_t *);
size_t name_len(const name_t *);
boolean_t name_empty(const name_t *);
boolean_t name_add(name_t *, const char *, size_t, const char *, size_t);
boolean_t name_add_str(name_t *, str_t *, str_t *);
boolean_t name_join(name_t *, size_t, const char *);
boolean_t name_fmt(name_t *, const char *);
str_pair_t *name_at(name_t *, size_t);
str_pair_t *name_top(name_t *);

typedef struct sub_s {
	name_t		*sub_items;
	sysdem_alloc_t	*sub_ops;
	size_t		sub_len;
	size_t		sub_size;
} sub_t;

void sub_clear(sub_t *);
void sub_init(sub_t *, sysdem_alloc_t *);
void sub_fini(sub_t *);
boolean_t sub_save(sub_t *, const name_t *);
boolean_t sub_substitute(sub_t *, size_t, name_t *);
boolean_t sub_empty(const sub_t *);

typedef struct templ_s {
	sub_t		*tpl_items;
	sysdem_alloc_t	*tpl_ops;
	size_t		tpl_len;
	size_t		tpl_size;
} templ_t;

void templ_init(templ_t *, sysdem_alloc_t *);
void templ_fini(templ_t *);
boolean_t templ_empty(const templ_t *);

#endif /* cpp_h */
