//
//  main.c
//  one
//
//  Created by Jason King on 4/1/17.
//  Copyright © 2017 Jason King. All rights reserved.
//
#include <stdio.h>
#include "sysdemangle.h"

static struct {
    const char *mangled;
    const char *demangled;
} test = {
    "_ZN12_GLOBAL__N_1L11static_condE",
    "(anonymous namespace)::static_cond"
};



int main(int argc, const char * argv[]) {
    printf("%s\n\n", test.mangled);
    printf("exp: %s\n", test.demangled);

    char *out = NULL;
    char *res = sysdemangle(test.mangled, NULL, &out);

    if (res != NULL)
        printf("res: %s\n", res);

    printf("\n%s\n", out);
    return 0;
}
