#include <memory>
#include <gsl/gsl_multifit.h>
#include "include/solver.h"
#include "include/colorscreen.h"
#include "nmsimplex.h"
#include "render-interpolate.h"
#include "dufaycolor.h"
#include "include/tiff-writer.h"

namespace {

struct finetune_solver
{
  /* Tile dimensions */
  int twidth, theight;
  /* Tile colors */
  rgbdata *tile;
  /* Black and white tile.  */
  luminosity_t *bwtile;
  /* Tile position  */
  point_t *tile_pos;
  /* 2 coordinates, blur radius, 3 * 3 colors, strip widths  */
  coord_t start[14];
  coord_t last_blur;
  coord_t last_width, last_height;

  coord_t fixed_blur, fixed_width, fixed_height;

  //const coord_t range = 0.2;
  const coord_t range = 0.2;

  screen scr1;
  screen scr;
  coord_t pixel_size;
  scr_type type;

  bool optimize_position;
  bool optimize_screen;
  bool least_squares;
  bool normalize;

  luminosity_t maxgray;

  int color_index;
  int screen_index;
  int n_values;

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

  coord_t get_blur_radius (coord_t *v)
  {
    if (!optimize_screen)
      return fixed_blur;
    return v[screen_index];
  }
  coord_t get_red_strip_width (coord_t *v)
  {
    if (!optimize_screen || type != Dufay)
      return fixed_width;
    return v[screen_index + 1];
  }
  coord_t get_green_strip_width (coord_t *v)
  {
    if (!optimize_screen || type != Dufay)
      return fixed_height;
    return v[screen_index + 2];
  }

  void
  print_values (coord_t *v)
  {
    if (optimize_position)
      {
	point_t p = get_offset (v);
        printf ("Center %f %f in pixels %f %f\n", p.x, p.y, p.x/pixel_size, p.y/pixel_size);
      }
    if (optimize_screen)
      {
        printf ("Screen blur %f (pixel size %f, scaled %f)\n", get_blur_radius (v), pixel_size, get_blur_radius (v) * pixel_size);
	if (type == Dufay)
	  {
	    printf ("Red strip width: %f\n", get_red_strip_width (v));
	    printf ("Green strip width: %f\n", get_green_strip_width (v));
	  }
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
    if (bwtile)
      {
	v[color_index] = std::min (v[color_index], (coord_t)2);
	v[color_index] = std::max (v[color_index], (coord_t)0);
	v[color_index + 1] = std::min (v[color_index + 1], (coord_t)2);
	v[color_index + 1] = std::max (v[color_index + 1], (coord_t)0);
	v[color_index + 2] = std::min (v[color_index + 2], (coord_t)2);
	v[color_index + 2] = std::max (v[color_index + 2], (coord_t)0);
      }

    if (optimize_screen)
      {
	/* Screen blur radius.  */
	v[screen_index] = std::max (v[screen_index], (coord_t)0);
	v[screen_index] = std::min (v[screen_index], screen::max_blur_radius / pixel_size);
	/* Dufaycolor red strip width and height.  */
	if (type == Dufay)
	  {
	    v[screen_index + 1] = std::min (v[screen_index + 1], (coord_t)0.7);
	    v[screen_index + 1] = std::max (v[screen_index + 1], (coord_t)0.3);
	    v[screen_index + 2] = std::min (v[screen_index + 2], (coord_t)0.7);
	    v[screen_index + 2] = std::max (v[screen_index + 2], (coord_t)0.3);
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

    screen_index = n_values;
    /* screen blur and for Dufaycolor also strip widths.  */
    if (optimize_screen)
      {
	if (type == Dufay)
	  n_values += 3;
	else
	  n_values++;
      }

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
    if (optimize_screen)
      {
        start[screen_index] = 0.8;
	if (type == Dufay)
	  {
	    start[screen_index + 1] = dufaycolor::red_width;
	    start[screen_index + 2] = dufaycolor::green_height;
	  }
      }

    maxgray = 0;
    if (bwtile)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  maxgray = std::max (maxgray, bw_get_pixel (x, y));
    constrain (start);
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
  get_pixel (int x, int y)
  {
     rgbdata d = tile[y * twidth + x];
     if (normalize)
       {
	 luminosity_t ssum = d.red + d.green + d.blue;
	 if (ssum > 0)
	   d /= ssum;
       }
     return d;
  }

  luminosity_t
  bw_get_pixel (int x, int y)
  {
     return bwtile[y * twidth + x];
  }

  void
  determine_colors (coord_t *v, rgbdata *red, rgbdata *green, rgbdata *blue)
  {
    int matrixw = 3;
    int matrixh = twidth * theight;
    point_t off = get_offset (v);

    gsl_multifit_linear_workspace * work
      = gsl_multifit_linear_alloc (matrixh, matrixw);
    gsl_matrix *X = gsl_matrix_alloc (matrixh, matrixw);
    gsl_vector *yy = gsl_vector_alloc (matrixh);
    gsl_vector *w = gsl_vector_alloc (matrixh);
    gsl_vector *c = gsl_vector_alloc (matrixw);
    gsl_matrix *cov = gsl_matrix_alloc (matrixw, matrixw);

    for (int ch = 0; ch < 3; ch++)
      {
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      int e = x + y * twidth;
	      point_t p = tile_pos [y * twidth + x];
	      p.x += off.x;
	      p.y += off.y;
	      rgbdata c = scr.interpolated_mult (p);
	      gsl_matrix_set (X, e, 0, c.red);
	      gsl_matrix_set (X, e, 1, c.green);
	      gsl_matrix_set (X, e, 2, c.blue);
	      gsl_vector_set (yy, e, get_pixel (x, y) /*/ (2 * maxgray)*/[ch]);
	      gsl_vector_set (w, e, 1);
	    }
	double chisq;
	gsl_multifit_wlinear (X, w, yy, c, cov,
			      &chisq, work);
	(*red)[ch] = gsl_vector_get (c, 0);
	(*green)[ch] = gsl_vector_get (c, 1);
	(*blue)[ch] = gsl_vector_get (c, 2);
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
    gsl_multifit_linear_free (work);
    gsl_matrix_free (X);
    gsl_vector_free (yy);
    gsl_vector_free (w);
    gsl_vector_free (c);
    gsl_matrix_free (cov);
  }

  rgbdata
  bw_determine_color (coord_t *v)
  {
    int matrixw = 3;
    int matrixh = twidth * theight;
    point_t off = get_offset (v);

    gsl_multifit_linear_workspace * work
      = gsl_multifit_linear_alloc (matrixh, matrixw);
    gsl_matrix *X = gsl_matrix_alloc (matrixh, matrixw);
    gsl_vector *yy = gsl_vector_alloc (matrixh);
    gsl_vector *w = gsl_vector_alloc (matrixh);
    gsl_vector *c = gsl_vector_alloc (matrixw);
    gsl_matrix *cov = gsl_matrix_alloc (matrixw, matrixw);

    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
        {
	  int e = x + y * twidth;
	  point_t p = tile_pos [y * twidth + x];
	  p.x += off.x;
	  p.y += off.y;
	  rgbdata c = scr.interpolated_mult (p);
	  gsl_matrix_set (X, e, 0, c.red);
	  gsl_matrix_set (X, e, 1, c.green);
	  gsl_matrix_set (X, e, 2, c.blue);
	  gsl_vector_set (yy, e, bw_get_pixel (x, y) / (2 * maxgray));
	  gsl_vector_set (w, e, 1);
        }
    double chisq;
    gsl_multifit_wlinear (X, w, yy, c, cov,
			  &chisq, work);
    gsl_multifit_linear_free (work);
    rgbdata ret = {gsl_vector_get (c, 0), gsl_vector_get (c, 1), gsl_vector_get (c, 2)};
    gsl_matrix_free (X);
    gsl_vector_free (yy);
    gsl_vector_free (w);
    gsl_vector_free (c);
    gsl_matrix_free (cov);
    return ret;
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
      return determine_colors (v, red, green, blue);
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
	    {
	      rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
	      rgbdata d = get_pixel (x, y);
	      sum += fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
		      /*(c.red - d.red) * (c.red - d.red) + (c.green - d.green) * (c.green - d.green) + (c.blue - d.blue) * (c.blue - d.blue)*/
	    }
      }
    else if (bwtile)
      {
	rgbdata color = bw_get_color (v);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
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

  void
  write_debug_files (coord_t *v)
  {
    init_screen (v);
    point_t off = get_offset (v);

    tiff_writer_params p;
    p.filename = "/tmp/rendered.tif";
    p.width = twidth;
    p.height = theight;
    p.depth = 16;
    const char *error;
    tiff_writer rendered (p, &error);
    if (error)
      return;

    tiff_writer_params p2;
    p2.filename = "/tmp/orig.tif";
    p2.width = twidth;
    p2.height = theight;
    p2.depth = 16;
    tiff_writer orig (p2, &error);
    if (error)
      return;

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
	      rgbdata d = get_pixel (x, y);
	      rmax = std::max (d.red, rmax);
	      gmax = std::max (d.green, gmax);
	      bmax = std::max (d.blue, bmax);
	    }

	for (int y = 0; y < theight; y++)
	  {
	    for (int x = 0; x < twidth; x++)
	      {
		rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
		rendered.put_pixel (x, c.red * 65535 / rmax, c.green * 65535 / gmax, c.blue * 65535 / bmax);
		rgbdata d = get_pixel (x, y);
		orig.put_pixel (x, d.red * 65535 / rmax, d.green * 65535 / gmax, d.blue * 65535 / bmax);
	      }
	    if (!rendered.write_row ())
	      return;
	    if (!orig.write_row ())
	      return;
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
	      {
		luminosity_t c = bw_evaulate_pixel (color, x, y, off);
		rendered.put_pixel (x, c * 65535 / lmax, c * 65535 / lmax, c * 65535 / lmax);
		luminosity_t d = bw_get_pixel (x, y);
		orig.put_pixel (x, d * 65535 / lmax, d * 65535 / lmax, d * 65535 / lmax);
	      }
	    if (!rendered.write_row ())
	      return;
	    if (!orig.write_row ())
	      return;
	  }
      }
  }
};
}

/* Finetune parameters and update RPARAM.  */

bool
finetune (render_parameters &rparam, scr_to_img_parameters &param, image_data &img, solver_parameters::point_t &point, int x, int y, progress_info *progress)
{
  scr_to_img map;
  map.set_parameters (param, img, 0, false);
  bool bw = false;

  if (!bw && !img.rgbdata)
    bw = true;

  /* Determine tile to analyze.  */
  coord_t tx, ty;
  map.to_scr (x, y, &tx, &ty);
  int sx = nearest_int (tx);
  int sy = nearest_int (ty);
  bool verbose = true;

#if 0
  coord_t range = 0.3;

  map.to_img (sx - range, sy - range, &tx, &ty);
  coord_t rxmin = tx, rxmax = tx, rymin = ty, rymax = ty;
  map.to_img (sx + range, sy - range, &tx, &ty);
  rxmin = std::min (rxmin, tx);
  rxmax = std::max (rxmax, tx);
  rymin = std::min (rymin, ty);
  rymax = std::max (rymax, ty);
  map.to_img (sx + range, sy + range, &tx, &ty);
  rxmin = std::min (rxmin, tx);
  rxmax = std::max (rxmax, tx);
  rymin = std::min (rymin, ty);
  rymax = std::max (rymax, ty);
  map.to_img (sx - range, sy + range, &tx, &ty);
  rxmin = std::min (rxmin, tx);
  rxmax = std::max (rxmax, tx);
  rymin = std::min (rymin, ty);
  rymax = std::max (rymax, ty);

  coord_t test_range = 6;
  coord_t rangex = (rxmax - rxmin), rangey = (rymax - rymin);


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

  if (verbose)
  {
    progress->pause_stdout ();
    fprintf (stderr, "\nScan range %f-%f %f-%f\n", sxmin, sxmax, symin, symax);
    progress->resume_stdout ();
  }

  int txmin = floor (sxmin - rangex), tymin = floor (symin - rangey), txmax = ceil (sxmax + rangex), tymax = ceil (symax + rangey);
  if (txmin < 0)
    txmin = 0;
  if (txmax > img.width)
    txmax = img.width;
  if (tymin < 0)
    tymin = 0;
  if (tymax > img.height)
    tymax = img.height;
  if (txmin + 10 > txmax || tymin + 10 > tymax)
    return false;
  int twidth = txmax - txmin + 1, theight = tymax - tymin + 1;
  if (verbose)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Will analyze tile %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
      progress->resume_stdout ();
    }
#endif
  coord_t test_range = 5;
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

#if 0
  if (verbose)
  {
    progress->pause_stdout ();
    fprintf (stderr, "\nScan range %f-%f %f-%f\n", sxmin, sxmax, symin, symax);
    progress->resume_stdout ();
  }
#endif

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
    return false;
  int twidth = txmax - txmin + 1, theight = tymax - tymin + 1;
  if (verbose)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Will analyze tile %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
      progress->resume_stdout ();
    }

  std::unique_ptr <rgbdata[]> tile;
  std::unique_ptr <luminosity_t[]> bwtile;
 
  if (!bw)
    tile = (std::unique_ptr <rgbdata[]>)(new  (std::nothrow) rgbdata [twidth * theight]);
  else
    bwtile = (std::unique_ptr <luminosity_t[]>)(new  (std::nothrow) luminosity_t [twidth * theight]);

  std::unique_ptr <point_t[]> tile_pos (new  (std::nothrow) point_t [twidth * theight]);
  if ((!tile && !bwtile) || !tile_pos)
    return false;

  render_to_scr render (param, img, rparam, 256);
  if (!render.precompute_img_range (bw /*grayscale*/, false /*normalized*/, txmin, tymin, txmax + 1, tymax + 1, progress))
    return false;
  for (int y = 0; y < theight; y++)
    for (int x = 0; x < twidth; x++)
      {
       	map.to_scr (txmin + x + 0.5, tymin + y + 0.5, &tile_pos [y * twidth + x].x, &tile_pos [y * twidth + x].y);
	if (tile)
	  tile[y * twidth + x] = render.get_unadjusted_rgb_pixel (x + txmin, y + tymin);
	if (bwtile)
	  bwtile[y * twidth + x] = render.get_unadjusted_data (x + txmin, y + tymin);
      }
  finetune_solver solver;
  solver.twidth = twidth;
  solver.theight = theight;
  solver.tile = tile.get ();
  solver.bwtile = bwtile.get ();
  solver.tile_pos = tile_pos.get ();
  solver.type = map.get_type ();
  solver.pixel_size = render.pixel_size ();
  solver.optimize_position = false;
  solver.optimize_screen = true;
  solver.least_squares = true;
  solver.normalize = true;
  solver.init (rparam.screen_blur_radius);

  //if (verbose)
    //solver.print_values (solver.start);
  simplex<coord_t, finetune_solver>(solver, "finetuning", progress);

  if (verbose)
    {
      progress->pause_stdout ();
      solver.print_values (solver.start);
      progress->resume_stdout ();
    }
  solver.write_debug_files (solver.start);
  if (solver.optimize_screen)
    {
      rparam.screen_blur_radius = solver.start[solver.screen_index];
      if (solver.type == Dufay)
        {
          rparam.dufay_red_strip_width = solver.start[solver.screen_index + 1];
          rparam.dufay_green_strip_width = solver.start[solver.screen_index + 2];
        }
    }
  return true;
}
