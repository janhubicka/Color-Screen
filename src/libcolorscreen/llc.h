#ifndef LLC_H
#define LLC_H
#include "include/colorscreen.h"
class llc
{
public:
  llc () : m_width (0), m_height (0), m_weights (NULL)
  {
  }
  bool
  alloc (int width, int height)
  {
    m_weights = (luminosity_t *)malloc (width * height * sizeof (luminosity_t));
    m_width = width;
    m_height = height;
    return (m_weights != NULL);
  }
  void
  set_weight (int x, int y, luminosity_t weight)
  {
	  //fprintf (stderr, "%f\n", weight);
    m_weights[y * m_width + x] = weight;
  }
  luminosity_t apply (float val, int width, int height, int x, int y)
  {
    if (x < 0)
      x = 0;
    if (x >= width)
      x = width;
    if (y < 0)
      y = 0;
    if (y >= height)
      y = height;
    int xx = (x * m_width) / width;
    int yy = (y * m_height) / height;
    if (xx < 0 || xx >= m_width || yy < 0 || yy >= m_height)
      fprintf (stderr, "%i %i %i %i %f\n", x, y, xx, yy, m_weights[yy * m_width + xx]);
    return val /* 32000 */* m_weights[yy * m_width + xx];
  }
  ~llc ()
  {
    if (m_weights)
      free (m_weights);
  }
  static llc *load (FILE *f, bool verbose = false);
private:
  int m_width, m_height;
  luminosity_t *m_weights;
};
#endif
