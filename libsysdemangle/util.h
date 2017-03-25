//
//  util.h
//  libsysdemangle
//
//  Created by Jason King on 3/22/17.
//  Copyright © 2017 Jason King. All rights reserved.
//

#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>
#include "libsysdemangle.h"

void *zalloc(sysdem_alloc_t *, size_t);
void sysdemfree(sysdem_alloc_t *, void *, size_t);

#endif /* _UTIL_H */
