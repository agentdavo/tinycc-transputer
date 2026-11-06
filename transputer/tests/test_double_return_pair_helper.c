#include "test_double_return_pair.h"
double tp_return_identity(double x) { return x; }
double tp_return_mix(double x, double y) { return x * 2.0 + y; }

double tp_return_read(const double *p) { return *p; }
