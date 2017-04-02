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
    "_ZGVZN12_GLOBAL__N_119get_safe_base_mutexEPvE15safe_base_mutex",
    "guard variable for (anonymous namespace)::get_safe_base_mutex(void*)::safe_base_mutex"
};



int main(int argc, const char * argv[]) {
    char *res = sysdemangle(test.mangled, NULL, NULL);

    if (res != NULL)
        printf("%s\n%s\n", test.demangled, res);

    return 0;
}
