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
    "_ZSt13__bind_simpleIMSt6threadFvvESt17reference_wrapperIS0_EENSt19_Bind_simple_helperIT_IDpT0_EE6__typeEOS6_DpOS7_",
    "void test1::f<test1::X, int>(test1::X<int>)"
};


int main(int argc, const char * argv[]) {
    printf("%s\n\n", test.mangled);
    printf("exp: %s\n", test.demangled);

    char *out = NULL;
    char *res = sysdemangle(test.mangled, SYSDEM_LANG_CPP, NULL, &out);

    if (res != NULL)
        printf("res: %s\n", res);

    printf("\n%s\n", out);
    return 0;
}
