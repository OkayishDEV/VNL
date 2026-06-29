#pragma once
#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

typedef struct {
    int fd;
    char **buf_ptr;
    size_t *size_ptr;
    char *buf;
    size_t cap;
    size_t len;
    int is_memstream;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list ap);
int asprintf(char **strp, const char *fmt, ...);
int vasprintf(char **strp, const char *fmt, va_list ap);
int fileno(FILE *stream);
FILE *fdopen(int fd, const char *mode);
FILE *fopen(const char *pathname, const char *mode);
int fclose(FILE *stream);

int putchar(int c);
int puts(const char *s);
int fputs(const char *s, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fflush(FILE *stream);
FILE *open_memstream(char **ptr, size_t *sizeloc);
