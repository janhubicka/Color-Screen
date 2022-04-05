#ifndef RENDER_H
#define RENDER_H
#include <math.h>
#include <algorithm>
#include <netpbm/pgm.h>
#include <netpbm/ppm.h>
#include "scr-to-img.h"
#include "color.h"

/* Scanned image descriptor.  */
struct image_data
{
  /* Grayscale scan.  */
  gray **data;
  /* Optional color scan.  */
  pixel **rgbdata;
  /* Dimensions of image data.  */
  int width, height;
  /* Maximal value of the image data.  */
  int maxval;
  /* Gamma of the scan (1.0 for linear scans 2.2 for sGray).  */
  double gamma;
};

/* Base class for rendering routines.  It holds
     - scr-to-img transformation info
     - the scanned image data
     - the desired range of input and output values
   and provides way to get a pixel at given screen or image coordinates.  */
class render
{
public:
  render (scr_to_img_parameters param, image_data &img, int dstmaxval);
  ~render ();
  inline double get_img_pixel (double x, double y);
  inline void get_img_rgb_pixel (double x, double y, double *r, double *g, double *b);
  inline double sample_img_square (double xc, double yc, double x1, double y1, double x2, double y2);
  inline double sample_scr_diag_square (double xc, double yc, double s);
  inline double sample_scr_square (double xc, double yc, double w, double h);
  inline double fast_get_img_pixel (double x, double y);
  inline double get_img_pixel_scr (double x, double y);
  void set_saturation (double s) { m_saturate = s; }
  void set_presaturation (double s) { m_presaturate = s; }
  void set_brightness (double b) { m_brightness = b; }
  void set_gray_range (int min, int max);
  void set_color_model (int m) { m_color_model = m; }
  void precompute_all ();
  void precompute (double, double, double, double) {precompute_all ();}
  void precompute_img_range (double, double, double, double) {precompute_all ();}
    
  static const int num_color_models = 4;
protected:
  inline double get_data (int x, int y);
  inline double get_data_red (int x, int y);
  inline double get_data_green (int x, int y);
  inline double get_data_blue (int x, int y);
  inline void set_color (double, double, double, int *, int *, int *);
  inline void set_color_luminosity (double, double, double, double, int *, int *, int *);

  /* Scanned image.  */
  image_data m_img;
  /* Transformation between screen and image coordinates.  */
  scr_to_img m_scr_to_img;
  /* Desired maximal value of output data (usually either 256 or 65536).  */
  int m_dst_maxval;
  /* Translates input gray values into normalized range 0...1 gamma 1.  */
  double *m_lookup_table;
  /* Translates back to gamma 2.  */
  double *m_out_lookup_table;
  /* Pre-saturation increase (this works on data collected from the scan before
     color model is applied and is intended to compensate for loss of sharpness.  */
  double m_presaturate;
  /* Saturation increase.  */
  double m_saturate;
  /* Brightness adjustments.  */
  double m_brightness;
  /* Gray range to boot to full contrast.  */
  int m_gray_min, m_gray_max;
  /* If true apply color model of Finlay taking plate.  */
  int m_color_model;
  /* Color matrix.  */
  matrix4x4 m_color_matrix;
};

/* Base class for renderes tha works in screen coordinates (so output image is
   geometrically corrected.  */
class render_to_scr : public render
{
public:
  render_to_scr (scr_to_img_parameters param, image_data &img, int dstmaxval)
    : render (param, img, dstmaxval)
  {
    m_scr_to_img.get_range (m_img.width, m_img.height, &m_scr_xshift, &m_scr_yshift, &m_scr_width, &m_scr_height);
  }
  /* This returns screen coordinate width of rendered output.  */
  int get_width ()
  {
    return m_scr_width;
  }
  /* This returns screen coordinate height of rendered output.  */
  int get_height ()
  {
    return m_scr_height;
  }
protected:
  /* Rectangular section of the screen to which the whole image fits.

     The section is having dimensions scr_width x scr_height and will
     start at position (-scr_xshift, -scr_yshift).  */
  int m_scr_xshift, m_scr_yshift;
  int m_scr_width, m_scr_height;
};

/* Set range of input grayscale that should be boosted to full contrast.  */

inline void
render::set_gray_range (int min, int max)
{
  m_gray_min = min;
  if (min == max)
    max++;
  m_gray_max = max;
}

/* Cubic interpolation helper.  */

static inline double
cubic_interpolate (double p0, double p1, double p2, double p3, double x)
{
  return p1 + 0.5 * x * (p2 - p0 +
			 x * (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 +
			      x * (3.0 * (p1 - p2) + p3 - p0)));
}

/* Get image data in normalized range 0...1.  */

inline double
render::get_data (int x, int y)
{
  return m_lookup_table [m_img.data[y][x]];
}

/* Get same for rgb data.  */

inline double
render::get_data_red (int x, int y)
{
  return m_lookup_table [m_img.rgbdata[y][x].r];
}

inline double
render::get_data_green (int x, int y)
{
  return m_lookup_table [m_img.rgbdata[y][x].g];
}

inline double
render::get_data_blue (int x, int y)
{
  return m_lookup_table [m_img.rgbdata[y][x].b];
}

inline double
cap_color (double val, double weight, double *diff, double *cnt_neg, double *cnt_pos)
{
  if (isnan (val))
    return 1;
  if (val < 0)
    {
      *cnt_neg += weight;
      *diff += val * weight;
      val = 0;
    }
  if (val > 1)
    {
      *cnt_pos += weight;
      *diff += (val - 1) * weight;
      val = 1;
    }
  return val;
}

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval.  */

inline void
render::set_color (double r, double g, double b, int *rr, int *gg, int *bb)
{
  double diff = 0;
  double cnt_neg = 0;
  double cnt_pos = 0;
  double r1 =r, g1= g, b1 = b;
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  double r2 =r, g2= g, b2 = b;
  r = cap_color (r, rwght, &diff, &cnt_neg, &cnt_pos);
  g = cap_color (g, gwght, &diff, &cnt_neg, &cnt_pos);
  b = cap_color (b, bwght, &diff, &cnt_neg, &cnt_pos);
#if 0
  if (fabs (diff) > 0.0001)
    {
      double lum = r * rwght + g * gwght + b * bwght;
      if (lum + diff < 0.00001)
	r = g = b = 0;
      else if (lum + diff > 0.99999)
	r = g = b = 1;
      else while (fabs (diff) > 0.0001)
	{
	  if (diff > 0)
	    {
	      double add = diff / (3 - cnt_pos);
	      if (r < 1)
		r += add;
	      if (g < 1)
		g += add;
	      if (b < 1)
		b += add;
	    }
	  if (diff < 0)
	    {
	      double add = diff / (3 - cnt_neg);
	      if (r > 0)
		r += add;
	      if (g > 0)
		g += add;
	      if (b > 0)
		b += add;
	    }
	  diff = 0;
	  cnt_neg = 0;
	  cnt_pos = 0;
	  r = cap_color (r, rwght, &diff, &cnt_neg, &cnt_pos);
	  g = cap_color (g, gwght, &diff, &cnt_neg, &cnt_pos);
	  b = cap_color (b, bwght, &diff, &cnt_neg, &cnt_pos);
	}
    }
#endif
  *rr = m_out_lookup_table [(int)(r * 65535.5)];
  *gg = m_out_lookup_table [(int)(g * 65535.5)];
  *bb = m_out_lookup_table [(int)(b * 65535.5)];
}

/* Compute color in the final gamma 2.2 and range 0...m_dst_maxval
   combining color and luminosity information.  */

inline void
render::set_color_luminosity (double r, double g, double b, double l, int *rr, int *gg, int *bb)
{
  m_color_matrix.apply_to_rgb (r, g, b, &r, &g, &b);
  r = std::min (1.0, std::max (0.0, r));
  g = std::min (1.0, std::max (0.0, g));
  b = std::min (1.0, std::max (0.0, b));
  l = std::min (1.0, std::max (0.0, l));
  double gr = (r * rwght + g * gwght + b * bwght);
  if (gr <= 0.00001 || l <= 0.00001)
    r = g = b = l;
  else
    {
      gr = l / gr;
      r *= gr;
      g *= gr;
      b *= gr;
    }
  r = std::min (1.0, std::max (0.0, r));
  g = std::min (1.0, std::max (0.0, g));
  b = std::min (1.0, std::max (0.0, b));

  *rr = m_out_lookup_table [(int)(r * 65535.5)];
  *gg = m_out_lookup_table [(int)(g * 65535.5)];
  *bb = m_out_lookup_table [(int)(b * 65535.5)];
}

/* Determine grayscale value at a given position in the image.  */

inline double
render::fast_get_img_pixel (double xp, double yp)
{
  int x = xp, y = yp;
  if (x < 0 || x >= m_img.width || y < 0 || y >= m_img.height)
    return 0;
  return render::get_data (x, y);
}


/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline double
render::get_img_pixel (double xp, double yp)
{
  double val;
  //return fast_get_img_pixel (xp, yp);

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= 0.5;
  yp -= 0.5;
  int sx = xp, sy = yp;

  if (sx < 1 || sx >= m_img.width - 2 || sy < 1 || sy >= m_img.height - 2)
    return 0;
  double rx = xp - sx, ry = yp - sy;
  val = cubic_interpolate (cubic_interpolate (get_data ( sx-1, sy-1), get_data (sx-1, sy), get_data (sx-1, sy+1), get_data (sx-1, sy+2), ry),
			   cubic_interpolate (get_data ( sx-0, sy-1), get_data (sx-0, sy), get_data (sx-0, sy+1), get_data (sx-0, sy+2), ry),
			   cubic_interpolate (get_data ( sx+1, sy-1), get_data (sx+1, sy), get_data (sx+1, sy+1), get_data (sx+1, sy+2), ry),
			   cubic_interpolate (get_data ( sx+2, sy-1), get_data (sx+2, sy), get_data (sx+2, sy+1), get_data (sx+2, sy+2), ry),
			   rx);
  return val;
}

/* Determine grayscale value at a given position in the image.
   Use bicubic interpolation.  */

inline void
render::get_img_rgb_pixel (double xp, double yp, double *r, double *g, double *b)
{
  double val;
  //return fast_get_img_pixel (xp, yp);

  /* Center of pixel [0,0] is [0.5,0.5].  */
  xp -= 0.5;
  yp -= 0.5;
  int sx = xp, sy = yp;

  if (sx < 1 || sx >= m_img.width - 2 || sy < 1 || sy >= m_img.height - 2)
    {
      *r = 0;
      *g = 0;
      *b = 0;
      return;
    }
  double rx = xp - sx, ry = yp - sy;
  *r = cubic_interpolate (cubic_interpolate (get_data_red ( sx-1, sy-1), get_data_red (sx-1, sy), get_data_red (sx-1, sy+1), get_data_red (sx-1, sy+2), ry),
			  cubic_interpolate (get_data_red ( sx-0, sy-1), get_data_red (sx-0, sy), get_data_red (sx-0, sy+1), get_data_red (sx-0, sy+2), ry),
			  cubic_interpolate (get_data_red ( sx+1, sy-1), get_data_red (sx+1, sy), get_data_red (sx+1, sy+1), get_data_red (sx+1, sy+2), ry),
			  cubic_interpolate (get_data_red ( sx+2, sy-1), get_data_red (sx+2, sy), get_data_red (sx+2, sy+1), get_data_red (sx+2, sy+2), ry),
			  rx);
  *g = cubic_interpolate (cubic_interpolate (get_data_green ( sx-1, sy-1), get_data_green (sx-1, sy), get_data_green (sx-1, sy+1), get_data_green (sx-1, sy+2), ry),
			  cubic_interpolate (get_data_green ( sx-0, sy-1), get_data_green (sx-0, sy), get_data_green (sx-0, sy+1), get_data_green (sx-0, sy+2), ry),
			  cubic_interpolate (get_data_green ( sx+1, sy-1), get_data_green (sx+1, sy), get_data_green (sx+1, sy+1), get_data_green (sx+1, sy+2), ry),
			  cubic_interpolate (get_data_green ( sx+2, sy-1), get_data_green (sx+2, sy), get_data_green (sx+2, sy+1), get_data_green (sx+2, sy+2), ry),
			  rx);
  *b = cubic_interpolate (cubic_interpolate (get_data_blue ( sx-1, sy-1), get_data_blue (sx-1, sy), get_data_blue (sx-1, sy+1), get_data_blue (sx-1, sy+2), ry),
			  cubic_interpolate (get_data_blue ( sx-0, sy-1), get_data_blue (sx-0, sy), get_data_blue (sx-0, sy+1), get_data_blue (sx-0, sy+2), ry),
			  cubic_interpolate (get_data_blue ( sx+1, sy-1), get_data_blue (sx+1, sy), get_data_blue (sx+1, sy+1), get_data_blue (sx+1, sy+2), ry),
			  cubic_interpolate (get_data_blue ( sx+2, sy-1), get_data_blue (sx+2, sy), get_data_blue (sx+2, sy+1), get_data_blue (sx+2, sy+2), ry),
			  rx);
}

/* Sample square patch with center xc and yc and x1/y1, x2/y2 determining a coordinates
   of top left and top right corner.  */
double
render::sample_img_square (double xc, double yc, double x1, double y1, double x2, double y2)
{
  double acc = 0, weights = 0;
  int xmin = std::max ((int)(std::min (std::min (std::min (xc - x1, xc + x1), xc - x2), xc + x2) - 0.5), 0);
  int xmax = std::min ((int)ceil (std::max(std::max (std::max (xc - x1, xc + x1), xc - x2), xc + x2) + 0.5), m_img.width - 1);
  /* If the resolution is too small, just sample given point.  */
  if (xmax-xmin < 2)
    return get_img_pixel (xc, yc);
  /* For bigger resolution we can sample few points in the square.  */
  if (xmax-xmin < 6)
    {
      /* Maybe this will give more reproducible results, but it is very slow.  */
      int samples = (sqrt (x1 * x1 + y1 * y1) + 0.5) * 2;
      double rec = 1.0 / samples;
      if (!samples)
	return get_img_pixel (xc, yc);
      for (int y = -samples ; y <= samples; y++)
	for (int x = -samples ; x <= samples; x++)
	  {
	    double w = 1 + (samples - abs (x) - abs (y));
	    if (w < 0)
	      continue;
	    acc += w * get_img_pixel (xc + (x1 * x + x2 * y) * rec, yc + (y1 * x + y2 * y) * rec);
	    weights += w;
	  }
    }
  /* Faster version of the above which does not need multiple calls to get_img_pixel.
     It however may suffer from banding when spots are too small.  */
  else
    {
      int ymin = std::max ((int)(std::min (std::min (std::min (yc - y1, yc + y1), yc - y2), yc + y2) - 0.5), 0);
      int ymax = std::min ((int)ceil (std::max(std::max (std::max (yc - y1, yc + y1), yc - y2), yc + y2) + 0.5), m_img.height - 1);
      matrix2x2 base (x1, x2,
		      y1, y2);
      matrix2x2 inv = base.invert ();
      for (int y = ymin; y <= ymax; y++)
	{
	  for (int x = xmin ; x <= xmax; x++)
	    {
	      double cx = x+0.5 -xc;
	      double cy = y+0.5 -yc;
	      double ccx, ccy;
	      inv.apply_to_vector (cx, cy, &ccx, &ccy);
	      double w = fabs (ccx) + fabs (ccy);

	      //if (w < 1)
		//printf ("%.1f ",w);
	      //else
		//printf ("    ",w);
	      if (w < 1)
		{
		  w = (1 - w);
		  acc += w * get_data (x, y);
		  weights += w;
		}
	    }
	    //printf ("\n");
	 }
    }
  if (weights)
    return acc / weights;
  return 0;
}

/* Sample diagonal square.
   Square is specified by its center and size of diagonal.  */
double
render::sample_scr_diag_square (double xc, double yc, double diagonal_size)
{
  double xxc, yyc, x1, y1, x2, y2;
  m_scr_to_img.to_img (xc, yc, &xxc, &yyc);
  m_scr_to_img.to_img (xc + diagonal_size / 2, yc, &x1, &y1);
  m_scr_to_img.to_img (xc, yc + diagonal_size / 2, &x2, &y2);
  return sample_img_square (xxc, yyc, x1 - xxc, y1 - yyc, x2 - xxc, y2 - yyc);
}

/* Sample diagonal square.
   Square is specified by center and width/height  */
double
render::sample_scr_square (double xc, double yc, double width, double height)
{
  double xxc, yyc, x1, y1, x2, y2;
  m_scr_to_img.to_img (xc, yc, &xxc, &yyc);
  m_scr_to_img.to_img (xc - width / 2, yc + height / 2, &x1, &y1);
  m_scr_to_img.to_img (xc + width / 2, yc + height / 2, &x2, &y2);
  return sample_img_square (xxc, yyc, x1 - xxc, y1 - yyc, x2 - xxc, y2 - yyc);
}

/* Determine grayscale value at a given position in the image.
   The position is in the screen coordinates.  */
inline double
render::get_img_pixel_scr (double x, double y)
{
  double xp, yp;
  m_scr_to_img.to_img (x, y, &xp, &yp);
  return get_img_pixel (xp, yp);
}
#endif
