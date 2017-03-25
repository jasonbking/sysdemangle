
//  main.c
//  unit
//
//  Created by Jason King on 3/24/17.
//  Copyright © 2017 Jason King. All rights reserved.
//

#include <stdio.h>
#include "str.h"
#include "cpp.h"

void
print_s(const str_t *s) {
    if (s->str_len == 0 || s->str_s == NULL) {
        return;
    }
    printf("%.*s", (int)s->str_len, s->str_s);
}

void
print_sp(str_pair_t *sp) {
    putc('{', stdout);
    print_s(&sp->strp_l);
    putc(',', stdout);
    print_s(&sp->strp_r);
    putc('}', stdout);
}

void
print_n(name_t *n) {
    printf("--------------\n");
    str_pair_t *sp = name_top(n);
    for (size_t i = 0; i < n->nm_len; i++, sp--) {
        printf("[%zu] ", i);
        print_sp(sp);
        putc('\n', stdout);
    }
    printf("--------------\n\n");
}

int main(int argc, const char * argv[]) {
    str_t s = { 0 };
    name_t n = { 0 };

    str_init(&s, sysdem_alloc_default, "initial value", 0);
    str_insert(&s, 8, "(...)", 0);
    (void) printf("%.*s\n", (int) s.str_len, s.str_s);

    str_append(&s, " hi there", 0);
    (void) printf("%.*s\n", (int) s.str_len, s.str_s);

    name_init(&n, sysdem_alloc_default);
    printf("Empty:\n"); print_n(&n);

    name_add(&n, "test 1", 0, NULL, 0);
    print_n(&n);

    name_add(&n, "test 2 L", 0, "test 2 R", 0);
    print_n(&n);

    name_fmt(&n, "{0:L} ({1}) {0:R}");
    print_n(&n);

    name_add(&n, "something else", 0, NULL, 0);
    print_n(&n);

    name_join(&n, 2, " || ");
    print_n(&n);

    return 0;
}
