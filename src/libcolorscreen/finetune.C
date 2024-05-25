#include <memory>
#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <gsl/gsl_multifit.h>
#include "include/finetune.h"
#include "include/histogram.h"
#include "render-interpolate.h"
#include "dufaycolor.h"
#include "include/tiff-writer.h"
#include "nmsimplex.h"
#include "include/bitmap.h"

namespace {

struct finetune_solver
{
private:
  gsl_multifit_linear_workspace *gsl_work;
  gsl_matrix *gsl_X;
  gsl_vector *gsl_y[3];
  gsl_vector *gsl_c;
  gsl_matrix *gsl_cov;
  int noutliers;
  std::unique_ptr <bitmap_2d> outliers;

public:
  finetune_solver ()
    : gsl_work (NULL), gsl_X (NULL), gsl_y {NULL, NULL, NULL}, gsl_c (NULL), gsl_cov (NULL), noutliers (0), outliers ()
  {
  }
  /* Tile dimensions */
  int twidth, theight;
  /* Tile colors */
  std::unique_ptr <rgbdata []> tile;
  /* Black and white tile.  */
  std::unique_ptr <luminosity_t []> bwtile;
  /* Tile position  */
  std::unique_ptr <point_t []> tile_pos;
  /* 2 coordinates, blur radius, 3 * 3 colors, strip widths, fog  */
  coord_t start[17];
  coord_t last_blur;
  coord_t last_width, last_height;

  coord_t fixed_blur, fixed_width, fixed_height;

  //const coord_t range = 0.2;
  const coord_t range = 0.2;

  screen scr1;
  screen scr;
  coord_t pixel_size;
  scr_type type;
  rgbdata fog_range;

  bool optimize_position;
  bool optimize_screen_blur;
  bool optimize_dufay_strips;
  bool optimize_fog;
  bool least_squares;
  bool normalize;

  luminosity_t maxgray;

  int fog_index;
  int color_index;
  int screen_index;
  int dufay_strips_index;
  int n_values;
  ~finetune_solver ()
  {
    free_least_squares ();
  }

  int num_values ()
  {
    return n_values;
  }
  constexpr static const coord_t rgbscale = /*256*/1;
  coord_t epsilon ()
  {
    return 0.00000001;
  }
  coord_t scale ()
  {
    return /*2 * rgbscale*/0.1;
  }
  bool verbose ()
  {
    return false;
  }

  point_t
  get_offset (coord_t *v)
  {
    if (!optimize_position)
      return {0, 0};
    return {v[0], v[1]};
  }

  bool has_outliers ()
  {
    return noutliers;
  }

  coord_t get_blur_radius (coord_t *v)
  {
    if (!optimize_screen_blur)
      return fixed_blur;
    return v[screen_index];
  }
  coord_t get_red_strip_width (coord_t *v)
  {
    if (!optimize_dufay_strips)
      return fixed_width;
    return v[dufay_strips_index];
  }
  coord_t get_green_strip_width (coord_t *v)
  {
    if (!optimize_dufay_strips)
      return fixed_height;
    return v[dufay_strips_index + 1];
  }

  void
  print_values (coord_t *v)
  {
    if (optimize_position)
      {
	point_t p = get_offset (v);
        printf ("Center %f %f in pixels %f %f\n", p.x, p.y, p.x/pixel_size, p.y/pixel_size);
      }
    if (optimize_screen_blur)
      printf ("Screen blur %f (pixel size %f, scaled %f)\n", get_blur_radius (v), pixel_size, get_blur_radius (v) * pixel_size);
    if (optimize_dufay_strips)
      {
	printf ("Red strip width: %f\n", get_red_strip_width (v));
	printf ("Green strip width: %f\n", get_green_strip_width (v));
      }
    if (tile)
      {
	rgbdata red, green, blue;
	get_colors (v, &red, &green, &blue);

	printf ("Red :");
	red.print (stdout);
	printf ("Normalized red :");
	luminosity_t sum = red.red + red.green + red.blue;
	(red / sum).print (stdout);

	printf ("Green :");
	green.print (stdout);
	printf ("Normalized green :");
	sum = green.red + green.green + green.blue;
	(green / sum).print (stdout);

	printf ("Blue :");
	blue.print (stdout);
	printf ("Normalized blue :");
	sum = blue.red + blue.green + blue.blue;
	(blue / sum).print (stdout);
      }
    if (bwtile)
      {
	printf ("Max gray %f\n", maxgray);
	rgbdata color = {v[color_index], v[color_index+1], v[color_index+2]};
	printf ("Intensities :");
	color.print (stdout);
	printf ("Normalized :");
	luminosity_t sum = color.red + color.green + color.blue;
	(color / sum).print (stdout);
      }
  }

  void
  constrain (coord_t *v)
  {
    /* x and y adjustments.  */
    if (optimize_position)
      {
	v[0] = std::min (v[0], type == Dufay ? range : range / 2);
	v[0] = std::max (v[0], type == Dufay ? -range : -range / 2);
	v[1] = std::min (v[1], type == Dufay ? range : range / 2);
	v[1] = std::max (v[1], type == Dufay ? -range : -range / 2);
      }
    if (optimize_fog)
      {
	v[fog_index] = std::min (v[fog_index], (coord_t)1);
	v[fog_index] = std::max (v[fog_index], (coord_t)0);
	v[fog_index+1] = std::min (v[fog_index+1], (coord_t)1);
	v[fog_index+1] = std::max (v[fog_index+1], (coord_t)0);
	v[fog_index+2] = std::min (v[fog_index+2], (coord_t)1);
	v[fog_index+2] = std::max (v[fog_index+2], (coord_t)0);
      }
    if (bwtile && !least_squares)
      {
	v[color_index] = std::min (v[color_index], (coord_t)2);
	v[color_index] = std::max (v[color_index], (coord_t)0);
	v[color_index + 1] = std::min (v[color_index + 1], (coord_t)2);
	v[color_index + 1] = std::max (v[color_index + 1], (coord_t)0);
	v[color_index + 2] = std::min (v[color_index + 2], (coord_t)2);
	v[color_index + 2] = std::max (v[color_index + 2], (coord_t)0);
      }

    if (optimize_screen_blur)
      {
	/* Screen blur radius.  */
	v[screen_index] = std::max (v[screen_index], (coord_t)0);
	v[screen_index] = std::min (v[screen_index], screen::max_blur_radius / pixel_size);
      }
    if (optimize_dufay_strips)
      {
	/* Dufaycolor red strip width and height.  */
	if (type == Dufay)
	  {
	    v[dufay_strips_index + 0] = std::min (v[dufay_strips_index + 0], (coord_t)0.7);
	    v[dufay_strips_index + 0] = std::max (v[dufay_strips_index + 0], (coord_t)0.3);
	    v[dufay_strips_index + 1] = std::min (v[dufay_strips_index + 1], (coord_t)0.7);
	    v[dufay_strips_index + 1] = std::max (v[dufay_strips_index + 1], (coord_t)0.3);
	  }
      }
  }
  void
  free_least_squares ()
  {
    if (gsl_work)
      {
	gsl_multifit_linear_free (gsl_work);
	gsl_work = NULL;
	gsl_matrix_free (gsl_X);
	gsl_vector_free (gsl_y[0]);
	if (tile)
	  {
	    gsl_vector_free (gsl_y[1]);
	    gsl_vector_free (gsl_y[2]);
	  }
	gsl_vector_free (gsl_c);
	gsl_matrix_free (gsl_cov);
      }
  }
  void
  alloc_least_squares ()
  {
    int matrixw = 3;
    int matrixh = twidth * theight - noutliers;
    gsl_work = gsl_multifit_linear_alloc (matrixh, matrixw);
    gsl_X = gsl_matrix_alloc (matrixh, matrixw);
    gsl_y[0] = gsl_vector_alloc (matrixh);
    if (tile)
      {
	gsl_y[1] = gsl_vector_alloc (matrixh);
	gsl_y[2] = gsl_vector_alloc (matrixh);
      }
    gsl_c = gsl_vector_alloc (matrixw);
    gsl_cov = gsl_matrix_alloc (matrixw, matrixw);
  }
  void
  init_least_squares (coord_t *v)
  {
    if (tile)
      {
	int e = 0;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    if (!noutliers || !outliers->test_bit (x, y))
	      {
		rgbdata c = get_pixel (v, x, y);
		gsl_vector_set (gsl_y[0], e, c.red);
		gsl_vector_set (gsl_y[1], e, c.green);
		gsl_vector_set (gsl_y[2], e, c.blue);
		e++;
	      }
      }
    else
      {
	int e = 0;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    if (!noutliers || !outliers->test_bit (x, y))
	      {
		gsl_vector_set (gsl_y[0], e, bw_get_pixel (x, y) / (2 * maxgray));
		e++;
	      }
      }
  }


  void
  init (coord_t blur_radius)
  {
    n_values = 0;
    /* 2 values for position.  */
    if (optimize_position)
      n_values += 2;


    if (!least_squares)
      {
	color_index = n_values;
	/* 3*3 values for color.
	   3 intensitied for B&W  */
	if (tile)
	  n_values += 9;
	else
	  n_values += 3;
      }

    fog_index = n_values;
    if (optimize_fog)
      n_values += tile ? 3 : 1;

    screen_index = n_values;
    if (optimize_screen_blur)
      n_values++;

    dufay_strips_index = n_values;
    if (optimize_dufay_strips)
      n_values += 2;

    last_blur = -1;
    last_width = -1;
    last_height = -1;
    if (optimize_position)
      {
	start[0] = 0;
	start[1] = 0;
      }

    if (!least_squares)
      {
	if (tile)
	  {
	    start[color_index] = finetune_solver::rgbscale;
	    start[color_index + 1] = 0;
	    start[color_index + 2] = 0;

	    start[color_index + 3] = 0;
	    start[color_index + 4] = finetune_solver::rgbscale;
	    start[color_index + 5] = 0;

	    start[color_index + 6] = 0;
	    start[color_index + 7] = 0;
	    start[color_index + 8] = finetune_solver::rgbscale;
	  }
	else
	  {
	    start[color_index] = 0;
	    start[color_index + 1] = 0;
	    start[color_index + 2] = 0;
	  }
      }

    /* Starting from blur 0 seems to work better, since other parameters
       are then more relevant.  */
    fixed_blur = blur_radius;
    fixed_width = dufaycolor::red_width;
    fixed_height = dufaycolor::green_height;
    if (optimize_screen_blur)
      start[screen_index] = 0.8;
    if (optimize_dufay_strips)
      {
	start[dufay_strips_index + 0] = dufaycolor::red_width;
	start[dufay_strips_index + 1] = dufaycolor::green_height;
      }

    maxgray = 0;
    if (bwtile)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  maxgray = std::max (maxgray, bw_get_pixel (x, y));
    constrain (start);
    if (optimize_fog)
      {
	rgb_histogram hist;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    hist.pre_account (tile[y * twidth + x]);
	hist.finalize_range (65535);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    hist.account (tile[y * twidth + x]);
	hist.finalize ();
	fog_range = hist.find_min (0.01);
	start[fog_index] = 0;
	start[fog_index + 1] = 0;
	start[fog_index + 2] = 0;
      }

    if (tile && normalize && !optimize_fog)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  {
	    rgbdata &c = tile[y * twidth + x];
	    luminosity_t sum = c.red + c.green + c.blue;
	    if (sum > 0)
	      c /= sum;
	  }

    if (least_squares)
      {
	free_least_squares ();
	alloc_least_squares ();
	if (!optimize_fog)
	  init_least_squares (NULL);
      }
  }

  void
  init_screen (coord_t *v)
  {
    luminosity_t blur = get_blur_radius (v);
    luminosity_t red_strip_width = get_red_strip_width (v);
    luminosity_t green_strip_height = get_green_strip_width (v);
    
    if (blur != last_blur || red_strip_width != last_width || green_strip_height != last_height)
      {
        scr1.initialize (type, red_strip_width, green_strip_height);
	scr.initialize_with_blur (scr1, blur * pixel_size);
	last_blur = blur;
	last_width = red_strip_width;
	last_height = green_strip_height;
      }
  }

  /* Evaulate pixel at (x,y) using RGB values v and offsets offx, offy
     compensating coordates stored in tole_pos.  */
  inline rgbdata
  evaulate_pixel (rgbdata red, rgbdata green, rgbdata blue, int x, int y, point_t off)
  {
    point_t p = tile_pos [y * twidth + x];
    p.x += off.x;
    p.y += off.y;
    /* Interpolation here is necessary to ensure smoothness.  */
    rgbdata m = scr.interpolated_mult (p);
    return ((red * m.red + green * m.green + blue * m.blue) * ((coord_t)1.0 / rgbscale));
  }

  /* Evaulate pixel at (x,y) using RGB values v and offsets offx, offy
     compensating coordates stored in tole_pos.  */
  inline luminosity_t
  bw_evaulate_pixel (rgbdata color, int x, int y, point_t off)
  {
    point_t p = tile_pos [y * twidth + x];
    p.x += off.x;
    p.y += off.y;
    /* Interpolation here is necessary to ensure smoothness.  */
    rgbdata m = scr.interpolated_mult (p);
    return ((m.red * color.red + m.green * color.green + m.blue * color.blue) * (2 * maxgray));
  }

  rgbdata
  get_pixel (coord_t *v, int x, int y)
  {
     if (!optimize_fog)
       return tile[y * twidth + x];
     rgbdata d = tile[y * twidth + x] - get_fog (v);
     if (normalize)
       {
	 luminosity_t ssum = fabs (d.red + d.green + d.blue);
	 if (ssum == 0)
	   ssum = 0.0000001;
         d /= ssum;
       }
     return d;
  }

  luminosity_t
  bw_get_pixel (int x, int y)
  {
     return bwtile[y * twidth + x];
  }

  coord_t
  determine_colors (coord_t *v, rgbdata *red, rgbdata *green, rgbdata *blue)
  {
    point_t off = get_offset (v);
    coord_t sqsum = 0;


    for (int ch = 0; ch < 3; ch++)
      {
	int e = 0;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    if (!noutliers || !outliers->test_bit (x, y))
	      {
		point_t p = tile_pos [y * twidth + x];
		p.x += off.x;
		p.y += off.y;
		rgbdata c = scr.interpolated_mult (p);
		gsl_matrix_set (gsl_X, e, 0, c.red);
		gsl_matrix_set (gsl_X, e, 1, c.green);
		gsl_matrix_set (gsl_X, e, 2, c.blue);
		e++;
	      }
	double chisq;
	gsl_multifit_linear (gsl_X, gsl_y[ch], gsl_c, gsl_cov,
			     &chisq, gsl_work);
	sqsum += chisq;
	(*red)[ch] = gsl_vector_get (gsl_c, 0);
	(*green)[ch] = gsl_vector_get (gsl_c, 1);
	(*blue)[ch] = gsl_vector_get (gsl_c, 2);
#if 0
	rgbdata cc = {gsl_vector_get (c, 0), gsl_vector_get (c, 1), gsl_vector_get (c, 2)};
	if (!ch)
	  *red = cc;
	else if (ch == 1)
	  *green = cc;
	else
	  *blue = cc;
#endif
      }
    return sqsum;
  }

  rgbdata
  bw_determine_color (coord_t *v)
  {
    point_t off = get_offset (v);
    int e = 0;

    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	if (!noutliers || !outliers->test_bit (x, y))
	  {
	    point_t p = tile_pos [y * twidth + x];
	    p.x += off.x;
	    p.y += off.y;
	    rgbdata c = scr.interpolated_mult (p);
	    gsl_matrix_set (gsl_X, e, 0, c.red);
	    gsl_matrix_set (gsl_X, e, 1, c.green);
	    gsl_matrix_set (gsl_X, e, 2, c.blue);
	    e++;
	    //gsl_vector_set (gsl_y[0], e, bw_get_pixel (x, y) / (2 * maxgray));
	  }
    double chisq;
    gsl_multifit_linear (gsl_X, gsl_y[0], gsl_c, gsl_cov, &chisq, gsl_work);
    rgbdata ret = {gsl_vector_get (gsl_c, 0), gsl_vector_get (gsl_c, 1), gsl_vector_get (gsl_c, 2)};
    return ret;
  }

  rgbdata
  get_fog (coord_t *v)
  {
    if (!optimize_fog)
      return {0, 0, 0};
    return {v[fog_index] * fog_range.red, v[fog_index+1] * fog_range.green, v[fog_index+2] * fog_range.blue};
  }

  rgbdata
  bw_get_color (coord_t *v)
  {
    if (!least_squares)
      return {v[color_index], v[color_index + 1], v[color_index + 2]};
    else
      return bw_determine_color (v);
  }
  void
  get_colors (coord_t *v, rgbdata *red, rgbdata *green, rgbdata *blue)
  {
    if (!least_squares)
      {
	*red = {v[color_index], v[color_index + 1], v[color_index + 2]};
	*green = {v[color_index + 3], v[color_index + 4], v[color_index + 5]};
	*green = {v[color_index + 6], v[color_index + 7], v[color_index + 8]};
      }
    else
      {
	if (least_squares && optimize_fog)
	  init_least_squares (v);
	determine_colors (v, red, green, blue);
      }
  }

  coord_t
  objfunc (coord_t *v)
  {
    init_screen (v);
    coord_t sum = 0;
    point_t off = get_offset (v);
    if (tile)
      {
	rgbdata red, green, blue;
	get_colors (v, &red, &green, &blue);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    if (!noutliers || !outliers->test_bit (x, y))
	      {
		rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
		rgbdata d = get_pixel (v, x, y);
		sum += fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
			/*(c.red - d.red) * (c.red - d.red) + (c.green - d.green) * (c.green - d.green) + (c.blue - d.blue) * (c.blue - d.blue)*/
	      }
      }
    else if (bwtile)
      {
	rgbdata color = bw_get_color (v);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    if (!noutliers || !outliers->test_bit (x, y))
	      {
		luminosity_t c = bw_evaulate_pixel (color, x, y, off);
		luminosity_t d = bw_get_pixel (x, y);
		sum += fabs (c - d);
	      }
	sum /= maxgray;
      }
    //printf ("%f\n", sum);
    return sum;
  }

  int
  determine_outliers (coord_t *v, coord_t ratio)
  {
    histogram hist;
    rgbdata red, green, blue;
    point_t off = get_offset (v);
    get_colors (v, &red, &green, &blue);
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
	  rgbdata d = get_pixel (v, x, y);
	  coord_t err = fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
	  hist.pre_account (err);
	}
    hist.finalize_range (65535);
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
	  rgbdata d = get_pixel (v, x, y);
	  coord_t err = fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
	  hist.account (err);
	}
    hist.finalize ();
    coord_t merr = hist.find_max (ratio) * 1.3;
    outliers = std::unique_ptr <bitmap_2d> (new bitmap_2d (twidth, theight));
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
	  rgbdata d = get_pixel (v, x, y);
	  coord_t err = fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
	  if (err > merr)
	    {
	      noutliers++;
	      outliers->set_bit (x, y);
	    }
	}
    if (!noutliers)
      return 0;
#if 0
    if (optimize_screen_blur)
      v[screen_index] = 0.8;
    if (optimize_dufay_strips)
      {
	v[dufay_strips_index + 0] = dufaycolor::red_width;
	v[dufay_strips_index + 1] = dufaycolor::green_height;
      }
#endif
    if (least_squares)
      {
	free_least_squares ();
	alloc_least_squares ();
	if (!optimize_fog)
	  init_least_squares (NULL);
      }
    return noutliers;
  }

  bool
  write_file (coord_t *v, const char *name, int type)
  {
    init_screen (v);
    point_t off = get_offset (v);

    tiff_writer_params p;
    p.filename = name;
    p.width = twidth;
    p.height = theight;
    p.depth = 16;
    const char *error;
    tiff_writer rendered (p, &error);
    if (error)
      return false;


    if (tile)
      {
	luminosity_t rmax = 0, gmax = 0, bmax = 0;
	rgbdata red, green, blue;
	get_colors (v, &red, &green, &blue);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
	      rmax = std::max (c.red, rmax);
	      gmax = std::max (c.green, gmax);
	      bmax = std::max (c.blue, bmax);
	      rgbdata d = get_pixel (v, x, y);
	      rmax = std::max (d.red, rmax);
	      gmax = std::max (d.green, gmax);
	      bmax = std::max (d.blue, bmax);
	    }

	for (int y = 0; y < theight; y++)
	  {
	    for (int x = 0; x < twidth; x++)
	      if (type == 1 || !noutliers || !outliers->test_bit (x, y))
		switch (type)
		  {
		  case 0:
		    {
		      rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
		      rendered.put_pixel (x, c.red * 65535 / rmax, c.green * 65535 / gmax, c.blue * 65535 / bmax);
		    }
		    break;
		  case 1:
		    {
		      rgbdata d = get_pixel (v, x, y);
		      rendered.put_pixel (x, d.red * 65535 / rmax, d.green * 65535 / gmax, d.blue * 65535 / bmax);
		    }
		    break;
		  }
	      else
		rendered.put_pixel (x, 0, 0, 0);
	    if (!rendered.write_row ())
	      return false;
	  }
      }
    if (bwtile)
      {
	luminosity_t lmax = 0;
	rgbdata color = bw_get_color (v);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      lmax = std::max (bw_evaulate_pixel (color, x, y, off), lmax);
	      lmax = std::max (bw_get_pixel (x, y), lmax);
	    }

	for (int y = 0; y < theight; y++)
	  {
	    for (int x = 0; x < twidth; x++)
	      if (type == 1 || !noutliers || !outliers->test_bit (x, y))
		switch (type)
		  {
		  case 0:
		    {
		      luminosity_t c = bw_evaulate_pixel (color, x, y, off);
		      rendered.put_pixel (x, c * 65535 / lmax, c * 65535 / lmax, c * 65535 / lmax);
		    }
		    break;
		  case 1:
		    {
		      luminosity_t d = bw_get_pixel (x, y);
		      rendered.put_pixel (x, d * 65535 / lmax, d * 65535 / lmax, d * 65535 / lmax);
		    }
		    break;
		  }
	      else
		rendered.put_pixel (x, 0, 0, 0);
	    if (!rendered.write_row ())
	      return false;
	  }
      }
    return true;
  }
};
}

/* Finetune parameters and update RPARAM.  */

finetune_result
finetune (render_parameters &rparam, const scr_to_img_parameters &param, const image_data &img, int x, int y, const finetune_parameters &fparams, progress_info *progress)
{
  scr_to_img map;
  map.set_parameters (param, img);
  bool bw = fparams.flags & finetune_bw;
  bool verbose = fparams.flags & finetune_verbose;
  finetune_result ret = {false, -1, -1, -1, -1, {-1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};

  if (!bw && !img.rgbdata)
    bw = true;

  /* Determine tile to analyze.  */
  coord_t tx, ty;
  map.to_scr (x, y, &tx, &ty);
  int sx = nearest_int (tx);
  int sy = nearest_int (ty);

  coord_t test_range = fparams.range ? fparams.range || (fparams.flags & finetune_no_normalize) : (bw ? 1 : 2);
  map.to_img (sx, sy, &tx, &ty);
  map.to_img (sx - test_range, sy - test_range, &tx, &ty);
  coord_t sxmin = tx, sxmax = tx, symin = ty, symax = ty;
  map.to_img (sx + test_range, sy - test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);
  map.to_img (sx + test_range, sy + test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);
  map.to_img (sx - test_range, sy + test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);

  int txmin = floor (sxmin), tymin = floor (symin), txmax = ceil (sxmax), tymax = ceil (symax);
  if (txmin < 0)
    txmin = 0;
  if (txmax > img.width)
    txmax = img.width;
  if (tymin < 0)
    tymin = 0;
  if (tymax > img.height)
    tymax = img.height;
  if (txmin + 10 > txmax || tymin + 10 > tymax)
    {
      if (verbose)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Too small tile %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
	  progress->resume_stdout ();
	}
      return ret;
    }
  int twidth = txmax - txmin + 1, theight = tymax - tymin + 1;
  if (verbose)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Will analyze tile %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
      progress->resume_stdout ();
    }
  const int maxtiles = 5;
  finetune_solver solvers[maxtiles][maxtiles], *best_solver = NULL;
  coord_t best_uncertainity = 0;
  bool multitile = /*fparams.flags & finetune_multitile */ true;
  {
    render_to_scr render (param, img, rparam, 256);
    int rxmin = txmin, rxmax = txmax, rymin = tymin, rymax = tymax;
    if (multitile)
      {
	rxmin = std::max (txmin - twidth * (maxtiles / 2), 0);
	rymin = std::max (tymin - theight * (maxtiles / 2), 0);
	rxmax = std::max (txmax + (twidth * maxtiles / 2), img.width - 1);
	rymax = std::max (tymax + (theight * maxtiles / 2), img.height - 1);
      }
    if (!render.precompute_img_range (bw /*grayscale*/, false /*normalized*/, rxmin, rymin, rxmax + 1, rymax + 1, !(fparams.flags & finetune_no_progress_report) ? progress : NULL))
      {
	if (verbose)
	  {
	    progress->pause_stdout ();
	    fprintf (stderr, "Precomputing failed. Tile: %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
	    progress->resume_stdout ();
	  }
	return ret;
      }

      for (int ty = multitile ? 0 : 1; ty < (multitile ? maxtiles : 2); ty++)
	for (int tx = multitile ? 0 : 1; tx < (multitile ? maxtiles : 2); tx++)
	  {
	    int cur_txmin = std::min (std::max (txmin - twidth * (maxtiles / 2) + tx * twidth, 0), img.width - twidth - 1);
	    int cur_tymin = std::min (std::max (tymin - theight * (maxtiles / 2) + ty * theight, 0), img.height - theight - 1);
	    int cur_txmax = cur_txmin + twidth;
	    int cur_tymax = cur_tymin + theight;
	    finetune_solver &solver = solvers[ty][tx];
	    if (!bw)
	      solver.tile = (std::unique_ptr <rgbdata[]>)(new  (std::nothrow) rgbdata [twidth * theight]);
	    else
	      solver.bwtile = (std::unique_ptr <luminosity_t[]>)(new  (std::nothrow) luminosity_t [twidth * theight]);

	    solver.tile_pos = (std::unique_ptr <point_t[]>)(new  (std::nothrow) point_t [twidth * theight]);
	    if ((!solver.tile && !solver.bwtile) || !solver.tile_pos)
	      {
		if (verbose)
		  {
		    progress->pause_stdout ();
		    fprintf (stderr, "Failed to allocate tile %i-%i %i-%i\n", cur_txmin, cur_txmax, cur_tymin, cur_tymax);
		    progress->resume_stdout ();
		  }
		return ret;
	      }
	    for (int y = 0; y < theight; y++)
	      for (int x = 0; x < twidth; x++)
		{
		  map.to_scr (cur_txmin + x + 0.5, cur_tymin + y + 0.5, &solver.tile_pos [y * twidth + x].x, &solver.tile_pos[y * twidth + x].y);
		  if (solver.tile)
		    solver.tile[y * twidth + x] = render.get_unadjusted_rgb_pixel (x + cur_txmin, y + cur_tymin);
		  if (solver.bwtile)
		    solver.bwtile[y * twidth + x] = render.get_unadjusted_data (x + cur_txmin, y + cur_tymin);
		}
	    solver.twidth = twidth;
	    solver.theight = theight;
	    solver.type = map.get_type ();
	    solver.pixel_size = render.pixel_size ();
	    solver.optimize_position = fparams.flags & finetune_position;
	    solver.optimize_screen_blur = fparams.flags & finetune_screen_blur;
	    solver.optimize_dufay_strips = (fparams.flags & finetune_dufay_strips) && solver.type == Dufay;
	    solver.optimize_fog = (fparams.flags & finetune_fog) && solver.tile;
	    //printf ("%i %i %i %u\n", solver.optimize_fog, (fparams.flags & finetune_fog), solver.tile != NULL, solver.bwtile != NULL);
	    solver.least_squares = !(fparams.flags & finetune_no_least_squares);
	    solver.normalize = !(fparams.flags & finetune_no_normalize);
	    solver.init (rparam.screen_blur_radius);

	    //if (verbose)
	      //solver.print_values (solver.start);
	    simplex<coord_t, finetune_solver>(solver, "finetuning", progress, !(fparams.flags & finetune_no_progress_report));
	    if (solver.tile && fparams.ignore_outliers > 0)
	      solver.determine_outliers (solver.start, fparams.ignore_outliers);
	    if (solver.has_outliers ())
	      simplex<coord_t, finetune_solver>(solver, "finetuning with outliers", progress, !(fparams.flags & finetune_no_progress_report));
	    coord_t uncertainity = solver.objfunc (solver.start) / (twidth * theight);
	    if (solver.bwtile)
	      {
		rgbdata c = solver.bw_get_color (solver.start);
		coord_t mmin = std::min (std::min (c.red, c.green), c.blue);
		coord_t mmax = std::max (std::max (c.red, c.green), c.blue);
		if (mmin != mmax)
		  uncertainity /= (mmin - mmax);
		else
		  uncertainity = 100000000;
	      }
	    if (!best_solver || best_uncertainity > uncertainity)
	      {
		best_solver = &solver;
		best_uncertainity = uncertainity;
	      }
	  }
    }


  if (verbose)
    {
      progress->pause_stdout ();
      best_solver->print_values (best_solver->start);
      progress->resume_stdout ();
    }
  if (fparams.simulated_file)
    best_solver->write_file (best_solver->start, fparams.simulated_file, 0);
  if (fparams.orig_file)
    best_solver->write_file (best_solver->start, fparams.orig_file, 1);
  if (fparams.diff_file)
    best_solver->write_file (best_solver->start, fparams.diff_file, 2);
  ret.success = true;
  ret.badness = best_solver->objfunc (best_solver->start) / (twidth * theight);
  //if (best_solver->optimize_screen_blur)
    //ret.screen_blur_radius = best_solver->start[best_solver->screen_index];
  if (best_solver->optimize_dufay_strips)
    {
      ret.dufay_red_strip_width = best_solver->start[best_solver->dufay_strips_index + 0];
      ret.dufay_green_strip_width = best_solver->start[best_solver->dufay_strips_index + 1];
    }
  if (best_solver->optimize_screen_blur)
    ret.screen_blur_radius = best_solver->start[best_solver->screen_index];
  if (best_solver->optimize_position)
    {
      ret.screen_coord_adjust.x = best_solver->start[0];
      ret.screen_coord_adjust.y = best_solver->start[1];
    }
  if (best_solver->optimize_fog)
    ret.fog = best_solver->get_fog (best_solver->start);
  return ret;
}
