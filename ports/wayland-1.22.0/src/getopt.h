#pragma once
#include <string.h>
#include <stdio.h>

#define no_argument 0
#define required_argument 1
#define optional_argument 2

struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

#ifdef __cplusplus
extern "C" {
#endif

__declspec(selectany) char *optarg = NULL;
__declspec(selectany) int optind = 1;
__declspec(selectany) int opterr = 1;
__declspec(selectany) int optopt = 0;

static inline int getopt_long(int argc, char *const argv[], const char *optstring,
                       const struct option *longopts, int *longindex) {
    if (optind >= argc) return -1;
    char *arg = argv[optind];
    if (arg[0] != '-' || arg[1] == '\0') return -1;
    
    if (arg[1] == '-') {
        const char *name = arg + 2;
        char *equal = strchr(name, '=');
        size_t len = equal ? (size_t)(equal - name) : strlen(name);
        
        for (int i = 0; longopts[i].name != NULL; i++) {
            if (strncmp(longopts[i].name, name, len) == 0 && longopts[i].name[len] == '\0') {
                if (longopts[i].has_arg == required_argument) {
                    if (equal) {
                        optarg = equal + 1;
                    } else {
                        optind++;
                        if (optind >= argc) {
                            fprintf(stderr, "Option --%s requires an argument\n", longopts[i].name);
                            return '?';
                        }
                        optarg = argv[optind];
                    }
                }
                optind++;
                if (longopts[i].flag) {
                    *longopts[i].flag = longopts[i].val;
                    return 0;
                }
                if (longindex) *longindex = i;
                return longopts[i].val;
            }
        }
        fprintf(stderr, "Unknown option %s\n", arg);
        optind++;
        return '?';
    } else {
        char opt = arg[1];
        char *p = strchr(optstring, opt);
        if (!p) {
            fprintf(stderr, "Unknown option -%c\n", opt);
            optind++;
            return '?';
        }
        if (p[1] == ':') {
            if (arg[2] != '\0') {
                optarg = arg + 2;
            } else {
                optind++;
                if (optind >= argc) {
                    fprintf(stderr, "Option -%c requires an argument\n", opt);
                    return '?';
                }
                optarg = argv[optind];
            }
        }
        optind++;
        return opt;
    }
}

#ifdef __cplusplus
}
#endif
