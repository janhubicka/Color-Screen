#include <memory>
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
  /* Tile position  */
  point_t *tile_pos;
  /* 2 coordinates, blur radius, 3 * 3 colors, strip widths  */
  coord_t start[14];
  coord_t last_blur;
  coord_t last_width, last_height;

  coord_t fixed_blur, fixed_width, fixed_height;

  const coord_t range = 0.2;

  screen scr1;
  screen scr;
  coord_t pixel_size;
  scr_type type;

  bool optimize_screen;
  bool normalize;

  int num_values ()
  {
    return 11 + (optimize_screen ? 3 : 0);
  }
  constexpr static const coord_t rgbscale = /*256*/1;
  coord_t epsilon ()
  {
    return 0.00000001;
  }
  coord_t scale ()
  {
    return 2 * rgbscale;
  }
  bool verbose ()
  {
    return false;
  }

  void
  print_values (coord_t *v)
  {
    printf ("Center %f %f in pixels %f %f\n", v[0], v[1], v[0]/pixel_size, v[1]/pixel_size);
    printf ("Screen blur %f (pixel size %f, scaled %f)\n", v[11], pixel_size, v[11] * pixel_size);
    rgbdata red = {v[2] * (1.0 / rgbscale), v[3] * (1.0 / rgbscale), v[4] * (1.0 / rgbscale)};
    printf ("Red :");
    red.print (stdout);
    printf ("Normalized red :");
    luminosity_t sum = red.red + red.green + red.blue;
    (red / sum).print (stdout);
    rgbdata green = {v[5] * (1.0 / rgbscale), v[6] * (1.0 / rgbscale), v[7] * (1.0 / rgbscale)};
    printf ("Green :");
    green.print (stdout);
    printf ("Normalized green :");
    sum = green.green + green.green + green.blue;
    (green / sum).print (stdout);
    rgbdata blue = {v[8] * (1.0 / rgbscale), v[9] * (1.0 / rgbscale), v[10] * (1.0 / rgbscale)};
    printf ("Blue :");
    blue.print (stdout);
    printf ("Normalized blue :");
    sum = blue.blue + blue.blue + blue.blue;
    (blue / sum).print (stdout);
    printf ("Red strip width: %f\n", v[12]);
    printf ("Green strip width: %f\n", v[13]);
  }

  void
  constrain (coord_t *v)
  {
    /* x and y adjustments.  */
    v[0] = std::min (v[0], range);
    v[0] = std::max (v[0], -range);
    v[1] = std::min (v[1], range);
    v[1] = std::max (v[1], -range);

    if (optimize_screen)
      {
	/* Screen blur radius.  */
	v[11] = std::max (v[11], (coord_t)0);
	/* 0.356399 is way too large with pixel size 0.128323)  */
	v[11] = std::min (v[11], (coord_t)2);
	/* Dufaycolor red strip widht and height.  */
	v[12] = std::min (v[12], 0.7);
	v[12] = std::max (v[12], 0.3);
	v[13] = std::min (v[13], 0.7);
	v[13] = std::max (v[13], 0.3);
      }
  }

  void
  init (coord_t blur_radius)
  {
    last_blur = -1;
    last_width = -1;
    last_height = -1;
    start[0] = 0;
    start[1] = 0;

    start[2] = finetune_solver::rgbscale;
    start[3] = 0;
    start[4] = 0;

    start[5] = 0;
    start[6] = finetune_solver::rgbscale;
    start[7] = 0;

    start[8] = 0;
    start[9] = 0;
    start[10] = finetune_solver::rgbscale;

    /* Starting from blur 0 seems to work better, since other parameters
       are then more relevant.  */
    fixed_blur = start[11] = optimize_screen ? 0 : blur_radius;
    fixed_width = start[12] = dufaycolor::red_width;
    fixed_height = start[13] = dufaycolor::green_height;
  }

  void
  init_screen (coord_t *v)
  {
    //print_values (v);
    luminosity_t blur = optimize_screen ? v[0] : fixed_blur;
    luminosity_t red_strip_width = optimize_screen ? v[1] : fixed_width;
    luminosity_t green_strip_height = optimize_screen ? v[2] : fixed_height;
    
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
  evaulate_pixel (int x, int y, coord_t *v, coord_t offx, coord_t offy)
  {
    point_t p = tile_pos [y * twidth + x];
    p.x += offx;
    p.y += offy;
    /* Interpolation here is necessary to ensure smoothness.  */
    rgbdata m = scr.interpolated_mult (p);
    //m.print (stdout);
    return ((rgbdata){v[0],  v[1], v[2]} * m.red
	    + (rgbdata){v[3], v[4], v[5]} * m.green
	    + (rgbdata){v[6], v[7], v[8]} * m.blue) * ((coord_t)1.0 / rgbscale);
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

  coord_t
  objfunc (coord_t *v)
  {
    init_screen (v + 11);
    coord_t sum = 0;
    coord_t offx = v[0];
    coord_t offy = v[1];
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
        {
	  rgbdata c = evaulate_pixel (x, y, v+2, offx, offy);
	  rgbdata d = get_pixel (x, y);
	  sum += fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue)
	          /*(c.red - d.red) * (c.red - d.red) + (c.green - d.green) * (c.green - d.green) + (c.blue - d.blue) * (c.blue - d.blue)*/;
        }
    //printf ("%f\n", sum);
    return sum;
  }

  void
  write_debug_files (coord_t *v)
  {
    init_screen (v + 11);
    luminosity_t rmax = 0, gmax = 0, bmax = 0;
    coord_t offx = v[0];
    coord_t offy = v[1];
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
        {
	  rgbdata c = evaulate_pixel (x, y, v+2, offx, offy);
	  rmax = std::max (c.red, rmax);
	  gmax = std::max (c.green, gmax);
	  bmax = std::max (c.blue, bmax);
	  rgbdata d = get_pixel (x, y);
	  rmax = std::max (d.red, rmax);
	  gmax = std::max (d.green, gmax);
	  bmax = std::max (d.blue, bmax);
	}
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

    for (int y = 0; y < theight; y++)
      {
        for (int x = 0; x < twidth; x++)
	  {
	    rgbdata c = evaulate_pixel (x, y, v+2, offx, offy);
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
};
}

/* Finetune parameters and update RPARAM.  */

bool
finetune (render_parameters &rparam, scr_to_img_parameters &param, image_data &img, solver_parameters::point_t &point, int x, int y, progress_info *progress)
{
  scr_to_img map;
  map.set_parameters (param, img, 0, false);

  /* Determine tile to analyze.  */
  coord_t tx, ty;
  map.to_scr (x, y, &tx, &ty);
  int sx = nearest_int (tx);
  int sy = nearest_int (ty);
  bool verbose = true;

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

  std::unique_ptr <rgbdata[]> tile (new  (std::nothrow) rgbdata [twidth * theight]);
  std::unique_ptr <point_t[]> tile_pos (new  (std::nothrow) point_t [twidth * theight]);
  if (!tile || !tile_pos)
    return false;

  render_to_scr render (param, img, rparam, 256);
  if (!render.precompute_img_range (false /*grayscale*/, false /*normalized*/, txmin, tymin, txmax + 1, tymax + 1, progress))
    return false;
  for (int y = 0; y < theight; y++)
    for (int x = 0; x < twidth; x++)
      {
       	map.to_scr (txmin + x + 0.5, tymin + y + 0.5, &tile_pos [y * twidth + x].x, &tile_pos [y * twidth + x].y);
	tile[y * twidth + x] = render.get_unadjusted_rgb_pixel (x + txmin, y + tymin);
      }
  finetune_solver solver;
  solver.twidth = twidth;
  solver.theight = theight;
  solver.tile = tile.get ();
  solver.tile_pos = tile_pos.get ();
  solver.type = map.get_type ();
  solver.pixel_size = render.pixel_size ();
  solver.optimize_screen = true;
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
  rparam.screen_blur_radius = solver.start[11];
  rparam.dufay_red_strip_width = solver.start[12];
  rparam.dufay_green_strip_width = solver.start[13];
  return true;
}
