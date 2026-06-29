#include "types.h"

#define VNL_PI 3.14159265358979323846

void vnl_fabs(double x, double *out) {
    *out = x < 0.0 ? -x : x;
}

void vnl_floor(double x, double *out) {
    long long i = (long long)x;
    double d = (double)i;
    if (x < 0.0 && x != d) {
        *out = d - 1.0;
    } else {
        *out = d;
    }
}

void vnl_ceil(double x, double *out) {
    long long i = (long long)x;
    double d = (double)i;
    if (x > 0.0 && x != d) {
        *out = d + 1.0;
    } else {
        *out = d;
    }
}

void vnl_sqrt(double x, double *out) {
    if (x <= 0.0) {
        *out = 0.0;
        return;
    }
    double z = x;
    for (int i = 0; i < 20; i++) {
        z = 0.5 * (z + x / z);
    }
    *out = z;
}

void vnl_fmod(double x, double y, double *out) {
    if (y == 0.0) {
        *out = 0.0;
        return;
    }
    double temp;
    vnl_floor(x / y, &temp);
    *out = x - temp * y;
}

void vnl_cos(double x, double *out) {
    // reduce x to [-PI, PI]
    double temp;
    vnl_fmod(x + VNL_PI, 2.0 * VNL_PI, &temp);
    if (temp < 0.0) temp += 2.0 * VNL_PI;
    temp -= VNL_PI;
    
    // Taylor series: 1 - x^2/2! + x^4/4! - x^6/6! + x^8/8! - x^10/10!
    double xx = temp * temp;
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i <= 10; i++) {
        term *= -xx / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    *out = sum;
}

void vnl_acos(double x, double *out) {
    if (x >= 1.0) {
        *out = 0.0;
        return;
    }
    if (x <= -1.0) {
        *out = VNL_PI;
        return;
    }
    
    double y = (1.0 - x) * (VNL_PI / 2.0);
    for (int i = 0; i < 15; i++) {
        double cy, sy;
        vnl_cos(y, &cy);
        vnl_sqrt(1.0 - cy * cy, &sy);
        if (sy < 1e-9) break;
        y = y + (cy - x) / sy;
    }
    *out = y;
}

void vnl_pow(double x, double y, double *out) {
    if (y == 0.0) {
        *out = 1.0;
        return;
    }
    if (x == 0.0) {
        *out = 0.0;
        return;
    }
    
    double diff;
    vnl_fabs(y - (1.0 / 3.0), &diff);
    if (diff < 1e-4) {
        double z = x > 0.0 ? x : -x;
        double val = z;
        for (int i = 0; i < 20; i++) {
            val = (2.0 * val + z / (val * val)) / 3.0;
        }
        *out = x > 0.0 ? val : -val;
        return;
    }
    
    double fl;
    vnl_floor(y, &fl);
    if (fl == y) {
        double res = 1.0;
        long long n = (long long)y;
        double base = x;
        if (n < 0) {
            base = 1.0 / base;
            n = -n;
        }
        while (n > 0) {
            if (n & 1) res *= base;
            base *= base;
            n >>= 1;
        }
        *out = res;
        return;
    }
    
    *out = x;
}

/* absolute value for integers */
int abs(int x) {
    return x < 0 ? -x : x;
}
