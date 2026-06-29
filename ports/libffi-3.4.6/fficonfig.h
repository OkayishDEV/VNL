
#pragma once
#define HAVE_MEMMOVE 1
#define HAVE_MEMCPY 1
#define HAVE_ALLOCA 1
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_VOID_P 8
#define STDC_HEADERS 1
#ifdef LIBFFI_ASM
# define FFI_HIDDEN(name) .hidden name
#else
# define FFI_HIDDEN __attribute__((visibility("hidden")))
#endif
#define FFI_BUILDING 1
#define FFI_STATIC_BUILD 1
#define FFI_SIZEOF_ARG 8
#define FFI_SIZEOF_JAVA_RAW 8
#define HAVE_AS_X86_PCREL 1
        