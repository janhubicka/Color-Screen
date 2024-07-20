#include <memory>
#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <gsl/gsl_multifit.h>
#include "include/finetune.h"
#include "include/histogram.h"
#include "include/stitch.h"
#include "render-interpolate.h"
#include "include/dufaycolor.h"
#include "include/tiff-writer.h"
#include "nmsimplex.h"
#include "include/bitmap.h"
#include "icc.h"

namespace {

inline void
to_range (coord_t &v, coord_t min, coord_t max)
{
  if (!(v >= min))
    v = min;
  if (!(v <= max))
    v = max;
}

/* v is in range 0...1 expand it to minv...maxv.  */
inline coord_t
expand_range (coord_t v, coord_t minv, coord_t maxv)
{
  return minv + v * (maxv - minv);
}
/* v is in range minv...maxv shrink it to 0...1.  */
inline coord_t
shrink_range (coord_t v, coord_t minv, coord_t maxv)
{
  return (v - minv) / (maxv - minv);
}

/* Solver used to find parameters of simulated scan (position of the grid,
   color of individual patches, lens blur ...) to match given scan tile
   (described tile and tile_pos) as well as possible.
   It is possible to match eitehr in BW or RGB and choose set of parameters
   to optimize for.  */
class finetune_solver
{
public:
  static constexpr const int max_tiles = 8;

  /* Data we need for each tile.  */
  class tile_data
  {
public:
    std::shared_ptr <bitmap_2d> outliers;
    /* Tile positions */
    int txmin, tymin;

    /* Tile colors */
    rgbdata *color;
    /* Black and white tile.  */
    luminosity_t *bw;
    /* Tile position  */
    point_t *pos;
    point_t fixed_offset, fixed_emulsion_offset;
    /* Screen merging emulsion and unblurred screen.  */
    screen *merged_scr;
    /* Blured screen used to render simulated scan.  */
    screen *scr;

    tile_data ()
    : outliers (), color (NULL), bw (NULL), pos (NULL), fixed_offset {-10, -10}, fixed_emulsion_offset {-10, -10},
      last_emulsion_intensities (-1, -1, -1), last_emulsion_offset {-10, -10}, merged_scr (NULL),
      scr (NULL), simulated_screen (NULL)
    { }
    ~tile_data ()
    {
      delete color;
      delete bw;
      delete pos;
      delete scr;
      delete merged_scr;
      delete simulated_screen;
    }
    void
    forget ()
    {
      color = NULL;
      bw = NULL;
      pos = NULL;
      scr = NULL;
      merged_scr = NULL;
      simulated_screen = NULL;
    }
protected:
    friend finetune_solver;
    rgbdata last_emulsion_intensities;
    point_t last_emulsion_offset;
    rgbdata *simulated_screen;
  };
  tile_data tiles[max_tiles];

private:
  gsl_multifit_linear_workspace *gsl_work;
  gsl_matrix *gsl_X;
  gsl_vector *gsl_y[3];
  gsl_vector *gsl_c;
  gsl_matrix *gsl_cov;
  bool least_squares_initialized;
  int noutliers;

  /* Indexes into optimized values array to fetch individual parameters  */
  int fog_index;
  int color_index;
  int emulsion_intensity_index;
  int emulsion_offset_index;
  int emulsion_blur_index;
  int screen_index;
  int dufay_strips_index;
  /* Number of values needed.  */
  int n_values;

  rgbdata fog_range;
  luminosity_t maxgray;

  rgbdata last_blur;
  luminosity_t last_mtf[4];
  luminosity_t last_emulsion_blur;
  coord_t last_width, last_height;
public:
  /* Unblured screen.  */
  std::shared_ptr <screen> original_scr;
  /* Screen with emulsion.  */
  std::shared_ptr <screen> emulsion_scr;

  finetune_solver ()
    : gsl_work (NULL), gsl_X (NULL), gsl_y {NULL, NULL, NULL}, gsl_c (NULL), gsl_cov (NULL), noutliers (0), start (NULL)
  {
  }
  int n_tiles;
  /* All tiles have same width and height.  */
  int twidth, theight;
  int simulated_screen_border;
  int simulated_screen_width;
  int simulated_screen_height;

  coord_t *start;

  /* Screen blur and duffay red strip widht and green strip height. */
  coord_t fixed_blur, fixed_width, fixed_height, fixed_emulsion_blur;

  /* Range of position adjustment.  Dufay screen has squares about size of 0.5 screen
     coordinates.  adjusting within -0.2 to 0.2 makes sure we do not exchange green for
     blue.  */
  constexpr static const coord_t dufay_range = 0.2;
  /* Paget range is smaller since there are more squares per screen period.
     Especially blue elements are small  */
  constexpr static const coord_t paget_range = 0.1;

  coord_t pixel_size;
  scr_type type;

  /* Try to adjust position of center of the patches (+- range)  */
  bool optimize_position;
  /* Try to optimize screen blur attribute (othervise fixed_blur is used.  */
  bool optimize_screen_blur;
  /* Try to optimize screen blur independently in each channel.  */
  bool optimize_screen_channel_blurs;
  /* Try to optimize screen blur guessing lens MTF.  */
  bool optimize_screen_mtf_blur;
  /* Try to optimize dufay strip widths (otherwise fixed_width, fixed_height is used).  */
  bool optimize_dufay_strips;
  /* Try to optimize dark point.  */
  bool optimize_fog;
  /* Try to otimize for blur caused by film emulsion.  For this screen blur needs to be fixed.  */
  bool optimize_emulsion_blur;
  /* Optimize colors using least squares method.
     Probably useful only for debugging and better to be true.  */
  bool least_squares;
  /* Determine color using data collection same as used by analyze_paget/analyze_dufay.  */
  bool data_collection;
  /* Normalize colors for simulation to uniform intensity.  This is useful
     in RGB simulation to eliminate underlying silver image (which works as
     netural density filter) of the input scan is linear.  */
  bool normalize;

  /* True if emulsion patch intensities should be finetuned.
     Initialized in init call.  */
  bool optimize_emulsion_intensities;
  bool optimize_emulsion_offset;
  bool fog_by_least_squares;

  /* Threshold for data collection.  */
  luminosity_t collection_threshold;

  /* Optimized values of red, green, blue for RGB simulation
     and optimized intensities for BW simulation.
     Initialized by objfunc and can be reused after it
     since get_colors is expensive.  */
  rgbdata last_red, last_green, last_blue, last_color;
  rgbdata last_fog;

  ~finetune_solver ()
  {
    free_least_squares ();
    free (start);
  }

  int num_values ()
  {
    return n_values;
  }
  constexpr static const coord_t rgbscale = /*256*/1;
  coord_t epsilon ()
  {
    /* the objective function computes average difference.
       1/65536 seems to be way too small epsilon.  */
    return /*0.00000001*/ 1.0/10000; /*65536*/
  }
  coord_t scale ()
  {
    return /*2 * rgbscale*/0.1;
  }
  bool verbose ()
  {
    return false;
  }
  int sample_points ()
  {
    return twidth * theight * n_tiles - noutliers;
  }

  point_t
  get_offset (coord_t *v, int tileid)
  {
    if (!optimize_position)
      return tiles[tileid].fixed_offset;
    coord_t range = type == Dufay ? dufay_range : paget_range;
    return {v[2 * tileid] * range, v[2 * tileid + 1] * range};
  }
  void
  set_offset (coord_t *v, int tileid, point_t off)
  {
    if (!optimize_position)
      {
	tiles[tileid].fixed_offset = off;
	return;
      }
    coord_t range = type == Dufay ? dufay_range : paget_range;
    v[2 * tileid] = off.x / range;
    v[2 * tileid + 1] = off.y / range;
  }
  point_t
  get_emulsion_offset (coord_t *v, int tileid)
  {
    if (!optimize_emulsion_offset)
      return tiles[tileid].fixed_emulsion_offset;
    /* Reduce dufay range since the screen is not removable and can only be adjusted
       by angle of scanner.  */
    coord_t range = type == Dufay ? dufay_range / 3: paget_range;
    range *= 2;
    return {v[emulsion_offset_index + 2 * tileid] * range, v[emulsion_offset_index + 2 * tileid + 1] * range};
  }
  void
  set_emulsion_offset (coord_t *v, int tileid, point_t off)
  {
    if (!optimize_emulsion_offset)
      {
	tiles[tileid].fixed_emulsion_offset = off;
	return;
      }
    coord_t range = type == Dufay ? dufay_range / 3: paget_range;
    range *= 2;
    v[emulsion_offset_index + 2 * tileid] = off.x / range;
    v[emulsion_offset_index + 2 * tileid + 1] = off.y / range;
  }

  point_t
  get_pos (coord_t *v, int tileid, int x, int y)
  {
    return tiles[tileid].pos[y * twidth + x] + get_offset (v, tileid);
  }

  bool has_outliers ()
  {
    return noutliers;
  }

  coord_t get_emulsion_blur_radius (coord_t *v)
  {
    if (!optimize_emulsion_blur)
      return fixed_emulsion_blur;
    return v[emulsion_blur_index] * (screen::max_blur_radius * 0.2 - 0.03) + 0.03;
  }
  void set_emulsion_blur_radius (coord_t *v, coord_t blur)
  {
    if (!optimize_emulsion_blur)
      fixed_emulsion_blur = blur;
    else
      v[emulsion_blur_index] = (blur - 0.03) / (screen::max_blur_radius * 0.2 - 0.03);
  }

  /* Do pixel blurs in the range 0.3 ... screen::max_blur_radius / pixel_size.  */
  coord_t pixel_blur (coord_t v)
  {
    return expand_range (v, 0.3, screen::max_blur_radius / pixel_size);
  }
  coord_t rev_pixel_blur (coord_t v)
  {
    return shrink_range (v, 0.3, screen::max_blur_radius / pixel_size);
  }

  coord_t get_blur_radius (coord_t *v)
  {
    if (optimize_screen_channel_blurs)
      return pixel_blur ((v[screen_index] + v[screen_index + 1] + v[screen_index + 2]) * (1 / (coord_t) 3));
    if (!optimize_screen_blur)
      return fixed_blur;
    return pixel_blur (v[screen_index]);
  }
  void set_blur_radius (coord_t *v, coord_t blur)
  {
    if (optimize_screen_channel_blurs)
      abort ();
    if (!optimize_screen_blur)
      fixed_blur = blur;
    else
      v[screen_index] = rev_pixel_blur (blur);
    return;
  }
  rgbdata get_channel_blur_radius (coord_t *v)
  {
    if (!optimize_screen_blur && !optimize_screen_channel_blurs)
      return {fixed_blur, fixed_blur, fixed_blur};
    if (!optimize_screen_channel_blurs)
      {
        coord_t b = pixel_blur (v[screen_index]);
        return {b, b, b};
      }
    return {pixel_blur (v[screen_index]), pixel_blur (v[screen_index] + 1), pixel_blur (v[screen_index] + 2)};
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

  void get_mtf (luminosity_t mtf[4], coord_t *v)
  {
    if (optimize_screen_mtf_blur)
      {
	/* Frequency should drop to 0 after reaching pixel size.  */
	coord_t maxfreq = 128 / pixel_size;
	if (maxfreq > 128)
	   maxfreq = 128;
	mtf[0] = v[screen_index] * maxfreq+0.1;
	mtf[1] = mtf[0]+v[screen_index + 1] * maxfreq + 0.1;
	mtf[2] = mtf[1]+v[screen_index + 2] * maxfreq + 0.1;
	mtf[3] = mtf[2]+v[screen_index + 3] * maxfreq + 0.1;
      }
    else
      mtf[0] = mtf[1] = mtf[2] = mtf[3] = -1;
  }

  void
  print_values (coord_t *v)
  {
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
	if (optimize_position)
	  {
	    point_t p = get_offset (v, tileid);
	    printf ("Screen offset %f %f (in pixels %f %f)\n", p.x, p.y, p.x/pixel_size, p.y/pixel_size);
	  }
	if (optimize_emulsion_offset)
	  {
	    point_t p = get_emulsion_offset (v, tileid);
	    printf ("Emulsion offset %f %f (%f %f in pixels; relative to screen)\n", p.x, p.y, p.x / pixel_size, p.y / pixel_size);
	  }
      }
    if (optimize_emulsion_blur)
      printf ("Emulsion blur %f (%f pixels)\n", get_emulsion_blur_radius (v), get_emulsion_blur_radius (v) / pixel_size);
    if (optimize_screen_mtf_blur)
      {
	luminosity_t mtf[4];
	get_mtf (mtf, v);
	screen::print_mtf (stdout, mtf, pixel_size);
      }
    if (optimize_screen_blur)
      printf ("Screen blur %f (pixel size %f, scaled %f)\n", get_blur_radius (v), pixel_size, get_blur_radius (v) * pixel_size);
    if (optimize_screen_channel_blurs)
      {
        rgbdata b = get_channel_blur_radius (v);
        printf ("Red screen blur %f (pixel size %f, scaled %f)\n", b.red, pixel_size, b.red * pixel_size);
        printf ("Green screen blur %f (pixel size %f, scaled %f)\n", b.green, pixel_size, b.green * pixel_size);
        printf ("Blue screen blur %f (pixel size %f, scaled %f)\n", b.blue, pixel_size, b.blue * pixel_size);
      }
    if (optimize_dufay_strips)
      {
	printf ("Red strip width: %f\n", get_red_strip_width (v));
	printf ("Green strip width: %f\n", get_green_strip_width (v));
      }
    if (tiles[0].color)
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
	if (optimize_fog)
	  {
	    printf ("Fog ");
	    get_fog (v).print (stdout);
	  }
	for (int tileid = 0; tileid < n_tiles; tileid++)
	  if (optimize_emulsion_intensities)
	    {
	      printf ("Emulsion intensities ");
	      get_emulsion_intensities (v, tileid).print (stdout);
	    }
	printf ("Mix weights ");
	get_mix_weights (v).print (stdout);
      }
    if (tiles[0].bw)
      {
	printf ("Max gray %f\n", maxgray);
	rgbdata color = bw_get_color (v);
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
      for (int tileid = 0; tileid < n_tiles; tileid++)
	{
	  to_range (v[tileid * 2 + 0], -1, 1);
	  to_range (v[tileid * 2 + 1], -1, 1);
	}
    if (optimize_fog && !fog_by_least_squares)
      {
	to_range (v[fog_index + 0], 0, 1);
	to_range (v[fog_index + 1], 0, 1);
	to_range (v[fog_index + 2], 0, 1);
      }
    if (tiles[0].bw && !least_squares && !data_collection)
      {
	to_range (v[color_index + 0], 0, 2);
	to_range (v[color_index + 1], 0, 2);
	to_range (v[color_index + 2], 0, 2);
      }
    if (optimize_emulsion_blur)
      to_range (v[emulsion_blur_index], 0, 1);
    if (optimize_emulsion_intensities)
      for (int i = 0; i < n_tiles * 3 - 1; i++)
	/* First 2 values are normalized and make only sense in range 0..1
	   Rest of values are relative to the first two and may be large if
	   first patch is dark.  */
	to_range (v[emulsion_intensity_index + i], 0, i < 3 ? 1 : 100);
    if (optimize_emulsion_offset)
      for (int tileid = 0; tileid < n_tiles; tileid++)
	{
	  to_range (v[emulsion_offset_index + 2 * tileid + 0], -1, 1);
	  to_range (v[emulsion_offset_index + 2 * tileid + 1], -1, 1);
	}
    if (optimize_screen_blur)
      to_range (v[screen_index], 0, 1);
    if (optimize_screen_channel_blurs)
      {
	/* Screen blur radius.  */
        to_range (v[screen_index + 0], 0, 1);
        to_range (v[screen_index + 1], 0, 1);
        to_range (v[screen_index + 2], 0, 1);
      }
    if (optimize_screen_mtf_blur)
      {
        to_range (v[screen_index + 0], 0, 1);
        to_range (v[screen_index + 1], 0, 1);
        to_range (v[screen_index + 2], 0, 1);
        to_range (v[screen_index + 3], 0, 1);
      }
    if (optimize_dufay_strips)
      {
	/* Dufaycolor red strip width and height.  */
        to_range (v[dufay_strips_index + 0], 0.1, 0.5);
        to_range (v[dufay_strips_index + 1], 0.3, 0.7);
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
	gsl_X = NULL;
	gsl_vector_free (gsl_y[0]);
	gsl_y[0] = NULL;
	if (tiles[0].color)
	  {
	    gsl_vector_free (gsl_y[1]);
	    gsl_y[1] = NULL;
	    gsl_vector_free (gsl_y[2]);
	    gsl_y[1] = NULL;
	  }
	gsl_vector_free (gsl_c);
	gsl_c = NULL;
	gsl_matrix_free (gsl_cov);
	gsl_cov = NULL;
      }
  }
  void
  alloc_least_squares ()
  {
    int matrixw = tiles[0].color ? (fog_by_least_squares ? 4 : 3) : 1;
    int matrixh = sample_points () + (fog_by_least_squares != 0);
    gsl_work = gsl_multifit_linear_alloc (matrixh, matrixw);
    gsl_X = gsl_matrix_alloc (matrixh, matrixw);
    gsl_y[0] = gsl_vector_alloc (matrixh);
    least_squares_initialized = false;
    if (tiles[0].color)
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
    last_fog = {0,0,0};
    if (tiles[0].color)
      {
	int e = 0;
	for (int tileid = 0; tileid < n_tiles; tileid++)
	  for (int y = 0; y < theight; y++)
	    for (int x = 0; x < twidth; x++)
	      if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
		{
		  rgbdata c = get_pixel (v, tileid, x, y);
		  gsl_vector_set (gsl_y[0], e, c.red);
		  gsl_vector_set (gsl_y[1], e, c.green);
		  gsl_vector_set (gsl_y[2], e, c.blue);
		  e++;
		}
	/* We want fog to be 0.  */
	if (fog_by_least_squares)
	  {
	    gsl_vector_set (gsl_y[0], e, 0);
	    gsl_vector_set (gsl_y[1], e, 0);
	    gsl_vector_set (gsl_y[2], e, 0);
	    e++;
	  }
	if (e != (int)gsl_y[0]->size)
	  abort ();
      }
    else
      {
	int e = 0;
	for (int tileid = 0; tileid < n_tiles; tileid++)
	  for (int y = 0; y < theight; y++)
	    for (int x = 0; x < twidth; x++)
	      if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
		{
		  gsl_vector_set (gsl_y[0], e, bw_get_pixel (tileid, x, y) / (2 * maxgray));
		  e++;
		}
	if (e != (int)gsl_y[0]->size)
	  abort ();
      }
    least_squares_initialized = true;
  }

  bool
  init_tile (int tileid, int cur_txmin, int cur_tymin, bool bw, scr_to_img &map, render &render)
  {
    tiles[tileid].txmin = cur_txmin;
    tiles[tileid].tymin = cur_tymin;
    type = map.get_type ();
    if (!bw)
      tiles[tileid].color = new  (std::nothrow) rgbdata [twidth * theight];
    else
      tiles[tileid].bw = new  (std::nothrow) luminosity_t [twidth * theight];

    tiles[tileid].pos = new  (std::nothrow) point_t [twidth * theight];
    if ((!tiles[tileid].color && !tiles[tileid].bw) || !tiles[tileid].pos)
      return false;
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  map.to_scr (cur_txmin + x + 0.5, cur_tymin + y + 0.5, &tiles[tileid].pos [y * twidth + x].x, &tiles[tileid].pos[y * twidth + x].y);
	  if (tiles[tileid].color)
	    tiles[tileid].color[y * twidth + x] = render.get_unadjusted_rgb_pixel (x + cur_txmin, y + cur_tymin);
	  if (tiles[tileid].bw)
	    tiles[tileid].bw[y * twidth + x] = render.get_unadjusted_data (x + cur_txmin, y + cur_tymin);
	}
    return true;
  }

  void
  init (int flags, coord_t blur_radius, const std::vector <finetune_result> *results)
  {
    optimize_position = flags & finetune_position;
    optimize_screen_blur = flags & finetune_screen_blur;
    optimize_screen_channel_blurs = flags & finetune_screen_channel_blurs;
    optimize_screen_mtf_blur = flags & finetune_screen_mtf_blur;
    optimize_emulsion_blur = flags & finetune_emulsion_blur;
    optimize_dufay_strips = (flags & finetune_dufay_strips) && type == Dufay;
    /* For one tile the effect of fog can always be simulated by adjusting the colors of screen.
       If multiple tiles (and colors) are samples we can try to estimate it.  */
    optimize_fog = (flags & finetune_fog) && tiles[0].color /*&& n_tiles > 1*/;
    least_squares = !(flags & finetune_no_least_squares);
    data_collection = !(flags & finetune_no_data_collection);
    optimize_emulsion_offset = false;
    normalize = !(flags & finetune_no_normalize);
    if (tiles[0].color && normalize)
      optimize_emulsion_blur = false;
    if (tiles[0].color && optimize_emulsion_blur && (optimize_screen_blur || optimize_screen_channel_blurs || optimize_screen_mtf_blur))
      {
        optimize_emulsion_intensities = true;
	optimize_emulsion_offset = /*(type != Dufay) && !results*/true;
	data_collection = false;
      }
    else
      optimize_emulsion_intensities = optimize_emulsion_offset = false;
    fog_by_least_squares = (optimize_fog && !normalize && least_squares);

    n_values = 0;
    /* 2 values for position.  */
    if (optimize_position)
      n_values += 2 * n_tiles;

    if (data_collection)
      least_squares = false;

    if (!least_squares && !data_collection)
      {
	color_index = n_values;
	/* 3*3 values for color.
	   3 intensitied for B&W  */
	if (tiles[0].color)
	  n_values += 9;
	else
	  n_values += 3;
      }

    fog_index = n_values;
    if (optimize_fog && !fog_by_least_squares)
      n_values += 3;

    emulsion_intensity_index = n_values;
    if (optimize_emulsion_intensities)
      n_values += 3 * n_tiles - 1;
    emulsion_offset_index = n_values;
    if (optimize_emulsion_offset)
      n_values += 2 * n_tiles;
    emulsion_blur_index = n_values;
    if (optimize_emulsion_blur)
      {
	if (!optimize_emulsion_intensities)
	  optimize_screen_blur = optimize_screen_channel_blurs = optimize_screen_mtf_blur = false;
	n_values += 1;
      }
    screen_index = n_values;
    if (optimize_screen_mtf_blur)
      {
	n_values += 4;
	optimize_screen_blur = false;
	optimize_screen_channel_blurs = false;
      }
    if (optimize_screen_channel_blurs)
      {
	optimize_screen_blur = false;
	n_values += 3;
      }
    if (optimize_screen_blur)
      n_values++;

    if (type != Dufay)
      optimize_dufay_strips = false;
    dufay_strips_index = n_values;
    if (optimize_dufay_strips)
      n_values += 2;
    start = (coord_t *)malloc (sizeof (*start) * n_values);

    original_scr = std::shared_ptr <screen> (new screen);
    if (optimize_emulsion_blur)
      emulsion_scr = std::shared_ptr <screen> (new screen);
    if (optimize_emulsion_intensities)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        tiles[tileid].merged_scr = new screen;
    for (int tileid = 0; tileid < n_tiles; tileid++)
      tiles[tileid].scr = new screen;

    last_blur = {-1, -1,-1};
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        tiles[tileid].last_emulsion_intensities = {-1, -1, -1};
        tiles[tileid].last_emulsion_offset = {-100,-100};
      }
    last_fog = {0,0,0};
    for (int i = 0; i < 4; i++)
      last_mtf[i] = -1;
    last_emulsion_blur = -1;
    last_width = -1;
    last_height = -1;
    if (!results)
      for (int tileid = 0; tileid < n_tiles; tileid++)
	{
	  set_offset (start, tileid, {0, 0});
	  set_emulsion_offset (start, tileid, {0, 0});
	}
    else
      for (int tileid = 0; tileid < n_tiles; tileid++)
	{
	  set_offset (start, tileid, (*results)[tileid].screen_coord_adjust);
	  set_emulsion_offset (start, tileid, (*results)[tileid].emulsion_coord_adjust);
	}

    if (!least_squares && !data_collection)
      {
	if (tiles[0].color)
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
    fixed_width = dufaycolor::red_width;
    fixed_height = dufaycolor::green_height;
    if (optimize_emulsion_intensities)
      for (int tileid = 0; tileid < 3 * n_tiles - 1; tileid++)
	start[emulsion_intensity_index + tileid] = 1 / 3.0;
    /* Sane scanner lens blurs close to Nqyist frequency.  */
    if (optimize_emulsion_blur)
      {
	coord_t blur = 0.03;
	if (results)
	  {
	    histogram hist;
	    for (int tileid = 0; tileid < n_tiles; tileid++)
	      hist.pre_account ((*results)[tileid].emulsion_blur_radius);
	    hist.finalize_range (65535);
	    for (int tileid = 0; tileid < n_tiles; tileid++)
	      hist.account ((*results)[tileid].emulsion_blur_radius);
	    hist.finalize ();
	    blur = hist.find_avg (0.1);
	  }
	set_emulsion_blur_radius (start, blur);
	if (fabs (get_emulsion_blur_radius (start) - blur) > 0.01)
	  {
	    printf ("Emulsion blur %f %f\n", get_emulsion_blur_radius (start), blur);
	    abort ();
	  }
      }
    if (optimize_screen_channel_blurs)
      start[screen_index] = start[screen_index + 1] = start[screen_index + 2]= rev_pixel_blur (0.3);
    else
      {
	/* Optimization seem to work better when it starts from small blur.  */
        if (optimize_screen_blur && !results)
	  blur_radius = 0.3;
	if (results)
	  {
	    histogram hist;
	    for (int tileid = 0; tileid < n_tiles; tileid++)
	      hist.pre_account ((*results)[tileid].screen_blur_radius);
	    hist.finalize_range (65535);
	    for (int tileid = 0; tileid < n_tiles; tileid++)
	      hist.account ((*results)[tileid].screen_blur_radius);
	    hist.finalize ();
	    blur_radius = hist.find_avg (0.1);
	  }
        set_blur_radius (start, blur_radius);
	if (fabs (get_blur_radius (start) - blur_radius) > 0.01)
	  {
	    printf ("Screen blur %f %f\n", get_blur_radius (start), blur_radius);
	    abort ();
	  }
      }
    if (optimize_screen_mtf_blur)
      {
        start[screen_index] = 0;
        start[screen_index + 1] = 0;
        start[screen_index + 2] = 0;
        start[screen_index + 3] = 0;
      }
    if (optimize_dufay_strips)
      {
	start[dufay_strips_index + 0] = dufaycolor::red_width;
	start[dufay_strips_index + 1] = dufaycolor::green_height;
      }

    maxgray = 0;
    if (tiles[0].bw)
      for (int tileid = 0; tileid < n_tiles; tileid++)
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    maxgray = std::max (maxgray, bw_get_pixel (tileid, x, y));
    constrain (start);
    if (optimize_fog)
      {
	rgb_histogram hist;
	for (int tileid = 0; tileid < n_tiles; tileid++)
	  for (int y = 0; y < theight; y++)
	    for (int x = 0; x < twidth; x++)
	      hist.pre_account (tiles[tileid].color[y * twidth + x]);
	hist.finalize_range (65535);
	for (int tileid = 0; tileid < n_tiles; tileid++)
	  for (int y = 0; y < theight; y++)
	    for (int x = 0; x < twidth; x++)
	      hist.account (tiles[tileid].color[y * twidth + x]);
	hist.finalize ();
	fog_range = hist.find_min (0.01);
	start[fog_index] = 0;
	start[fog_index + 1] = 0;
	start[fog_index + 2] = 0;
      }

    if (tiles[0].color && normalize && !optimize_fog)
      for (int tileid = 0; tileid < n_tiles; tileid++)
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      rgbdata &c = tiles[tileid].color[y * twidth + x];
	      luminosity_t sum = c.red + c.green + c.blue;
	      if (sum > 0)
		c /= sum;
	    }

    if (least_squares)
      {
	free_least_squares ();
	alloc_least_squares ();
	if (!optimize_fog || fog_by_least_squares)
	  init_least_squares (NULL);
      }
    simulated_screen_border = 0;
    simulated_screen_width = twidth;
    simulated_screen_height = theight;
    for (int tileid = 0; tileid < n_tiles; tileid++)
      tiles[tileid].simulated_screen = new  (std::nothrow) rgbdata [simulated_screen_width * simulated_screen_height];
  }

  coord_t
  solve (progress_info *progress, bool report)
  {
    //if (verbose)
      //solver.print_values (solver.start);
    coord_t uncertainity = simplex<coord_t, finetune_solver>(*this, "finetuning", progress, report);
    if (tiles[0].bw)
      {
	rgbdata c = last_color;
	coord_t mmin = std::min (std::min (c.red, c.green), c.blue);
	coord_t mmax = std::max (std::max (c.red, c.green), c.blue);
	if (mmin != mmax)
	  uncertainity /= (mmax - mmin);
	else
	  uncertainity = 100000000;
      }
    free_least_squares ();
    return uncertainity;
  }

  rgbdata
  get_simulated_screen_pixel (int tile, int x, int y)
  {
    return tiles[tile].simulated_screen [y * simulated_screen_width + x];
  }

  bool
  mtf_differs (luminosity_t mtf[4], luminosity_t last_mtf[4])
  {
    for (int i = 0; i < 4; i++)
      if (mtf[i] != last_mtf[i])
	return true;
    return false;
  }

  void
  apply_blur (coord_t *v, int tileid, screen *dst_scr, screen *src_scr, screen *weight_scr = NULL)
  {
    rgbdata blur = get_channel_blur_radius (v);
    luminosity_t mtf[4];
    get_mtf (mtf, v);

    if (weight_scr)
      {
	rgbdata i = get_emulsion_intensities (v, tileid);
	point_t offset = get_emulsion_offset (v, tileid);
	if (offset.x == 0 && offset.y == 0)
	  for (int y = 0; y < screen::size; y++)
	    for (int x = 0; x < screen::size; x++)
	      {
		luminosity_t w = weight_scr->mult[y][x][0] * i.red + weight_scr->mult[y][x][1] * i.green + weight_scr->mult[y][x][2] * i.blue;
		tiles[tileid].merged_scr->mult[y][x][0] = src_scr->mult[y][x][0] * w;
		tiles[tileid].merged_scr->mult[y][x][1] = src_scr->mult[y][x][1] * w;
		tiles[tileid].merged_scr->mult[y][x][2] = src_scr->mult[y][x][2] * w;
	      }
	else
	  for (int y = 0; y < screen::size; y++)
	    for (int x = 0; x < screen::size; x++)
	      {
		rgbdata wd = weight_scr->interpolated_mult
		  ((point_t){x * (1 / (coord_t)screen::size), y * (1 / (coord_t)screen::size)} + offset);
		luminosity_t w = wd.red * i.red + wd.green * i.green + wd.blue * i.blue;
		tiles[tileid].merged_scr->mult[y][x][0] = src_scr->mult[y][x][0] * w;
		tiles[tileid].merged_scr->mult[y][x][1] = src_scr->mult[y][x][1] * w;
		tiles[tileid].merged_scr->mult[y][x][2] = src_scr->mult[y][x][2] * w;
	      }
	src_scr = tiles[tileid].merged_scr;
      }

    if (optimize_screen_mtf_blur)
      dst_scr->initialize_with_blur (*src_scr, mtf);
    else
      dst_scr->initialize_with_blur (*src_scr, blur * pixel_size);
  }

  void
  init_screen (coord_t *v, int tileid)
  {
    luminosity_t emulsion_blur = get_emulsion_blur_radius (v);
    rgbdata blur = get_channel_blur_radius (v);
    luminosity_t red_strip_width = get_red_strip_width (v);
    luminosity_t green_strip_height = get_green_strip_width (v);
    luminosity_t mtf[4];
    rgbdata intensities = get_emulsion_intensities (v, tileid);
    point_t emulsion_offset = get_emulsion_offset (v, tileid);
    get_mtf (mtf, v);

    bool updated = false;
    if (red_strip_width != last_width || green_strip_height != last_height)
      {
        original_scr->initialize (type, red_strip_width, green_strip_height);
	last_width = red_strip_width;
	last_height = green_strip_height;
	updated = true;
      }

    if (optimize_emulsion_blur && (emulsion_blur != last_emulsion_blur || updated))
      {
	emulsion_scr->initialize_with_blur (*original_scr, emulsion_blur);
	last_emulsion_blur = emulsion_blur;
	updated = true;
      }

    /* Force update on all tiles.  */
    if (updated)
      for (int t = 0; t < n_tiles; t++)
	tiles[t].last_emulsion_offset = {-10,-10};
    
    if (blur != last_blur || optimize_screen_mtf_blur || mtf_differs (mtf, last_mtf) || tiles[tileid].last_emulsion_intensities != intensities || tiles[tileid].last_emulsion_offset != emulsion_offset)
      {
	apply_blur (v, tileid, tiles[tileid].scr,
		    optimize_emulsion_blur && !optimize_emulsion_intensities? emulsion_scr.get () : original_scr.get (),
		    optimize_emulsion_intensities ? emulsion_scr.get () : NULL);
	last_blur = blur;
	tiles[tileid].last_emulsion_intensities = intensities;
	tiles[tileid].last_emulsion_offset = emulsion_offset;
	memcpy (last_mtf, mtf, sizeof (last_mtf));
      }

  }

  /* Determine color of screen at pixel X,Y with offset OFF.  */
  pure_attr inline rgbdata
  evaulate_screen_pixel (int tileid, int x, int y, point_t off)
  {
    point_t p = tiles[tileid].pos[y * twidth + x] + off;
    if (0)
      {
	/* Interpolation here is necessary to ensure smoothness.  */
	return tiles[tileid].scr->interpolated_mult (p);
      }
    int dx = x == twidth - 1 ? -1 : 1;
    point_t px = tiles[tileid].pos [y * twidth + (x + dx)] + off;
    int dy = y == theight - 1 ? -1 : 1;
    point_t py = tiles[tileid].pos [(y + dy) * twidth + x] + off;
    point_t pdx = (px - p) * (1.0 / 6.0) * dx;
    point_t pdy = (py - p) * (1.0 / 6.0) * dy;
    rgbdata m = {0,0,0};
    for (int yy = -2; yy <= 2; yy++)
      for (int xx = -2; xx <= 2; xx++)
	m+= tiles[tileid].scr->interpolated_mult (p + pdx * xx + pdy * yy);
    return m * ((coord_t)1.0 / 25);
  }

  /* Evaulate pixel at (x,y) using RGB values v and offsets offx, offy
     compensating coordates stored in tole_pos.  */
  pure_attr inline rgbdata
  evaulate_pixel (int tileid, rgbdata red, rgbdata green, rgbdata blue, int x, int y, point_t off)
  {
    rgbdata m = get_simulated_screen_pixel (tileid, x, y);
    return ((red * m.red + green * m.green + blue * m.blue) * ((coord_t)1.0 / rgbscale));
  }

  void
  simulate_screen (coord_t *v, int tileid)
  {
    rgbdata red, green, blue;
    point_t off = get_offset (v, tileid);
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	tiles[tileid].simulated_screen [y * simulated_screen_width + x] = evaulate_screen_pixel (tileid, x, y, off);
  }

  /* Evaulate pixel at (x,y) using RGB values v and offsets offx, offy
     compensating coordates stored in tole_pos.  */
  pure_attr inline luminosity_t
  bw_evaulate_pixel (int tileid, rgbdata color, int x, int y, point_t off)
  {
    rgbdata m = get_simulated_screen_pixel (tileid, x, y);
    return ((m.red * color.red + m.green * color.green + m.blue * color.blue) /** (2 * maxgray)*/);
  }

  pure_attr rgbdata
  get_pixel (coord_t *v, int tileid, int x, int y)
  {
     if (!optimize_fog)
       return tiles[tileid].color[y * twidth + x];
     rgbdata d = tiles[tileid].color[y * twidth + x] - get_fog (v);
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
  bw_get_pixel (int tileid, int x, int y)
  {
     return tiles[tileid].bw[y * twidth + x];
  }
  void
  determine_colors_using_data_collection (coord_t *v, rgbdata *ret_red, rgbdata *ret_green, rgbdata *ret_blue)
  {
    rgbdata red = {0,0,0}, green = {0,0,0}, blue = {0,0,0};
    rgbdata color_red = {0,0,0}, color_green = {0,0,0}, color_blue = {0,0,0};
    luminosity_t threshold = collection_threshold;
    coord_t wr = 0, wg = 0, wb = 0;

    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
	    {
	      rgbdata m = get_simulated_screen_pixel (tileid,x, y);
	      rgbdata d = get_pixel (v, tileid, x, y);
	      if (m.red > threshold)
		{
		  coord_t val = m.red - threshold;
		  wr += val;
		  red += m * val;
		  color_red += d * val;
		}
	      if (m.green > threshold)
		{
		  coord_t val = m.green - threshold;
		  wg += val;
		  green += m * val;
		  color_green += d * val;
		}
	      if (m.blue > threshold)
		{
		  coord_t val = m.blue - threshold;
		  wb += val;
		  blue += m * val;
		  color_blue += d * val;
		}
	    }
  if (!wr || !wg || !wb)
    {
      *ret_red = *ret_green = *ret_blue = {-15,-15,-15};
      return;
    }

  red /= wr;
  green /= wg;
  blue /= wb;
  color_red /= wr;
  color_green /= wg;
  color_blue /= wb;
  //sum /= n;
  //sum.print (stdout);
  rgbdata cred = (rgbdata){red.red, green.red, blue.red};
  rgbdata cgreen = (rgbdata){red.green, green.green, blue.green};
  rgbdata cblue = (rgbdata){red.blue, green.blue, blue.blue};
  color_matrix sat (cred.red  , cgreen.red  , cblue.red,   0,
		    cred.green, cgreen.green, cblue.green, 0,
		    cred.blue , cgreen.blue , cblue.blue , 0,
		    0         , 0           , 0          , 1);
  sat = sat.invert ();
  //sat.apply_to_rgb (color.red / (2 * maxgray), color.green / (2 * maxgray), color.blue / (2 * maxgray), &color.red, &color.green, &color.blue);
  sat.apply_to_rgb (color_red.red, color_green.red, color_blue.red, &color_red.red, &color_green.red, &color_blue.red);
  sat.apply_to_rgb (color_red.green, color_green.green, color_blue.green, &color_red.green, &color_green.green, &color_blue.green);
  sat.apply_to_rgb (color_red.blue, color_green.blue, color_blue.blue, &color_red.blue, &color_green.blue, &color_blue.blue);
  /* Colors should be real reactions of scanner, so no negative values and also
     no excessively large values. Allow some overexposure.  */
  for (int c = 0; c < 3; c++)
    {
      to_range (color_red[c], 0, 2);
      to_range (color_green[c], 0, 2);
      to_range (color_blue[c], 0, 2);
    }

   *ret_red = color_red;
   *ret_green = color_green;
   *ret_blue = color_blue;
  }

  coord_t
  determine_colors_using_least_squares (coord_t *v, rgbdata *red, rgbdata *green, rgbdata *blue)
  {
    coord_t sqsum = 0;

    if (!least_squares_initialized)
      abort ();

    int e = 0;
    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
	    {
	      rgbdata c = get_simulated_screen_pixel (tileid, x, y);
	      gsl_matrix_set (gsl_X, e, 0, c.red);
	      gsl_matrix_set (gsl_X, e, 1, c.green);
	      gsl_matrix_set (gsl_X, e, 2, c.blue);
	      if (fog_by_least_squares)
		gsl_matrix_set (gsl_X, e, 3, 1);
	      e++;
	    }
    if (fog_by_least_squares)
      {
	gsl_matrix_set (gsl_X, e, 0, 0);
	gsl_matrix_set (gsl_X, e, 1, 0);
	gsl_matrix_set (gsl_X, e, 2, 0);
	gsl_matrix_set (gsl_X, e, 3, sample_points () * ((double)4 / 65546));
	e++;
      }
    if (e != (int)gsl_X->size1)
      abort ();

    for (int ch = 0; ch < 3; ch++)
      {
	double chisq;
	gsl_multifit_linear (gsl_X, gsl_y[ch], gsl_c, gsl_cov,
			     &chisq, gsl_work);
	sqsum += chisq;
	/* Colors should be real reactions of scanner, so no negative values and also
	   no excessively large values. Allow some overexposure.  */
	(*red)[ch] = gsl_vector_get (gsl_c, 0);
	to_range ((*red)[ch], 0, 2);
	(*green)[ch] = gsl_vector_get (gsl_c, 1);
	to_range ((*green)[ch], 0, 2);
	(*blue)[ch] = gsl_vector_get (gsl_c, 2);
	to_range ((*blue)[ch], 0, 2);
	if (fog_by_least_squares)
	  {
	    last_fog[ch] = gsl_vector_get (gsl_c, 3);
	    to_range (last_fog[ch], 0, fog_range[ch]);
	  }
      }
    return sqsum;
  }

  rgbdata
  bw_determine_color_using_data_collection (coord_t *v)
  {
    rgbdata red = {0,0,0}, green = {0,0,0}, blue = {0,0,0};
    rgbdata color = {0,0,0};
    luminosity_t threshold = collection_threshold;
    coord_t wr = 0, wg = 0, wb = 0;

    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
	    {
	      rgbdata m = get_simulated_screen_pixel (tileid, x, y);
	      luminosity_t l = bw_get_pixel (tileid, x, y);
	      if (m.red > threshold)
		{
		  coord_t val = m.red - threshold;
		  wr += val;
		  red += m * val;
		  color.red += l * val;
		}
	      if (m.green > threshold)
		{
		  coord_t val = m.green - threshold;
		  wg += val;
		  green += m * val;
		  color.green += l * val;
		}
	      if (m.blue > threshold)
		{
		  coord_t val = m.blue - threshold;
		  wb += val;
		  blue += m * val;
		  color.blue += l * val;
		}
	    }
  if (!wr || !wg || !wb)
    return {-10,-10,-10};

  red /= wr;
  green /= wg;
  blue /= wb;
  color.red /= wr;
  color.green /= wg;
  color.blue /= wb;
  //sum /= n;
  //sum.print (stdout);
  rgbdata cred = (rgbdata){red.red, green.red, blue.red};
  rgbdata cgreen = (rgbdata){red.green, green.green, blue.green};
  rgbdata cblue = (rgbdata){red.blue, green.blue, blue.blue};
  color_matrix sat (cred.red  , cgreen.red  , cblue.red,   0,
		    cred.green, cgreen.green, cblue.green, 0,
		    cred.blue , cgreen.blue , cblue.blue , 0,
		    0         , 0           , 0          , 1);
  sat = sat.invert ();
  //sat.apply_to_rgb (color.red / (2 * maxgray), color.green / (2 * maxgray), color.blue / (2 * maxgray), &color.red, &color.green, &color.blue);
  sat.apply_to_rgb (color.red, color.green, color.blue, &color.red, &color.green, &color.blue);
  if (color.red < 0)
    color.red = 0;
  if (color.green < 0)
    color.green = 0;
  if (color.blue < 0)
    color.blue = 0;
  return color;
  }

  rgbdata
  bw_determine_color_using_least_squares (coord_t *v)
  {
    int e = 0;
    if (!least_squares_initialized)
      abort ();
    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
	    {
	      rgbdata c = get_simulated_screen_pixel (tileid, x, y);
	      gsl_matrix_set (gsl_X, e, 0, c.red);
	      gsl_matrix_set (gsl_X, e, 1, c.green);
	      gsl_matrix_set (gsl_X, e, 2, c.blue);
	      e++;
	      //gsl_vector_set (gsl_y[0], e, bw_get_pixel (x, y) / (2 * maxgray));
	    }
    if (e != (int)gsl_X->size1)
      abort ();
    double chisq;
    gsl_multifit_linear (gsl_X, gsl_y[0], gsl_c, gsl_cov, &chisq, gsl_work);
    rgbdata color = {gsl_vector_get (gsl_c, 0) * (2 * maxgray), gsl_vector_get (gsl_c, 1) * (2 * maxgray), gsl_vector_get (gsl_c, 2) * (2 * maxgray)};
    if (color.red < 0)
      color.red = 0;
    if (color.green < 0)
      color.green = 0;
    if (color.blue < 0)
      color.blue = 0;
    return color;
  }

  rgbdata
  get_fog (coord_t *v)
  {
    if (!optimize_fog)
      return {0, 0, 0};
    if (fog_by_least_squares)
      return last_fog;
    return {v[fog_index] * fog_range.red, v[fog_index+1] * fog_range.green, v[fog_index+2] * fog_range.blue};
  }

  rgbdata
  get_emulsion_intensities (coord_t *v, int tileid)
  {
    if (optimize_emulsion_intensities)
      {
        /* Together with screen colors these are defined only up to scaling factor.  */
	if (!tileid)
	  {
	    luminosity_t red = v[emulsion_intensity_index];
	    luminosity_t green = v[emulsion_intensity_index + 1];
	    luminosity_t blue = 1 - red - green;
	    if (blue < 0)
	      blue = 0;
	    return {red, green, blue};
	  }
	return {v[emulsion_intensity_index + 3 * tileid - 1],
	        v[emulsion_intensity_index + 3 * tileid - 0],
	        v[emulsion_intensity_index + 3 * tileid + 1]};
      }
    else
      return {1, 1, 1};
  }

  rgbdata
  get_mix_weights (coord_t *v)
  {
    rgbdata red, green, blue;
    get_colors (v, &red, &green, &blue);
    color_matrix process_colors (red.red,   green.red,   blue.red, 0,
				 red.green, green.green, blue.green, 0,
				 red.blue,  green.blue,  blue.blue, 0,
				 0, 0, 0, 1);
    process_colors.transpose ();
    rgbdata ret;
    process_colors.invert ().apply_to_rgb (1/3.0, 1/3.0, 1/3.0, &ret.red, &ret.green, &ret.blue);
    luminosity_t sum = ret.red + ret.green + ret.blue;
    return ret * (1 / sum);
  }

  rgbdata
  bw_get_color (coord_t *v)
  {
    if (!least_squares && !data_collection)
      last_color = {v[color_index], v[color_index + 1], v[color_index + 2]};
    if (data_collection)
      last_color = bw_determine_color_using_data_collection (v);
    else
      last_color = bw_determine_color_using_least_squares (v);
    return last_color;
  }
  void
  get_colors (coord_t *v, rgbdata *red, rgbdata *green, rgbdata *blue)
  {
    if (!least_squares && !data_collection)
      {
	*red = {v[color_index], v[color_index + 1], v[color_index + 2]};
	*green = {v[color_index + 3], v[color_index + 4], v[color_index + 5]};
	*green = {v[color_index + 6], v[color_index + 7], v[color_index + 8]};
      }
    else if (data_collection)
      determine_colors_using_data_collection (v, red, green, blue);
    else
      {
	if (least_squares && (optimize_fog && !fog_by_least_squares))
	  init_least_squares (v);
	determine_colors_using_least_squares (v, red, green, blue);
      }
    last_red = *red;
    last_green = *green;
    last_blue = *blue;
  }

  coord_t
  objfunc (coord_t *v)
  {
    coord_t sum = 0;
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
	init_screen (v, tileid);
	simulate_screen (v, tileid);
      }
    rgbdata red, green, blue, color;
    if (tiles[0].color)
      get_colors (v, &red, &green, &blue);
    else
      color = bw_get_color (v);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
	point_t off = get_offset (v, tileid);
	if (tiles[0].color)
	  {
	    for (int y = 0; y < theight; y++)
	      for (int x = 0; x < twidth; x++)
		if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
		  {
		    rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
		    rgbdata d = get_pixel (v, tileid, x, y);

		    /* Bayer pattern. */
		    if (!(x&1) && !(y&1))
		      sum += fabs (c.red - d.red) * 2;
		    else if ((x&1) && (y&1))
		      sum += fabs (c.blue - d.blue) * 2;
		    else
		      sum += fabs (c.green - d.green);
#if 0
		    sum += fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
#endif
			    /*(c.red - d.red) * (c.red - d.red) + (c.green - d.green) * (c.green - d.green) + (c.blue - d.blue) * (c.blue - d.blue)*/
		  }
	  }
	else if (tiles[tileid].bw)
	  {
	    for (int y = 0; y < theight; y++)
	      for (int x = 0; x < twidth; x++)
		if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
		  {
		    luminosity_t c = bw_evaulate_pixel (tileid, color, x, y, off);
		    luminosity_t d = bw_get_pixel (tileid, x, y);
		    sum += fabs (c - d);
		  }
	    sum /= maxgray;
	  }
      }
    //printf ("%f\n", sum);
    /* Avoid solver from increasing blur past point it is no longer useful.
       Otherwise it will pick solutions with too large blur and very contrasty
       colors.  */
   return (sum / sample_points ()) * (1 + get_blur_radius (v) * 0.01) /** (1 + get_emulsion_blur_radius (v) * 0.0001)*/;
  }

  void
  collect_screen (screen *s, coord_t *v, int tileid)
  {
    point_t off = get_offset (v, tileid);
    for (int y = 0; y < screen::size; y++)
      for (int x = 0; x < screen::size; x++)
	for (int c = 0; c < 3; c++)
	  {
	    s->mult[y][x][c] = (luminosity_t)0;
	    s->add[y][x][c] = (luminosity_t)0;
	  }
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
	  {
	    point_t p = tiles[tileid].pos [y * twidth + x] + off;
	    int xx = ((int64_t)nearest_int (p.x * screen::size)) & (screen::size - 1);
	    int yy = ((int64_t)nearest_int (p.y * screen::size)) & (screen::size - 1);
	    if (tiles[tileid].color)
	      {
		s->mult[yy][xx][0] = tiles[tileid].color[y * twidth + x].red;
		s->mult[yy][xx][1] = tiles[tileid].color[y * twidth + x].green;
		s->mult[yy][xx][2] = tiles[tileid].color[y * twidth + x].blue;
	      }
	    else
	      {
		s->mult[yy][xx][0] = tiles[tileid].bw[y * twidth + x];
		s->mult[yy][xx][1] = tiles[tileid].bw[y * twidth + x];
		s->mult[yy][xx][2] = tiles[tileid].bw[y * twidth + x];
	      }
	    s->add[yy][xx][0] = 1;
	  }
    for (int i = 0; i < screen::size; i++)
      for (int y = 0; y < screen::size; y++)
        for (int x = 0; x < screen::size; x++)
	  if (!s->add[y][x][0])
	    {
	      luminosity_t newv[3] = {(luminosity_t)0,(luminosity_t)0,(luminosity_t)0};
	      int n = 0;
	      for (int xo = -1; xo <= 1; xo++)
	        for (int yo = -1; yo <= 1; yo++)
		  {
		    int nx = (x + xo) & (screen::size - 1);
		    int ny = (y + yo) & (screen::size - 1);
		    if (s->mult[ny][nx][0] + s->mult[ny][nx][1] + s->mult[ny][nx][2])
		      {
		        newv[0] += s->mult[ny][nx][0];
		        newv[1] += s->mult[ny][nx][1];
		        newv[2] += s->mult[ny][nx][2];
			n++;
		      }
		  }
	      if (n)
		{
		  s->mult[y][x][0] = newv[0] / n;
		  s->mult[y][x][1] = newv[1] / n;
		  s->mult[y][x][2] = newv[2] / n;
		}
	    }
    for (int y = 0; y < screen::size; y++)
      for (int x = 0; x < screen::size; x++)
	s->add[y][x][0] = 0;
  }

  int
  determine_outliers (coord_t *v, coord_t ratio)
  {
    rgbdata red, green, blue;
    get_colors (v, &red, &green, &blue);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
	histogram hist;
	point_t off = get_offset (v, tileid);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
	      rgbdata d = get_pixel (v, tileid, x, y);
	      coord_t err = fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
	      hist.pre_account (err);
	    }
	hist.finalize_range (65535);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
	      rgbdata d = get_pixel (v, tileid, x, y);
	      coord_t err = fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
	      hist.account (err);
	    }
	hist.finalize ();
	coord_t merr = hist.find_max (ratio) * 1.3;
	tiles[tileid].outliers = std::make_unique <bitmap_2d> (twidth, theight);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
	      rgbdata d = get_pixel (v, tileid, x, y);
	      coord_t err = fabs (c.red - d.red) + fabs (c.green - d.green) + fabs (c.blue - d.blue);
	      if (err > merr)
		{
		  noutliers++;
		  tiles[tileid].outliers->set_bit (x, y);
		}
	    }
      }
    if (!noutliers)
      return 0;
    if (least_squares)
      {
	free_least_squares ();
	alloc_least_squares ();
	if (!optimize_fog || fog_by_least_squares)
	  init_least_squares (NULL);
      }
    return noutliers;
  }

  int
  bw_determine_outliers (coord_t *v, coord_t ratio)
  {
    rgbdata color = bw_get_color (v);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
	histogram hist;
	point_t off = get_offset (v, tileid);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      luminosity_t c = bw_evaulate_pixel (tileid, color, x, y, off);
	      luminosity_t d = bw_get_pixel (tileid, x, y);
	      coord_t err = fabs (c - d);
	      hist.pre_account (err);
	    }
	hist.finalize_range (65535);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      luminosity_t c = bw_evaulate_pixel (tileid, color, x, y, off);
	      luminosity_t d = bw_get_pixel (tileid, x, y);
	      coord_t err = fabs (c - d);
	      hist.account (err);
	    }
	hist.finalize ();
	coord_t merr = hist.find_max (ratio) * 1.3;
	tiles[tileid].outliers = std::make_unique <bitmap_2d> (twidth, theight);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      luminosity_t c = bw_evaulate_pixel (tileid, color, x, y, off);
	      luminosity_t d = bw_get_pixel (tileid, x, y);
	      coord_t err = fabs (c - d);
	      if (err > merr)
		{
		  noutliers++;
		  tiles[tileid].outliers->set_bit (x, y);
		}
	    }
      }
    if (!noutliers)
      return 0;
    if (least_squares)
      {
	free_least_squares ();
	alloc_least_squares ();
	if (!optimize_fog || fog_by_least_squares)
	  init_least_squares (NULL);
      }
    return noutliers;
  }

  bool
  write_file (coord_t *v, const char *name, int tileid, int type)
  {
    init_screen (v, tileid);
    point_t off = get_offset (v, tileid);
    void *buffer;
    size_t len = create_linear_srgb_profile (&buffer);

    tiff_writer_params p;
    p.filename = name;
    p.width = twidth;
    p.icc_profile = buffer;
    p.icc_profile_len = len;
    p.height = theight;
    p.depth = 16;
    const char *error;
    tiff_writer rendered (p, &error);
    free (buffer);
    if (error)
      return false;


    if (tiles[0].color)
      {
	luminosity_t rmax = 0, gmax = 0, bmax = 0;
	rgbdata red, green, blue;
	get_colors (v, &red, &green, &blue);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
	      rmax = std::max (c.red, rmax);
	      gmax = std::max (c.green, gmax);
	      bmax = std::max (c.blue, bmax);
	      rgbdata d = get_pixel (v, tileid, x, y);
	      rmax = std::max (d.red, rmax);
	      gmax = std::max (d.green, gmax);
	      bmax = std::max (d.blue, bmax);
	    }

	for (int y = 0; y < theight; y++)
	  {
	    for (int x = 0; x < twidth; x++)
	      if (type == 1 || !noutliers || !tiles[tileid].outliers->test_bit (x, y))
		switch (type)
		  {
		  case 0:
		    {
		      rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
		      rendered.put_pixel (x, c.red * 65535 / rmax, c.green * 65535 / gmax, c.blue * 65535 / bmax);
		    }
		    break;
		  case 1:
		    {
		      rgbdata d = get_pixel (v, tileid, x, y);
		      rendered.put_pixel (x, d.red * 65535 / rmax, d.green * 65535 / gmax, d.blue * 65535 / bmax);
		    }
		    break;
		  case 2:
		    {
		      rgbdata c = evaulate_pixel (tileid, red, green, blue, x, y, off);
		      rgbdata d = get_pixel (v, tileid, x, y);
		      rendered.put_pixel (x, (c.red - d.red) * 65535 / rmax + 65536/2, (c.green - d.green) * 65535 / gmax + 65536/2, (c.blue - d.blue) * 65535 / bmax + 65536/2);
		    }
		    break;
		  }
	      else
		rendered.put_pixel (x, 0, 0, 0);
	    if (!rendered.write_row ())
	      return false;
	  }
      }
    if (tiles[tileid].bw)
      {
	luminosity_t lmax = 0;
	rgbdata color = bw_get_color (v);
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    {
	      lmax = std::max (bw_evaulate_pixel (tileid, color, x, y, off), lmax);
	      lmax = std::max (bw_get_pixel (tileid, x, y), lmax);
	    }

	for (int y = 0; y < theight; y++)
	  {
	    for (int x = 0; x < twidth; x++)
	      if (type == 1 || !noutliers || !tiles[tileid].outliers->test_bit (x, y))
		switch (type)
		  {
		  case 0:
		    {
		      luminosity_t c = bw_evaulate_pixel (tileid, color, x, y, off);
		      rendered.put_pixel (x, c * 65535 / lmax, c * 65535 / lmax, c * 65535 / lmax);
		    }
		    break;
		  case 1:
		    {
		      luminosity_t d = bw_get_pixel (tileid, x, y);
		      rendered.put_pixel (x, d * 65535 / lmax, d * 65535 / lmax, d * 65535 / lmax);
		    }
		    break;
		  case 2:
		    {
		      luminosity_t c = bw_evaulate_pixel (tileid, color, x, y, off);
		      luminosity_t d = bw_get_pixel (tileid, x, y);
		      rendered.put_pixel (x, (c-d) * 65535 / lmax + 65536/2, (c-d) * 65535 / lmax + 65536/2, (c-d) * 65535 / lmax + 65536/2);
		    }
		    break;
		  }
	      else
		rendered.put_pixel (x, 65535, 0, 0);
	    if (!rendered.write_row ())
	      return false;
	  }
      }
    return true;
  }
};

static coord_t
sign (point_t p1, point_t p2, point_t p3)
{
  return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}
/* Compute a, b such that
   x1 + dx1 * a = x2 + dx2 * b
   y1 + dy1 * a = y2 + dy2 * b  */
void
intersect_vectors (coord_t x1, coord_t y1, coord_t dx1, coord_t dy1,
		   coord_t x2, coord_t y2, coord_t dx2, coord_t dy2,
		   coord_t *a, coord_t *b)
{
  matrix2x2<coord_t> m (dx1, -dx2, dy1, -dy2);
  m = m.invert ();
  m.apply_to_vector (x2 - x1, y2 - y1, a, b);
}
}

/* Finetune parameters and update RPARAM.  */

finetune_result
finetune (render_parameters &rparam, const scr_to_img_parameters &param, const image_data &img, const std::vector <point_t> &locs, const std::vector <finetune_result> *results, const finetune_parameters &fparams, progress_info *progress)
{
  finetune_result ret = {false, {-1, -1}, -1, -1, -1, {-1, -1, -1}, {-1, -1, -1, -1}, -1, -1, -1, {-1, -1}, {-1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};

  int n_tiles = locs.size ();
  if (n_tiles > finetune_solver::max_tiles)
    n_tiles = finetune_solver::max_tiles;
  if (!n_tiles)
    {
      ret.err = "no tile locations";
      return ret;
    }
  const image_data *imgp[finetune_solver::max_tiles];
  scr_to_img *mapp[finetune_solver::max_tiles];
  int x[finetune_solver::max_tiles];
  int y[finetune_solver::max_tiles];
  coord_t pixel_size = -1;

  scr_to_img map;
  imgp[0] = NULL;
  mapp[0] = NULL;
  x[0] = 0;
  y[0] = 0;
  for (int tileid = 0; tileid < n_tiles; tileid++)
    {
      x[tileid] = locs[tileid].x;
      y[tileid] = locs[tileid].y;
      imgp[tileid] = &img;
      if (img.stitch)
	{
	  coord_t sx, sy;
	  int tx, ty;
	  img.stitch->common_scr_to_img.final_to_scr (x[tileid] + img.xmin, y[tileid] + img.ymin, &sx, &sy);
	  pixel_size = img.stitch->pixel_size;
	  if (!img.stitch->tile_for_scr (&rparam, sx, sy, &tx, &ty, true))
	    {
	      ret.err = "no tile for given coordinates";
	      return ret;
	    }
	  img.stitch->images[ty][tx].common_scr_to_img (sx, sy, &sx, &sy);
	  x[tileid] = nearest_int (sx);
	  y[tileid] = nearest_int (sy);
	  imgp[tileid] = img.stitch->images[ty][tx].img.get ();
	  mapp[tileid] = &img.stitch->images[ty][tx].scr_to_img_map;
	}
      else
	{
	  if (!tileid)
	    {
	      map.set_parameters (param, *imgp[tileid]);
	      pixel_size = map.pixel_size (imgp[tileid]->width, imgp[tileid]->height);
	    }
	  mapp[tileid] = &map;
	}
    }
  bool bw = fparams.flags & finetune_bw;
  bool verbose = fparams.flags & finetune_verbose;

  if (!bw && !imgp[0]->rgbdata)
    bw = true;

  /* Determine tile to analyze.  */
  coord_t tx, ty;
  mapp[0]->to_scr (x[0], y[0], &tx, &ty);
  int sx = nearest_int (tx);
  int sy = nearest_int (ty);

  coord_t test_range = fparams.range ? fparams.range : ((fparams.flags & finetune_no_normalize) || bw ? 1 : 2);
  mapp[0]->to_img (sx, sy, &tx, &ty);
  mapp[0]->to_img (sx - test_range, sy - test_range, &tx, &ty);
  coord_t sxmin = tx, sxmax = tx, symin = ty, symax = ty;
  mapp[0]->to_img (sx + test_range, sy - test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);
  mapp[0]->to_img (sx + test_range, sy + test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);
  mapp[0]->to_img (sx - test_range, sy + test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);

  int txmin = floor (sxmin), tymin = floor (symin), txmax = ceil (sxmax), tymax = ceil (symax);
  if (txmin < 0)
    txmin = 0;
  if (txmax > imgp[0]->width)
    txmax = imgp[0]->width;
  if (tymin < 0)
    tymin = 0;
  if (tymax > imgp[0]->height)
    tymax = imgp[0]->height;
  if (txmin + 10 > txmax || tymin + 10 > tymax)
    {
      if (verbose)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Too small tile %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
	  progress->resume_stdout ();
	}
      ret.err = "too small tile";
      return ret;
    }
  int twidth = txmax - txmin + 1, theight = tymax - tymin + 1;
  if (verbose)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Tile size %ix%i; %i tiles\n", twidth, theight, n_tiles);
      progress->resume_stdout ();
    }
  finetune_solver best_solver;
  coord_t best_uncertainity = -1;
  bool failed = false;

  render_parameters rparam2 = rparam;
  rparam2.invert = 0;

  /* Multitile support only for 1 tile.  */
  if (n_tiles == 1)
    {
      ///* FIXME: Hack; render is too large for stack in openmp thread.  */
      //std::unique_ptr<render_to_scr> rp(new render_to_scr (param, img, rparam, 256));
      render render (*imgp[0], rparam2, 256);
      //int rxmin = txmin, rxmax = txmax, rymin = tymin, rymax = tymax;
      int maxtiles = fparams.multitile;
      if (maxtiles < 1)
	maxtiles = 1;
      if (!(maxtiles & 1))
	maxtiles++;
#if 0
      if (maxtiles > 1)
	{
	  rxmin = std::max (txmin - twidth * (maxtiles / 2), 0);
	  rymin = std::max (tymin - theight * (maxtiles / 2), 0);
	  rxmax = std::max (txmax + (twidth * maxtiles / 2), imgp[0]->width - 1);
	  rymax = std::max (tymax + (theight * maxtiles / 2), imgp[0]->height - 1);
	}
      //if (!render.precompute_img_range (bw /*grayscale*/, false /*normalized*/, rxmin, rymin, rxmax + 1, rymax + 1, !(fparams.flags & finetune_no_progress_report) ? progress : NULL))
#endif
      if (!render.precompute_all (bw /*grayscale*/, false /*normalized*/, patch_proportions (param.type, &rparam2), !(fparams.flags & finetune_no_progress_report) ? progress : NULL))
	{
	  if (verbose)
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Precomputing failed. Tile: %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
	      progress->resume_stdout ();
	    }
	  ret.err = "precompting failed";
	  return ret;
	}
      if (progress && progress->cancel_requested ()) 
	{
	  ret.err = "cancelled";
	  return ret;
	}

      if (maxtiles * maxtiles > 1 && !(fparams.flags & finetune_no_progress_report))
	progress->set_task ("finetuning samples", maxtiles * maxtiles);



#pragma omp parallel for default(none) collapse (2) schedule(dynamic) shared(fparams,maxtiles,rparam,pixel_size,best_uncertainity,verbose,std::nothrow,imgp,twidth,theight,txmin,tymin,bw,progress,mapp,render,failed,best_solver,results) if (maxtiles > 1 && !(fparams.flags & finetune_no_progress_report))
	for (int ty = 0; ty < maxtiles; ty++)
	  for (int tx = 0; tx < maxtiles; tx++)
	    {
	      int cur_txmin = std::min (std::max (txmin - twidth * (maxtiles / 2) + tx * twidth, 0), imgp[0]->width - twidth - 1) & ~1;
	      int cur_tymin = std::min (std::max (tymin - theight * (maxtiles / 2) + ty * theight, 0), imgp[0]->height - theight - 1) & ~1;
	      //int cur_txmax = cur_txmin + twidth;
	      //int cur_tymax = cur_tymin + theight;
	      finetune_solver solver;
	      solver.n_tiles = 1;
	      solver.twidth = twidth;
	      solver.theight = theight;
	      solver.pixel_size = pixel_size;
	      solver.collection_threshold = rparam.collection_threshold;
	      if (!solver.init_tile (0, cur_txmin, cur_tymin, bw, *mapp[0], render))
		{
		  failed = true;
		  continue;
		}
	      solver.init (fparams.flags, rparam.screen_blur_radius, results);
	      if (progress && progress->cancel_requested ()) 
		continue;
	      coord_t uncertainity = solver.solve (progress, !(fparams.flags & finetune_no_progress_report) && maxtiles == 1);

	      if (maxtiles * maxtiles > 1 && !(fparams.flags & finetune_no_progress_report) && progress)
		progress->inc_progress ();
#pragma omp critical
	      {
		if (best_uncertainity < 0 || best_uncertainity > uncertainity)
		  {
		    best_solver = solver;
		    solver.start = NULL;
		    for (int i = 0; i < solver.n_tiles; i++)
		      solver.tiles[i].forget ();
		    best_uncertainity = uncertainity;
		  }
	      }
	    }
      }
  else
    {
      best_solver.n_tiles = n_tiles;
      best_solver.twidth = twidth;
      best_solver.theight = theight;
      best_solver.collection_threshold = rparam.collection_threshold;
      best_solver.pixel_size = pixel_size;
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
	  int cur_txmin = std::min (std::max (x[tileid] - twidth / 2, 0), imgp[tileid]->width - twidth - 1) & ~1;
	  int cur_tymin = std::min (std::max (y[tileid] - theight / 2, 0), imgp[tileid]->height - theight - 1) & ~1;
	  /* FIXME: We only use render_to_scr since we eventually want to know pixel size.
	     For stitched projects this is wrong.  */
	  render render (*imgp[tileid], rparam2, 256);
	  if (!render.precompute_all (bw /*grayscale*/, false /*normalized*/, patch_proportions (param.type, &rparam2), !(fparams.flags & finetune_no_progress_report) ? progress : NULL))
	    {
	      ret.err = "precomputing failed";
	      return ret;
	    }
	  if (progress && progress->cancel_requested ()) 
	    {
	      ret.err = "cancelled";
	      return ret; 
	    }
	  if (cur_txmin < 0 || cur_tymin < 0)
	    {
	      ret.err = "tile too large for image";
	      return ret;
	    }
	  if (!best_solver.init_tile (tileid, cur_txmin, cur_tymin, bw, *mapp[tileid], render))
	    {
	      ret.err = "out of memory";
	      return ret;
	    }
	}
      best_solver.init (fparams.flags, rparam.screen_blur_radius, results);
      best_uncertainity = best_solver.solve (progress, !(fparams.flags & finetune_no_progress_report));
    }
  if (progress && progress->cancel_requested ()) 
    {
      ret.err = "cancelled";
      return ret; 
    }
  if (failed)
    {
      ret.err = "failed memory allocaion";
      return ret;
    }
  if (best_uncertainity < 0)
    {
      ret.err = "negative uncertaininty";
      return ret;
    }

  if (best_solver.least_squares)
    {
      best_solver.alloc_least_squares ();
      if (!best_solver.optimize_fog || best_solver.fog_by_least_squares)
        best_solver.init_least_squares (best_solver.start);
    }
  if (best_solver.tiles[0].color && fparams.ignore_outliers > 0)
    best_solver.determine_outliers (best_solver.start, fparams.ignore_outliers);
  else if (fparams.ignore_outliers > 0)
    best_solver.bw_determine_outliers (best_solver.start, fparams.ignore_outliers);
  if (best_solver.has_outliers ())
    simplex<coord_t, finetune_solver>(best_solver, "finetuning with outliers", progress, !(fparams.flags & finetune_no_progress_report));
  if (progress && progress->cancel_requested ()) 
    {
      ret.err = "cancelled";
      return ret; 
    }

  if (fparams.simulated_file)
    best_solver.write_file (best_solver.start, fparams.simulated_file, 0, 0);
  if (fparams.orig_file)
    best_solver.write_file (best_solver.start, fparams.orig_file, 0, 1);
  if (fparams.diff_file)
    best_solver.write_file (best_solver.start, fparams.diff_file, 0, 2);
  ret.uncertainity = best_uncertainity;
  if (verbose)
    {
      progress->pause_stdout ();
      best_solver.print_values (best_solver.start);
      progress->resume_stdout ();
    }
  ret.badness = best_solver.objfunc (best_solver.start);
  //if (best_solver.optimize_screen_blur)
    //ret.screen_blur_radius = best_solver.start[best_solver.screen_index];
  /* TODO: Translate back to stitched project coordinates.  */
  ret.tile_pos = {(coord_t)(best_solver.tiles[0].txmin + best_solver.twidth / 2),
		  (coord_t)(best_solver.tiles[0].tymin + best_solver.theight / 2)};
  ret.dufay_red_strip_width = best_solver.get_red_strip_width (best_solver.start);
  ret.dufay_green_strip_width = best_solver.get_green_strip_width (best_solver.start);
  ret.screen_blur_radius = best_solver.get_blur_radius (best_solver.start);
  ret.screen_channel_blur_radius = best_solver.get_channel_blur_radius (best_solver.start);
  ret.emulsion_blur_radius = best_solver.get_emulsion_blur_radius (best_solver.start);
  best_solver.get_mtf (ret.screen_mtf_blur, best_solver.start);
  ret.screen_coord_adjust = best_solver.get_offset (best_solver.start, 0);
  ret.emulsion_coord_adjust = best_solver.get_emulsion_offset (best_solver.start, 0);
  ret.fog = best_solver.get_fog (best_solver.start);
  if (best_solver.optimize_emulsion_intensities)
    {
      ret.mix_weights = best_solver.get_mix_weights (best_solver.start);
      ret.mix_dark = best_solver.get_fog (best_solver.start);
    }
  else
    {
     ret.mix_weights.red = rparam.mix_red;
     ret.mix_weights.green = rparam.mix_green;
     ret.mix_weights.blue = rparam.mix_blue;
     ret.mix_dark = rparam.mix_dark;
    }

  if (best_solver.optimize_position)
    {
      int tileid = 0;
      /* Construct solver point.  Try to get closest point to the center of analyzed tile.  */
      int fsx = nearest_int (best_solver.get_pos (best_solver.start, tileid, twidth/2, theight/2).x);
      int fsy = nearest_int (best_solver.get_pos (best_solver.start, tileid, twidth/2, theight/2).y);
      int bx = - 1, by = -1;
      coord_t bdist = 0;
      for (int y = 0; y < theight; y++)
	{
	  for (int x = 0; x < twidth; x++)
	    {
	      point_t p = best_solver.get_pos (best_solver.start, tileid, x, y);
	      //printf ("  %-5.2f,%-5.2f", p.x, p.y);
	      coord_t dist = fabs (p.x - fsx) + fabs (p.y - fsy);
	      if (bx < 0 || dist < bdist)
		{
		  bx = x;
		  by = y;
		  bdist = dist;
		}
	    }
	  //printf ("\n");
	}
      if (!bx || bx == twidth - 1 || !by || by == theight - 1)
	{
	  if (verbose)
	    {
	      progress->pause_stdout ();
	      printf ("Solver point is out of tile\n");
	      progress->resume_stdout ();
	    }
	  ret.err = "Solver point is out of tile";
	  return ret;
	}
      point_t fp = {-1000,-1000};

      bool found = false;
      //printf ("%i %i %i %i %f %f\n", bx, by, fsx, fsy, best_solver.tile_pos[twidth/2+(theight/2)*twidth].x, best_solver.tile_pos[twidth/2+(theight/2)*twidth].y);
      for (int y = by - 1; y <= by + 1; y++)
	for (int x = bx - 1; x <= bx + 1; x++)
	  {
	    /* Determine cell corners.  */
	    point_t p = {(coord_t)fsx, (coord_t)fsy};
	    point_t p1 = best_solver.get_pos (best_solver.start, tileid, x, y);
	    point_t p2 = best_solver.get_pos (best_solver.start, tileid, x + 1, y);
	    point_t p3 = best_solver.get_pos (best_solver.start, tileid, x, y + 1);
	    point_t p4 = best_solver.get_pos (best_solver.start, tileid, x + 1, y + 1);
	    /* Check if point is above or bellow diagonal.  */
	    coord_t sgn1 = sign (p, p1, p4);
	    if (sgn1 > 0)
	      {
		/* Check if point is inside of the triangle.  */
		if (sign (p, p4, p3) < 0 || sign (p, p3, p1) < 0)
		  continue;
		coord_t rx, ry;
		intersect_vectors (p1.x, p1.y,
				   p.x - p1.x, p.y - p1.y,
				   p3.x, p3.y,
				   p4.x - p3.x, p4.y - p3.y,
				   &rx, &ry);
		rx = 1 / rx;
		found = true;
		fp = {(ry * rx + x), (rx + y)};
	      }
	    else
	      {
		/* Check if point is inside of the triangle.  */
		if (sign (p, p4, p2) > 0 || sign (p, p2, p1) > 0)
		  continue;
		coord_t rx, ry;
		intersect_vectors (p1.x, p1.y,
				   p.x - p1.x, p.y - p1.y,
				   p2.x, p2.y,
				   p4.x - p2.x, p4.y - p2.y,
				   &rx, &ry);
		rx = 1 / rx;
		found = true;
		fp = {(rx + x), (ry * rx + y)};
	      }
	  }
      /* TODO: If we did not find the tile we could try some non-integer location.  */
      if (!found)
	{
	  if (verbose)
	    {
	      progress->pause_stdout ();
	      printf ("Failed to find solver point\n");
	      progress->resume_stdout ();
	    }
	  ret.err = "Failed to find solver point";
	  return ret;
	}
      ret.solver_point_img_location = {fp.x + best_solver.tiles[tileid].txmin + 0.5, fp.y + best_solver.tiles[tileid].tymin + 0.5};
      ret.solver_point_screen_location = {(coord_t)fsx, (coord_t)fsy};
      ret.solver_point_color = solver_parameters::green;
    }
  if (fparams.screen_file)
   best_solver.original_scr->save_tiff (fparams.screen_file);
  if (fparams.screen_blur_file)
    best_solver.tiles[0].scr->save_tiff (fparams.screen_blur_file);
  if (best_solver.emulsion_scr && fparams.emulsion_file)
    best_solver.emulsion_scr->save_tiff (fparams.emulsion_file);
  if (best_solver.tiles[0].merged_scr && fparams.merged_file)
    best_solver.tiles[0].merged_scr->save_tiff (fparams.merged_file);
  if (fparams.collected_file)
    {
      screen tmp;
      best_solver.collect_screen (&tmp, best_solver.start, 0);
      tmp.save_tiff (fparams.collected_file);
    }
  if (fparams.dot_spread_file)
    {
      screen scr, scr1;
      scr1.initialize_dot ();
      best_solver.apply_blur (best_solver.start, 0, &scr, &scr1);
      scr.save_tiff (fparams.dot_spread_file, true, 1);
    }
  ret.success = true;
  //printf ("%i %i %i %i %f %f %f %f\n", bx, by, fsx, fsy, best_solver.tile_pos[twidth/2+(theight/2)*twidth].x, best_solver.tile_pos[twidth/2+(theight/2)*twidth].y, fp.x, fp.y);
  return ret;
}

bool
finetune_area (solver_parameters *solver, render_parameters &rparam, const scr_to_img_parameters &param, const image_data &img, int xmin, int ymin, int xmax, int ymax, progress_info *progress)
{
  if (xmin < 0)
    xmin = 0;
  if (xmin > img.width - 1)
    return false;
  if (ymin < 0)
    ymin = 0;
  if (ymin > img.height - 1)
    return false;
  if (xmax > img.width - 1)
    xmax = img.width - 1;
  if (ymax > img.height - 1)
    ymax = img.height - 1;
  if (xmax < xmin || ymax < ymin)
    return false;
  const int steps = 100;
  int overall_xsteps = steps;
  int overall_ysteps = steps;
  if (param.scanner_type == lens_move_horisontally || param.scanner_type == fixed_lens_sensor_move_horisontally)
    overall_xsteps *= 3, overall_ysteps /= 3;
  else if (param.scanner_type == lens_move_vertically || param.scanner_type == fixed_lens_sensor_move_vertically)
    overall_ysteps *= 3, overall_xsteps /= 3;
  int xstep = img.width / overall_xsteps;
  int ystep = img.width / overall_ysteps;
  int xsteps = (xmax - xmin + xstep) / xstep;
  int ysteps = (ymax - ymin + ystep) / ystep;
  std::vector <finetune_result> res(xsteps * ysteps);
  if (progress)
    progress->set_task ("finetuning grid", ysteps * xsteps);
#pragma omp parallel for default (none) collapse(2) schedule(dynamic) shared (xsteps, ysteps, rparam, param,progress,img, solver, res, xmin, ymin, xstep, ystep)
  for (int x = 0; x < xsteps; x++)
    for (int y = 0; y < ysteps; y++)
      {
	if (progress && progress->cancel_requested ()) 
	   continue; 
	finetune_parameters fparam;
	fparam.flags |= finetune_position /*| finetune_multitile*/ | finetune_bw | finetune_no_progress_report;
	res[x+y*xsteps] = finetune (rparam, param, img, {{xmin + (x + 0.5) * xstep, ymin + (y + 0.5) * ystep}}, NULL, fparam, progress);
	progress->inc_progress ();
      }
  if (progress && progress->cancel_requested ()) 
    return false; 
  std::sort (res.begin(), res.end(), [] (finetune_result &a, finetune_result &b) { return a.uncertainity > b.uncertainity;});
  coord_t max_uncertainity = res[(xsteps * ysteps) * 0.2].uncertainity;
  for (int x = 0; x < xsteps; x++)
    for (int y = 0; y < ysteps; y++)
      {
	finetune_result &r = res[x+y*xsteps];
	if (r.success && r.uncertainity <= max_uncertainity)
	  solver->add_point (r.solver_point_img_location.x, r.solver_point_img_location.y, r.solver_point_screen_location.x, r.solver_point_screen_location.y, r.solver_point_color);
      }
  return true;
}

/* Simulate data collection of scan of given color screen (assumed to be blurred) and
   return colected red, green and blue.  This can be used to increase color saturation
   to compensate loss caused by the collection.  */

bool
determine_color_loss (rgbdata *ret_red, rgbdata *ret_green, rgbdata *ret_blue, screen &scr, luminosity_t threshold, scr_to_img &map, int xmin, int ymin, int xmax, int ymax)
{
  rgbdata red = {0,0,0}, green = {0,0,0}, blue = {0,0,0};
  coord_t wr = 0, wg = 0, wb = 0;
#pragma omp declare reduction (+:rgbdata:omp_out=omp_out+omp_in)
#pragma omp parallel for default(none) collapse (2) shared (ymin,ymax,xmin,xmax,threshold,map,scr) reduction(+:wr,wg,wb,red,green,blue)
  for (int y = ymin; y <= ymax; y++)
    for (int x = xmin; x <= xmax; x++)
      {
#if 0
	point_t p;
        map.to_scr (x + 0.5, y + 0.5, &p.x, &p.y);
	rgbdata m = scr.interpolated_mult (p);
	rgbdata am = m;
#else
	point_t p;
        map.to_scr (x + 0.5, y + 0.5, &p.x, &p.y);
	point_t px;
        map.to_scr (x + 1.5, y + 0.5, &px.x, &px.y);
	point_t py;
        map.to_scr (x + 0.5, y + 1.5, &py.x, &py.y);
	rgbdata am = {0, 0, 0};
	point_t pdx = (px - p) * (1.0 / 6.0);
	point_t pdy = (py - p) * (1.0 / 6.0);
	for (int yy = -2; yy <= 2; yy++)
	  for (int xx = -2; xx <= 2; xx++)
	    am+= scr.interpolated_mult (p + pdx * xx + pdy * yy);
	am *= ((coord_t)1.0 / 25);
	rgbdata m = scr.noninterpolated_mult (p);
#endif
	if (m.red > threshold)
	  {
	    coord_t val = m.red - threshold;
	    wr += val;
	    red += am * val;
	  }
	if (m.green > threshold)
	  {
	    coord_t val = m.green - threshold;
	    wg += val;
	    green += am * val;
	  }
	if (m.blue > threshold)
	  {
	    coord_t val = m.blue - threshold;
	    wb += val;
	    blue += am * val;
	  }
      }
  if (!(wr >0 && wg > 0 && wb > 0))
    return false;
  red /= wr;
  green /= wg;
  blue /= wb;
  *ret_red = (rgbdata){red.red, green.red, blue.red};
  *ret_green = (rgbdata){red.green, green.green, blue.green};
  *ret_blue = (rgbdata){red.blue, green.blue, blue.blue};
#if 0
  *ret_red = red / wr;
  *ret_green = green / wg;
  *ret_blue = blue / wb;
#endif
#if 0
  printf ("Color loss info\n");
  ret_red->print (stdout);
  ret_green->print (stdout);
  ret_blue->print (stdout);
#endif
  return true;
}
