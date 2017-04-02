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
    "_ZGVNSt7num_getIcSt19istreambuf_iteratorIcSt11char_traitsIcEEE2idE",
    "guard variable for std::num_get<char, std::istreambuf_iterator<char, std::char_traits<char> > >::id"
};



int main(int argc, const char * argv[]) {
    char *res = sysdemangle(test.mangled, NULL, NULL);

    if (res != NULL)
        printf("%s\n%s\n", test.demangled, res);

    return 0;
}
