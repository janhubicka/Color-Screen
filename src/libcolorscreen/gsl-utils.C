#include "gsl-utils.h"
namespace colorscreen
{
/* Debug facility.  */
int
print_matrix (FILE *f, const char *name, const gsl_matrix *m)
{
  int status, n = 0;
  printf ("Matrix %s\n", name);

  for (size_t i = 0; i < m->size1; i++)
    {
      for (size_t j = 0; j < m->size2; j++)
        {
          if ((status = fprintf (f, "%4.2f ", gsl_matrix_get (m, i, j))) < 0)
            return -1;
          n += status;
        }

      if ((status = fprintf (f, "\n")) < 0)
        return -1;
      n += status;
    }

  return n;
}

int
print_system (FILE *f, const gsl_matrix *m, gsl_vector *v, gsl_vector *w,
              gsl_vector *c)
{
  int status, n = 0;

  printf ("Solution:\n");

  if (c)
    for (size_t i = 0; i < m->size2; i++)
      {
        if ((status = fprintf (f, "%+7.4f ", gsl_vector_get (c, i))) < 0)
          return -1;
        n += status;
      }
  printf ("\n");

  for (size_t i = 0; i < m->size1; i++)
    {
      double sol = 0;
      for (size_t j = 0; j < m->size2; j++)
        {
          if ((status = fprintf (f, "%+7.4f ", gsl_matrix_get (m, i, j))) < 0)
            return -1;
          n += status;
          if (c)
            sol += gsl_vector_get (c, j) * gsl_matrix_get (m, i, j);
        }

      if ((status = fprintf (f, "| %+7.4f", gsl_vector_get (v, i))) < 0)
        return -1;
      n += status;

      if (w)
        {
          if ((status = fprintf (f, " (weight %+7.4f)", gsl_vector_get (w, i)))
              < 0)
            return -1;
          n += status;
        }
      if (c)
        {
          if ((status = fprintf (f, " (solution %+7.4f; error %f)", sol,
                                 sol - gsl_vector_get (v, i))
                        < 0))
            return -1;
          n += status;
        }

      if ((status = fprintf (f, "\n")) < 0)
        return -1;
      n += status;
    }

  return n;
}
}
