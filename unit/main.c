
//  main.c
//  unit
//
//  Created by Jason King on 3/24/17.
//  Copyright © 2017 Jason King. All rights reserved.
//

#include <stdio.h>
#include "str.h"
#include "cpp.h"

int main(int argc, const char * argv[]) {
    str_t s = { 0 };

    str_init(&s, sysdem_alloc_default, "initial value", 0);
    str_insert(&s, 8, "(...)", 0);
    (void) printf("%.*s\n", (int) s.str_len, s.str_s);

    str_append(&s, " hi there", 0);
    (void) printf("%.*s\n", (int) s.str_len, s.str_s);

    
    return 0;
}
