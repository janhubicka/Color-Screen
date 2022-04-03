#ifndef RENDEINTERPOLATE_H
#define RENDEINTERPOLATE_H
#include <netpbm/ppm.h>
#include "render.h"
#include "screen.h"
class render_interpolate : public render_to_scr
{
public:
  render_interpolate (scr_to_img_parameters param, image_data &img, int dst_maxval);
  ~render_interpolate ();
  void precompute (double xmin, double ymin, double xmax, double ymax);
  void render_pixel (double x, double y, int *r, int *g, int *b)
  {
    render_pixel_scr (x - m_scr_xshift, y - m_scr_yshift, r, g, b);
  }
  void render_pixel_img (double x, double y, int *r, int *g, int *b)
  {
    double xx, yy;
    m_scr_to_img.to_scr (x, y, &xx, &yy);
    render_pixel_scr (xx, yy, r, g, b);
  }
  void precompute_all ()
  {
    precompute (-m_scr_xshift, -m_scr_yshift, -m_scr_xshift + m_img.width, -m_scr_yshift + m_scr_height);
  }
  void precompute_img_range (double x1, double y1, double x2, double y2)
  {
    int xshift, yshift, width, height;
    m_scr_to_img.get_range (x1, y1, x2, y2, &xshift, &yshift, &width, &height);
    precompute (-xshift, -yshift, -xshift + width, -yshift + height);
  }
  void set_precise ()
  {
    m_precise = true;
  }
  void set_adjust_luminosity ()
  {
    m_adjust_luminosity = true;
  }
  void set_screen (double radius);
private:
  int m_prec_xshift, m_prec_yshift, m_prec_width, m_prec_height;
  double *m_prec_red;
  double *m_prec_green;
  double *m_prec_blue;
  bool m_precise, m_adjust_luminosity;
  screen *m_screen;

  double &prec_blue (int x, int y) { return m_prec_blue [y * m_prec_width * 2 + x];}
  double &prec_red (int x, int y) { return m_prec_red [y * m_prec_width + x];}
  double &prec_green (int x, int y) { return m_prec_green [y * m_prec_width + x];}
  double &dufay_prec_blue (int x, int y) { return m_prec_blue [y * m_prec_width + x];}
  double &dufay_prec_red (int x, int y) { return m_prec_red [y * m_prec_width * 2 + x];}
  double &dufay_prec_green (int x, int y) { return m_prec_green [y * m_prec_width + x];}
  void render_pixel_scr (double x, double y, int *r, int *g, int *b);

  /* Diagonal cooredinates have coordiate vectors (0.5,0.5) and (-0.5,0.5)  */
  inline void to_diagonal_coordinates (double x, double y, double *xx, double *yy)
  {
    *xx = x - y;
    *yy = x + y;
  }
  /* Green pixel in diagonal coordinates.  */
  double prec_diag_green (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return prec_green (xx / 2 /*+ (yy&1)*/, yy);
  }
  /* Green pixel in diagonal coordinates.  */
  double prec_diag_red (unsigned int x, unsigned int y)
  {
     unsigned int xx = x + y;
     unsigned int yy = -x + y;
     return prec_red (xx / 2, yy);
  }
};
#endif