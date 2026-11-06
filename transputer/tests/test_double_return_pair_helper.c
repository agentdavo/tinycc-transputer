#include "test_double_return_pair.h"
double tp_return_identity(double x) { return x; }
double tp_return_mix(double x, double y) { return x * 2.0 + y; }

double tp_return_read(const double *p) { return *p; }

double tp_return_literal(int which)
{
    switch(which) {
    case 0:return 0.0;
    case 1:return -0.0;
    case 2:return 1.0;
    case 3:return -1.0;
    case 4:return 0x1.23456789abcdep+17;
    case 5:return 0x1.000008p+0;
    case 6:return 0x0.0000000000001p-1022;
    case 7:return 0x1.fffffffffffffp+1023;
    default:return -0x1.23456789abcdep+17;
    }
}

long double tp_return_long_literal(void) { return 1.25L; }
