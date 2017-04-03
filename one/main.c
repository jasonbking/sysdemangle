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
    "_ZN5Casts8implicitILj4EEEvPN9enable_ifIXstT_EvE4typeE",
    "void Casts::implicit<4u>(enable_if<sizeof (4u), void>::type*)"
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
