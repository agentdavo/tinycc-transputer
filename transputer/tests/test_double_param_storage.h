#ifndef TEST_DOUBLE_PARAM_STORAGE_H
#define TEST_DOUBLE_PARAM_STORAGE_H
struct pair { double x, y; int tag; };
double tp_external_double(double x, double y, int tag);
struct pair tp_external_pair(double x, double y);
#endif
