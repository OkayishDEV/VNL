#pragma once
#include "types.h"

void vnl_fabs(double x, double *out);
void vnl_floor(double x, double *out);
void vnl_ceil(double x, double *out);
void vnl_sqrt(double x, double *out);
void vnl_fmod(double x, double y, double *out);
void vnl_cos(double x, double *out);
void vnl_acos(double x, double *out);
void vnl_pow(double x, double y, double *out);
int abs(int x);

#define fabs(x)   ({ double _r; vnl_fabs(x, &_r); _r; })
#define floor(x)  ({ double _r; vnl_floor(x, &_r); _r; })
#define ceil(x)   ({ double _r; vnl_ceil(x, &_r); _r; })
#define sqrt(x)   ({ double _r; vnl_sqrt(x, &_r); _r; })
#define fmod(x,y) ({ double _r; vnl_fmod(x, y, &_r); _r; })
#define cos(x)    ({ double _r; vnl_cos(x, &_r); _r; })
#define acos(x)   ({ double _r; vnl_acos(x, &_r); _r; })
#define pow(x,y)  ({ double _r; vnl_pow(x, y, &_r); _r; })
