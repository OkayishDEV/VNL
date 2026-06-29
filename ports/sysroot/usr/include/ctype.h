#ifndef _CTYPE_H
#define _CTYPE_H

static inline int isdigit(int c) {
    return c >= '0' && c <= '9';
}

static inline int isxdigit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static inline int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

static inline int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static inline int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

#endif
