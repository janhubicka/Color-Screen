#ifndef LOADSAVE_H
#define LOADSAVE_H
#include <stdio.h>
bool expect_keyword (FILE *f, const char *);
bool parse_bool (FILE *f, bool *val);
bool read_scalar (FILE *f, coord_t *);
#endif
