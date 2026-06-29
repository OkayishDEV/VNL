#pragma once
#include "types.h"
#include "heap.h"
#include "math_kernel.h"

#define malloc(sz)         kmalloc(sz)
#define free(p)            kfree(p)
#define realloc(p, sz)     krealloc(p, sz)
