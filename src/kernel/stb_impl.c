#include "types.h"
#include "heap.h"
#include "math_kernel.h"
#include "string.h"

/* stb_image configuration */
#define STB_IMAGE_IMPLEMENTATION
#define STBIDEF static inline __attribute__((always_inline))
#define STBI_MALLOC(sz)           kmalloc(sz)
#define STBI_FREE(p)              kfree(p)
#define STBI_REALLOC(p,newsz)     krealloc(p,newsz)
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_ASSERT(x)

/* Include stb_image from include directory */
#include "stb_image.h"

/* stb_truetype configuration */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_DEF static inline __attribute__((always_inline))
#define STBTT_malloc(sz,u)        ((void)(u), kmalloc(sz))
#define STBTT_free(p,u)           ((void)(u), kfree(p))
#define STBTT_assert(x)
#define STBTT_sqrt(x)             sqrt(x)
#define STBTT_pow(x,y)            pow(x,y)
#define STBTT_floor(x)            floor(x)
#define STBTT_ceil(x)             ceil(x)
#define STBTT_fabs(x)             fabs(x)
#define STBTT_fmod(x,y)           fmod(x,y)
#define STBTT_cos(x)              cos(x)
#define STBTT_acos(x)             acos(x)

#include "stb_truetype.h"
