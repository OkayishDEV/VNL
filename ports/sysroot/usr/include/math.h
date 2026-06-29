#pragma once

#define M_PI 3.14159265358979323846

#define fabs(x)   __builtin_fabs(x)
#define floor(x)  __builtin_floor(x)
#define ceil(x)   __builtin_ceil(x)
#define sqrt(x)   __builtin_sqrt(x)
#define exp(x)    __builtin_exp(x)
#define log(x)    __builtin_log(x)
#define isnan(x)  __builtin_isnan(x)

#define fabsf(x)  __builtin_fabsf(x)
#define floorf(x) __builtin_floorf(x)
#define ceilf(x)  __builtin_ceilf(x)
#define sqrtf(x)  __builtin_sqrtf(x)
#define expf(x)   __builtin_expf(x)
#define logf(x)   __builtin_logf(x)
#define cosf(x)   __builtin_cosf(x)
#define sinf(x)   __builtin_sinf(x)

double pow(double x, double y);
double cos(double x);
double acos(double x);
double sin(double x);
double tan(double x);
double round(double x);
double fmod(double x, double y);
double atan2(double y, double x);
