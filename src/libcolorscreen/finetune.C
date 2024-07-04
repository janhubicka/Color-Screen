#include <memory>
#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <gsl/gsl_multifit.h>
#include "include/finetune.h"
#include "include/histogram.h"
#include "include/stitch.h"
#include "render-interpolate.h"
#include "dufaycolor.h"
#include "include/tiff-writer.h"
#include "nmsimplex.h"
#include "include/bitmap.h"
#include "icc.h"

namespace {

/* Solver used to find parameters of simulated scan (position of the grid,
   color of individual patches, lens blur ...) to match given scan tile
   (described tile and tile_pos) as well as possible.
   It is possible to match eitehr in BW or RGB and choose set of parameters
   to optimize for.  */
struct finetune_solver
{
private:
  gsl_multifit_linear_workspace *gsl_work;
  gsl_matrix *gsl_X;
  gsl_vector *gsl_y[3];
  gsl_vector *gsl_c;
  gsl_matrix *gsl_cov;
  int noutliers;
  std::shared_ptr <bitmap_2d> outliers;

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
  rgbdata last_emulsion_intensities;
  point_t last_emulsion_offset;
  luminosity_t last_mtf[4];
  luminosity_t last_emulsion_blur;
  coord_t last_width, last_height;
public:
  /* Unblured screen.  */
  std::shared_ptr <screen> original_scr;
  /* Screen with emulsion.  */
  std::shared_ptr <screen> emulsion_scr;
  /* Screen merging emulsion and unblurred screen.  */
  std::shared_ptr <screen> merged_scr;
  /* Blured screen used to render simulated scan.  */
  screen scr;

  finetune_solver ()
    : gsl_work (NULL), gsl_X (NULL), gsl_y {NULL, NULL, NULL}, gsl_c (NULL), gsl_cov (NULL), noutliers (0), outliers ()
  {
  }
  /* Tile position and dimensions */
  int txmin, tymin;
  int twidth, theight;
  /* Tile colors */
  std::shared_ptr <rgbdata []> tile;
  /* Black and white tile.  */
  std::shared_ptr <luminosity_t []> bwtile;
  /* Tile position  */
  std::shared_ptr <point_t []> tile_pos;
  int simulated_screen_border;
  int simulated_screen_width;
  int simulated_screen_height;
  /* Tile colors */
  std::shared_ptr <rgbdata []> simulated_screen;

  /* 2 coordinates, 1*emulsion blur, 3*emulsion inensities, 3* blur radius or 4 * mtf blur, 3 * 3 colors, strip widths, fog  */
  coord_t start[25];

  /* Screen blur and duffay red strip widht and green strip height. */
  coord_t fixed_blur, fixed_width, fixed_height;

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
  }

  int num_values ()
  {
    return n_values;
  }
  constexpr static const coord_t rgbscale = /*256*/1;
  coord_t epsilon ()
  {
    return /*0.00000001*/ 1.0/65535;
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
    return twidth * theight - noutliers;
  }

  point_t
  get_offset (coord_t *v)
  {
    if (!optimize_position)
      return {0, 0};
    coord_t range = type == Dufay ? dufay_range : paget_range;
    return {v[0] * range, v[1] * range};
  }
  point_t
  get_emulsion_offset (coord_t *v)
  {
    if (!optimize_emulsion_offset)
      return {0, 0};
    coord_t range = type == Dufay ? dufay_range : paget_range;
    return {v[emulsion_offset_index] * range, v[emulsion_offset_index + 1] * range};
  }

  point_t
  get_pos (coord_t *v, int x, int y)
  {
    return tile_pos [y * twidth + x] + get_offset (v);
  }

  bool has_outliers ()
  {
    return noutliers;
  }

  coord_t get_emulsion_blur_radius (coord_t *v)
  {
    if (!optimize_emulsion_blur)
      return 0;
    return v[emulsion_blur_index] * (screen::max_blur_radius * 0.2 - 0.03) + 0.03;
  }

  /* Do pixel blurs in the range 0.3 ... screen::max_blur_radius / pixel_size.  */
  coord_t pixel_blur (coord_t v)
  {
    return v * (screen::max_blur_radius / pixel_size - 0.3) + 0.3;
  }
  coord_t rev_pixel_blur (coord_t v)
  {
    return (v - 0.3) / (screen::max_blur_radius / pixel_size - 0.3);
  }

  coord_t get_blur_radius (coord_t *v)
  {
    if (optimize_screen_channel_blurs)
      return pixel_blur ((v[screen_index] + v[screen_index + 1] + v[screen_index + 2]) * (1 / (coord_t) 3));
    if (!optimize_screen_blur)
      return fixed_blur;
    return pixel_blur (v[screen_index]);
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
    if (optimize_position)
      {
	point_t p = get_offset (v);
        printf ("Screen offset %f %f (in pixels %f %f)\n", p.x, p.y, p.x/pixel_size, p.y/pixel_size);
      }
    if (optimize_emulsion_offset)
      {
        point_t p = get_emulsion_offset (v);
        printf ("Emulsion offset %f %f (%f %f in pixels; relative to screen)\n", p.x, p.y, p.x / pixel_size, p.y / pixel_size);
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
	if (optimize_fog)
	  {
	    printf ("Fog ");
	    get_fog (v).print (stdout);
	  }
	if (optimize_emulsion_intensities)
	  {
	    printf ("Emulsion intensities ");
	    get_emulsion_intensities (v).print (stdout);
	    printf ("Mix weights ");
	    get_mix_weights (v).print (stdout);
	  }
      }
    if (bwtile)
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
      {
	v[0] = std::min (v[0], (coord_t)1);
	v[0] = std::max (v[0], (coord_t)-1);
	v[1] = std::min (v[1], (coord_t)1);
	v[1] = std::max (v[1], (coord_t)-1);
      }
    if (optimize_fog && !fog_by_least_squares)
      {
	v[fog_index] = std::min (v[fog_index], (coord_t)1);
	v[fog_index] = std::max (v[fog_index], (coord_t)0);
	v[fog_index+1] = std::min (v[fog_index+1], (coord_t)1);
	v[fog_index+1] = std::max (v[fog_index+1], (coord_t)0);
	v[fog_index+2] = std::min (v[fog_index+2], (coord_t)1);
	v[fog_index+2] = std::max (v[fog_index+2], (coord_t)0);
      }
    if (bwtile && !least_squares && !data_collection)
      {
	v[color_index] = std::min (v[color_index], (coord_t)2);
	v[color_index] = std::max (v[color_index], (coord_t)0);
	v[color_index + 1] = std::min (v[color_index + 1], (coord_t)2);
	v[color_index + 1] = std::max (v[color_index + 1], (coord_t)0);
	v[color_index + 2] = std::min (v[color_index + 2], (coord_t)2);
	v[color_index + 2] = std::max (v[color_index + 2], (coord_t)0);
      }
    if (optimize_emulsion_blur)
      {
	/* Screen blur radius.  */
	v[emulsion_blur_index] = std::max (v[emulsion_blur_index], (coord_t)0);
	v[emulsion_blur_index] = std::min (v[emulsion_blur_index], (coord_t)1);
      }
    if (optimize_emulsion_intensities)
      {
	v[emulsion_intensity_index] = std::max (v[emulsion_intensity_index], (coord_t)0);
	v[emulsion_intensity_index] = std::min (v[emulsion_intensity_index], (coord_t)1);
	v[emulsion_intensity_index + 1] = std::max (v[emulsion_intensity_index + 1], (coord_t)0);
	v[emulsion_intensity_index + 1] = std::min (v[emulsion_intensity_index + 1], (coord_t)1);
	//v[emulsion_intensity_index + 2] = std::max (v[emulsion_intensity_index + 2], (coord_t)0);
	//v[emulsion_intensity_index + 2] = std::min (v[emulsion_intensity_index + 2], (coord_t)1);
      }
    if (optimize_emulsion_offset)
      {
	v[emulsion_offset_index] = std::max (v[emulsion_offset_index], (coord_t)-1);
	v[emulsion_offset_index] = std::min (v[emulsion_offset_index], (coord_t)1);
	v[emulsion_offset_index + 1] = std::max (v[emulsion_offset_index + 1], (coord_t)-1);
	v[emulsion_offset_index + 1] = std::min (v[emulsion_offset_index + 1], (coord_t)1);
      }

    if (optimize_screen_blur)
      {
	/* Screen blur radius.  */
	v[screen_index] = std::max (v[screen_index], (coord_t)0);
	v[screen_index] = std::min (v[screen_index], (coord_t)1);
      }
    if (optimize_screen_channel_blurs)
      {
	/* Screen blur radius.  */
	v[screen_index] = std::max (v[screen_index], (coord_t)0);
	v[screen_index] = std::min (v[screen_index], (coord_t)1);
	v[screen_index + 1] = std::max (v[screen_index + 1], (coord_t)0);
	v[screen_index + 1] = std::min (v[screen_index + 1], (coord_t)1);
	v[screen_index + 2] = std::max (v[screen_index + 2], (coord_t)0);
	v[screen_index + 2] = std::min (v[screen_index + 2], (coord_t)1);
      }
    if (optimize_screen_mtf_blur)
      {
	v[screen_index] = std::max (v[screen_index], (coord_t)0);
	v[screen_index] = std::min (v[screen_index], (coord_t)1);
	v[screen_index + 1] = std::max (v[screen_index + 1], (coord_t)0);
	v[screen_index + 1] = std::min (v[screen_index + 1], (coord_t)1);
	v[screen_index + 2] = std::max (v[screen_index + 2], (coord_t)0);
	v[screen_index + 2] = std::min (v[screen_index + 2], (coord_t)1);
	v[screen_index + 3] = std::max (v[screen_index + 3], (coord_t)0);
	v[screen_index + 3] = std::min (v[screen_index + 3], (coord_t)1);
      }
    if (optimize_dufay_strips)
      {
	/* Dufaycolor red strip width and height.  */
	v[dufay_strips_index + 0] = std::min (v[dufay_strips_index + 0], (coord_t)0.5);
	v[dufay_strips_index + 0] = std::max (v[dufay_strips_index + 0], (coord_t)0.1);
	v[dufay_strips_index + 1] = std::min (v[dufay_strips_index + 1], (coord_t)0.7);
	v[dufay_strips_index + 1] = std::max (v[dufay_strips_index + 1], (coord_t)0.3);
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
	if (tile)
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
    int matrixw = tile ? (fog_by_least_squares ? 4 : 3) : 1;
    int matrixh = sample_points ();
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
    last_fog = {0,0,0};
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

    if (tile && optimize_emulsion_blur && (optimize_screen_blur || optimize_screen_channel_blurs || optimize_screen_mtf_blur))
      {
        optimize_emulsion_intensities = true;
	optimize_emulsion_offset = type != Dufay;
	data_collection = false;
      }
    else
      optimize_emulsion_intensities = optimize_emulsion_offset = false;

    if (data_collection)
      least_squares = false;

    if (!least_squares && !data_collection)
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
    if (!tile)
      optimize_fog = false;
    fog_by_least_squares = (optimize_fog && !normalize && least_squares);
    if (optimize_fog && !fog_by_least_squares)
      n_values += 3;

    emulsion_intensity_index = n_values;
    if (optimize_emulsion_intensities)
      n_values += 2;
    emulsion_offset_index = n_values;
    if (optimize_emulsion_offset)
      n_values += 2;
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
    if ((unsigned)n_values > sizeof (start) / sizeof (start[0]))
      abort ();

    original_scr = std::shared_ptr <screen> (new screen);
    if (optimize_emulsion_blur)
      emulsion_scr = std::shared_ptr <screen> (new screen);
    if (optimize_emulsion_intensities)
      merged_scr = std::shared_ptr <screen> (new screen);

    last_blur = {-1, -1,-1};
    last_emulsion_intensities = {-1, -1, -1};
    last_emulsion_offset = {-100,-100};
    last_fog = {0,0,0};
    for (int i = 0; i < 4; i++)
      last_mtf[i] = -1;
    last_emulsion_blur = -1;
    last_width = -1;
    last_height = -1;
    if (optimize_position)
      {
	start[0] = 0;
	start[1] = 0;
      }

    if (!least_squares && !data_collection)
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
    if (tile && normalize)
      optimize_emulsion_blur = false;
    if (optimize_emulsion_intensities)
      {
	start[emulsion_intensity_index] = 1 / 3.0;
	start[emulsion_intensity_index + 1] = 1 / 3.0;
	//start[emulsion_intensity_index + 2] = 0;
      }
    if (optimize_emulsion_offset)
      {
	start[emulsion_offset_index] = 0;
	start[emulsion_offset_index + 1] = 0;
      }
    /* Sane scanner lens blurs close to Nqyist frequency.  */
    if (optimize_emulsion_blur)
      start[emulsion_blur_index] = rev_pixel_blur (0.3);
    if (optimize_screen_blur)
      start[screen_index] = rev_pixel_blur (0.3);
    if (optimize_screen_channel_blurs)
      start[screen_index] = start[screen_index + 1] = start[screen_index + 2]= rev_pixel_blur (0.3);
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
	init_least_squares (NULL);
      }
    simulated_screen_border = 0;
    simulated_screen_width = twidth;
    simulated_screen_height = theight;
    simulated_screen = (std::unique_ptr <rgbdata[]>)(new  (std::nothrow) rgbdata [simulated_screen_width * simulated_screen_height]);
  }

  rgbdata
  get_simulated_screen_pixel (int x, int y)
  {
    return simulated_screen [y * simulated_screen_width + x];
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
  apply_blur (coord_t *v, screen *dst_scr, screen *src_scr, screen *weight_scr = NULL)
  {
    rgbdata blur = get_channel_blur_radius (v);
    luminosity_t mtf[4];
    get_mtf (mtf, v);

    if (weight_scr)
      {
	rgbdata i = get_emulsion_intensities (v);
	point_t offset = get_emulsion_offset (v);
	if (offset.x == 0 && offset.y == 0)
	  for (int y = 0; y < screen::size; y++)
	    for (int x = 0; x < screen::size; x++)
	      {
		luminosity_t w = weight_scr->mult[y][x][0] * i.red + weight_scr->mult[y][x][1] * i.green + weight_scr->mult[y][x][2] * i.blue;
		merged_scr->mult[y][x][0] = src_scr->mult[y][x][0] * w;
		merged_scr->mult[y][x][1] = src_scr->mult[y][x][1] * w;
		merged_scr->mult[y][x][2] = src_scr->mult[y][x][2] * w;
	      }
	else
	  for (int y = 0; y < screen::size; y++)
	    for (int x = 0; x < screen::size; x++)
	      {
		rgbdata wd = weight_scr->interpolated_mult
		  ((point_t){x * (1 / (coord_t)screen::size), y * (1 / (coord_t)screen::size)} + offset);
		luminosity_t w = wd.red * i.red + wd.green * i.green + wd.blue * i.blue;
		merged_scr->mult[y][x][0] = src_scr->mult[y][x][0] * w;
		merged_scr->mult[y][x][1] = src_scr->mult[y][x][1] * w;
		merged_scr->mult[y][x][2] = src_scr->mult[y][x][2] * w;
	      }
	src_scr = merged_scr.get ();
      }

    if (optimize_screen_mtf_blur)
      dst_scr->initialize_with_blur (*src_scr, mtf);
    else
      dst_scr->initialize_with_blur (*src_scr, blur * pixel_size);
  }

  void
  init_screen (coord_t *v)
  {
    luminosity_t emulsion_blur = get_emulsion_blur_radius (v);
    rgbdata blur = get_channel_blur_radius (v);
    luminosity_t red_strip_width = get_red_strip_width (v);
    luminosity_t green_strip_height = get_green_strip_width (v);
    luminosity_t mtf[4];
    rgbdata intensities = get_emulsion_intensities (v);
    point_t emulsion_offset = get_emulsion_offset (v);
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
    
    if (blur != last_blur || updated || optimize_screen_mtf_blur || mtf_differs (mtf, last_mtf) || last_emulsion_intensities != intensities || last_emulsion_offset != emulsion_offset)
      {
	apply_blur (v, &scr,
		    optimize_emulsion_blur && !optimize_emulsion_intensities? emulsion_scr.get () : original_scr.get (),
		    optimize_emulsion_intensities ? emulsion_scr.get () : NULL);
	last_blur = blur;
	last_emulsion_intensities = intensities;
	last_emulsion_offset = emulsion_offset;
	memcpy (last_mtf, mtf, sizeof (last_mtf));
      }
  }

  /* Determine color of screen at pixel X,Y with offset OFF.  */
  pure_attr inline rgbdata
  evaulate_screen_pixel (int x, int y, point_t off)
  {
    point_t p = tile_pos [y * twidth + x] + off;
    if (0)
      {
	/* Interpolation here is necessary to ensure smoothness.  */
	return scr.interpolated_mult (p);
      }
    int dx = x == twidth - 1 ? -1 : 1;
    point_t px = tile_pos [y * twidth + (x + dx)] + off;
    int dy = y == theight - 1 ? -1 : 1;
    point_t py = tile_pos [(y + dy) * twidth + x] + off;
    point_t pdx = (px - p) * (1.0 / 6.0) * dx;
    point_t pdy = (py - p) * (1.0 / 6.0) * dy;
    rgbdata m = {0,0,0};
    for (int yy = -2; yy <= 2; yy++)
      for (int xx = -2; xx <= 2; xx++)
	m+= scr.interpolated_mult (p + pdx * xx + pdy * yy);
    return m * ((coord_t)1.0 / 25);
  }

  /* Evaulate pixel at (x,y) using RGB values v and offsets offx, offy
     compensating coordates stored in tole_pos.  */
  pure_attr inline rgbdata
  evaulate_pixel (rgbdata red, rgbdata green, rgbdata blue, int x, int y, point_t off)
  {
    rgbdata m = get_simulated_screen_pixel (x, y);
    return ((red * m.red + green * m.green + blue * m.blue) * ((coord_t)1.0 / rgbscale));
  }

  void
  simulate_screen (coord_t *v)
  {
    rgbdata red, green, blue;
    point_t off = get_offset (v);
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	simulated_screen [y * simulated_screen_width + x] = evaulate_screen_pixel (x, y, off);
  }

  /* Evaulate pixel at (x,y) using RGB values v and offsets offx, offy
     compensating coordates stored in tole_pos.  */
  pure_attr inline luminosity_t
  bw_evaulate_pixel (rgbdata color, int x, int y, point_t off)
  {
    rgbdata m = get_simulated_screen_pixel (x, y);
    return ((m.red * color.red + m.green * color.green + m.blue * color.blue) /** (2 * maxgray)*/);
  }

  pure_attr rgbdata
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
  void
  determine_colors_using_data_collection (coord_t *v, rgbdata *ret_red, rgbdata *ret_green, rgbdata *ret_blue)
  {
    rgbdata red = {0,0,0}, green = {0,0,0}, blue = {0,0,0};
    rgbdata color_red = {0,0,0}, color_green = {0,0,0}, color_blue = {0,0,0};
    luminosity_t threshold = collection_threshold;
    coord_t wr = 0, wg = 0, wb = 0;

    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	if (!noutliers || !outliers->test_bit (x, y))
	  {
	    rgbdata m = get_simulated_screen_pixel (x, y);
	    rgbdata d = get_pixel (v, x, y);
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
  /* Colors should be real reactions of scanner, so no negative values and also no excessively large values. Allow some overexposure.  */
  if (color_red.red < 0)
    color_red.red = 0;
  if (color_red.green < 0)
    color_red.green = 0;
  if (color_red.blue < 0)
    color_red.blue = 0;
  if (color_red.red > 2)
    color_red.red = 2;
  if (color_red.green > 2)
    color_red.green = 2;
  if (color_red.blue > 2)
    color_red.blue = 2;
  if (color_green.red < 0)
    color_green.red = 0;
  if (color_green.green < 0)
    color_green.green = 0;
  if (color_green.blue < 0)
    color_green.blue = 0;
  if (color_green.red > 2)
    color_green.red = 2;
  if (color_green.green > 2)
    color_green.green = 2;
  if (color_green.blue > 2)
    color_green.blue = 2;
  if (color_blue.red < 0)
    color_blue.red = 0;
  if (color_blue.green < 0)
    color_blue.green = 0;
  if (color_blue.blue < 0)
    color_blue.blue = 0;
  if (color_blue.red > 2)
    color_blue.red = 2;
  if (color_blue.green > 2)
    color_blue.green = 2;
  if (color_blue.blue > 2)
    color_blue.blue = 2;

   *ret_red = color_red;
   *ret_green = color_green;
   *ret_blue = color_blue;
  }

  coord_t
  determine_colors_using_least_squares (coord_t *v, rgbdata *red, rgbdata *green, rgbdata *blue)
  {
    coord_t sqsum = 0;


    for (int ch = 0; ch < 3; ch++)
      {
	int e = 0;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    if (!noutliers || !outliers->test_bit (x, y))
	      {
		rgbdata c = get_simulated_screen_pixel (x, y);
		gsl_matrix_set (gsl_X, e, 0, c.red);
		gsl_matrix_set (gsl_X, e, 1, c.green);
		gsl_matrix_set (gsl_X, e, 2, c.blue);
		if (fog_by_least_squares)
		  gsl_matrix_set (gsl_X, e, 3, 1);
		e++;
	      }
	double chisq;
	gsl_multifit_linear (gsl_X, gsl_y[ch], gsl_c, gsl_cov,
			     &chisq, gsl_work);
	sqsum += chisq;
	(*red)[ch] = gsl_vector_get (gsl_c, 0);
	if (!((*red)[ch] > -5))
	  (*red)[ch] = -5;
	if (!((*red)[ch] < 5))
	  (*red)[ch] = 5;
	(*green)[ch] = gsl_vector_get (gsl_c, 1);
	if (!((*green)[ch] > -5))
	  (*green)[ch] = -5;
	if (!((*green)[ch] < 5))
	  (*green)[ch] = 5;
	(*blue)[ch] = gsl_vector_get (gsl_c, 2);
	if (!((*blue)[ch] > -5))
	  (*blue)[ch] = -5;
	if (!((*blue)[ch] < 5))
	  (*blue)[ch] = 5;
	if (fog_by_least_squares)
	  {
	    last_fog[ch] = gsl_vector_get (gsl_c, 3);
	    if (!(last_fog[ch] > 0))
	      last_fog[ch] = 0;
	    if (!(last_fog[ch] < fog_range[ch]))
	      last_fog[ch] = fog_range[ch];
	  }
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
  bw_determine_color_using_data_collection (coord_t *v)
  {
    rgbdata red = {0,0,0}, green = {0,0,0}, blue = {0,0,0};
    rgbdata color = {0,0,0};
    luminosity_t threshold = collection_threshold;
    coord_t wr = 0, wg = 0, wb = 0;

    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	if (!noutliers || !outliers->test_bit (x, y))
	  {
	    rgbdata m = get_simulated_screen_pixel (x, y);
	    luminosity_t l = bw_get_pixel (x, y);
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
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	if (!noutliers || !outliers->test_bit (x, y))
	  {
	    rgbdata c = get_simulated_screen_pixel (x, y);
	    gsl_matrix_set (gsl_X, e, 0, c.red);
	    gsl_matrix_set (gsl_X, e, 1, c.green);
	    gsl_matrix_set (gsl_X, e, 2, c.blue);
	    e++;
	    //gsl_vector_set (gsl_y[0], e, bw_get_pixel (x, y) / (2 * maxgray));
	  }
    double chisq;
    gsl_multifit_linear (gsl_X, gsl_y[0], gsl_c, gsl_cov, &chisq, gsl_work);
    rgbdata ret = {gsl_vector_get (gsl_c, 0) * (2 * maxgray), gsl_vector_get (gsl_c, 1) * (2 * maxgray), gsl_vector_get (gsl_c, 2) * (2 * maxgray)};
    return ret;
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
  get_emulsion_intensities (coord_t *v)
  {
    /* Together with screen colors these are defined only up to scaling factor.  */
    if (optimize_emulsion_intensities)
      {
	luminosity_t blue = 1 - v[emulsion_intensity_index + 0] - v[emulsion_intensity_index + 1];
	if (blue < 0)
	  blue = 0;
        return {v[emulsion_intensity_index + 0], v[emulsion_intensity_index + 1], blue};
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
    init_screen (v);
    coord_t sum = 0;
    point_t off = get_offset (v);
    simulate_screen (v);
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
    /* Avoid solver from increasing blur past point it is no longer useful.
       Otherwise it will pick solutions with too large blur and very contrasty
       colors.  */
   return (sum / sample_points ()) * (1 + get_blur_radius (v) * 0.01) /** (1 + get_emulsion_blur_radius (v) * 0.0001)*/;
  }

  void
  collect_screen (screen *s, coord_t *v)
  {
    point_t off = get_offset (v);
    for (int y = 0; y < screen::size; y++)
      for (int x = 0; x < screen::size; x++)
	for (int c = 0; c < 3; c++)
	  {
	    s->mult[y][x][c] = (luminosity_t)0;
	    s->add[y][x][c] = (luminosity_t)0;
	  }
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	if (!noutliers || !outliers->test_bit (x, y))
	  {
	    point_t p = tile_pos [y * twidth + x] + off;
	    int xx = ((int64_t)nearest_int (p.x * screen::size)) & (screen::size - 1);
	    int yy = ((int64_t)nearest_int (p.y * screen::size)) & (screen::size - 1);
	    s->mult[yy][xx][0] = tile [y * twidth + x].red;
	    s->mult[yy][xx][1] = tile [y * twidth + x].green;
	    s->mult[yy][xx][2] = tile [y * twidth + x].blue;
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
    histogram hist;
    point_t off = get_offset (v);
    rgbdata color = bw_get_color (v);
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  luminosity_t c = bw_evaulate_pixel (color, x, y, off);
	  luminosity_t d = bw_get_pixel (x, y);
	  coord_t err = fabs (c - d);
	  hist.pre_account (err);
	}
    hist.finalize_range (65535);
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  luminosity_t c = bw_evaulate_pixel (color, x, y, off);
	  luminosity_t d = bw_get_pixel (x, y);
	  coord_t err = fabs (c - d);
	  hist.account (err);
	}
    hist.finalize ();
    coord_t merr = hist.find_max (ratio) * 1.3;
    outliers = std::unique_ptr <bitmap_2d> (new bitmap_2d (twidth, theight));
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
	{
	  luminosity_t c = bw_evaulate_pixel (color, x, y, off);
	  luminosity_t d = bw_get_pixel (x, y);
	  coord_t err = fabs (c - d);
	  if (err > merr)
	    {
	      noutliers++;
	      outliers->set_bit (x, y);
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
  write_file (coord_t *v, const char *name, int type)
  {
    init_screen (v);
    point_t off = get_offset (v);
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
		  case 2:
		    {
		      rgbdata c = evaulate_pixel (red, green, blue, x, y, off);
		      rgbdata d = get_pixel (v, x, y);
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
		  case 2:
		    {
		      luminosity_t c = bw_evaulate_pixel (color, x, y, off);
		      luminosity_t d = bw_get_pixel (x, y);
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
finetune (render_parameters &rparam, const scr_to_img_parameters &param, const image_data &img, int x, int y, const finetune_parameters &fparams, progress_info *progress)
{
  finetune_result ret = {false, -1, -1, -1, {-1, -1, -1}, {-1, -1, -1, -1}, -1, -1, -1, {-1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};
  const image_data *imgp = &img;
  scr_to_img map;
  scr_to_img *mapp;
  if (img.stitch)
    {
      coord_t sx, sy;
      int tx, ty;
      img.stitch->common_scr_to_img.final_to_scr (x + img.xmin, y + img.ymin, &sx, &sy);
      if (!img.stitch->tile_for_scr (&rparam, sx, sy, &tx, &ty, true))
        {
	  ret.err = "no tile for given coordinates";
	  return ret;
        }
      img.stitch->images[ty][tx].common_scr_to_img (sx, sy, &sx, &sy);
      x = nearest_int (sx);
      y = nearest_int (sy);
      imgp = img.stitch->images[ty][tx].img;
      mapp = &img.stitch->images[ty][tx].scr_to_img_map;
    }
  else
    {
      map.set_parameters (param, *imgp);
      mapp = &map;
    }
  bool bw = fparams.flags & finetune_bw;
  bool verbose = fparams.flags & finetune_verbose;

  if (!bw && !imgp->rgbdata)
    bw = true;

  /* Determine tile to analyze.  */
  coord_t tx, ty;
  mapp->to_scr (x, y, &tx, &ty);
  int sx = nearest_int (tx);
  int sy = nearest_int (ty);

  coord_t test_range = fparams.range ? fparams.range : ((fparams.flags & finetune_no_normalize) || bw ? 1 : 2);
  mapp->to_img (sx, sy, &tx, &ty);
  mapp->to_img (sx - test_range, sy - test_range, &tx, &ty);
  coord_t sxmin = tx, sxmax = tx, symin = ty, symax = ty;
  mapp->to_img (sx + test_range, sy - test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);
  mapp->to_img (sx + test_range, sy + test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);
  mapp->to_img (sx - test_range, sy + test_range, &tx, &ty);
  sxmin = std::min (sxmin, tx);
  sxmax = std::max (sxmax, tx);
  symin = std::min (symin, ty);
  symax = std::max (symax, ty);

  int txmin = floor (sxmin), tymin = floor (symin), txmax = ceil (sxmax), tymax = ceil (symax);
  if (txmin < 0)
    txmin = 0;
  if (txmax > imgp->width)
    txmax = imgp->width;
  if (tymin < 0)
    tymin = 0;
  if (tymax > imgp->height)
    tymax = imgp->height;
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
      fprintf (stderr, "Will analyze tile %i-%i %i-%i\n", txmin, txmax, tymin, tymax);
      progress->resume_stdout ();
    }
  finetune_solver best_solver;
  coord_t best_uncertainity = -1;
  bool failed = false;
  {
    ///* FIXME: Hack; render is too large for stack in openmp thread.  */
    //std::unique_ptr<render_to_scr> rp(new render_to_scr (param, img, rparam, 256));
    render_parameters rparam2 = rparam;
    rparam2.invert = 0;
    render_to_scr render (param, *imgp, rparam2, 256);
    int rxmin = txmin, rxmax = txmax, rymin = tymin, rymax = tymax;
    int maxtiles = fparams.multitile;
    if (maxtiles < 1)
      maxtiles = 1;
    if (!(maxtiles & 1))
      maxtiles++;
    if (maxtiles > 1)
      {
	rxmin = std::max (txmin - twidth * (maxtiles / 2), 0);
	rymin = std::max (tymin - theight * (maxtiles / 2), 0);
	rxmax = std::max (txmax + (twidth * maxtiles / 2), imgp->width - 1);
	rymax = std::max (tymax + (theight * maxtiles / 2), imgp->height - 1);
      }
    if (!render.precompute_img_range (bw /*grayscale*/, false /*normalized*/, rxmin, rymin, rxmax + 1, rymax + 1, !(fparams.flags & finetune_no_progress_report) ? progress : NULL))
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


#pragma omp parallel for default(none) collapse (2) schedule(dynamic) shared(fparams,maxtiles,rparam,best_uncertainity,verbose,std::nothrow,imgp,twidth,theight,txmin,tymin,bw,progress,mapp,render,failed,best_solver) if (maxtiles > 1 && !(fparams.flags & finetune_no_progress_report))
      for (int ty = 0; ty < maxtiles; ty++)
	for (int tx = 0; tx < maxtiles; tx++)
	  {
	    int cur_txmin = std::min (std::max (txmin - twidth * (maxtiles / 2) + tx * twidth, 0), imgp->width - twidth - 1) & ~1;
	    int cur_tymin = std::min (std::max (tymin - theight * (maxtiles / 2) + ty * theight, 0), imgp->height - theight - 1) & ~1;
	    //int cur_txmax = cur_txmin + twidth;
	    //int cur_tymax = cur_tymin + theight;
	    finetune_solver solver;
	    if (progress && progress->cancel_requested ()) 
	      continue;
	    if (!bw)
	      solver.tile = (std::unique_ptr <rgbdata[]>)(new  (std::nothrow) rgbdata [twidth * theight]);
	    else
	      solver.bwtile = (std::unique_ptr <luminosity_t[]>)(new  (std::nothrow) luminosity_t [twidth * theight]);

	    solver.tile_pos = (std::unique_ptr <point_t[]>)(new  (std::nothrow) point_t [twidth * theight]);
	    if ((!solver.tile && !solver.bwtile) || !solver.tile_pos)
	      {
		failed = true;
		continue;
	      }
	    for (int y = 0; y < theight; y++)
	      for (int x = 0; x < twidth; x++)
		{
		  mapp->to_scr (cur_txmin + x + 0.5, cur_tymin + y + 0.5, &solver.tile_pos [y * twidth + x].x, &solver.tile_pos[y * twidth + x].y);
		  if (solver.tile)
		    solver.tile[y * twidth + x] = render.get_unadjusted_rgb_pixel (x + cur_txmin, y + cur_tymin);
		  if (solver.bwtile)
		    solver.bwtile[y * twidth + x] = render.get_unadjusted_data (x + cur_txmin, y + cur_tymin);
		}
	    solver.txmin = cur_txmin;
	    solver.tymin = cur_tymin;
	    solver.twidth = twidth;
	    solver.theight = theight;
	    solver.type = mapp->get_type ();
	    solver.pixel_size = render.pixel_size ();
	    solver.optimize_position = fparams.flags & finetune_position;
	    solver.optimize_screen_blur = fparams.flags & finetune_screen_blur;
	    solver.optimize_screen_channel_blurs = fparams.flags & finetune_screen_channel_blurs;
	    solver.optimize_screen_mtf_blur = fparams.flags & finetune_screen_mtf_blur;
	    solver.optimize_emulsion_blur = fparams.flags & finetune_emulsion_blur;
	    solver.optimize_dufay_strips = (fparams.flags & finetune_dufay_strips) && solver.type == Dufay;
	    solver.optimize_fog = (fparams.flags & finetune_fog) && solver.tile;
	    solver.collection_threshold = rparam.collection_threshold;
	    //printf ("%i %i %i %u\n", solver.optimize_fog, (fparams.flags & finetune_fog), solver.tile != NULL, solver.bwtile != NULL);
	    solver.least_squares = !(fparams.flags & finetune_no_least_squares);
	    solver.data_collection = !(fparams.flags & finetune_no_data_collection);
	    solver.normalize = !(fparams.flags & finetune_no_normalize);
	    solver.init (rparam.screen_blur_radius);

	    //if (verbose)
	      //solver.print_values (solver.start);
	    simplex<coord_t, finetune_solver>(solver, "finetuning", progress, !(fparams.flags & finetune_no_progress_report) && maxtiles == 1);
	    coord_t uncertainity = solver.objfunc (solver.start);
	    if (maxtiles * maxtiles > 1 && !(fparams.flags & finetune_no_progress_report) && progress)
	      progress->inc_progress ();
	    if (solver.bwtile)
	      {
		rgbdata c = solver.last_color;
		coord_t mmin = std::min (std::min (c.red, c.green), c.blue);
		coord_t mmax = std::max (std::max (c.red, c.green), c.blue);
		if (mmin != mmax)
		  uncertainity /= (mmax - mmin);
		else
		  uncertainity = 100000000;
	      }
	    solver.free_least_squares ();
#pragma omp critical
	      {
		if (best_uncertainity < 0 || best_uncertainity > uncertainity)
		  {
		    best_solver = solver;
		    best_uncertainity = uncertainity;
		  }
	      }
	  }
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
  if (best_solver.tile && fparams.ignore_outliers > 0)
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
    best_solver.write_file (best_solver.start, fparams.simulated_file, 0);
  if (fparams.orig_file)
    best_solver.write_file (best_solver.start, fparams.orig_file, 1);
  if (fparams.diff_file)
    best_solver.write_file (best_solver.start, fparams.diff_file, 2);
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
  ret.dufay_red_strip_width = best_solver.get_red_strip_width (best_solver.start);
  ret.dufay_green_strip_width = best_solver.get_green_strip_width (best_solver.start);
  ret.screen_blur_radius = best_solver.get_blur_radius (best_solver.start);
  ret.screen_channel_blur_radius = best_solver.get_channel_blur_radius (best_solver.start);
  ret.emulsion_blur_radius = best_solver.get_emulsion_blur_radius (best_solver.start);
  best_solver.get_mtf (ret.screen_mtf_blur, best_solver.start);
  ret.screen_coord_adjust = best_solver.get_offset (best_solver.start);
  ret.fog = best_solver.get_fog (best_solver.start);
  if (best_solver.optimize_emulsion_intensities)
    ret.mix_weights = best_solver.get_mix_weights (best_solver.start);
  else
    {
     ret.mix_weights.red = rparam.mix_red;
     ret.mix_weights.green = rparam.mix_green;
     ret.mix_weights.blue = rparam.mix_blue;
    }

  if (best_solver.optimize_position)
    {
      /* Construct solver point.  Try to get closest point to the center of analyzed tile.  */
      int fsx = nearest_int (best_solver.get_pos (best_solver.start, twidth/2, theight/2).x);
      int fsy = nearest_int (best_solver.get_pos (best_solver.start, twidth/2, theight/2).y);
      int bx = - 1, by = -1;
      coord_t bdist = 0;
      for (int y = 0; y < theight; y++)
	{
	  for (int x = 0; x < twidth; x++)
	    {
	      point_t p = best_solver.get_pos (best_solver.start, x, y);
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
      point_t fp;

      bool found = false;
      //printf ("%i %i %i %i %f %f\n", bx, by, fsx, fsy, best_solver.tile_pos[twidth/2+(theight/2)*twidth].x, best_solver.tile_pos[twidth/2+(theight/2)*twidth].y);
      for (int y = by - 1; y <= by + 1; y++)
	for (int x = bx - 1; x <= bx + 1; x++)
	  {
	    /* Determine cell corners.  */
	    point_t p = {(coord_t)fsx, (coord_t)fsy};
	    point_t p1 = best_solver.get_pos (best_solver.start, x, y);
	    point_t p2 = best_solver.get_pos (best_solver.start, x + 1, y);
	    point_t p3 = best_solver.get_pos (best_solver.start, x, y + 1);
	    point_t p4 = best_solver.get_pos (best_solver.start, x + 1, y + 1);
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
      ret.solver_point_img_location = {fp.x + best_solver.txmin + 0.5, fp.y + best_solver.tymin + 0.5};
      ret.solver_point_screen_location = {(coord_t)fsx, (coord_t)fsy};
      ret.solver_point_color = solver_parameters::green;
    }
  if (fparams.screen_file)
   best_solver.original_scr->save_tiff (fparams.screen_file);
  if (fparams.screen_blur_file)
    best_solver.scr.save_tiff (fparams.screen_blur_file);
  if (best_solver.emulsion_scr)
    best_solver.emulsion_scr->save_tiff ("/tmp/colorscr-emulsion.tif");
  if (best_solver.merged_scr)
    best_solver.merged_scr->save_tiff ("/tmp/colorscr-merged.tif");
  {
    screen tmp;
    best_solver.collect_screen (&tmp, best_solver.start);
    tmp.save_tiff ("/tmp/colorscr-collected.tif");
  }
  if (fparams.dot_spread_file)
    {
      screen scr, scr1;
      scr1.initialize_dot ();
      best_solver.apply_blur (best_solver.start, &scr, &scr1);
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
	res[x+y*xsteps] = finetune (rparam, param, img, xmin + (x + 0.5) * xstep, ymin + (y + 0.5) * ystep, fparam, progress);
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
