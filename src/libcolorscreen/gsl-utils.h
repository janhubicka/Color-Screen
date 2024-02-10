#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
int print_system(FILE *f, const gsl_matrix *m, gsl_vector *v, gsl_vector *w = 0, gsl_vector *c = 0);
int print_matrix (FILE *f, const char *name, const gsl_matrix *m);
