#include "test_double_param_storage.h"
double tp_external_double(double x, double y, int tag)
{
    x += 1.0;
    return x + y * 2.0 + tag;
}
struct pair tp_external_pair(double x, double y)
{
    struct pair p;
    p.x = x + 1.0;
    p.y = y + 2.0;
    p.tag = 7;
    return p;
}
