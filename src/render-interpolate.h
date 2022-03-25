#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include <netpbm/ppm.h>
#include "render.h"
#include "screen.h"
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters param, gray **img, int img_width, int img_height, int maxval, int dst_maxval, int scale);
  void render_row (int y, pixel ** outrow);
private:
  static const int NBLUE = 8;			/* We need 6 rows of blue.  */
  static const int NRED = 8;			/* We need 7 rows of the others.  */

  inline int getmatrixsample (double **sample, int *shift, int pos, int xp, int x, int y);

  double *m_redsample[8];
  double *m_greensample[8];
  double *m_bluesample[NBLUE];
  int m_bluepos[NBLUE];
  int m_redpos[8];
  int m_redshift[8];
  int m_greenpos[8];
  int m_greenshift[8];
  int m_redp, m_greenp, m_bluep;
  int m_scale;
};
#endif
