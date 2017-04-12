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
    "_ZNSt16allocator_traitsISaIN4llvm3sys2fs18directory_iteratorEEE9constructIS3_IS3_EEEDTcl12_S_constructfp_fp0_spcl7forwardIT0_Efp1_EEERS4_PT_DpOS7_",
    "void test1::f<test1::X, int>(test1::X<int>)"
};


int main(int argc, const char * argv[]) {
    printf("%s\n\n", test.mangled);
    printf("exp: %s\n", test.demangled);

    char *res = sysdemangle(test.mangled, SYSDEM_LANG_CPP, NULL);

    if (res != NULL)
        printf("res: %s\n", res);

    return 0;
}
