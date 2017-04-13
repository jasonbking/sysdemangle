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
    "_Z1fIiEDcT_",
    "decltype(auto) f<int>(int)"
};


int main(int argc, const char * argv[]) {
    printf("%s\n\n", test.mangled);
    printf("exp: %s\n", test.demangled);

    char *res = sysdemangle(test.mangled, SYSDEM_LANG_CPP, NULL);

    if (res != NULL)
        printf("res: %s\n", res);

    return 0;
}
