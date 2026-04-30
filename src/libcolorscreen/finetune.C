/* Parameter finetuning.
   Copyright (C) 2014-2026 Jan Hubicka
   This file is part of Color-Screen.  */

#include <memory>
#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include "bitmap.h"
#include "deconvolve.h"
#include "icc.h"
#include "include/colorscreen.h"
#include "include/dufaycolor.h"
#include "include/finetune.h"
#include "include/histogram.h"
#include "include/stitch.h"
#include "include/tiff-writer.h"
#include "nmsimplex.h"
#include "render-interpolate.h"
#include "sharpen.h"
#include <gsl/gsl_multifit.h>
namespace colorscreen
{
namespace
{
struct gsl_work_deleter
{
  void
  operator() (gsl_multifit_linear_workspace *p)
  {
    gsl_multifit_linear_free (p);
  }
};
struct gsl_matrix_deleter
{
  void
  operator() (gsl_matrix *p)
  {
    gsl_matrix_free (p);
  }
};
struct gsl_vector_deleter
{
  void
  operator() (gsl_vector *p)
  {
    gsl_vector_free (p);
  }
};

/* Translate center to given coordinates (x,y).  */
class translation_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize translation to CENTER.  */
  translation_3x3matrix (point_t center)
  {
    (*this)(0, 2) = center.x;
    (*this)(1, 2) = center.y;
  }
};

/* Rotate by given angle.  */
class rotation_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize rotation by ROTATION degrees.  */
  rotation_3x3matrix (coord_t rotation)
  {
    rotation *= (coord_t)M_PI / 180;
    const coord_t s = std::sin (rotation);
    const coord_t c = std::cos (rotation);
    (*this)(0, 0) = c; (*this)(0, 1) = -s;
    (*this)(1, 0) = s; (*this)(1, 1) = c;
    (*this)(2, 2) = 1; 
  }
};

/* Rotate by given angle.  */
class scale_3x3matrix : public matrix3x3<coord_t>
{
public:
  /* Initialize rotation by ROTATION degrees.  */
  scale_3x3matrix (coord_t scale)
  {
    (*this)(0, 0) = scale;
    (*this)(1, 1) = scale;
  }
};

/* Return contrast which is useful for registration.  */
luminosity_t
get_positional_color_contrast (scr_type type, rgbdata c)
{
  /* In Paget like screens any difference is good since each color
     forms a grid.
     For screen with strips we only determine one direction and that one
     is also always good.  */
  if (paget_like_screen_p (type)
      || screen_with_vertical_strips_p (type))
    {
      luminosity_t mmin = std::min ({c.red, c.green, c.blue});
      luminosity_t mmax = std::max ({c.red, c.green, c.blue});
      return mmax - mmin;
    }
  /* Dufaycolor has green and blue squares, red strips.
     We need contrast between the squares to determine position in both
     directions.  */
  if (dufay_like_screen_p (type))
    {
      if (type == Dufay)
	;
      else if (type == DioptichromeB)
	std::swap (c.red, c.green);
      else if (type == ImprovedDioptichromeB || type == Omnicolore)
	std::swap (c.red, c.blue);
      else
	abort ();
      luminosity_t mmin = std::min (c.blue, c.green);
      luminosity_t mmax = std::max (c.blue, c.green);
      return mmax - mmin;
    }
  abort ();
}

/* Callback used for sharpening.  Fetch data from buffer R at point P.
   WIDTH and HEIGHT are dimensions of the buffer.  */
rgbdata
getdata_helper (rgbdata *r, int_point_t p, int width, int height)
{
  if (colorscreen_checking)
    assert (p.x >= 0 && p.x < width && p.y >= 0 && p.y < height);
  return r[p.y * width + p.x];
}

/* Sign of angle used for mesh transform.  Compute sign of triangle formed
   by P1, P2 and P3.  */
static coord_t
sign (point_t p1, point_t p2, point_t p3)
{
  return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
}

/* Intersect two vectors starting at [X1, Y1] with direction [DX1, DY1]
   and [X2, Y2] with direction [DX2, DY2].  Store result in [A, B].  */
void
intersect_vectors (coord_t x1, coord_t y1, coord_t dx1, coord_t dy1,
                   coord_t x2, coord_t y2, coord_t dx2, coord_t dy2,
                   coord_t *a, coord_t *b)
{
  matrix2x2<coord_t> m (dx1, -dx2, dy1, -dy2);
  m = m.invert ();
  m.apply_to_vector (x2 - x1, y2 - y1, a, b);
}

/* Clamp value V to range [MIN, MAX].  */
inline void
to_range (coord_t &v, coord_t min, coord_t max)
{
#if 0
  if (!(v >= min))
    v = min;
  if (!(v <= max))
    v = max;
#endif

  v = std::clamp (v, min, max);
}

/* V is in range 0...1 expand it to MINV...MAXV.
   Finetuning works well if values are generally kept
   in range 0...1.  */
inline coord_t
expand_range (coord_t v, coord_t minv, coord_t maxv)
{
  return minv + v * (maxv - minv);
}

/* V is in range MINV...MAXV shrink it to 0...1.  */
inline coord_t
shrink_range (coord_t v, coord_t minv, coord_t maxv)
{
  return (v - minv) * ((coord_t)1 / (maxv - minv));
}

/* Solver used to find parameters of simulated scan (position of the grid,
   color of individual patches, lens blur ...) to match given scan tile
   (described tile and tile_pos) as well as possible.
   It is possible to match either in BW or RGB and choose set of parameters
   to optimize for.  */
class finetune_solver
{
public:
  static constexpr const int max_tiles = 8;

  /* Data we need for each tile.  */
  class tile_data
  {
  public:
    std::shared_ptr<bitmap_2d> outliers;
    /* Tile positions */
    int txmin, tymin;

    /* Tile colors collected from the scan for faster access.
       Empty for BW mode.  */
    std::vector<rgbdata> color;
    /* Sharpened tile.  */
    rgbdata *sharpened_color = nullptr;
    /* Memory buffer for sharpened tile when it is not alias of color.  */
    std::vector<rgbdata> sharpened_color_buffer;
    /* Black and white tile.
       Empty for color mode.  */
    std::vector<luminosity_t> bw;
    /* Tile position  */
    std::vector<point_t> pos;
    /* If we do not finetune offsets, fix one. Usually 0,0.  */
    point_t fixed_offset = { -10, -10 }, fixed_emulsion_offset = { -10, -10 };
    /* Screen merging emulsion and unblurred screen.  */
    std::unique_ptr<screen> merged_scr;
    /* Blurred screen used to render simulated scan.  */
    std::unique_ptr<screen> scr;

    tile_data () {}
    tile_data (const tile_data &) = delete;
    tile_data (tile_data &&) = default;
    tile_data &operator= (const tile_data &) = delete;
    tile_data &operator= (tile_data &&) = default;
    ~tile_data () = default;

  protected:
    friend finetune_solver;

    /* Remember last settings, so we do not recompute screens uselessly.  */
    rgbdata last_emulsion_intensities = { -1, -1, -1 };
    point_t last_emulsion_offset = { -10, -10 };
    int last_screen_revision = -1;
    point_t last_simulated_offset = { -100, -100 };

    /* Simulation of screen.  */
    std::vector<rgbdata> simulated_screen;
  };
  tile_data tiles[max_tiles];

private:
  /* Matrix for scaling & rotation */
  matrix3x3<coord_t> transformation;

  /* Least squares solver for optimizing parameters that behaves linearly.  */
  std::unique_ptr<gsl_multifit_linear_workspace, gsl_work_deleter> gsl_work;
  std::unique_ptr<gsl_matrix, gsl_matrix_deleter> gsl_X;
  std::unique_ptr<gsl_vector, gsl_vector_deleter> gsl_y[3];
  std::unique_ptr<gsl_vector, gsl_vector_deleter> gsl_c;
  std::unique_ptr<gsl_matrix, gsl_matrix_deleter> gsl_cov;
  bool least_squares_initialized;

  /* we ignore some outliers to get more realistic result.  */
  int noutliers = 0;

  /* Indexes into optimized values array to fetch individual parameters  */
  int coordinate_index;
  int fog_index;
  int color_index;
  int emulsion_intensity_index;
  int emulsion_offset_index;
  int emulsion_blur_index;
  int sharpen_index;
  int mtf_sigma_index;
  int mtf_defocus_index;
  int screen_index;
  int strips_index;
  int mix_weights_index;
  int mix_dark_index;
  /* Number of values needed.  */
  int n_values;
  int border;

  rgbdata fog_range;
  luminosity_t maxgray;
  luminosity_t mingray;
  luminosity_t min_nonone_clen;

  /* Global tracking of shared screen parameters.  */
  int screen_revision;
  rgbdata last_blur;
  luminosity_t last_scanner_mtf_sigma;
  rgbdata last_scanner_mtf_defocus;
  luminosity_t last_emulsion_blur;
  coord_t last_width, last_height;
public:
  /* Unblurred screen.  */
  std::shared_ptr<screen> original_scr;
  /* Screen with emulsion.  */
  std::shared_ptr<screen> emulsion_scr;
  sharpen_parameters render_sharpen_params;

  finetune_solver () {}
  int n_tiles;
  /* All tiles have same width and height.  */
  int twidth, theight;
  int simulated_screen_border;
  int simulated_screen_width;
  int simulated_screen_height;

  std::vector<coord_t> start;
  /* True if openMP parallelism is desired.  */
  bool parallel;

  /* Scanner mtf if known.  */
  mtf *fixed_scanner_mtf;

  /* Screen blur and strip widths. */
  coord_t fixed_blur, fixed_red_width, fixed_green_width, fixed_emulsion_blur;

  /* Range of position adjustment.  Dufay screen has squares about size of 0.5
     screen coordinates.  adjusting within -0.2 to 0.2 makes sure we do not
     exchange green for blue.  */
  constexpr static const coord_t dufay_range = 0.2;
  /* Paget range is smaller since there are more squares per screen period.
     Especially blue elements are small  */
  constexpr static const coord_t paget_range = 0.1;
  /* Screens with strips have strips with offset 1/3.
     Shifting too far may make us to mix up the strips.
     Be sure we do not move more than -1/6 to 1/6  */
  constexpr static const coord_t strips_range = (coord_t)1 / 6.0;

  coord_t pixel_size;
  scr_type type;

  /* True if we optimize coordinate system.  */
  bool optimize_coordinates;
  /* True if tile is already sharpened.  */
  bool tile_sharpened;
  /* Try to adjust position of center of the patches (+- range)  */
  bool optimize_position;
  /* Try adjusting coordinate1 (rotation/scale)  */
  bool optimize_coordinate1;
  /* Try to optimize scanner mtf sigma (gaussian blur) (otherwise fixed_mtf is
   * used, if any).  */
  bool optimize_scanner_mtf_sigma;
  /* Try to optimize screen blur attribute (otherwise fixed_defocus is used, if
   * any).  */
  bool optimize_scanner_mtf_defocus;
  /* Same but per-channel.  */
  bool optimize_scanner_mtf_channel_defocus;
  /* Try to optimize screen blur attribute (otherwise fixed_blur is used).  */
  bool optimize_screen_blur;
  /* Try to optimize screen blur independently in each channel.  */
  bool optimize_screen_channel_blurs;
  /* Try to optimize strip widths for dufay and strip screens
     (otherwise fixed_width, fixed_height is used).  */
  bool optimize_strips;
  /* Try to optimize dark point.  */
  bool optimize_fog;
  /* Try to optimize for blur caused by film emulsion.  For this screen blur
     needs to be fixed.  */
  bool optimize_sharpening;
  /* Try to optimize sharpening radius and amount.  */
  bool optimize_emulsion_blur;
  /* Optimize colors using least squares method.
     Probably useful only for debugging and better to be true.  */
  bool least_squares;
  /* Determine color using data collection same as used by
     analyze_base_worker.  */
  bool data_collection;
  /* Normalize colors for simulation to uniform intensity.  This is useful
     in RGB simulation to eliminate underlying silver image (which works as
     neutral density filter) of the input scan is linear.  */
  bool normalize;
  /* Use simulated infrared channel when rendering.  */
  bool simulate_infrared;
  /* True if mixing weights should be optimized.  */
  bool optimize_mix_weights;
  /* True if mixing dark (in addition to fog) should be optimized.
     This is more an experiment.  Theoretically fog should be good way
     to avoid this  */
  bool optimize_mix_dark;
  /* True if we are using simulated infrared as source of tiles's bw.  */
  bool bw_is_simulated_infrared;

  /* True if emulsion patch intensities should be finetuned.
     Initialized in init call.  */
  bool optimize_emulsion_intensities;
  bool optimize_emulsion_offset;
  bool fog_by_least_squares;

  /* Threshold for data collection.  */
  luminosity_t collection_threshold;

  /* Contrast determined.  */
  luminosity_t contrast;

  /* Optimized values of red, green, blue for RGB simulation
     and optimized intensities for BW simulation.
     Initialized by objfunc and can be reused after it is computed
     since get_colors is expensive.  */
  double_rgbdata last_red, last_green, last_blue, last_color;
  double_rgbdata last_fog;

  finetune_solver (const finetune_solver &) = delete;
  finetune_solver (finetune_solver &&) = default;
  finetune_solver &operator= (const finetune_solver &) = delete;
  finetune_solver &operator= (finetune_solver &&) = default;

  ~finetune_solver () = default;

  /* Return number of values to optimize non-linearly.  */
  int
  num_values () const
  {
    return n_values;
  }
  constexpr static const coord_t rgbscale = 1;

  /* Epsilon used by nonlinear solver.  */
  coord_t
  epsilon () const
  {
    /* the objective function computes average difference.
       1/65536 seems to be way too small epsilon.  */
    return (coord_t)1.0 / 10000;
  }

  /* Scale of original simplex.  */
  coord_t
  scale () const
  {
    return 0.1;
  }

  /* Should nonlinear solver output info?  */
  bool
  verbose () const
  {
    return false;
  }

  /* How many samples we work with.
     We ignore outliers and when sharpening also some border  */
  int
  sample_points () const
  {
    return (twidth - 2 * border) * (theight - 2 * border) * n_tiles
           - noutliers;
  }

  /* Return correction to the scr-to-img map for TILEID.
     Values are in vector V.  */
  point_t
  get_offset (coord_t *v, int tileid) const
  {
    if (!optimize_position)
      return tiles[tileid].fixed_offset;
    /* Screens with two-dimensional structure needs two offsets.  */
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        return { v[2 * tileid] * range, v[2 * tileid + 1] * range };
      }
    /* Screens with one-dimensional structure needs just one.  */
    else
      return { v[tileid] * strips_range, 0 };
  }

  /* Set correction to the scr-to-img-map OFF for TILEID.
     Store values in vector V.  */
  void
  set_offset (coord_t *v, int tileid, point_t off)
  {
    if (!optimize_position)
      {
        tiles[tileid].fixed_offset = off;
        return;
      }
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        v[2 * tileid] = off.x / range;
        v[2 * tileid + 1] = off.y / range;
      }
    else
      v[tileid] = off.x / strips_range;
  }

  /* Return displacement between the image layer and emulsion for TILEID.
     Values are in vector V.  */
  point_t
  get_emulsion_offset (coord_t *v, int tileid)
  {
    if (!optimize_emulsion_offset)
      return tiles[tileid].fixed_emulsion_offset;
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        /* Reduce range if is not removable and can only be
           adjusted by angle of scanner.  On the other hand
           increase the range for non-removable screens since there is
           lesser change to make off-by-one error when we have color of the
           patch.  */
        if (integrated_screen_p (type))
          range /= 2;
        else
          range *= 2;
        return { v[emulsion_offset_index + 2 * tileid] * range,
                 v[emulsion_offset_index + 2 * tileid + 1] * range };
      }
    else
      return { v[emulsion_offset_index + tileid] * (strips_range * (coord_t)2), 0 };
  }
  /* Set displacement between the image layer and emulsion OFF for TILEID.
     Store values in vector V.  */
  void
  set_emulsion_offset (coord_t *v, int tileid, point_t off)
  {
    if (!optimize_emulsion_offset)
      {
        tiles[tileid].fixed_emulsion_offset = off;
        return;
      }
    if (!screen_with_vertical_strips_p (type))
      {
        coord_t range = dufay_like_screen_p (type) ? dufay_range : paget_range;
        /* Reduce range if is not removable and can only be
           adjusted by angle of scanner.  */
        if (integrated_screen_p (type))
          range /= 2;
        else
          range *= 2;
        v[emulsion_offset_index + 2 * tileid] = off.x / range;
        v[emulsion_offset_index + 2 * tileid + 1] = off.y / range;
      }
    else
      v[emulsion_offset_index + tileid] = off.x / (2 * strips_range);
  }

  /* Return current emulsion blur radius from vector V.  Value is in screen
     size. This does not use pixel_blur since physical emulsion is not
     dependent on scanner parameters.  */
  coord_t
  get_emulsion_blur_radius (coord_t *v)
  {
    if (!optimize_emulsion_blur)
      return fixed_emulsion_blur;
    /* screen::max_blur_radius allows so large blurs that the resulting
       process would be next to useless. Also the minimal blur is non-zero.  */
    return v[emulsion_blur_index] * (screen::max_blur_radius * (coord_t)0.2 - (coord_t)0.03)
           + (coord_t)0.03;
  }

  /* Set current emulsion blur radius BLUR.
     Store values in vector V.  */
  void
  set_emulsion_blur_radius (coord_t *v, coord_t blur)
  {
    if (!optimize_emulsion_blur)
      fixed_emulsion_blur = blur;
    else
      v[emulsion_blur_index]
          = (blur - (coord_t)0.03) / (screen::max_blur_radius * (coord_t)0.2 - (coord_t)0.03);
  }

  /* Return pixel blur for value V.  Do pixel blurs in the range 0.3 ...
     screen::max_blur_radius / pixel_size. Value is in pixels, not screen size.
     Scans are always bit blurred at pixel level, so 0.3 should be reasonable
     minima.  */
  coord_t
  pixel_blur (coord_t v)
  {
    return expand_range (v, (coord_t)0.3, screen::max_blur_radius / pixel_size);
  }
  /* Inverse of pixel_blur.  */
  coord_t
  rev_pixel_blur (coord_t v)
  {
    return shrink_range (v, (coord_t)0.3, screen::max_blur_radius / pixel_size);
  }

  /* Return sigma of screen from vector V. */
  coord_t
  get_scanner_mtf_sigma (coord_t *v)
  {
    if (!optimize_scanner_mtf_sigma)
      return render_sharpen_params.scanner_mtf.sigma;
    return v[mtf_sigma_index];
  }

  /* Return blur radius of screen from vector V. */
  coord_t
  get_scanner_mtf_defocus (coord_t *v)
  {
    if (!optimize_scanner_mtf_defocus && !optimize_scanner_mtf_channel_defocus)
      return render_sharpen_params.scanner_mtf.simulate_diffraction_p ()
                 ? render_sharpen_params.scanner_mtf.defocus
                 : render_sharpen_params.scanner_mtf.blur_diameter;
    if (optimize_scanner_mtf_channel_defocus)
      return (v[mtf_defocus_index] + v[mtf_defocus_index + 1]
              + v[mtf_defocus_index + 2])
             * ((coord_t)1 / (coord_t)3);
    return v[mtf_defocus_index];
  }
  /* Return blur radius of screen for individual channels from vector V. */
  rgbdata
  get_scanner_mtf_channel_defocus (coord_t *v)
  {
    if (!optimize_scanner_mtf_defocus && !optimize_scanner_mtf_channel_defocus)
      {
        auto r = render_sharpen_params.scanner_mtf.simulate_diffraction_p ()
                     ? render_sharpen_params.scanner_mtf.defocus
                     : render_sharpen_params.scanner_mtf.blur_diameter;
        return { (luminosity_t)r, (luminosity_t)r, (luminosity_t)r };
      }
    if (!optimize_scanner_mtf_channel_defocus)
      {
        luminosity_t b = v[mtf_defocus_index];
        return { b, b, b };
      }
    return { (luminosity_t)v[mtf_defocus_index],
             (luminosity_t)v[mtf_defocus_index + 1],
             (luminosity_t)v[mtf_defocus_index + 2] };
  }

  /* Return blur radius of screen from vector V. */
  coord_t
  get_blur_radius (coord_t *v)
  {
    if (optimize_screen_channel_blurs)
      return pixel_blur (
          (v[screen_index] + v[screen_index + 1] + v[screen_index + 2])
          * ((coord_t)1 / (coord_t)3));
    if (!optimize_screen_blur)
      return fixed_blur;
    return pixel_blur (v[screen_index]);
  }
  /* Set blur radius BLUR.
     Store values in vector V.  */
  void
  set_blur_radius (coord_t *v, coord_t blur)
  {
    if (optimize_screen_channel_blurs)
      abort ();
    if (!optimize_screen_blur)
      fixed_blur = blur;
    else
      v[screen_index] = rev_pixel_blur (blur);
    return;
  }

  /* Same as get_blur_radius but when optimizing blurs of individual
     channels from vector V.  */
  rgbdata
  get_channel_blur_radius (coord_t *v)
  {
    if (!optimize_screen_blur && !optimize_screen_channel_blurs)
      return { (luminosity_t)fixed_blur, (luminosity_t)fixed_blur,
               (luminosity_t)fixed_blur };
    if (!optimize_screen_channel_blurs)
      {
        coord_t b = pixel_blur (v[screen_index]);
        return { (luminosity_t)b, (luminosity_t)b, (luminosity_t)b };
      }
    return { (luminosity_t)pixel_blur (v[screen_index]),
             (luminosity_t)pixel_blur (v[screen_index + 1]),
             (luminosity_t)pixel_blur (v[screen_index + 2]) };
  }

  /* Return red strip width from vector V.  */
  coord_t
  get_red_strip_width (coord_t *v)
  {
    if (!optimize_strips)
      return fixed_red_width;
    return v[strips_index];
  }
  /* Return green strip width from vector V.  */
  coord_t
  get_green_strip_width (coord_t *v)
  {
    if (!optimize_strips)
      return fixed_green_width;
    return v[strips_index + 1];
  }
  /* Set red strip width W.
     Store values in vector V.  */
  void
  set_red_strip_width (coord_t *v, coord_t w)
  {
    if (!optimize_strips)
      fixed_red_width = w;
    else
      v[strips_index] = w;
  }
  /* Set green strip width W.
     Store values in vector V.  */
  void
  set_green_strip_width (coord_t *v, coord_t w)
  {
    if (!optimize_strips)
      fixed_green_width = w;
    else
      v[strips_index + 1] = w;
  }

  /* Scale of coordinate system for V.  */
  coord_t
  get_scale (coord_t *v)
  {
    if (!optimize_coordinates)
      return 1;
    return v[coordinate_index] * 0.3 + 1;
  }

  /* Rotation of coordinate system for V.  */
  coord_t
  get_rotation (coord_t *v)
  {
    if (!optimize_coordinates)
      return 0;
    return v[coordinate_index + 1] * 25;
  }

  /* Get screen coordinates of a given pixel P of a given tile TILEID.
     Values are in vector V.  */
  pure_attr point_t
  get_pos (coord_t *v, int tileid, int_point_t p) const
  {
    if (optimize_coordinates)
      return transformation.apply (tiles[tileid].pos[p.y * twidth + p.x]);
    return tiles[tileid].pos[p.y * twidth + p.x] + get_offset (v, tileid);
  }

  /* Return true if we consider outliers.  */
  bool
  has_outliers ()
  {
    return noutliers;
  }

  /* Output values in vector V.  */
  void
  print_values (coord_t *v)
  {
    printf ("\n\nOptimizing %i values:", num_values ());
    if (optimize_coordinates)
      printf (" coordinates");
    if (optimize_position)
      printf (" position");
    if (optimize_scanner_mtf_sigma)
      printf (" scanner_mtf_sigma");
    if (optimize_scanner_mtf_defocus)
      printf (" scanner_mtf_defocus");
    if (optimize_scanner_mtf_channel_defocus)
      printf (" scanner_mtf_channel_defocus");
    if (optimize_screen_blur)
      printf (" screen_blur");
    if (optimize_screen_channel_blurs)
      printf (" screen_channel_blur");
    if (optimize_strips)
      printf (" strips");
    if (optimize_fog)
      printf (" fog");
    if (optimize_sharpening)
      printf (" sharpening");
    if (optimize_emulsion_blur)
      printf (" emulsion_blur");
    if (optimize_emulsion_intensities)
      printf (" emulsion_intensities");
    if (optimize_emulsion_offset)
      printf (" emulsion_offset");
    if (optimize_mix_weights)
      printf (" mix_weights");
    if (optimize_mix_dark)
      printf (" mix_dark");
    printf ("\n");
    if (least_squares)
      printf (" Estimating color using least squares %s\n",
              fog_by_least_squares ? "(including fog)" : "");
    if (data_collection)
      printf (" Estimating color using data collection with threshold %f\n",
              collection_threshold);
    if (normalize)
      printf (" Normalizing colors to eliminate image layer\n");
    if (simulate_infrared)
      printf (" Simulating infrared scan to determine image layer\n");
    if (bw_is_simulated_infrared)
      printf (" Image layer is simulated from RGB scan\n");
    printf ("\n");
    if (sharpen_index >= 0)
      printf ("sharpen radius %f and amount %f\n", get_sharpen_radius (v),
              get_sharpen_amount (v));
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        if (optimize_position)
          {
            point_t p = get_offset (v, tileid);
            printf ("Screen offset %f %f (in pixels %f %f)\n", p.x, p.y,
                    p.x / pixel_size, p.y / pixel_size);
          }
	if (optimize_coordinates)
	  printf ("Scale %f rotation %f degrees\n", get_scale (v), get_rotation (v));
        if (optimize_emulsion_offset)
          {
            point_t p = get_emulsion_offset (v, tileid);
            printf ("Emulsion offset %f %f (%f %f in pixels; relative to "
                    "screen)\n",
                    p.x, p.y, p.x / pixel_size, p.y / pixel_size);
          }
      }
    if (optimize_emulsion_blur)
      printf ("Emulsion blur %f (%f pixels)\n", get_emulsion_blur_radius (v),
              get_emulsion_blur_radius (v) / pixel_size);
    if (optimize_screen_blur)
      printf ("Screen blur %f (pixel size %f, scaled %f)\n",
              get_blur_radius (v), pixel_size,
              get_blur_radius (v) * pixel_size);
    if (optimize_scanner_mtf_sigma)
      printf ("Scanner mtf sigma %f px\n", get_scanner_mtf_sigma (v));
    if (optimize_scanner_mtf_defocus)
      {
        if (!render_sharpen_params.scanner_mtf.simulate_diffraction_p ())
          printf ("Scanner mtf blur diameter %f px\n",
                  get_scanner_mtf_defocus (v));
        else
          printf ("Scanner mtf defocus %f mm\n", get_scanner_mtf_defocus (v));
      }
    if (optimize_scanner_mtf_channel_defocus)
      {
        rgbdata b = get_scanner_mtf_channel_defocus (v);
        if (!render_sharpen_params.scanner_mtf.simulate_diffraction_p ())
          printf ("Scanner mtf blur diameter %f px (red) %f px (green) %f px "
                  "(blue)\n",
                  b.red, b.green, b.blue);
        else
          printf (
              "Scanner mtf defocus %f mm (red) %f mm (green) %f mm (blue)\n",
              b.red, b.green, b.blue);
      }
    if (optimize_screen_channel_blurs)
      {
        rgbdata b = get_channel_blur_radius (v);
        printf ("Red screen blur %f (pixel size %f, scaled %f)\n", b.red,
                pixel_size, b.red * pixel_size);
        printf ("Green screen blur %f (pixel size %f, scaled %f)\n", b.green,
                pixel_size, b.green * pixel_size);
        printf ("Blue screen blur %f (pixel size %f, scaled %f)\n", b.blue,
                pixel_size, b.blue * pixel_size);
      }
    if (optimize_strips)
      {
        printf ("Red strip width: %f\n", get_red_strip_width (v));
        printf ("Green strip width: %f\n", get_green_strip_width (v));
      }
    if (!tiles[0].color.empty ())
      {
        double_rgbdata red, green, blue;
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
            printf ("Fog range ");
            fog_range.print (stdout);
          }
        for (int tileid = 0; tileid < n_tiles; tileid++)
          if (optimize_emulsion_intensities)
            {
              printf ("Emulsion intensities tile %i ", tileid);
              get_emulsion_intensities (v, tileid).print (stdout);
            }
        if (!normalize)
          {
            printf ("Mix weights ");
            get_mix_weights (v).print (stdout);
            if (optimize_mix_dark)
              {
                printf ("Mix dark: %f\n", get_mix_dark (v));
              }
          }
      }
    if (!tiles[0].bw.empty ())
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

  /* Constrain values in vector V to reasonable range.  */
  void
  constrain (coord_t *v)
  {
    if (optimize_coordinates)
      {
        to_range (v[coordinate_index], -1, 1);
        to_range (v[coordinate_index + 1], -1, 1);
      }
    /* x and y adjustments.  */
    if (optimize_position)
      {
        /* Two dimensional screens has two coordinates.  */
        if (!screen_with_vertical_strips_p (type))
          for (int tileid = 0; tileid < n_tiles; tileid++)
            {
              to_range (v[tileid * 2 + 0], -1, 1);
              to_range (v[tileid * 2 + 1], -1, 1);
            }
        /* One dimensional screens just one.  */
        else
          for (int tileid = 0; tileid < n_tiles; tileid++)
            to_range (v[tileid], -1, 1);
      }
    if (optimize_emulsion_offset)
      {
        /* Two dimensional screens has two coordinates.  */
        if (!screen_with_vertical_strips_p (type))
          for (int tileid = 0; tileid < n_tiles; tileid++)
            {
              to_range (v[emulsion_offset_index + 2 * tileid + 0], -1, 1);
              to_range (v[emulsion_offset_index + 2 * tileid + 1], -1, 1);
            }
        /* One dimensional screens just one.  */
        else
          for (int tileid = 0; tileid < n_tiles; tileid++)
            to_range (v[emulsion_offset_index + tileid], -1, 1);
      }
    if (fog_index >= 0)
      {
        assert (!colorscreen_checking || optimize_fog);
        to_range (v[fog_index + 0], (coord_t)-0.1 / (coord_t)fog_range.red, (coord_t)1);
        to_range (v[fog_index + 1], (coord_t)-0.1 / (coord_t)fog_range.green, (coord_t)1);
        to_range (v[fog_index + 2], (coord_t)-0.1 / (coord_t)fog_range.blue, (coord_t)1);
      }
    if (!tiles[0].bw.empty () && !least_squares && !data_collection)
      {
        /* If infrared channel is simulated, negative values may be possible
           and it is kind of hard to constrain to reasonable bounds.
           Still allow values somewhat out of range to account for possible
           over-exposure or cropping  */
        if (!bw_is_simulated_infrared)
          {
            to_range (v[color_index + 0], (coord_t)-0.1, (coord_t)1.1);
            to_range (v[color_index + 1], (coord_t)-0.1, (coord_t)1.1);
            to_range (v[color_index + 2], (coord_t)-0.1, (coord_t)1.1);
          }
      }
    if (sharpen_index >= 0)
      {
        to_range (v[sharpen_index], (coord_t)0, (coord_t)5);        // radius
        to_range (v[sharpen_index + 1], (coord_t)0, (coord_t)1000); // amount
      }
    if (optimize_emulsion_blur)
      to_range (v[emulsion_blur_index], (coord_t)0, (coord_t)1);
    if (optimize_emulsion_intensities)
      for (int i = 0; i < n_tiles * 3 - 1; i++)
        /* First 2 values are normalized and make only sense in range 0..1
           Rest of values are relative to the first two and may be large if
           first patch is dark.  */
        to_range (v[emulsion_intensity_index + i], (coord_t)0, i < 3 ? (coord_t)1 : (coord_t)100);
    if (optimize_screen_blur)
      to_range (v[screen_index], (coord_t)0, (coord_t)1);
    if (optimize_scanner_mtf_sigma)
      to_range (v[mtf_sigma_index], (coord_t)0, (coord_t)20);
    if (optimize_scanner_mtf_defocus)
      to_range (v[mtf_defocus_index], (coord_t)0, (coord_t)20);
    if (optimize_scanner_mtf_channel_defocus)
      {
        to_range (v[mtf_defocus_index], (coord_t)0, (coord_t)20);
        to_range (v[mtf_defocus_index + 1], (coord_t)0, (coord_t)20);
        to_range (v[mtf_defocus_index + 2], (coord_t)0, (coord_t)20);
      }
    if (optimize_screen_channel_blurs)
      {
        /* Screen blur radius.  */
        to_range (v[screen_index + 0], (coord_t)0, (coord_t)1);
        to_range (v[screen_index + 1], (coord_t)0, (coord_t)1);
        to_range (v[screen_index + 2], (coord_t)0, (coord_t)1);
      }
    if (optimize_strips)
      {
        /* strip widths.  */
        if (type == Dufay)
          {
            /* Dufay screen is
               RR
               BG

               Red strip width is more narrow in Dufay screens
               so overall coverage of colors is equal.  */
            to_range (v[strips_index + 0], (coord_t)0.1, (coord_t)0.6);
            /* Green strip width approx 0.5 in Dufay screens.  */
            to_range (v[strips_index + 1], (coord_t)0.3, (coord_t)0.7);
          }
        /* Dioptichrome screens come in various combinations
           and strip widths.  */
        else if (dufay_like_screen_p (type))
          {
            to_range (v[strips_index + 0], (coord_t)0.1, (coord_t)0.7);
            to_range (v[strips_index + 1], (coord_t)0.1, (coord_t)0.7);
          }
        /* Widths of red, green and blue strip needs to sub to 1
           when we have screens with vertical strips.
           Constrain the to min width of 0.1.

           TODO: Warner powrie screens seems to have 4 strips with
           green second tiny green strip between red and blue.  */
        else if (screen_with_vertical_strips_p (type))
          {
            to_range (v[strips_index + 0], (coord_t)0.1, (coord_t)0.7);
            to_range (v[strips_index + 1], (coord_t)0.1, (coord_t)0.9 - v[strips_index + 0]);
          }
        else
          abort ();
      }
  }

  /* Free data used by least squares solver.  */
  void
  free_least_squares ()
  {
    gsl_work.reset ();
    gsl_X.reset ();
    gsl_y[0].reset ();
    gsl_y[1].reset ();
    gsl_y[2].reset ();
    gsl_c.reset ();
    gsl_cov.reset ();
    least_squares_initialized = false;
  }

  /* Allocate least square solver.  */
  void
  alloc_least_squares ()
  {
    int matrixw;

    /* In color mode we produce 3 equations.  In easiest case

       ss is simulated screen (in screen colors after blur)
       red is screen's red color
       green is screen's green color
       blue is screen's blue color

       ss.red * red.red     + ss.green * green.red   + ss.blue * blue.red   =
       tile.red ss.green * red.green + ss.green * green.green + ss.blue *
       blue.green = tile.green ss.blue * red.blue   + ss.blue * green.blue   +
       ss.blue * blue.blue  = tile.blue

       So there are 3 independent equations with 3 variables (red.*, green.*,
       blue.*) each. Tile can be normalized or after fog applied.  If we apply
       fog, then we need to compute RHS each time, otherwise RHS is invariant
       and computed once.

       If fog is optimized we do:

       ss.red * red.red     + ss.green * green.red   + ss.blue * blue.red   +
       fog.red   = tile.red ss.green * red.green + ss.green * green.green +
       ss.blue * blue.green + fog.green = tile.green ss.blue * red.blue   +
       ss.blue * green.blue   + ss.blue * blue.blue  + fog.blue  = tile.blue

       In this case there are 3 independent equations with 4 variables. We also
       add

       fog.red   = 0;
       fog.green = 0;
       fog.blue  = 0;

       Now if infrared is simulated we first we use non-linear solver to guess
       mix_weights and fog. Image layer (simulated infrared) intensity is
       computed by:

       c = tile.red * mix_weights.red + tile.green * mix_weights.green +
       tile.blue * mix_weights.blue

       We know that screen colors should have all the same intensity when
       scalled by the formula above. So we have:

       red.red   * mix_weights.red + red.green   * mix_weights.green + red.blue
       * mix_weights.blue = cst green.red * mix_weights.red + green.green *
       mix_weights.green + green.blue * mix_weights.blue = cst blue.red  *
       mix_weights.red + blue.green  * mix_weights.green + blue.blue  *
       mix_weights.blue = cst

       (We put cst=1.) Consequently:

       red.blue = (cst - red.red * mix_weights.red - red.green *
       mix_weights.green) / mix_weights.blue green.blue = (cst - green.red *
       mix_weights.red - green.green * mix_weights.green) / mix_weights.blue
       blue.blue = (cst - blue.red * mix_weights.red - blue.green *
       mix_weights.green) / mix_weights.blue

       We want to optimize:

       ss.red * c * red.red + ss.green * c * green.red + ss.blue * c * blue.red
       = tile.red - fog.red ss.green * c * red.green + ss.green * c *
       green.green + ss.blue * c * blue.green = tile.green - fog.green ss.blue
       * c * red.blue + ss.green * c * green.blue + ss.blue * c * blue.blue =
       tile.blue - fog.blue

       There are 6 unknowns: red.red, red.green, green.red, green.green,
       blue.red and blue.green. Variables red.blue/green.blue/blue.blue are
       substituted by the identity above. We no longer can treat individual
       channels by independent equations.  */

    if (!tiles[0].color.empty ())
      {
        if (!simulate_infrared)
          {
            matrixw = 3;
            if (fog_by_least_squares)
              matrixw++;
          }
        else
          {
            matrixw = 6;
            if (fog_by_least_squares)
              matrixw += 3;
          }
      }
    /* In BW mode we guess intensity.  */
    else
      matrixw = 1;
    int matrixh = sample_points () + (fog_by_least_squares != 0);
    if (simulate_infrared)
      matrixh *= 3;
    gsl_work.reset (gsl_multifit_linear_alloc (matrixh, matrixw));
    gsl_X.reset (gsl_matrix_alloc (matrixh, matrixw));
    gsl_y[0].reset (gsl_vector_alloc (matrixh));
    least_squares_initialized = false;
    if (!tiles[0].color.empty () && !simulate_infrared)
      {
        gsl_y[1].reset (gsl_vector_alloc (matrixh));
        gsl_y[2].reset (gsl_vector_alloc (matrixh));
      }
    gsl_c.reset (gsl_vector_alloc (matrixw));
    gsl_cov.reset (gsl_matrix_alloc (matrixw, matrixw));
  }

  /* Initialize least square solver using values in vector V.
     Only those values which do not change during optimization are computed.
     Rest is done later.

     In most settings we do not use least squares here and base everything on
     data collection.  */
  void
  init_least_squares (coord_t *v)
  {
    last_fog = { 0, 0, 0 };

    /* In color we solve 3 independent equations for red, green and blue
       channel. Initialize RHS sides which are invariant.  */
    if (!tiles[0].color.empty () && !simulate_infrared)
      {
        int e = 0;

        /* RHS side of all equations should expect the tile's color.  */
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  rgbdata d = fog_by_least_squares
                                  ? get_pixel_nofog (tileid, { x, y })
                                  : get_pixel (v, tileid, { x, y });
                  gsl_vector_set (gsl_y[0].get (), e, d.red);
                  gsl_vector_set (gsl_y[1].get (), e, d.green);
                  gsl_vector_set (gsl_y[2].get (), e, d.blue);
                  e++;
                }
        /* We want fog to be 0.  */
        if (fog_by_least_squares)
          {
            gsl_vector_set (gsl_y[0].get (), e, 0);
            gsl_vector_set (gsl_y[1].get (), e, 0);
            gsl_vector_set (gsl_y[2].get (), e, 0);
            e++;
          }
        if (e != (int)gsl_y[0]->size)
          abort ();
      }
    /* In infrared simulation we set everything later.  */
    else if (!tiles[0].color.empty () && simulate_infrared)
      ;
    /* In BW mode there is only one equation to compute.  */
    else
      {
        int e = 0;
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  gsl_vector_set (gsl_y[0].get (), e,
                                  bw_get_pixel (tileid, { x, y })
                                      / (2 * maxgray));
                  e++;
                }
        if (e != (int)gsl_y[0]->size)
          abort ();
      }
    least_squares_initialized = true;
  }

  /* Used to set up optimization of tile TILEID
     with left corner (CUR_TXMIN, CUR_TYMIN) and screen-to-image map MAP.
     if BW is true, ignore color data.  RENDER is used to fetch data.  */
  bool
  init_tile (int tileid, int cur_txmin, int cur_tymin, bool bw,
             scr_to_img &map, render &render)
  {
    tiles[tileid].txmin = cur_txmin;
    tiles[tileid].tymin = cur_tymin;
    type = map.get_type ();
    if (!bw)
      tiles[tileid].color.resize (twidth * theight);
    else
      tiles[tileid].bw.resize (twidth * theight);

    tiles[tileid].pos.resize (twidth * theight);
    if ((tiles[tileid].color.empty () && tiles[tileid].bw.empty ())
        || tiles[tileid].pos.empty ())
      return false;
    for (int y = 0; y < theight; y++)
      for (int x = 0; x < twidth; x++)
        {
          tiles[tileid].pos[y * twidth + x]
              = map.to_scr ({ cur_txmin + x + (coord_t)0.5, cur_tymin + y + (coord_t)0.5 });
          if (!tiles[tileid].color.empty ())
            tiles[tileid].color[y * twidth + x]
                = render.get_unadjusted_rgb_pixel (
                    { x + cur_txmin, y + cur_tymin });
          if (!tiles[tileid].bw.empty ())
            tiles[tileid].bw[y * twidth + x] = render.get_unadjusted_data (
                { x + cur_txmin, y + cur_tymin });
        }
    return true;
  }

  /* Init solver with FLAGS, BLUR_RADIUS, RED_STRIP_WIDTH, GREEN_STRIP_WIDTH.
     If SIM_INFRARED is true, simulate infrared channel.
     IS_TILE_SHARPENED indicates if tile is already sharpened.
     RESULTS are previous results if any.  */
  void
  init (uint64_t flags, coord_t blur_radius, coord_t red_strip_width,
        coord_t green_strip_width, bool sim_infrared, bool is_tile_sharpened,
        const std::vector<finetune_result> *results)
  {
    bw_is_simulated_infrared = sim_infrared;

    /* First decide on what to optimize.  */
    tile_sharpened = is_tile_sharpened;
    optimize_coordinates = flags & finetune_coordinates;
    optimize_position = flags & finetune_position;
    optimize_coordinate1 = flags & finetune_coordinates;
    optimize_screen_blur = flags & finetune_screen_blur;
    optimize_scanner_mtf_sigma = flags & finetune_scanner_mtf_sigma;
    optimize_scanner_mtf_defocus = flags & finetune_scanner_mtf_defocus;
    optimize_scanner_mtf_channel_defocus
        = flags & finetune_scanner_mtf_channel_defocus;
    optimize_screen_channel_blurs = flags & finetune_screen_channel_blurs;
    optimize_emulsion_blur = flags & finetune_emulsion_blur;
    optimize_strips
        = (flags & finetune_strips) && screen_with_varying_strips_p (type);
    /* Strips needs to be optimized only for some screens, like Dufay, Joly or
     * Powrie.  */
    if (!screen_with_varying_strips_p (type))
      optimize_strips = false;
    /* For one tile the effect of fog can always be simulated by adjusting the
       colors of screen. If multiple tiles (and colors) are sampled we can try
       to estimate it.  */
    optimize_fog = (flags & finetune_fog) && !tiles[0].color.empty () /*&& n_tiles > 1*/;
    /* Colors can be determined either by data collection, least squares
       or using nonlinear solver.  Data collection is fastest, but only works
       if threshold and blurs are meaningful.  Second two should be equivalent
       with least squares being faster and more robust (avoiding local
       minima).

       We later use at most one of least squares and data collection.  Mode
       that does not make sense is turned off.  */
    least_squares = !(flags & finetune_no_least_squares);
    data_collection = !(flags & finetune_no_data_collection);
    simulate_infrared = (flags & finetune_simulate_infrared) && !tiles[0].color.empty ();
    optimize_sharpening
        = (flags & finetune_sharpening) && !tiles[0].color.empty ();
    optimize_mix_weights = false;
    optimize_mix_dark = false;
    optimize_emulsion_offset = false;
    /* TODO; We probably can sharpen and normalize.  */
    normalize = !(flags & finetune_no_normalize) && !optimize_sharpening
                && !tiles[0].color.empty ();
    /* Normalization turns every color of every pixel to have sum of 1.
       This simplifies the optimization since it effectively removes
       the image layer and we can more easily estimate screen position and
       blur.  However this removal is not precise since it can not account for
       scan sharpness.  It is not useful to optimize emulsion blur.
       */
    if (!tiles[0].color.empty () && normalize)
      optimize_emulsion_blur = false;
    /* In infrared simulation we try to estimate the image layer.
       We want to be extra precise, so do not use data collection.
       Emulsion blur so far really has only chance to work on areas of
       solid saturated color.  */
    if (simulate_infrared)
      {
        data_collection = normalize = optimize_emulsion_blur = false;
        if (least_squares)
          optimize_mix_weights = optimize_mix_dark = true;
      }
    /* When finetuning emulsion blur, tune also offset carefully.  */
    if (!tiles[0].color.empty () && optimize_emulsion_blur
        && (optimize_screen_blur || optimize_screen_channel_blurs
            || optimize_scanner_mtf_sigma || optimize_scanner_mtf_defocus
            || optimize_scanner_mtf_channel_defocus))
      {
        optimize_emulsion_intensities = true;
        optimize_emulsion_offset = true;
        data_collection = false;
      }
    else
      optimize_emulsion_intensities = optimize_emulsion_offset = false;

    /* To optimize emulsion blur we can either assume that screen blur
       is already known and use it or try to simulate everything including
       intensities.  */
    if (optimize_emulsion_blur && !optimize_emulsion_intensities)
      optimize_screen_blur = optimize_screen_channel_blurs
          = optimize_scanner_mtf_sigma = optimize_scanner_mtf_defocus
          = optimize_scanner_mtf_channel_defocus = false;
    /* When simulating infrared fog needs to be subtracted before applying
       mixing weights. This makes equations non-linear.  */
    fog_by_least_squares
        = (optimize_fog && !normalize && least_squares) && !simulate_infrared;
    // fog_by_least_squares = 0;

    /* Data collection is faster, so if available prefer it over least
       squares.  */
    if (data_collection)
      least_squares = false;

    /* Next determine values to optimize.  */

    n_values = 0;
    /* Position needs 1 or 2 values per tile depending on if screen
       is 2d or 1d.  */
    if (optimize_position)
      n_values += (1 + !screen_with_vertical_strips_p (type)) * n_tiles;

    if (optimize_coordinates)
      {
        coordinate_index = n_values;
        n_values += 2;
      }
    else
      coordinate_index = -1;

    /* When optimizing sharpening, be ready for borders of the tile to not be
       right. Also allocate the memory buffer.  */
    if (optimize_sharpening)
      {
        sharpen_index = n_values;
        border = 10;
        n_values += 2;
        for (int i = 0; i < n_tiles; i++)
          tiles[i].sharpened_color_buffer.resize (twidth * theight);
        /* Determine minimum meaningful sharpening radius.  */
        min_nonone_clen = 0.3;
      }
    else
      {
        sharpen_index = -1;
        border = 0;
        for (int i = 0; i < n_tiles; i++)
          tiles[i].sharpened_color = tiles[i].color.data ();
      }

    /* When not doing data collection or least squares, we need to optimize
     * colors.  */
    if (!least_squares && !data_collection)
      {
        color_index = n_values;
        /* 3*3 values for color.
           3 intensities for B&W  */
        if (!tiles[0].color.empty ())
          n_values += 9;
        else
          n_values += 3;
      }
    else
      color_index = -1;

    /* Fog is RGB value.  We can not determine it in BW, since it can not
       be separated from color.  */
    if (optimize_fog && !fog_by_least_squares)
      {
        fog_index = n_values;
        n_values += 3;
      }
    else
      fog_index = -1;

    /* If we do not use least squares, mixing weights are 2 values.
       Last value is complement of the other two since we optimize
       screen colors freely.

       If we use least squares then we need all 3 values since screen
       colors are normalized to have sum of (1,1,1) so we save some
       variables.  */
    if (optimize_mix_weights)
      {
        mix_weights_index = n_values;
        n_values += 2 + (least_squares != 0);
      }
    else
      mix_weights_index = -1;

    if (optimize_mix_dark)
      {
        mix_dark_index = n_values;
        n_values++;
      }
    else
      mix_dark_index = -1;

    /* Try to gues intensity of emulsion below each of primary colors.
       Used when trying to determine emulsion blur.
       This must be per-tile since every tile is assumed to have different
       color (but uniform in each tile).  */
    if (optimize_emulsion_intensities)
      {
        emulsion_intensity_index = n_values;
        n_values += 3 * n_tiles - 1;
      }
    else
      emulsion_intensity_index = -1;
    if (optimize_emulsion_offset)
      {
        emulsion_offset_index = n_values;
        n_values += (1 + !screen_with_vertical_strips_p (type)) * n_tiles;
      }
    else
      emulsion_offset_index = -1;
    if (optimize_emulsion_blur)
      {
        emulsion_blur_index = n_values;
        n_values += 1;
      }
    else
      emulsion_blur_index = -1;

    /* Screen index has different meanings depending on how well
       we want to estimate the blur.  */
    if (optimize_screen_channel_blurs)
      {
        screen_index = n_values;
        optimize_screen_blur = false;
        n_values += 3;
        assert (!optimize_screen_blur);
      }
    else if (optimize_screen_blur)
      {
        screen_index = n_values;
        n_values++;
      }
    else
      screen_index = -1;

    if (optimize_scanner_mtf_sigma)
      {
        mtf_sigma_index = n_values;
        n_values++;
      }
    if (optimize_scanner_mtf_channel_defocus)
      {
        mtf_defocus_index = n_values;
        optimize_scanner_mtf_defocus = false;
        n_values += 3;
      }
    else if (optimize_scanner_mtf_defocus)
      {
        mtf_defocus_index = n_values;
        n_values++;
      }

    if (optimize_strips)
      {
        strips_index = n_values;
        n_values += 2;
      }
    else
      strips_index = -1;

    /* We know number of values to optimize; allocate them and get initial
       values.  */
    start.resize (n_values, 0);
    /* Poison values, so we know we initialized them all.  */
    if (colorscreen_checking)
      {
        for (int i = 0; i < n_values; i++)
          start[i] = INT_MAX;
        for (int i = 0; i < n_values; i++)
          assert (start[i] == INT_MAX);
      }

    /* Allocate also memory for all simulations.  */
    original_scr = std::make_shared<screen> ();
    if (optimize_emulsion_blur)
      emulsion_scr = std::make_shared<screen> ();
    if (optimize_emulsion_intensities)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        tiles[tileid].merged_scr = std::make_unique<screen> ();
    for (int tileid = 0; tileid < n_tiles; tileid++)
      tiles[tileid].scr = std::make_unique<screen> ();

    /* Set up cached values.   */
    screen_revision = 0;
    last_blur = { -1, -1, -1 };
    last_scanner_mtf_sigma = -1;
    last_scanner_mtf_defocus = { -1, -1, -1 };
    last_emulsion_blur = -1;
    last_width = -1;
    last_height = -1;

    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        tiles[tileid].last_emulsion_intensities = { -1, -1, -1 };
        tiles[tileid].last_emulsion_offset = { -100, -100 };
        tiles[tileid].last_screen_revision = -1;
        tiles[tileid].last_simulated_offset = { -100, -100 };
      }
    last_fog = { 0, 0, 0 };

    /* If we are not reusing older results, offset should be 0
       since we assume scr-to-img map to be meaningful.  */
    if (!results)
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
          set_offset (start.data (), tileid, { 0, 0 });
          set_emulsion_offset (start.data (), tileid, { 0, 0 });
        }
    else
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
          set_offset (start.data (), tileid, (*results)[tileid].screen_coord_adjust);
          set_emulsion_offset (start.data (), tileid,
                               (*results)[tileid].emulsion_coord_adjust);
        }
    if (optimize_coordinates)
      {
        /* scale = 1 */
        start [coordinate_index] = 0;
	/* rotation = 1 */
        start [coordinate_index + 1] = 0;
      }

    /* Always start from scratch with sharpening; otherwise the optimizer tends
       to pick up very large values.  */
    if (sharpen_index >= 0)
      {
        start[sharpen_index] = 0;
        start[sharpen_index + 1] = 0;
      }

    /* Start with color being red, green and blue. */
    if (color_index >= 0)
      {
        if (!tiles[0].color.empty ())
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

    if (optimize_emulsion_intensities)
      for (int tileid = 0; tileid < 3 * n_tiles - 1; tileid++)
        start[emulsion_intensity_index + tileid] = (coord_t)1 / (coord_t)3;
    /* Starting from small blur seems to work better, since other parameters
       are then more relevant.  Sane scanner lens blurs are close to Nyquist
       frequency.  */
    if (optimize_emulsion_blur)
      {
        coord_t blur = (coord_t)0.03;
        if (results)
          {
            histogram hist;
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.pre_account ((*results)[tileid].emulsion_blur_radius);
            hist.finalize_range (65535);
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.account ((*results)[tileid].emulsion_blur_radius);
            hist.finalize ();
            blur = hist.find_avg ((coord_t)0.1);
          }
        set_emulsion_blur_radius (start.data (), blur);
        if (my_fabs (get_emulsion_blur_radius (start.data ()) - blur) > (coord_t)0.01)
          {
            printf ("Emulsion blur %f %f\n", get_emulsion_blur_radius (start.data ()),
                    blur);
            abort ();
          }
      }
    /* Avoid valgrind warnings on undefined values.  We will not really
       use the value, but we will read it to set up last value tracking  */
    else
      set_emulsion_blur_radius (start.data (), -1);
    if (optimize_screen_channel_blurs)
      start[screen_index] = start[screen_index + 1] = start[screen_index + 2]
          = rev_pixel_blur ((coord_t)0.3);
    else
      {
        /* Optimizations seem to work better when it starts from small blur. */
        if (optimize_screen_blur && !results)
          {
            if (!(flags & finetune_use_screen_blur))
              blur_radius = (coord_t)0.3;
          }
        if (results)
          {
            histogram hist;
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.pre_account ((*results)[tileid].screen_blur_radius);
            hist.finalize_range (65535);
            for (int tileid = 0; tileid < n_tiles; tileid++)
              hist.account ((*results)[tileid].screen_blur_radius);
            hist.finalize ();
            blur_radius = hist.find_avg ((coord_t)0.1);
          }
        set_blur_radius (start.data (), blur_radius);
        if (my_fabs (get_blur_radius (start.data ()) - blur_radius) > (coord_t)0.01)
          {
            printf ("Screen blur %f %f\n", get_blur_radius (start.data ()),
                    blur_radius);
            abort ();
          }
      }
    if (optimize_scanner_mtf_sigma)
      {
        start[mtf_sigma_index] = 0;
      }
    if (optimize_scanner_mtf_defocus)
      {
        start[mtf_defocus_index] = 0;
      }
    if (optimize_scanner_mtf_channel_defocus)
      {
        start[mtf_defocus_index] = 0;
        start[mtf_defocus_index + 1] = 0;
        start[mtf_defocus_index + 2] = 0;
      }
    /* TODO: Maybe we want to use previous results and start from params by
     * default.  */
    if (flags & finetune_use_strip_widths)
      {
        set_red_strip_width (start.data (), red_strip_width);
        set_green_strip_width (start.data (), green_strip_width);
      }
    /* Default Dufaycolor strip widths.  */
    else if (type == Dufay)
      {
        set_red_strip_width (start.data (), dufaycolor::red_strip_width);
        set_green_strip_width (start.data (), dufaycolor::green_strip_width);
      }
    /* Dioptichromes seem to be printed with strips of equal widths.  */
    else if (dufay_like_screen_p (type))
      {
        set_red_strip_width (start.data (), (coord_t)0.5);
        set_green_strip_width (start.data (), (coord_t)0.5);
      }
    /* Joly and Warner-Powrie should be approx 1/3 each.  */
    else
      {
        set_red_strip_width (start.data (), (coord_t)1.0 / (coord_t)3);
        set_green_strip_width (start.data (), (coord_t)1.0 / (coord_t)3);
      }
    if (fog_index >= 0)
      {
        start[fog_index + 0] = 0;
        start[fog_index + 1] = 0;
        start[fog_index + 2] = 0;
      }
    if (mix_weights_index >= 0)
      {
        start[mix_weights_index + 0] = (coord_t)1.0 / (coord_t)3;
        start[mix_weights_index + 1] = (coord_t)1.0 / (coord_t)3;
        if (least_squares)
          start[mix_weights_index + 2] = (coord_t)1.0 / (coord_t)3;
      }
    if (mix_dark_index >= 0)
      start[mix_dark_index] = 0;

    /* Verify that everything is set up.  */
    if (colorscreen_checking)
      for (int i = 0; i < n_values; i++)
        assert (start[i] != INT_MAX);

    /* Once values are set up, be sure they are in range.  This should be NOOP
       most of time unless we get mad input.  */
    constrain (start.data ());

    /* Maxgray is used to normalize equations for least squares to reduce
       numeric errors.  */
    maxgray = mingray = 0;
    if (!tiles[0].bw.empty ())
      {
        mingray = maxgray = bw_get_pixel (0, { 0, 0 });
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = 0; y < theight; y++)
            for (int x = 0; x < twidth; x++)
              {
                maxgray = std::max (maxgray, bw_get_pixel (tileid, { x, y }));
                mingray = std::min (mingray, bw_get_pixel (tileid, { x, y }));
              }
      }

    /* Fog should not be much greater than minimal value in the tile.  */
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
        fog_range = hist.find_min ((coord_t)0.1);
        if (!(fog_range.red > 0))
          fog_range.red = (luminosity_t)4 / (luminosity_t)65536;
        if (!(fog_range.green > 0))
          fog_range.green = (luminosity_t)4 / (luminosity_t)65536;
        if (!(fog_range.blue > 0))
          fog_range.blue = (luminosity_t)4 / (luminosity_t)65536;
      }
    else
      assert (!colorscreen_checking || fog_index == -1);

    /* Normalize tile.  This depends on fog, so with fog optimization
       we normalize later.  */
    if (normalize && !optimize_fog)
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
          init_least_squares (nullptr);
      }
    simulated_screen_border = 0;
    simulated_screen_width = twidth;
    simulated_screen_height = theight;
    for (int tileid = 0; tileid < n_tiles; tileid++)
      tiles[tileid].simulated_screen.resize (
          simulated_screen_width * simulated_screen_height);
  }

  /* Invoke solver.  If REPORT is true, set progress report.
     PROGRESS is used to report progress.
     This may be disabled if we run in OpenMP parallel.  */
  coord_t
  solve (progress_info *progress, bool report)
  {
    // if (verbose)
    // solver.print_values (solver.start);
    coord_t uncertainty = simplex<coord_t, finetune_solver> (
        *this, "finetuning", progress, report);
    if (!tiles[0].bw.empty ())
      contrast = get_positional_color_contrast (type, last_color);
    else
      contrast = std::max ({get_positional_color_contrast (type, {(luminosity_t)last_red.red, (luminosity_t)last_green.red, (luminosity_t)last_blue.red}),
			    get_positional_color_contrast (type, {(luminosity_t)last_red.green, (luminosity_t)last_green.green, (luminosity_t)last_blue.green}),
			    get_positional_color_contrast (type, {(luminosity_t)last_red.blue, (luminosity_t)last_green.blue, (luminosity_t)last_blue.blue}),});
    if (contrast > 1 / (luminosity_t)65535)
      uncertainty = std::min (uncertainty / contrast, (coord_t)(10000000));
    else
      uncertainty = 100000000;
    free_least_squares ();
    return uncertainty;
  }

  /* Get screen pixel for simulated screen TILE at point P.  */
  rgbdata
  get_simulated_screen_pixel (int tile, int_point_t p)
  {
    return tiles[tile].simulated_screen[p.y * simulated_screen_width + p.x];
  }

  /* Apply blur to SRC_SCR and compute DST_SCR.
     Values are in vector V.  TILEID is the tile ID.
     If WEIGHT_SCR is non-null, use it as a weight screen.  */
  void
  apply_blur (coord_t *v, int tileid, screen *dst_scr, screen *src_scr,
              screen *weight_scr = nullptr)
  {
    rgbdata blur = get_channel_blur_radius (v);

    if (weight_scr)
      {
        rgbdata i = get_emulsion_intensities (v, tileid);
        point_t offset = get_emulsion_offset (v, tileid);
        if (offset.x == 0 && offset.y == 0)
          for (int y = 0; y < screen::size; y++)
            for (int x = 0; x < screen::size; x++)
              {
                luminosity_t w = weight_scr->mult[y][x][0] * i.red
                                 + weight_scr->mult[y][x][1] * i.green
                                 + weight_scr->mult[y][x][2] * i.blue;
                tiles[tileid].merged_scr->mult[y][x][0]
                    = src_scr->mult[y][x][0] * w;
                tiles[tileid].merged_scr->mult[y][x][1]
                    = src_scr->mult[y][x][1] * w;
                tiles[tileid].merged_scr->mult[y][x][2]
                    = src_scr->mult[y][x][2] * w;
              }
        else
          for (int y = 0; y < screen::size; y++)
            for (int x = 0; x < screen::size; x++)
              {
                rgbdata wd = weight_scr->interpolated_mult (
                    (point_t){ x * ((coord_t)1 / (coord_t)screen::size),
                               y * ((coord_t)1 / (coord_t)screen::size) }
                    + offset);
                luminosity_t w
                    = wd.red * i.red + wd.green * i.green + wd.blue * i.blue;
                tiles[tileid].merged_scr->mult[y][x][0]
                    = src_scr->mult[y][x][0] * w;
                tiles[tileid].merged_scr->mult[y][x][1]
                    = src_scr->mult[y][x][1] * w;
                tiles[tileid].merged_scr->mult[y][x][2]
                    = src_scr->mult[y][x][2] * w;
              }
        src_scr = tiles[tileid].merged_scr.get ();
      }

    if ((optimize_scanner_mtf_sigma || optimize_scanner_mtf_defocus)
        || (!optimize_screen_blur && !optimize_screen_channel_blurs))
      {
        sharpen_parameters sp, sp_green, sp_blue;
        sharpen_parameters *vs[3] = { &sp, &sp, &sp };
        sp = render_sharpen_params;
        // sp.scanner_mtf = mtf_params;
        sp.scanner_mtf.sigma = get_scanner_mtf_sigma (v);
        sp.scanner_mtf_scale *= pixel_size;
        // sp.mode = sharpen_parameters::none;
        if (sp.scanner_mtf.simulate_diffraction_p ())
          {
            sp.scanner_mtf.defocus = get_scanner_mtf_defocus (v);
            if (!tiles[0].color.empty ())
              sp.scanner_mtf.wavelength = 550;
            if (/*tiles[0].color ||*/ optimize_scanner_mtf_channel_defocus)
              {
                vs[0] = &sp;
                sp_green = sp;
                vs[1] = &sp_green;
                sp_blue = sp;
                vs[2] = &sp_blue;

                rgbdata defocus = get_scanner_mtf_channel_defocus (v);
                sp.scanner_mtf.defocus = defocus.red;
                sp_green.scanner_mtf.defocus = defocus.green;
                sp_blue.scanner_mtf.defocus = defocus.blue;
#if 0
	        /* TODO: We should defocus only after applying scanner reaction to screen colors.  */
		sp.scanner_mtf.wavelength = 466;
		sp_green.scanner_mtf.wavelength = 526;
		sp_blue.scanner_mtf.wavelength = 653;
#endif
              }
          }
        else
          {
            sp.scanner_mtf.blur_diameter = get_scanner_mtf_defocus (v);
            if (optimize_scanner_mtf_channel_defocus)
              {
                vs[0] = &sp;
                sp_green = sp;
                vs[1] = &sp_green;
                sp_blue = sp;
                vs[2] = &sp_blue;
                rgbdata blur_diameter = get_scanner_mtf_channel_defocus (v);
                sp.scanner_mtf.blur_diameter = blur_diameter.red;
                sp_green.scanner_mtf.blur_diameter = blur_diameter.green;
                sp_blue.scanner_mtf.blur_diameter = blur_diameter.blue;
              }
          }
        dst_scr->initialize_with_sharpen_parameters (*src_scr, vs,
                                                     tile_sharpened, parallel);
      }
    else
      dst_scr->initialize_with_blur (*src_scr, blur * pixel_size);
  }

  /* Initialize screen for tile TILEID using values in vector V.
     Return true if screen was updated.  */
  bool
  init_screen (coord_t *v, int tileid)
  {
    luminosity_t emulsion_blur = get_emulsion_blur_radius (v);
    rgbdata blur = get_channel_blur_radius (v);
    luminosity_t scanner_mtf_sigma = get_scanner_mtf_sigma (v);
    rgbdata scanner_mtf_defocus = get_scanner_mtf_channel_defocus (v);
    luminosity_t red_strip_width = get_red_strip_width (v);
    luminosity_t green_strip_width = get_green_strip_width (v);
    rgbdata intensities = get_emulsion_intensities (v, tileid);
    point_t emulsion_offset = get_emulsion_offset (v, tileid);
    bool global_updated = false;
    if (red_strip_width != last_width || green_strip_width != last_height)
      {
        original_scr->initialize (type, red_strip_width, green_strip_width);
        last_width = red_strip_width;
        last_height = green_strip_width;
        global_updated = true;
      }

    /* Fast path: If everything is fixed, use screen cache. We will not
       fill it with temporary screens then.  */
    if (!optimize_scanner_mtf_sigma && !optimize_scanner_mtf_defocus
	&& !optimize_scanner_mtf_channel_defocus
       	&& !optimize_screen_blur && !optimize_screen_channel_blurs
       	&& !optimize_strips && !optimize_emulsion_blur
	&& !optimize_emulsion_intensities && !optimize_emulsion_offset)
      {
	if (global_updated)
	  screen_revision++;
	if (tiles[tileid].last_screen_revision != screen_revision)
	  {
	    sharpen_parameters sp = render_sharpen_params;
	    sp.scanner_mtf_scale *= pixel_size;
	    std::shared_ptr <screen> scr = render_to_scr::get_screen (type, false,
								 tile_sharpened,
								 sp,
								 red_strip_width,
								 green_strip_width,
								 NULL);
	    memcpy (tiles[tileid].scr.get (), scr.get (), sizeof (screen));
	    return true;
	  }
	return false;
      }

    if (optimize_emulsion_blur
        && (emulsion_blur != last_emulsion_blur || global_updated))
      {
        emulsion_scr->initialize_with_blur (*original_scr, emulsion_blur);
        last_emulsion_blur = emulsion_blur;
        global_updated = true;
      }

    if (blur != last_blur || scanner_mtf_sigma != last_scanner_mtf_sigma
        || scanner_mtf_defocus != last_scanner_mtf_defocus)
      {
        last_blur = blur;
        last_scanner_mtf_sigma = scanner_mtf_sigma;
        last_scanner_mtf_defocus = scanner_mtf_defocus;
        global_updated = true;
      }

    if (global_updated)
      screen_revision++;

    if (tiles[tileid].last_screen_revision != screen_revision
        || tiles[tileid].last_emulsion_intensities != intensities
        || tiles[tileid].last_emulsion_offset != emulsion_offset)
      {
        apply_blur (v, tileid, tiles[tileid].scr.get (),
                    optimize_emulsion_blur && !optimize_emulsion_intensities
                        ? emulsion_scr.get ()
                        : original_scr.get (),
                    optimize_emulsion_intensities ? emulsion_scr.get ()
                                                  : nullptr);
        tiles[tileid].last_screen_revision = screen_revision;
        tiles[tileid].last_emulsion_intensities = intensities;
        tiles[tileid].last_emulsion_offset = emulsion_offset;
        return true;
      }
    return false;
  }

  /* Evaluate screen pixel for TILEID at X,Y with offset OFF.
     Fast version that does not assume scaling/rotation.  */
  pure_attr inline rgbdata
  evaluate_screen_pixel_fast (int tileid, int x, int y, point_t off) const
  {
    point_t p = tiles[tileid].pos[y * twidth + x] + off;
    /* When using scanner mtf, the screen is already blurred to
       estimate sensor mtf as well.  No need for antialiasing
       then.  */
    return tiles[tileid].scr->interpolated_mult (p);
  }

  /* Evaluate screen pixel for TILEID at X,Y with offset OFF.  */
  pure_attr inline rgbdata
  evaluate_screen_pixel_slow (coord_t *v, int tileid, int x, int y) const
  {
    point_t  p = get_pos (v, tileid, {x, y});
    /* When using scanner mtf, the screen is already blurred to
       estimate sensor mtf as well.  No need for antialiasing
       then.  */
    return tiles[tileid].scr->interpolated_mult (p);
  }

  /* Evaluate pixel at (X,Y) using RGB values RED, GREEN, BLUE and offsets OFF
     compensating coordinates stored in tile_pos.
     Values are in vector V.  TILEID is the tile ID.
     MIX_WEIGHTS and MIX_DARK are used for infrared simulation.  */
  pure_attr inline rgbdata
  evaluate_pixel (coord_t *v, int tileid, double_rgbdata red,
                  double_rgbdata green, double_rgbdata blue, int x, int y,
                  double_rgbdata mix_weights, double mix_dark)
  {
    rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
    rgbdata c = ((red * m.red + green * m.green + blue * m.blue)
                 * ((coord_t)1.0 / (coord_t)rgbscale));
    if (simulate_infrared)
      {
        rgbdata p = get_pixel (v, tileid, { x, y });
        luminosity_t intensity = p.red * mix_weights.red
                                 + p.green * mix_weights.green
                                 + p.blue * mix_weights.blue - mix_dark;
        c *= intensity;
      }
    return c;
  }

  /* Simulate screen for TILEID using values in vector V.
     Return true if simulation was updated.  */
  bool
  simulate_screen (coord_t *v, int tileid, bool force = false)
  {
    double_rgbdata red, green, blue;
    if (!optimize_coordinates)
      {
	point_t off = get_offset (v, tileid);
	if (!force && tiles[tileid].last_simulated_offset == off)
	  return false;
	for (int y = 0; y < theight; y++)
	  for (int x = 0; x < twidth; x++)
	    tiles[tileid].simulated_screen[y * simulated_screen_width + x]
		= evaluate_screen_pixel_fast (tileid, x, y, off);
	tiles[tileid].last_simulated_offset = off;
      }
    else
      for (int y = 0; y < theight; y++)
	for (int x = 0; x < twidth; x++)
	  tiles[tileid].simulated_screen[y * simulated_screen_width + x]
	      = evaluate_screen_pixel_slow (v, tileid, x, y);
    return true;
  }

  /* Evaluate pixel at (X,Y) using COLOR and offset OFF for TILEID.
     This is used for black and white mode.  */
  pure_attr inline luminosity_t
  bw_evaluate_pixel (int tileid, double_rgbdata color, int x, int y)
  {
    rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
    return ((m.red * color.red + m.green * color.green
             + m.blue * color.blue) /** (2 * maxgray)*/);
  }
  pure_attr rgbdata
  get_orig_pixel (coord_t *v, int tileid, int_point_t p)
  {
    if (!optimize_fog)
      return tiles[tileid].color[p.y * twidth + p.x];
    rgbdata d = tiles[tileid].color[p.y * twidth + p.x] - get_fog (v);
    if (normalize)
      {
        luminosity_t ssum = my_fabs (d.red + d.green + d.blue);
        if (ssum == 0)
          ssum = (luminosity_t)0.0000001;
        d /= ssum;
      }
    return d;
  }

  pure_attr rgbdata
  get_pixel_nofog (int tileid, int_point_t p)
  {
    return tiles[tileid].sharpened_color[p.y * twidth + p.x];
  }

  pure_attr rgbdata
  get_pixel (coord_t *v, int tileid, int_point_t p)
  {
    if (!optimize_fog)
      return tiles[tileid].sharpened_color[p.y * twidth + p.x];
    rgbdata d
        = tiles[tileid].sharpened_color[p.y * twidth + p.x] - get_fog (v);
    if (normalize)
      {
        luminosity_t ssum = my_fabs (d.red + d.green + d.blue);
        if (ssum == 0)
          ssum = (luminosity_t)0.0000001;
        d /= ssum;
      }
    return d;
  }

  luminosity_t
  bw_get_pixel (int tileid, int_point_t p)
  {
    return tiles[tileid].bw[p.y * twidth + p.x];
  }
  void
  determine_colors_using_data_collection (coord_t *v, double_rgbdata *ret_red,
                                          double_rgbdata *ret_green,
                                          double_rgbdata *ret_blue)
  {
    /* Use double_rgbdata to avoid accumulation of roundoff error.  */
    double_rgbdata red = { 0, 0, 0 }, green = { 0, 0, 0 }, blue = { 0, 0, 0 };
    double_rgbdata color_red = { 0, 0, 0 }, color_green = { 0, 0, 0 },
                   color_blue = { 0, 0, 0 };
    luminosity_t threshold = collection_threshold;
    /* Use double to avoid accumulation of roundoff error.  */
    double wr = 0, wg = 0, wb = 0;

    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = border; y < theight - border; y++)
        for (int x = border; x < twidth - border; x++)
          if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
            {
              rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
              rgbdata d = get_pixel (v, tileid, { x, y });
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
        *ret_red = *ret_green = *ret_blue = { -15, -15, -15 };
        return;
      }

    red /= wr;
    green /= wg;
    blue /= wb;
    color_red /= wr;
    color_green /= wg;
    color_blue /= wb;
    // sum /= n;
    // sum.print (stdout);
    double_rgbdata cred = (double_rgbdata){ red.red, green.red, blue.red };
    double_rgbdata cgreen
        = (double_rgbdata){ red.green, green.green, blue.green };
    double_rgbdata cblue = (double_rgbdata){ red.blue, green.blue, blue.blue };
    matrix4x4<double> sat (cred.red, cgreen.red, cblue.red, 0, cred.green,
                           cgreen.green, cblue.green, 0, cred.blue,
                           cgreen.blue, cblue.blue, 0, 0, 0, 0, 1);
    sat = sat.invert ();
    // sat.apply_to_rgb (color.red / (2 * maxgray), color.green / (2 *
    // maxgray), color.blue / (2 * maxgray), &color.red, &color.green,
    // &color.blue);
    sat.apply_to_rgb (color_red.red, color_green.red, color_blue.red,
                      &color_red.red, &color_green.red, &color_blue.red);
    sat.apply_to_rgb (color_red.green, color_green.green, color_blue.green,
                      &color_red.green, &color_green.green, &color_blue.green);
    sat.apply_to_rgb (color_red.blue, color_green.blue, color_blue.blue,
                      &color_red.blue, &color_green.blue, &color_blue.blue);
    /* Colors should be real reactions of scanner, so no negative values and
       also no excessively large values. Allow some overexposure.  */
    for (int c = 0; c < 3; c++)
      {
        color_red[c] = std::clamp (color_red[c], (double)-0.01, (double)2);
        color_green[c] = std::clamp (color_green[c], (double)-0.01, (double)2);
        color_blue[c] = std::clamp (color_blue[c], (double)-0.01, (double)2);
      }

    *ret_red = color_red;
    *ret_green = color_green;
    *ret_blue = color_blue;
  }

  coord_t
  determine_colors_using_least_squares (coord_t *v, double_rgbdata *red,
                                        double_rgbdata *green,
                                        double_rgbdata *blue)
  {
    /* Use double to not accumulate errors.  */
    double sqsum = 0;

    if (!least_squares_initialized)
      abort ();

    int e = 0;
    if (simulate_infrared)
      {
        rgbdata mix_weights = get_mix_weights (v);
        luminosity_t mix_dark = get_mix_dark (v);
        luminosity_t sum = /*v[mix_weights_index + 3]*/ 1;

        /* See below.  */
        assert (!fog_by_least_squares);
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  rgbdata c = get_simulated_screen_pixel (tileid, { x, y });
                  rgbdata d = fog_by_least_squares
                                  ? get_pixel_nofog (tileid, { x, y })
                                  : get_pixel (v, tileid, { x, y });
                  /* ??? This is not right when fog is optimized by least
                     squares. For this reason we use non-linear solver to
                     optimize fog.  */
                  c *= d.red * mix_weights.red + d.green * mix_weights.green
                       + d.blue * mix_weights.blue - mix_dark;
                  gsl_matrix_set (gsl_X.get (), e, 0, c.red);   /* red.red */
                  gsl_matrix_set (gsl_X.get (), e, 1, c.green); /* green.red */
                  gsl_matrix_set (gsl_X.get (), e, 2, c.blue);  /* blue.red */
                  gsl_matrix_set (gsl_X.get (), e, 3, 0);       /* red.green */
                  gsl_matrix_set (gsl_X.get (), e, 4, 0);       /* green.green */
                  gsl_matrix_set (gsl_X.get (), e, 5, 0);       /* blue.green  */
                  if (fog_by_least_squares)
                    {
                      gsl_matrix_set (gsl_X.get (), e, 6, 1);
                      gsl_matrix_set (gsl_X.get (), e, 7, 0);
                      gsl_matrix_set (gsl_X.get (), e, 8, 0);
                    }
                  gsl_vector_set (gsl_y[0].get (), e, d.red);
                  e++;
                  gsl_matrix_set (gsl_X.get (), e, 0, 0);       /* red.red */
                  gsl_matrix_set (gsl_X.get (), e, 1, 0);       /* green.red */
                  gsl_matrix_set (gsl_X.get (), e, 2, 0);       /* blue.red */
                  gsl_matrix_set (gsl_X.get (), e, 3, c.red);   /* red.green */
                  gsl_matrix_set (gsl_X.get (), e, 4, c.green); /* green.green */
                  gsl_matrix_set (gsl_X.get (), e, 5, c.blue);  /* blue.green  */
                  if (fog_by_least_squares)
                    {
                      gsl_matrix_set (gsl_X.get (), e, 6, 0);
                      gsl_matrix_set (gsl_X.get (), e, 7, 1);
                      gsl_matrix_set (gsl_X.get (), e, 8, 0);
                    }
                  gsl_vector_set (gsl_y[0].get (), e, d.green);
                  e++;

                  /* red.red * mix_weights.red + red.green * mix_weights.green
                     + red.blue * mix_weights.blue = sum gives: red.blue = (sum
                     - red.red * mix_weights.red - red.green *
                     mix_weights.green) / mix_weights.blue

                     Analogously:
                     green.blue = (sum - green.red * mix_weights.red -
                     green.green * mix_weights.green) / mix_weights.blue
                     blue.blue = (sum - blue.red * mix_weights.red - blue.green
                     * mix_weights.green) / mix_weights.blue  */

                  gsl_matrix_set (gsl_X.get (), e, 0,
                                  -c.red
                                      * (mix_weights.red
                                         / mix_weights.blue)); /* red.red */
                  gsl_matrix_set (gsl_X.get (), e, 1,
                                  -c.green
                                      * (mix_weights.red
                                         / mix_weights.blue)); /* green.red */
                  gsl_matrix_set (gsl_X.get (), e, 2,
                                  -c.blue
                                      * (mix_weights.red
                                         / mix_weights.blue)); /* blue.red */
                  gsl_matrix_set (gsl_X.get (), e, 3,
                                  -c.red
                                      * (mix_weights.green
                                         / mix_weights.blue)); /* red.green */
                  gsl_matrix_set (
                      gsl_X.get (), e, 4,
                      -c.green
                          * (mix_weights.green
                             / mix_weights.blue)); /* green.green */
                  gsl_matrix_set (gsl_X.get (), e, 5,
                                  -c.blue
                                      * (mix_weights.green
                                         / mix_weights.blue)); /* blue.green */
                  if (fog_by_least_squares)
                    {
                      gsl_matrix_set (gsl_X.get (), e, 6, 0);
                      gsl_matrix_set (gsl_X.get (), e, 7, 0);
                      gsl_matrix_set (gsl_X.get (), e, 8, 1);
                    }
                  gsl_vector_set (gsl_y[0].get (), e,
                                  d.blue
                                      - sum * (c.red + c.green + c.blue)
                                            / mix_weights.blue);
                  e++;
                }
        if (fog_by_least_squares)
          {
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3, 0);
            gsl_matrix_set (gsl_X.get (), e, 4, 0);
            gsl_matrix_set (gsl_X.get (), e, 5, 0);
            gsl_matrix_set (gsl_X.get (), e, 6,
                            sample_points () * ((double)4 / 65546));
            gsl_matrix_set (gsl_X.get (), e, 7, 0);
            gsl_matrix_set (gsl_X.get (), e, 8, 0);
            gsl_vector_set (gsl_y[0].get (), e, 0);
            e++;
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3, 0);
            gsl_matrix_set (gsl_X.get (), e, 4, 0);
            gsl_matrix_set (gsl_X.get (), e, 5, 0);
            gsl_matrix_set (gsl_X.get (), e, 6, 0);
            gsl_matrix_set (gsl_X.get (), e, 7,
                            sample_points () * ((double)4 / 65546));
            gsl_matrix_set (gsl_X.get (), e, 8, 0);
            gsl_vector_set (gsl_y[0].get (), e, 0);
            e++;
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3, 0);
            gsl_matrix_set (gsl_X.get (), e, 4, 0);
            gsl_matrix_set (gsl_X.get (), e, 5, 0);
            gsl_matrix_set (gsl_X.get (), e, 6, 0);
            gsl_matrix_set (gsl_X.get (), e, 7, 0);
            gsl_matrix_set (gsl_X.get (), e, 8,
                            sample_points () * ((double)4 / 65546));
            gsl_vector_set (gsl_y[0].get (), e, 0);
            e++;
          }
        double chisq;
        if (gsl_multifit_linear (gsl_X.get (), gsl_y[0].get (), gsl_c.get (), gsl_cov.get (), &chisq,
                                 gsl_work.get ()) != GSL_SUCCESS)
          return 1e10;
        /* Colors should be real reactions of scanner, so no negative values
           and also no excessively large values. Allow some overexposure.  */
        (*red).red = gsl_vector_get (gsl_c.get (), 0);
        // to_range ((*red).red, -0.2, 2);
        (*green).red = gsl_vector_get (gsl_c.get (), 1);
        // to_range ((*green).red, -0.2, 2);
        (*blue).red = gsl_vector_get (gsl_c.get (), 2);
        // to_range ((*blue).red, -0.2, 2);
 
        (*red).green = gsl_vector_get (gsl_c.get (), 3);
        // to_range ((*red).green, -0.2, 2);
        (*green).green = gsl_vector_get (gsl_c.get (), 4);
        // to_range ((*green).green, -0.2, 2);
        (*blue).green = gsl_vector_get (gsl_c.get (), 5);
        // to_range ((*blue).green, -0.2, 2);

        (*red).blue = (sum - (*red).red * mix_weights.red
                       - (*red).green * mix_weights.green)
                      / mix_weights.blue;
        // to_range ((*red).blue, 0, 2);
        (*green).blue = (sum - (*green).red * mix_weights.red
                         - (*green).green * mix_weights.green)
                        / mix_weights.blue;
        // to_range ((*green).blue, 0, 2);
        (*blue).blue = (sum - (*blue).red * mix_weights.red
                        - (*blue).green * mix_weights.green)
                       / mix_weights.blue;
        // to_range ((*blue).blue, 0, 2);

        if (fog_by_least_squares)
          {
            last_fog.red = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 6),
                                       /*-fog_range.red*/ (luminosity_t)-0.1,
                                       fog_range.red);
            last_fog.green = std::clamp (
                (luminosity_t)gsl_vector_get (gsl_c.get (), 7),
                /*-fog_range.green*/ (luminosity_t)-0.1, fog_range.green);
            last_fog.blue = std::clamp (
                (luminosity_t)gsl_vector_get (gsl_c.get (), 8),
                /*-fog_range.blue*/ (luminosity_t)-0.1, fog_range.blue);
          }
        return chisq;
      }
    else
      {
        for (int tileid = 0; tileid < n_tiles; tileid++)
          for (int y = border; y < theight - border; y++)
            for (int x = border; x < twidth - border; x++)
              if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                {
                  rgbdata c = get_simulated_screen_pixel (tileid, { x, y });
                  gsl_matrix_set (gsl_X.get (), e, 0, c.red);
                  gsl_matrix_set (gsl_X.get (), e, 1, c.green);
                  gsl_matrix_set (gsl_X.get (), e, 2, c.blue);
                  if (fog_by_least_squares)
                    gsl_matrix_set (gsl_X.get (), e, 3, 1);
                  e++;
                }
        if (fog_by_least_squares)
          {
            gsl_matrix_set (gsl_X.get (), e, 0, 0);
            gsl_matrix_set (gsl_X.get (), e, 1, 0);
            gsl_matrix_set (gsl_X.get (), e, 2, 0);
            gsl_matrix_set (gsl_X.get (), e, 3,
                            sample_points () * ((double)4 / 65546));
            e++;
          }
        if (e != (int)gsl_X->size1)
          abort ();
        for (int ch = 0; ch < 3; ch++)
          {
            double chisq;
            if (gsl_multifit_linear (gsl_X.get (), gsl_y[ch].get (), gsl_c.get (), gsl_cov.get (), &chisq,
                                     gsl_work.get ()) != GSL_SUCCESS)
              return 1e10;
            sqsum += chisq;
            /* Colors should be real reactions of scanner, so no negative
               values and also no excessively large values. Allow some
               overexposure.  */
            (*red)[ch] = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 0),
                                     (luminosity_t)0, (luminosity_t)2);
            (*green)[ch] = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 1),
                                       (luminosity_t)0, (luminosity_t)2);
            (*blue)[ch] = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 2),
                                      (luminosity_t)0, (luminosity_t)2);
            if (fog_by_least_squares)
              {
                last_fog[ch]
                    = std::clamp ((luminosity_t)gsl_vector_get (gsl_c.get (), 3),
                                  (luminosity_t)-0.1, fog_range[ch]);
              }
          }
        return sqsum;
      }
  }

  rgbdata
  bw_determine_color_using_data_collection (coord_t *v)
  {
    /* Use double_rgbdata to avoid accumulation of roundoff error.  */
    double_rgbdata red = { 0, 0, 0 }, green = { 0, 0, 0 }, blue = { 0, 0, 0 };
    double_rgbdata color = { 0, 0, 0 };
    luminosity_t threshold = collection_threshold;
    /* Use double to avoid accumulation of roundoff error.  */
    double wr = 0, wg = 0, wb = 0;

    /* This follows same algorithm as data collection in analyze_base.
       We collect data only if screen has intensity greater than zero
       in given channel.  We also make statistics on how much saturation
       this process can lose and reverts that.  */
    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = border; y < theight - border; y++)
        for (int x = border; x < twidth - border; x++)
          if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
            {
              rgbdata m = get_simulated_screen_pixel (tileid, { x, y });
              luminosity_t l = bw_get_pixel (tileid, { x, y });
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
    if (!(wr > 0) || !(wg > 0) || !(wb > 0))
      return { -10, -10, -10 };

    red /= wr;
    green /= wg;
    blue /= wb;
    color.red /= wr;
    color.green /= wg;
    color.blue /= wb;

    double_rgbdata cred = (double_rgbdata){ red.red, green.red, blue.red };
    double_rgbdata cgreen
        = (double_rgbdata){ red.green, green.green, blue.green };
    double_rgbdata cblue = (double_rgbdata){ red.blue, green.blue, blue.blue };
    matrix4x4<double> sat (cred.red, cgreen.red, cblue.red, 0, cred.green,
                           cgreen.green, cblue.green, 0, cred.blue,
                           cgreen.blue, cblue.blue, 0, 0, 0, 0, 1);
    sat = sat.invert ();
    sat.apply_to_rgb (color.red, color.green, color.blue, &color.red,
                      &color.green, &color.blue);
    /* If infrared channel is simulated, negative values may be possible
       and it is kind of hard to constrain to reasonable bounds.
       Still allow values somewhat out of range to account for possible
       over-exposure or cropping  */
    if (!bw_is_simulated_infrared)
      {
        color.red = std::clamp (color.red, (double)-0.1, (double)1.1);
        color.green = std::clamp (color.green, (double)-0.1, (double)1.1);
        color.blue = std::clamp (color.blue, (double)-0.1, (double)1.1);
      }
    return color;
  }

  rgbdata
  bw_determine_color_using_least_squares (coord_t *v)
  {
    int e = 0;
    if (!least_squares_initialized)
      abort ();
    for (int tileid = 0; tileid < n_tiles; tileid++)
      for (int y = border; y < theight - border; y++)
        for (int x = border; x < twidth - border; x++)
          if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
            {
              rgbdata c = get_simulated_screen_pixel (tileid, { x, y });
              gsl_matrix_set (gsl_X.get (), e, 0, c.red);
              gsl_matrix_set (gsl_X.get (), e, 1, c.green);
              gsl_matrix_set (gsl_X.get (), e, 2, c.blue);
              e++;
              // gsl_vector_set (gsl_y[0], e, bw_get_pixel (x, y) / (2 *
              // maxgray));
            }
    if (e != (int)gsl_X->size1)
      abort ();
    double chisq;
    if (gsl_multifit_linear (gsl_X.get (), gsl_y[0].get (), gsl_c.get (),
                             gsl_cov.get (), &chisq, gsl_work.get ()) != GSL_SUCCESS)
      return { -1, -1, -1 };
    rgbdata color
        = { (luminosity_t)gsl_vector_get (gsl_c.get (), 0) * (2 * maxgray),
            (luminosity_t)gsl_vector_get (gsl_c.get (), 1) * (2 * maxgray),
            (luminosity_t)gsl_vector_get (gsl_c.get (), 2) * (2 * maxgray) };
    /* If infrared channel is simulated, negative values may be possible
       and it is kind of hard to constrain to reasonable bounds.
       Still allow values somewhat out of range to account for possible
       over-exposure or cropping  */
    if (!bw_is_simulated_infrared)
      {
        color.red
            = std::clamp (color.red, (luminosity_t)-0.1, (luminosity_t)1.1);
        color.green
            = std::clamp (color.green, (luminosity_t)-0.1, (luminosity_t)1.1);
        color.blue
            = std::clamp (color.blue, (luminosity_t)-0.1, (luminosity_t)1.1);
      }
    return color;
  }

  rgbdata
  get_fog (coord_t *v)
  {
    if (!optimize_fog)
      return { 0, 0, 0 };
    if (fog_by_least_squares)
      return last_fog;
    assert (!colorscreen_checking || fog_index >= 0);
    return { (luminosity_t)v[fog_index] * fog_range.red,
             (luminosity_t)v[fog_index + 1] * fog_range.green,
             (luminosity_t)v[fog_index + 2] * fog_range.blue };
  }

  rgbdata
  get_emulsion_intensities (coord_t *v, int tileid)
  {
    if (optimize_emulsion_intensities)
      {
        /* Together with screen colors these are defined only up to scaling
         * factor.  */
        if (!tileid)
          {
            luminosity_t red = v[emulsion_intensity_index];
            luminosity_t green = v[emulsion_intensity_index + 1];
            luminosity_t blue = 1 - red - green;
            if (blue < 0)
              blue = 0;
            return { red, green, blue };
          }
        return { (luminosity_t)v[emulsion_intensity_index + 3 * tileid - 1],
                 (luminosity_t)v[emulsion_intensity_index + 3 * tileid - 0],
                 (luminosity_t)v[emulsion_intensity_index + 3 * tileid + 1] };
      }
    else
      return { 1, 1, 1 };
  }

  coord_t
  get_sharpen_radius (coord_t *v)
  {
    if (sharpen_index >= 0)
      return expand_range (v[sharpen_index], min_nonone_clen, 10);
    return 0;
  }
  coord_t
  get_sharpen_amount (coord_t *v)
  {
    if (sharpen_index >= 0)
      return v[sharpen_index + 1];
    return 0;
  }

  rgbdata
  get_mix_weights (coord_t *v)
  {
    if (mix_weights_index >= 0)
      {
        if (!least_squares)
          return { (luminosity_t)v[mix_weights_index],
                   (luminosity_t)v[mix_weights_index + 1],
                   1 - (luminosity_t)v[mix_weights_index]
                       - (luminosity_t)v[mix_weights_index + 1] };
        else
          return { (luminosity_t)v[mix_weights_index],
                   (luminosity_t)v[mix_weights_index + 1],
                   (luminosity_t)v[mix_weights_index + 2] };
      }
    double_rgbdata red, green, blue;
    get_colors (v, &red, &green, &blue);
    color_matrix process_colors (red.red, red.green, red.blue, 0, green.red,
                                 green.green, green.blue, 0, blue.red,
                                 blue.green, blue.blue, 0, 0, 0, 0, 1);
    rgbdata ret;
    process_colors.invert ().apply_to_rgb (1, 1, 1, &ret.red, &ret.green,
                                           &ret.blue);

#if 0
    if (simulate_infrared)
      return ret;
#endif
    luminosity_t sum = ret.red + ret.green + ret.blue;
    return ret * ((coord_t)1.0 / (coord_t)sum);
  }

  luminosity_t
  get_mix_dark (coord_t *v)
  {
    if (mix_dark_index >= 0)
      return v[mix_dark_index];
    return 0;
  }

  double_rgbdata
  bw_get_color (coord_t *v)
  {
    if (!least_squares && !data_collection)
      last_color = { v[color_index], v[color_index + 1], v[color_index + 2] };
    if (data_collection)
      last_color = bw_determine_color_using_data_collection (v);
    else
      last_color = bw_determine_color_using_least_squares (v);
    return last_color;
  }
  void
  get_colors (coord_t *v, double_rgbdata *red, double_rgbdata *green,
              double_rgbdata *blue)
  {
    if (!least_squares && !data_collection)
      {
        *red = { v[color_index], v[color_index + 1], v[color_index + 2] };
        *green
            = { v[color_index + 3], v[color_index + 4], v[color_index + 5] };
        *blue = { v[color_index + 6], v[color_index + 7], v[color_index + 8] };
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

  void
  update_transformation (coord_t *v)
  {
    if (optimize_coordinates)
      {
	point_t center = tiles[0].pos[(theight/2) * twidth + twidth/2];
	/* First move center to 0.  */
	matrix3x3 trans = translation_3x3matrix ((center + get_offset (v, 0)) * -1);
	/* Next apply rotation.  */
	trans = trans * rotation_3x3matrix (get_rotation (v));
	/* Next apply scale  */
	trans = trans * scale_3x3matrix (get_scale (v));
	/* Now translate back.  */
	trans = trans * translation_3x3matrix (center);
	transformation = trans;
      }
  }

  /* Objective function to minimize difference between simulated and actual
     scan.  V is vector of parameters.  */
  coord_t
  objfunc (coord_t *v)
  {
    /* Use double to avoid accumulation of round-off error.  */
    double sum = 0;
    update_transformation (v);
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        /* FIXME: parallelism is disabled because sometimes we are called from
         * parallel block.  */
        if (tiles[tileid].sharpened_color
            && tiles[tileid].sharpened_color != tiles[tileid].color.data ())
          sharpen<rgbdata, rgbdata, rgbdata *, int, getdata_helper> (
              tiles[tileid].sharpened_color, tiles[tileid].color.data (), theight,
              twidth, theight, get_sharpen_radius (v), get_sharpen_amount (v),
              nullptr, false);
        bool updated = init_screen (v, tileid);
        simulate_screen (v, tileid, updated);
      }
    double_rgbdata red, green, blue;
    rgbdata color;
    if (!tiles[0].color.empty ())
      get_colors (v, &red, &green, &blue);
    else
      color = bw_get_color (v);
    rgbdata mix_weights;
    luminosity_t mix_dark = 0;
    if (simulate_infrared)
      {
        mix_weights = get_mix_weights (v);
        mix_dark = get_mix_dark (v);
      }
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        if (!tiles[0].color.empty ())
          {
            for (int y = border; y < theight - border; y++)
              for (int x = border; x < twidth - border; x++)
                if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                  {
                    rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x,
                                                y, mix_weights, mix_dark);
                    rgbdata d = get_pixel (v, tileid, { x, y });

                    /* Bayer pattern.
                       TODO: This weighting is specific to Bayer patterns; for
                       general screen plates, a more uniform weighting should
                       be used. We will address this later.  */
                    if (!(x & 1) && !(y & 1))
                      sum += my_fabs (c.red - d.red) * 2;
                    else if ((x & 1) && (y & 1))
                      sum += my_fabs (c.blue - d.blue) * 2;
                    else
                      sum += my_fabs (c.green - d.green);
#if 0
		    sum += my_fabs (c.red - d.red) + my_fabs (c.green - d.green) + my_fabs (c.blue - d.blue);
#endif
                    /*(c.red - d.red) * (c.red - d.red) + (c.green - d.green) *
                     * (c.green - d.green) + (c.blue - d.blue) * (c.blue -
                     * d.blue)*/
                  }
          }
        else if (!tiles[tileid].bw.empty ())
          {
            for (int y = border; y < theight - border; y++)
              for (int x = border; x < twidth - border; x++)
                if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
                  {
                    luminosity_t c
                        = bw_evaluate_pixel (tileid, color, x, y);
                    luminosity_t d = bw_get_pixel (tileid, { x, y });
                    sum += my_fabs (c - d);
                  }
            sum /= maxgray;
          }
      }
    // printf ("%f\n", sum);
    /* Avoid solver from increasing blur past point it is no longer useful.
       Otherwise it will pick solutions with too large blur and very contrasty
       colors.  */
    return (sum / sample_points ())
           * ((coord_t)1
              + get_blur_radius (v)
                    * (coord_t)0.01) /** (1 + get_emulsion_blur_radius (v) * 0.0001)*/;
  }

  void
  collect_screen (screen *s, coord_t *v, int tileid)
  {
    for (int y = 0; y < screen::size; y++)
      for (int x = 0; x < screen::size; x++)
        for (int c = 0; c < 3; c++)
          {
            s->mult[y][x][c] = (luminosity_t)0;
            s->add[y][x][c] = (luminosity_t)0;
          }
    for (int y = border; y < theight - border; y++)
      for (int x = border; x < twidth - border; x++)
        if (!noutliers || !tiles[tileid].outliers->test_bit (x, y))
          {
	    point_t p = get_pos (v, tileid, {x, y});
            int xx = ((int64_t)nearest_int (p.x * screen::size))
                     & (screen::size - 1);
            int yy = ((int64_t)nearest_int (p.y * screen::size))
                     & (screen::size - 1);
            if (!tiles[tileid].color.empty ())
              {
                s->mult[yy][xx][0]
                    = tiles[tileid].sharpened_color[y * twidth + x].red;
                s->mult[yy][xx][1]
                    = tiles[tileid].sharpened_color[y * twidth + x].green;
                s->mult[yy][xx][2]
                    = tiles[tileid].sharpened_color[y * twidth + x].blue;
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
              luminosity_t newv[3]
                  = { (luminosity_t)0, (luminosity_t)0, (luminosity_t)0 };
              int n = 0;
              for (int xo = -1; xo <= 1; xo++)
                for (int yo = -1; yo <= 1; yo++)
                  {
                    int nx = (x + xo) & (screen::size - 1);
                    int ny = (y + yo) & (screen::size - 1);
                    if (s->mult[ny][nx][0] + s->mult[ny][nx][1]
                        + s->mult[ny][nx][2])
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
    double_rgbdata red, green, blue;
    get_colors (v, &red, &green, &blue);
    rgbdata mix_weights = { 0, 0, 0 };
    luminosity_t mix_dark = 0;

    if (simulate_infrared)
      {
        mix_weights = get_mix_weights (v);
        mix_dark = get_mix_dark (v);
      }
    for (int tileid = 0; tileid < n_tiles; tileid++)
      {
        histogram hist;
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rgbdata d = get_pixel (v, tileid, { x, y });
              coord_t err = my_fabs (c.red - d.red) + my_fabs (c.green - d.green)
                            + my_fabs (c.blue - d.blue);
              hist.pre_account (err);
            }
        hist.finalize_range (65535);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rgbdata d = get_pixel (v, tileid, { x, y });
              coord_t err = my_fabs (c.red - d.red) + my_fabs (c.green - d.green)
                            + my_fabs (c.blue - d.blue);
              hist.account (err);
            }
        hist.finalize ();
        coord_t merr = hist.find_max (ratio) * (coord_t)1.3;
        tiles[tileid].outliers = std::make_shared<bitmap_2d> (twidth, theight);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rgbdata d = get_pixel (v, tileid, { x, y });
              coord_t err = my_fabs (c.red - d.red) + my_fabs (c.green - d.green)
                            + my_fabs (c.blue - d.blue);
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
          init_least_squares (nullptr);
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
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              luminosity_t c = bw_evaluate_pixel (tileid, color, x, y);
              luminosity_t d = bw_get_pixel (tileid, { x, y });
              coord_t err = my_fabs (c - d);
              hist.pre_account (err);
            }
        hist.finalize_range (65535);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              luminosity_t c = bw_evaluate_pixel (tileid, color, x, y);
              luminosity_t d = bw_get_pixel (tileid, { x, y });
              coord_t err = my_fabs (c - d);
              hist.account (err);
            }
        hist.finalize ();
        coord_t merr = hist.find_max (ratio) * (coord_t)1.3;
        tiles[tileid].outliers = std::make_shared<bitmap_2d> (twidth, theight);
        for (int y = border; y < theight - border; y++)
          for (int x = border; x < twidth - border; x++)
            {
              luminosity_t c = bw_evaluate_pixel (tileid, color, x, y);
              luminosity_t d = bw_get_pixel (tileid, { x, y });
              coord_t err = my_fabs (c - d);
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
          init_least_squares (nullptr);
      }
    return noutliers;
  }
  std::unique_ptr<simple_image>
  produce_image (coord_t *v, int tileid, int type)
  {
    init_screen (v, tileid);

    std::unique_ptr<simple_image> img = std::make_unique<simple_image> ();
    if (!img || !img->allocate (twidth, theight))
      return img;

    if (!tiles[0].color.empty ())
      {
        luminosity_t rmax = 0, gmax = 0, bmax = 0;
        double_rgbdata red, green, blue;
        rgbdata mix_weights = { 0, 0, 0 };
        luminosity_t mix_dark = 0;
        if (simulate_infrared)
          {
            mix_weights = get_mix_weights (v);
            mix_dark = get_mix_dark (v);
          }
        get_colors (v, &red, &green, &blue);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rmax = std::max (c.red, rmax);
              gmax = std::max (c.green, gmax);
              bmax = std::max (c.blue, bmax);
              rgbdata d = get_pixel (v, tileid, { x, y });
              rmax = std::max (d.red, rmax);
              gmax = std::max (d.green, gmax);
              bmax = std::max (d.blue, bmax);
            }

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      img->put_linear_pixel (
                          x, y,
                          { c.red / rmax, c.green / gmax, c.blue / bmax });
                    }
                    break;
                  case 1:
                    {
                      rgbdata d = get_orig_pixel (v, tileid, { x, y });
                      img->put_linear_pixel (
                          x, y,
                          { d.red / rmax, d.green / gmax, d.blue / bmax });
                    }
                    break;
                  case 2:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      rgbdata d = c - get_pixel (v, tileid, { x, y });
                      img->put_linear_pixel (
                          x, y,
                          { d.red / rmax + (luminosity_t)0.5,
                            d.green / gmax + (luminosity_t)0.5,
                            d.blue / bmax + (luminosity_t)0.5 });
                    }
                    break;
                  case 3:
                    {
                      rgbdata d = get_pixel (v, tileid, { x, y });
                      img->put_linear_pixel (
                          x, y,
                          { d.red / rmax, d.green / gmax, d.blue / bmax });
                    }
                    break;
                  }
              else
                img->put_pixel (x, y, { 0, 0, 0 });
          }
      }
    if (!tiles[tileid].bw.empty ())
      {
        luminosity_t lmax = 0;
        rgbdata color = bw_get_color (v);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              lmax = std::max (bw_evaluate_pixel (tileid, color, x, y),
                               lmax);
              lmax = std::max (bw_get_pixel (tileid, { x, y }), lmax);
            }

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y)
                            / lmax;
                      img->put_linear_pixel (x, y, { c, c, c });
                    }
                    break;
                  case 1:
                    {
                      luminosity_t d = bw_get_pixel (tileid, { x, y }) / lmax;
                      img->put_linear_pixel (x, y, { d, d, d });
                    }
                    break;
                  case 2:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y);
                      luminosity_t d
                          = (c - bw_get_pixel (tileid, { x, y })) / lmax + (luminosity_t)0.5;
                      img->put_linear_pixel (x, y, { d, d, d });
                    }
                    break;
                  }
              else
                img->put_pixel (x, y, { 0, 0, 0 });
          }
      }
    return img;
  }

  bool
  write_file (coord_t *v, const char *name, int tileid, int type)
  {
    init_screen (v, tileid);
    // void *buffer;
    // size_t len = create_linear_srgb_profile (&buffer);

    tiff_writer_params p;
    p.filename = name;
    p.width = twidth;
    // p.icc_profile = buffer;
    // p.icc_profile_len = len;
    p.height = theight;
    p.depth = 16;
    const char *error;
    tiff_writer rendered (p, &error);
    // free (buffer);
    if (error)
      return false;

    if (!tiles[0].color.empty ())
      {
        luminosity_t rmax = 0, gmax = 0, bmax = 0;
        double_rgbdata red, green, blue;
        rgbdata mix_weights = { 0, 0, 0 };
        luminosity_t mix_dark = 0;
        if (simulate_infrared)
          {
            mix_weights = get_mix_weights (v);
            mix_dark = get_mix_dark (v);
          }
        get_colors (v, &red, &green, &blue);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              rgbdata c = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                          mix_weights, mix_dark);
              rmax = std::max (c.red, rmax);
              gmax = std::max (c.green, gmax);
              bmax = std::max (c.blue, bmax);
              rgbdata d = get_pixel (v, tileid, { x, y });
              rmax = std::max (d.red, rmax);
              gmax = std::max (d.green, gmax);
              bmax = std::max (d.blue, bmax);
            }

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      rendered.put_pixel (
                          x, invert_gamma (c.red / rmax, -1) * 65535,
                          invert_gamma (c.green / gmax, -1) * 65535,
                          invert_gamma (c.blue / bmax, -1) * 65535);
                    }
                    break;
                  case 1:
                    {
                      rgbdata d = get_orig_pixel (v, tileid, { x, y });
                      rendered.put_pixel (
                          x, invert_gamma (d.red / rmax, -1) * 65535,
                          invert_gamma (d.green / gmax, -1) * 65535,
                          invert_gamma (d.blue / bmax, -1) * 65535);
                    }
                    break;
                  case 2:
                    {
                      rgbdata c
                          = evaluate_pixel (v, tileid, red, green, blue, x, y,
                                            mix_weights, mix_dark);
                      rgbdata d = get_pixel (v, tileid, { x, y });
                      rendered.put_pixel (
                          x, (c.red - d.red) * (luminosity_t)65535 / rmax + (luminosity_t)32768,
                          (c.green - d.green) * (luminosity_t)65535 / gmax + (luminosity_t)32768,
                          (c.blue - d.blue) * (luminosity_t)65535 / bmax + (luminosity_t)32768);
                    }
                    break;
                  case 3:
                    {
                      rgbdata d = get_pixel (v, tileid, { x, y });
                      rendered.put_pixel (x, d.red * (luminosity_t)65535 / rmax,
                                          d.green * (luminosity_t)65535 / gmax,
                                          d.blue * (luminosity_t)65535 / bmax);
                    }
                    break;
                  }
              else
                rendered.put_pixel (x, 0, 0, 0);
            if (!rendered.write_row ())
              return false;
          }
      }
    if (!tiles[tileid].bw.empty ())
      {
        luminosity_t lmax = 0;
        rgbdata color = bw_get_color (v);
        for (int y = 0; y < theight; y++)
          for (int x = 0; x < twidth; x++)
            {
              lmax = std::max (bw_evaluate_pixel (tileid, color, x, y),
                               lmax);
              lmax = std::max (bw_get_pixel (tileid, { x, y }), lmax);
            }

        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              if (type == 1 || !noutliers
                  || !tiles[tileid].outliers->test_bit (x, y))
                switch (type)
                  {
                  case 0:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y);
                      rendered.put_pixel (x, c * (luminosity_t)65535 / lmax,
                                          c * (luminosity_t)65535 / lmax, c * (luminosity_t)65535 / lmax);
                    }
                    break;
                  case 1:
                    {
                      luminosity_t d = bw_get_pixel (tileid, { x, y });
                      rendered.put_pixel (x, d * (luminosity_t)65535 / lmax,
                                          d * (luminosity_t)65535 / lmax, d * (luminosity_t)65535 / lmax);
                    }
                    break;
                  case 2:
                    {
                      luminosity_t c
                          = bw_evaluate_pixel (tileid, color, x, y);
                      luminosity_t d = bw_get_pixel (tileid, { x, y });
                      rendered.put_pixel (x,
                                          (c - d) * (luminosity_t)65535 / lmax + (luminosity_t)32768,
                                          (c - d) * (luminosity_t)65535 / lmax + (luminosity_t)32768,
                                          (c - d) * (luminosity_t)65535 / lmax + (luminosity_t)32768);
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
  void
  set_results (finetune_result &ret, scr_to_img_parameters param,
               render_parameters &rparam, bool verbose,
               progress_info *progress)
  {
    ret.badness = objfunc (start.data ());
    // if (optimize_screen_blur)
    // ret.screen_blur_radius = start[screen_index];
    /* TODO: Translate back to stitched project coordinates.  */
    ret.tile_pos = { (coord_t)(tiles[0].txmin + twidth / 2),
                     (coord_t)(tiles[0].tymin + theight / 2) };
    ret.red_strip_width = get_red_strip_width (start.data ());
    ret.green_strip_width = get_green_strip_width (start.data ());
    ret.scanner_mtf_sigma = get_scanner_mtf_sigma (start.data ());
    ret.contrast = get_positional_color_contrast (type, last_color);

    if (optimize_coordinates)
      {
        point_t p1_img = {(coord_t)tiles[0].txmin, (coord_t)tiles[0].tymin};
        point_t p1_scr = get_pos (start.data (), 0, {0, 0});
        point_t p2_scr = get_pos (start.data (), 0, {twidth - 1, 0});
        point_t p3_scr = get_pos (start.data (), 0, {0, theight - 1});

        coord_t dx_scr1_x = p2_scr.x - p1_scr.x;
        coord_t dx_scr1_y = p2_scr.y - p1_scr.y;
        coord_t dx_scr2_x = p3_scr.x - p1_scr.x;
        coord_t dx_scr2_y = p3_scr.y - p1_scr.y;
        coord_t det = dx_scr1_x * dx_scr2_y - dx_scr1_y * dx_scr2_x;

        if (det != 0)
          {
            ret.coordinate1.x = (twidth - 1) * dx_scr2_y / det;
            ret.coordinate2.x = -(twidth - 1) * dx_scr2_x / det;
            ret.coordinate1.y = -(theight - 1) * dx_scr1_y / det;
            ret.coordinate2.y = (theight - 1) * dx_scr1_x / det;
            ret.center.x = p1_img.x - ret.coordinate1.x * p1_scr.x - ret.coordinate2.x * p1_scr.y;
            ret.center.y = p1_img.y - ret.coordinate1.y * p1_scr.x - ret.coordinate2.y * p1_scr.y;
#ifdef COLORSCREEN_CHECKING
            {
              scr_to_img_parameters test_p;
              test_p.center = ret.center;
              test_p.coordinate1 = ret.coordinate1;
              test_p.coordinate2 = ret.coordinate2;
              scr_to_img test_map;
              test_map.set_parameters (test_p, 1, 1);
              point_t p1_scr_test = test_map.to_scr (p1_img);
              point_t p2_scr_test = test_map.to_scr ({(coord_t)tiles[0].txmin + twidth - 1, (coord_t)tiles[0].tymin});
              point_t p3_scr_test = test_map.to_scr ({(coord_t)tiles[0].txmin, (coord_t)tiles[0].tymin + theight - 1});
              if (p1_scr_test.dist_from (p1_scr) > 1e-4
                  || p2_scr_test.dist_from (p2_scr) > 1e-4
                  || p3_scr_test.dist_from (p3_scr) > 1e-4)
                {
                  printf ("VERIFICATION FAILED:\n");
                  printf ("  p1: %f %f -> %f %f (should be %f %f)\n", p1_img.x, p1_img.y, p1_scr_test.x, p1_scr_test.y, p1_scr.x, p1_scr.y);
                  printf ("  p2: %f %f -> %f %f (should be %f %f)\n", (coord_t)tiles[0].txmin + twidth - 1, (coord_t)tiles[0].tymin, p2_scr_test.x, p2_scr_test.y, p2_scr.x, p2_scr.y);
                  printf ("  p3: %f %f -> %f %f (should be %f %f)\n", (coord_t)tiles[0].txmin, (coord_t)tiles[0].tymin + theight - 1, p3_scr_test.x, p3_scr_test.y, p3_scr.x, p3_scr.y);
		  printf ("Center %f %f to %f %f; Coordinates %f %f to %f %f; %f %f to %f %f\n", param.center.x, param.center.y, ret.center.x, ret.center.y, param.coordinate1.x, param.coordinate1.y, ret.coordinate1.x, ret.coordinate1.y, param.coordinate2.x, param.coordinate2.y, ret.coordinate2.x, ret.coordinate2.y);
		  abort ();
                }
            }
#endif
          }
        else
          {
            ret.center = param.center;
            ret.coordinate1 = param.coordinate1;
            ret.coordinate2 = param.coordinate2;
          }
      }
    else
      {
        ret.center = param.center;
        ret.coordinate1 = param.coordinate1;
        ret.coordinate2 = param.coordinate2;
      }

    if (optimize_scanner_mtf_defocus || optimize_scanner_mtf_channel_defocus)
      {
        ret.scanner_mtf_defocus = get_scanner_mtf_defocus (start.data ());
        ret.scanner_mtf_blur_diameter = get_scanner_mtf_defocus (start.data ());
      }
    if (optimize_scanner_mtf_channel_defocus)
      ret.scanner_mtf_channel_defocus_or_blur
          = get_scanner_mtf_channel_defocus (start.data ());
    ret.screen_blur_radius = get_blur_radius (start.data ());
    ret.screen_channel_blur_radius = get_channel_blur_radius (start.data ());
    if (!tiles[0].color.empty ())
      {
        double_rgbdata screen_red, screen_green, screen_blue;
        get_colors (start.data (), &screen_red, &screen_green, &screen_blue);
        ret.screen_red = screen_red;
        ret.screen_green = screen_green;
        ret.screen_blue = screen_blue;
      }
    ret.emulsion_blur_radius = get_emulsion_blur_radius (start.data ());
    ret.screen_coord_adjust = get_offset (start.data (), 0);
    ret.emulsion_coord_adjust = get_emulsion_offset (start.data (), 0);
    ret.fog = get_fog (start.data ());
    if (optimize_emulsion_intensities || simulate_infrared)
      {
        ret.mix_weights = get_mix_weights (start.data ());
        ret.mix_dark = get_fog (start.data ());
      }
    else
      {
        ret.mix_weights.red = rparam.mix_red;
        ret.mix_weights.green = rparam.mix_green;
        ret.mix_weights.blue = rparam.mix_blue;
        ret.mix_dark = rparam.mix_dark;
      }

    if (optimize_position && !optimize_coordinate1)
      {
        int tileid = 0;
        /* Construct solver point.  Try to get closest point to the center of
         * analyzed tile.  */
        int fsx = nearest_int (
            get_pos (start.data (), tileid, { twidth / 2, theight / 2 }).x);
        int fsy = nearest_int (
            get_pos (start.data (), tileid, { twidth / 2, theight / 2 }).y);
        int bx = -1, by = -1;
        coord_t bdist = 0;
        for (int y = 0; y < theight; y++)
          {
            for (int x = 0; x < twidth; x++)
              {
                point_t p = get_pos (start.data (), tileid, { x, y });
                // printf ("  %-5.2f,%-5.2f", p.x, p.y);
                coord_t dist = my_fabs (p.x - fsx) + my_fabs (p.y - fsy);
                if (bx < 0 || dist < bdist)
                  {
                    bx = x;
                    by = y;
                    bdist = dist;
                  }
              }
            // printf ("\n");
          }
        if (!bx || bx == twidth - 1 || !by || by == theight - 1)
          {
            if (verbose)
              {
                if (progress)
                  progress->pause_stdout ();
                printf ("Solver point is out of tile\n");
                if (progress)
                  progress->resume_stdout ();
              }
            ret.err = "Solver point is out of tile";
            return;
          }
        point_t fp = { -1000, -1000 };

        bool found = false;
        // printf ("%i %i %i %i %f %f\n", bx, by, fsx, fsy,
        // tile_pos[twidth/2+(theight/2)*twidth].x,
        // tile_pos[twidth/2+(theight/2)*twidth].y);
        for (int y = by - 1; y <= by + 1; y++)
          for (int x = bx - 1; x <= bx + 1; x++)
            {
              /* Determine cell corners.  */
              point_t p = { (coord_t)fsx, (coord_t)fsy };
              point_t p1 = get_pos (start.data (), tileid, { x, y });
              point_t p2 = get_pos (start.data (), tileid, { x + 1, y });
              point_t p3 = get_pos (start.data (), tileid, { x, y + 1 });
              point_t p4 = get_pos (start.data (), tileid, { x + 1, y + 1 });
              /* Check if point is above or below diagonal.  */
              coord_t sgn1 = sign (p, p1, p4);
              if (sgn1 > 0)
                {
                  /* Check if point is inside of the triangle.  */
                  if (sign (p, p4, p3) < 0 || sign (p, p3, p1) < 0)
                    continue;
                  coord_t rx, ry;
                  intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p3.x,
                                     p3.y, p4.x - p3.x, p4.y - p3.y, &rx, &ry);
                  rx = (coord_t)1 / rx;
                  found = true;
                  fp = { (ry * rx + x), (rx + y) };
                }
              else
                {
                  /* Check if point is inside of the triangle.  */
                  if (sign (p, p4, p2) > 0 || sign (p, p2, p1) > 0)
                    continue;
                  coord_t rx, ry;
                  intersect_vectors (p1.x, p1.y, p.x - p1.x, p.y - p1.y, p2.x,
                                     p2.y, p4.x - p2.x, p4.y - p2.y, &rx, &ry);
                  rx = (coord_t)1 / rx;
                  found = true;
                  fp = { (rx + x), (ry * rx + y) };
                }
            }
        /* TODO: If we did not find the tile we could try some non-integer
           location.  */
        if (!found)
          {
            if (verbose)
              {
                if (progress)
                  progress->pause_stdout ();
                printf ("Failed to find solver point\n");
                if (progress)
                  progress->resume_stdout ();
              }
            ret.err = "Failed to find solver point";
            return;
          }
        ret.solver_point_img_location = { fp.x + tiles[tileid].txmin + (coord_t)0.5,
                                          fp.y + tiles[tileid].tymin + (coord_t)0.5 };
        // printf ("New location %f %f %f %f  %f %f\n", fp.x +
        // tiles[tileid].txmin
        // + 0.5, fp.y + tiles[tileid].tymin + 0.5, bx + tiles[tileid].txmin +
        // 0.5, by + tiles[tileid].tymin + 0.05, get_pos (start, tileid, {bx,
        // by}).x, get_pos (start, tileid, {bx, by}).y);
        ret.solver_point_screen_location.x = (coord_t)fsx;
        ret.solver_point_screen_location.y = (coord_t)fsy;
        ret.solver_point_color = solver_parameters::green;
      }
    ret.success = true;
  }
};
} // namespace

/* Finetune parameters and update RPARAM.
   PARAM is scr-to-img parameters.  IMG is the image data.
   LOCS are tile locations.  RESULTS are previous results.
   FPARAMS are finetune parameters.  PROGRESS is used to report progress.  */

finetune_result
finetune (render_parameters &rparam, const scr_to_img_parameters &param,
          const image_data &img, const std::vector<point_t> &locs,
          const std::vector<finetune_result> *results,
          const finetune_parameters &fparams, progress_info *progress)
{
  finetune_result ret;
  bool tile_sharpened = false;

  int n_tiles = locs.size ();
  if (fparams.flags & finetune_coordinates)
    {
      if (n_tiles)
        {
          ret.err = "did not expect tiles";
          return ret;
        }
      n_tiles = 1;
    }
  else
    {
      if (n_tiles > finetune_solver::max_tiles)
        n_tiles = finetune_solver::max_tiles;
      if (!n_tiles)
        {
          ret.err = "no tile locations";
          return ret;
        }
    }
  const image_data *imgp[finetune_solver::max_tiles];
  scr_to_img *mapp[finetune_solver::max_tiles];
  int x[finetune_solver::max_tiles];
  int y[finetune_solver::max_tiles];
  coord_t pixel_size = -1;

  scr_to_img map;
  imgp[0] = nullptr;
  mapp[0] = nullptr;
  x[0] = 0;
  y[0] = 0;
  for (int tileid = 0; tileid < n_tiles; tileid++)
    {
      if (fparams.flags & finetune_coordinates)
        {
          x[tileid] = param.center.x;
          y[tileid] = param.center.y;
        }
      else
        {
          x[tileid] = locs[tileid].x;
          y[tileid] = locs[tileid].y;
        }
      imgp[tileid] = &img;
      if (img.stitch)
        {
          int tx, ty;
          point_t scr = img.stitch->common_scr_to_img.final_to_scr (
              { (coord_t)(x[tileid] + img.xmin),
                (coord_t)(y[tileid] + img.ymin) });
          pixel_size = img.stitch->pixel_size;
          if (!img.stitch->tile_for_scr (&rparam, scr.x, scr.y, &tx, &ty,
                                         true))
            {
              ret.err = "no tile for given coordinates";
              return ret;
            }
          point_t p = img.stitch->images[ty][tx].common_scr_to_img (scr);
          x[tileid] = nearest_int (p.x);
          y[tileid] = nearest_int (p.y);
          imgp[tileid] = img.stitch->images[ty][tx].img.get ();
          mapp[tileid] = &img.stitch->images[ty][tx].scr_to_img_map;
        }
      else
        {
          if (!tileid)
            {
              if (!map.set_parameters (param, *imgp[tileid]))
	        {
		  ret.err = "failed to convert screen to image coordinates";
		  return ret;
	        }
              pixel_size
                  = map.pixel_size (imgp[tileid]->width, imgp[tileid]->height);
            }
          mapp[tileid] = &map;
        }
    }
  bool bw = fparams.flags & finetune_bw;
  bool verbose = fparams.flags & finetune_verbose;

  if (!bw && !imgp[0]->has_rgb ())
    bw = true;

  /* Determine tile to analyze.  */
  point_t tp = mapp[0]->to_scr ({ (coord_t)x[0], (coord_t)y[0] });
  int sx = nearest_int (tp.x);
  int sy = nearest_int (tp.y);

  coord_t def_xrange = 2;
  /* When not normalizing we want to avoid image in the tile.  */
  if ((fparams.flags & finetune_no_normalize) || bw)
    def_xrange = 1;
  if (fparams.flags & finetune_coordinates)
    def_xrange = 3;

  coord_t test_xrange = fparams.range ? fparams.range : def_xrange;
  coord_t test_yrange = test_xrange;

  /* If screen tile is far from rectangular, compensate.
     Also screen with strips has too few elements, so
     finetuning is not very stressed to pick reasonable solution.  */
  if (screen_with_vertical_strips_p (param.type))
    test_yrange *= 3;
  int iterations = 0;
  int txmin, txmax, tymin, tymax;
  do
    {
      if (iterations)
	test_xrange++, test_yrange++;
      point_t p = mapp[0]->to_img ({ (coord_t)sx, (coord_t)sy });
      coord_t sxmin = p.x, sxmax = p.x, symin = p.y, symax = p.y;
      p = mapp[0]->to_img ({ sx - test_xrange, sy - test_yrange });
      sxmin = std::min (sxmin, p.x);
      sxmax = std::max (sxmax, p.x);
      symin = std::min (symin, p.y);
      symax = std::max (symax, p.y);
      p = mapp[0]->to_img ({ sx + test_xrange, sy - test_yrange });
      sxmin = std::min (sxmin, p.x);
      sxmax = std::max (sxmax, p.x);
      symin = std::min (symin, p.y);
      symax = std::max (symax, p.y);
      p = mapp[0]->to_img ({ sx + test_xrange, sy + test_yrange });
      sxmin = std::min (sxmin, p.x);
      sxmax = std::max (sxmax, p.x);
      symin = std::min (symin, p.y);
      symax = std::max (symax, p.y);
      p = mapp[0]->to_img ({ sx - test_xrange, sy + test_yrange });
      sxmin = std::min (sxmin, p.x);
      sxmax = std::max (sxmax, p.x);
      symin = std::min (symin, p.y);
      symax = std::max (symax, p.y);

      txmin = my_floor (sxmin);
      tymin = my_floor (symin);
      txmax = my_ceil (sxmax);
      tymax = my_ceil (symax);
      if (txmin < 0)
	txmin = 0;
      if (txmax > imgp[0]->width)
	txmax = imgp[0]->width;
      if (tymin < 0)
	tymin = 0;
      if (tymax > imgp[0]->height)
	tymax = imgp[0]->height;
      iterations++;
    }
  while (iterations < 10 && (txmin + 10 > txmax || tymin + 10 > tymax));

  if (txmin + 10 > txmax || tymin + 10 > tymax)
    {
      if (verbose)
	{
	  if (progress)
	    progress->pause_stdout ();
	  fprintf (stderr, "Too small tile %i-%i %i-%i\n", txmin, txmax, tymin,
		   tymax);
	  if (progress)
	    progress->resume_stdout ();
	}
      ret.err = "too small tile";
      return ret;
    }
  int twidth = txmax - txmin + 1, theight = tymax - tymin + 1;
  if (verbose)
    {
      if (progress)
        progress->pause_stdout ();
      fprintf (stderr, "Tile size %ix%i; %i tiles\n", twidth, theight,
               n_tiles);
      if (progress)
        progress->resume_stdout ();
    }
  finetune_solver best_solver;
  coord_t best_uncertainty = -1;
  bool failed = false;

  render_parameters rparam2 = rparam;
  /* Working with sharpened tile is easier, since it is likely already
     used by renderers.  But if we are finetuning sharpening, we must
     use unsharpened data, so the parameters can be adjusted.
     TODO: It is not clear if sharpening does decrease quality of the
     simulation.  */
  if (bw)
    {
      if (fparams.flags
          & (finetune_screen_blur | finetune_screen_channel_blurs
             | finetune_scanner_mtf_sigma | finetune_scanner_mtf_defocus))
        rparam2.sharpen.mode = sharpen_parameters::none;
      else
        tile_sharpened = true;
    }

  /* TODO: shall we reset contact copy?  */
  // rparam2.invert = 0;
  int maxtiles = fparams.multitile;
  if (maxtiles < 1)
    maxtiles = 1;
  if (!(maxtiles & 1))
    maxtiles++;

  bool bw_is_simulated_infrared = false;

  /* Multitile support only for 1 tile.  */
  if (n_tiles == 1
      /* Avoid openmp when we do not need it.  This seems also necessary to get
         Windows builds working.  */
      && (maxtiles > 1 && !(fparams.flags & finetune_no_progress_report)))
    {
      ///* FIXME: Hack; render is too large for stack in openmp thread.  */
      // std::unique_ptr<render_to_scr> rp(new render_to_scr (param, img,
      // rparam, 256));
      render render (*imgp[0], rparam2, 256);
#if 0
      if (maxtiles > 1)
	{
	  rxmin = std::max (txmin - twidth * (maxtiles / 2), 0);
	  rymin = std::max (tymin - theight * (maxtiles / 2), 0);
	  rxmax = std::max (txmax + (twidth * maxtiles / 2), imgp[0]->width - 1);
	  rymax = std::max (tymax + (theight * maxtiles / 2), imgp[0]->height - 1);
	}
      //if (!render.precompute_img_range (bw /*grayscale*/, false /*normalized*/, rxmin, rymin, rxmax + 1, rymax + 1, !(fparams.flags & finetune_no_progress_report) ? progress : nullptr))
#endif
      if (bw && (rparam2.ignore_infrared || !imgp[0]->has_grayscale_or_ir ()))
        bw_is_simulated_infrared = true;
      if (!render.precompute_all (
              bw /*grayscale*/, false /*normalized*/,
              patch_proportions (param.type, &rparam2),
              !(fparams.flags & finetune_no_progress_report) ? progress
                                                             : nullptr))
        {
          if (verbose)
            {
              if (progress)
                progress->pause_stdout ();
              fprintf (stderr, "Precomputing failed. Tile: %i-%i %i-%i\n",
                       txmin, txmax, tymin, tymax);
              if (progress)
                progress->resume_stdout ();
            }
          ret.err = "precomputing failed";
          return ret;
        }
      if (progress && progress->cancel_requested ())
        {
          ret.err = "cancelled";
          return ret;
        }

      if (maxtiles * maxtiles > 1
          && !(fparams.flags & finetune_no_progress_report) && progress)
        progress->set_task ("finetuning samples", maxtiles * maxtiles);

      gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
    shared(fparams, maxtiles, rparam, pixel_size, best_uncertainty, verbose,  \
               std::nothrow, imgp, twidth, theight, txmin, tymin, bw,         \
               progress, mapp, render, failed, best_solver, results,          \
               bw_is_simulated_infrared, tile_sharpened)
      for (int ty = 0; ty < maxtiles; ty++)
        for (int tx = 0; tx < maxtiles; tx++)
          {
            int cur_txmin = std::min (std::max (txmin - twidth * (maxtiles / 2)
                                                    + tx * twidth,
                                                0),
                                      imgp[0]->width - twidth - 1)
                            & ~1;
            int cur_tymin
                = std::min (std::max (tymin - theight * (maxtiles / 2)
                                          + ty * theight,
                                      0),
                            imgp[0]->height - theight - 1)
                  & ~1;
            // int cur_txmax = cur_txmin + twidth;
            // int cur_tymax = cur_tymin + theight;
            finetune_solver solver;
            solver.n_tiles = 1;
            solver.twidth = twidth;
            solver.theight = theight;
            solver.pixel_size = pixel_size;
            solver.render_sharpen_params = rparam.sharpen;
            solver.collection_threshold = rparam.collection_threshold;
            solver.parallel = !(fparams.flags & finetune_no_progress_report);
            if (!solver.init_tile (0, cur_txmin, cur_tymin, bw, *mapp[0],
                                   render))
              {
                failed = true;
                continue;
              }
            solver.init (fparams.flags, rparam.screen_blur_radius,
                         rparam.red_strip_width, rparam.green_strip_width,
                         bw_is_simulated_infrared, tile_sharpened, results);
            if (progress && progress->cancel_requested ())
              continue;
            coord_t uncertainty = solver.solve (
                progress, !(fparams.flags & finetune_no_progress_report)
                              && maxtiles == 1);

            if (maxtiles * maxtiles > 1
                && !(fparams.flags & finetune_no_progress_report) && progress)
              progress->inc_progress ();
#pragma omp critical
            {
              if (best_uncertainty < 0 || best_uncertainty > uncertainty)
                {
                  best_solver = std::move (solver);
                  best_uncertainty = uncertainty;
                }
            }
          }
      gsl_set_error_handler (old_handler);
    }
  else
    {
      best_solver.n_tiles = n_tiles;
      best_solver.twidth = twidth;
      best_solver.theight = theight;
      best_solver.collection_threshold = rparam.collection_threshold;
      best_solver.render_sharpen_params = rparam.sharpen;
      best_solver.pixel_size = pixel_size;
      best_solver.parallel = !(fparams.flags & finetune_no_progress_report);
      for (int tileid = 0; tileid < n_tiles; tileid++)
        {
          int cur_txmin = std::min (std::max (x[tileid] - twidth / 2, 0),
                                    imgp[tileid]->width - twidth - 1)
                          & ~1;
          int cur_tymin = std::min (std::max (y[tileid] - theight / 2, 0),
                                    imgp[tileid]->height - theight - 1)
                          & ~1;
          if (bw && (rparam2.ignore_infrared || !imgp[tileid]->has_grayscale_or_ir ()))
            bw_is_simulated_infrared = true;
          /* FIXME: We only use render_to_scr since we eventually want to know
             pixel size. For stitched projects this is wrong.  */
          render render (*imgp[tileid], rparam2, 256);
          if (!render.precompute_all (
                  bw /*grayscale*/, false /*normalized*/,
                  patch_proportions (param.type, &rparam2),
                  !(fparams.flags & finetune_no_progress_report) ? progress
                                                                 : nullptr))
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
          if (!best_solver.init_tile (tileid, cur_txmin, cur_tymin, bw,
                                      *mapp[tileid], render))
            {
              ret.err = "out of memory";
              return ret;
            }
        }
      best_solver.init (fparams.flags, rparam.screen_blur_radius,
                        rparam.red_strip_width, rparam.green_strip_width,
                        bw_is_simulated_infrared, tile_sharpened, results);
      /* FIXME: For parallel solving this will yield race condition  */
      gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
      best_uncertainty = best_solver.solve (
          progress, !(fparams.flags & finetune_no_progress_report));
      gsl_set_error_handler (old_handler);
    }
  if (progress && progress->cancel_requested ())
    {
      ret.err = "cancelled";
      return ret;
    }
  if (failed)
    {
      ret.err = "failed memory allocation";
      return ret;
    }
  if (best_uncertainty < 0)
    {
      ret.err = "negative uncertainty";
      return ret;
    }

  if (best_solver.least_squares)
    {
      best_solver.alloc_least_squares ();
      if (!best_solver.optimize_fog || best_solver.fog_by_least_squares)
        best_solver.init_least_squares (best_solver.start.data ());
    }
  if (!best_solver.tiles[0].color.empty () && fparams.ignore_outliers > 0)
    best_solver.determine_outliers (best_solver.start.data (),
                                    fparams.ignore_outliers);
  else if (fparams.ignore_outliers > 0)
    best_solver.bw_determine_outliers (best_solver.start.data (),
                                       fparams.ignore_outliers);
  if (best_solver.has_outliers ())
    simplex<coord_t, finetune_solver> (
        best_solver, "finetuning with outliers", progress,
        !(fparams.flags & finetune_no_progress_report));
  if (progress && progress->cancel_requested ())
    {
      ret.err = "cancelled";
      return ret;
    }

  ret.uncertainty = best_uncertainty;
  if (verbose)
    {
      if (progress)
        progress->pause_stdout ();
      best_solver.print_values (best_solver.start.data ());
      if (progress)
        progress->resume_stdout ();
    }
  best_solver.set_results (ret, param, rparam, verbose, progress);

  if (fparams.simulated_file)
    best_solver.write_file (best_solver.start.data (), fparams.simulated_file, 0, 0);
  if (fparams.orig_file)
    best_solver.write_file (best_solver.start.data (), fparams.orig_file, 0, 1);
  if (fparams.sharpened_file)
    best_solver.write_file (best_solver.start.data (), fparams.sharpened_file, 0, 3);
  if (fparams.diff_file)
    best_solver.write_file (best_solver.start.data (), fparams.diff_file, 0, 2);
  if (fparams.flags & finetune_produce_images)
    {
      ret.simulated = best_solver.produce_image (best_solver.start.data (), 0, 0);
      ret.orig = best_solver.produce_image (best_solver.start.data (), 0, 1);
      if (best_solver.optimize_sharpening)
        ret.sharpened = best_solver.produce_image (best_solver.start.data (), 0, 3);
      ret.diff = best_solver.produce_image (best_solver.start.data (), 0, 2);
      if (fparams.screen_blur_file)
        {
          screen tmp;
          best_solver.collect_screen (&tmp, best_solver.start.data (), 0);
          screen scr, scr1;
          scr.initialize (best_solver.type,
                          best_solver.get_red_strip_width (best_solver.start.data ()),
                          best_solver.get_green_strip_width (best_solver.start.data ()));
          best_solver.apply_blur (best_solver.start.data (), 0, &scr, &scr1);
          ret.dot_spread = scr.get_image (true, 1);
        }
      ret.screen = best_solver.original_scr->get_image ();
      ret.blurred_screen = best_solver.tiles[0].scr->get_image ();
      if (best_solver.optimize_emulsion_blur)
        ret.emulsion_screen = best_solver.emulsion_scr->get_image ();
      if (best_solver.optimize_emulsion_intensities)
        ret.merged_screen = best_solver.tiles[0].merged_scr->get_image ();

      screen tmp;
      best_solver.collect_screen (&tmp, best_solver.start.data (), 0);
      ret.collected_screen = tmp.get_image ();

      screen scr, scr1;
      scr1.initialize_dot ();
      best_solver.apply_blur (best_solver.start.data (), 0, &scr, &scr1);
      ret.dot_spread = scr.get_image (true, 1);
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
      best_solver.collect_screen (&tmp, best_solver.start.data (), 0);
      tmp.save_tiff (fparams.collected_file);
    }
  if (fparams.dot_spread_file)
    {
      screen scr, scr1;
      scr1.initialize_dot ();
      best_solver.apply_blur (best_solver.start.data (), 0, &scr, &scr1);
      scr.save_tiff (fparams.dot_spread_file, true, 1);
    }
  // printf ("%i %i %i %i %f %f %f %f\n", bx, by, fsx, fsy,
  // best_solver.tile_pos[twidth/2+(theight/2)*twidth].x,
  // best_solver.tile_pos[twidth/2+(theight/2)*twidth].y, fp.x, fp.y);
  return ret;
}

/* Finetune SOLVER parameters in given AREA using RPARAM and PARAM in IMG.
   PROGRESS is used to report progress.  
   Assume the registration is correct only in existing points in AREA.
   Start from these and try to carefully insert new points.  */

DLL_PUBLIC bool
finetune_misregistered_area (solver_parameters *solver,
                             render_parameters &rparam,
                             const scr_to_img_parameters &param,
                             const image_data &img,
                             const int_image_area &in_area,
                             const finetune_area_parameters &fparam,
                             progress_info *progress)
{
  int_image_area area = in_area.intersect ({ 0, 0, img.width, img.height });
  const bool verbose = true;
  if (area.empty_p () || param.type == Random)
    {
      if (verbose)
        printf ("Finetuning area failed since area is empty or screen is "
                "Random\n");
      return false;
    }
  int xsteps, ysteps;
  fparam.get_grid_dimensions (area, param, &xsteps, &ysteps);
  if (!xsteps || !ysteps)
    {
      if (verbose)
        printf ("Finetuning area failed since xsteps or ysteps is 0\n");
      return false;
    }
  int_image_area crop = rparam.get_scan_crop (img.width, img.height);
  int xstep = std::max (1, crop.width / xsteps);
  int ystep = std::max (1, crop.height / ysteps);
  int max_points = 10000;

  if (!solver->points.size ())
    {
      /* If registration seem to make sense, try to expand it.  */
      if (!area.contains_p ({ (int)param.center.x, (int)param.center.y })
          || param.coordinate1.length () < 3
          || param.coordinate2.length () < 3)
        {
          if (verbose)
            printf ("Finetuning area failed since there are no solver points "
                    "and coordinate system starts elsewhere\n");
          return false;
        }
      finetune_parameters fparam;
      fparam.flags |= finetune_position /*| finetune_multitile*/ | finetune_bw
                      | finetune_no_progress_report;
      finetune_result res = finetune (rparam, param, img, { param.center },
                                      nullptr, fparam, progress);
      finetune_result res2
          = finetune (rparam, param, img, { param.center + param.coordinate1 },
                      nullptr, fparam, progress);
      finetune_result res3
          = finetune (rparam, param, img, { param.center + param.coordinate2 },
                      nullptr, fparam, progress);
      if (!res.success || !res2.success || !res3.success)
        {
          if (verbose)
            printf ("Finetuning area failed since we failed to identify 3 "
                    "basis points\n");
          return false;
        }
      solver->add_point (res.solver_point_img_location,
                         res.solver_point_screen_location,
                         res.solver_point_color);
      solver->add_or_modify_point (res2.solver_point_img_location,
                                   res2.solver_point_screen_location,
                                   res2.solver_point_color);
      solver->add_or_modify_point (res3.solver_point_img_location,
                                   res3.solver_point_screen_location,
                                   res3.solver_point_color);
      max_points = 50;
      xstep = my_ceil (
          std::max (fabs (param.coordinate1.x), fabs (param.coordinate2.x)));
      ystep = my_ceil (
          std::max (fabs (param.coordinate1.y), fabs (param.coordinate2.y)));
      if (verbose)
        printf ("Finetuning area started by adding basis, steps %i %i\n",
                xstep, ystep);
    }
  /* See if points are corelated around a line.  In this case the current
     solution is probably quite iffy and we need to expand slowly.  */
  else if (solver->points.size () < 10000)
    {
      point_t origin, dir;
      double line_width = solver->fit_line (origin, dir);
      printf ("line width: %f\n", line_width);
      const int ratio = 5;
      if (xstep > line_width / ratio)
        {
          xstep = my_ceil ((line_width) / ratio);
          max_points = 50;
        }
      if (ystep > line_width / ratio)
        {
          ystep = my_ceil ((line_width) / ratio);
          max_points = 50;
        }
    }

  /* Too small step will lead to re-solving existing points only.  */
  xstep = std::max (xstep,
		    (int)my_ceil (std::max (fabs (param.coordinate1.x),
					    fabs (param.coordinate2.x)) * 2));
  ystep = std::max (ystep,
		    (int)my_ceil (std::max (fabs (param.coordinate1.y),
					    fabs (param.coordinate2.y)) * 2));

  const int range = 3;
  int xsubstep = std::max (1, xstep / range);
  int ysubstep = std::max (1, ystep / range);
  int xsubsteps = std::max (1, (area.width + xsubstep - 1) / xsubstep);
  int ysubsteps = std::max (1, (area.height + ysubstep - 1) / ysubstep);
  int npoints;
  int nfound = 0;
  coord_t max_uncertainty = 10000;

  enum elt
  {
    unknown,
    known,
    to_be_computed,
    bad
  };

  std::vector<elt> tiles (xsubsteps * ysubsteps, unknown);

  const auto get_cell_pos = [area,xsubstep,ysubstep] (point_t p) -> int_point_t
  {
    return {(int64_t)((p.x - area.x) / (coord_t)xsubstep),
	    (int64_t)((p.y - area.y) / (coord_t)ysubstep)};
  };
  const auto get_cellp = [get_cell_pos] (int_point_t p) -> int_point_t
  {
    return get_cell_pos ((point_t){(coord_t)p.x, (coord_t)p.y});
  };
  const auto in_range = [xsubsteps,ysubsteps] (int_point_t p) -> bool
  {
    return p.x >= 0 && p.x < xsubsteps && p.y >= 0 && p.y < ysubsteps;
  };
  const auto set_cell = [in_range,&tiles,xsubsteps] (int_point_t p, enum elt value) -> void
  {
    if (in_range (p))
      tiles[p.y * xsubsteps + p.x] = value;
  };
  const auto get_cell = [in_range,&tiles,xsubsteps] (int_point_t p) -> elt
  {
    assert (in_range (p));
    return tiles[p.y * xsubsteps + p.x];
  };

  if (verbose)
    printf ("Adding points to area with top left (%i,%i) width %i height %i, "
            "steps %i %i size %i %i with known points %i\n",
            area.x, area.y, area.width, area.height, xsubsteps, ysubsteps,
            xsubstep, ysubstep, (int)solver->points.size ());
  for (auto p : solver->points)
    set_cell (get_cell_pos (p.img), known);

  /* This is essentialy an floodfill.  If there is 3x3 tile
     with no known data such that just outside of it exists
     known point; enqueue its center for finetuning.  */
  do
    {

      std::vector<int_point_t> points;
      for (int y = range; y < ysubsteps - range; y++)
        for (int x = range; x < xsubsteps - range; x++)
          {
            bool ok = true;
            /* If range is 3 we search

               b b b b b b b
               b . . . . . p
               b . . . . . p
               b . . p . . p
               b . . . . . p
               b . . . . . p
               b b b b b b b

               p is the tile we consider to compute points n.
               . is required to have no control point
               b is required to have at least one control point.

               So at the end, the computed points should approximately
               make grid with spacing of 3

               p . . p . . p
               . . . . . . .
               . . . . . . .
               p . . p . . p
               . . . . . . .
               . . . . . . .
               p . . p . . p */
            for (int yy = y - range + 1; yy <= y + range - 1 && ok; yy++)
              for (int xx = x - range + 1; xx <= x + range - 1 && ok; xx++)
                if (get_cell ({xx, yy}) != unknown)
                  ok = false;
            if (!ok)
              continue;
            int nknown = 0;
            for (int yy = y - range; yy <= y + range; yy++)
              for (int xx = x - range; xx <= x + range; xx++)
                if (get_cell ({xx, yy}) == known)
                  nknown++;
            if (!nknown)
              continue;
	    set_cell ({x, y}, to_be_computed);
            points.push_back ({ nearest_int ((x + 0.5) * xsubstep) + area.x,
                                nearest_int ((y + 0.5) * ysubstep) + area.y });
	    assert ((get_cellp (points.back ()) == (int_point_t){x,y}));
            if (verbose && 0)
              printf ("Will compute %i %i\n", x, y);
          }
      if (!points.size ())
        break;
      if (progress)
        progress->set_task ("finetuning points nearby known points",
                            points.size ());
      std::vector<finetune_result> res (points.size ());
      /* We are going to initialize render inside of nested region.
         TODO: We probably want to set omp_nested on proper place.  */
#ifdef _OPENMP
      omp_set_max_active_levels (3);
#endif
#pragma omp parallel for default(none) schedule(dynamic)                      \
    shared(rparam, param, progress, img, solver, res, points)
      for (size_t i = 0; i < points.size (); i++)
        {
          if (progress && progress->cancel_requested ())
            continue;
          finetune_parameters fparam;
          fparam.flags
              |= finetune_position /*| finetune_multitile*/ | finetune_bw
                 | finetune_no_progress_report;
          res[i]
              = finetune (rparam, param, img,
                          { { (coord_t)points[i].x, (coord_t)points[i].y } },
                          nullptr, fparam, progress);
          if (progress)
            progress->inc_progress ();
        }
      if (progress && progress->cancel_requested ())
        {
          if (verbose)
            printf ("Finetuning area cancelled\n");
          return false;
        }

      scr_to_img map;
      if (!map.set_parameters (param, img))
        {
          if (verbose)
            printf ("Finetuning area failed since it failed to initialize "
                    "scr-to-img\n");
          return false;
        }

      /* Clear info about points to be computed.  */
      for (int i = 0; i < xsubsteps * ysubsteps; i++)
        if (tiles[i] == to_be_computed)
          tiles[i] = unknown;
      npoints = 0;
      if (verbose)
        printf ("Will consider %i results\n", (int)res.size ());
      /* Now prune points that seems badr.  */
      for (size_t i = 0; i < res.size ();)
        {
          finetune_result &r = res[i];
          bool ok = r.success;
          if (ok)
            {
	      int_point_t cell = get_cell_pos (r.solver_point_img_location);
	      /* Point must be new.  */
              if (solver->find_point (r.solver_point_screen_location) >= 0)
                {
                  if (verbose)
                    printf ("found grid: %f %f which already exists\n",
                            r.solver_point_img_location.x,
                            r.solver_point_img_location.y);
		  set_cell (cell, known);
                  ok = false;
                }
	      /* Check contrast to be within threshold.  */
	      else if (r.contrast < fparam.min_contrast)
	        {
                  if (verbose)
                    printf ("found grid: %f %f with too small contrast %f\n",
                            r.solver_point_img_location.x,
                            r.solver_point_img_location.y,
			    r.contrast);
		  set_cell (cell, bad);
                  ok = false;
	        }
	      /* Check distance threshold.  */
              else
                {
                  point_t transformed
                      = map.to_scr (r.solver_point_img_location);
                  ok = transformed.dist_from (r.solver_point_screen_location)
                       < fparam.max_displacement;
		  if (!ok)
		    set_cell (cell, bad);
                  if (verbose)
                    printf (
                        "found grid: %i %i transformed: %f %f finetuned: %f "
                        "%f displacement %f %s\n",
                        (int)cell.x, (int)cell.y, transformed.x, transformed.y,
                        r.solver_point_screen_location.x,
                        r.solver_point_screen_location.y,
                        transformed.dist_from (r.solver_point_screen_location),
                        ok ? "in threshold" : "out of threshold");
                }
            }
          if (!ok)
            {
              res[i] = std::move (res.back ());
              res.pop_back ();
            }
          else
            i++;
        }
      if (verbose)
        printf ("Will consider %i meaningful results\n", (int)res.size ());
      /* If we have many points; rule out uncertain ones.  Let the value only
         drop in each wave.  */
      if (res.size () > 5)
        {
          std::sort (res.begin (), res.end (),
                     [] (finetune_result &a, finetune_result &b)
                       { return a.uncertainty > b.uncertainty; });
          max_uncertainty = std::min (
              max_uncertainty,
              res[(res.size () - 1) * (1 - fparam.uncertainty_ratio)]
                  .uncertainty);
        }

      /* Now add computed points to solver and update tiles.  */
      for (size_t i = 0; i < res.size (); i++)
        {
          finetune_result &r = res[i];
	  int_point_t cell = get_cell_pos (r.solver_point_img_location);
          if (r.uncertainty <= max_uncertainty
	      && solver->find_point (r.solver_point_screen_location) < 0)
            {
              solver->add_point (r.solver_point_img_location,
                                 r.solver_point_screen_location,
                                 r.solver_point_color);
	      set_cell (cell, known);
              nfound++;
              npoints++;
              if (nfound > max_points)
                {
                  if (verbose)
                    printf ("reached max points of %i\n", max_points);
                  return true;
                }
            }
	  else
	    set_cell (cell, bad);
        }
    }
  while (npoints);
  if (verbose)
    printf ("found %i points\n", nfound);
  return true;
}

/* Finetune SOLVER parameters in given AREA using RPARAM and PARAM in IMG.
   PROGRESS is used to report progress.  */

DLL_PUBLIC bool
finetune_area (solver_parameters *solver, render_parameters &rparam,
               const scr_to_img_parameters &param, const image_data &img,
               const int_image_area &in_area,
	       const finetune_area_parameters &fparam,
	       progress_info *progress)
{
  int_image_area area = in_area.intersect ({ 0, 0, img.width, img.height });
  if (area.empty_p ())
    return false;
  int xsteps, ysteps;
  fparam.get_grid_dimensions (area, param, &xsteps, &ysteps);
  if (!xsteps || !ysteps)
    return false;
  int_image_area crop = rparam.get_scan_crop (img.width, img.height);
  int xstep = std::max (1, crop.width / xsteps);
  int ystep = std::max (1, crop.height / ysteps);
  xsteps = (area.width + xstep - 1) / xstep;
  ysteps = (area.height + ystep - 1) / ystep;
  if (!xsteps || !ysteps)
    return false;
  std::vector<finetune_result> res (xsteps * ysteps);
  if (progress)
    progress->set_task ("finetuning grid", ysteps * xsteps);
  /* We are going to initialize render inside of nested region.
     TODO: We probably want to set omp_nested on proper place.  */
#ifdef _OPENMP
  omp_set_max_active_levels (3);
#endif
  if (xsteps > 1 || ysteps > 1)
    {
#pragma omp parallel for default(none) collapse(2) schedule(dynamic)          \
    shared(xsteps, ysteps, rparam, param, progress, img, solver, res, area,   \
               xstep, ystep)
      for (int x = 0; x < xsteps; x++)
        for (int y = 0; y < ysteps; y++)
          {
            if (progress && progress->cancel_requested ())
              continue;
            finetune_parameters fparam;
            fparam.flags
                |= finetune_position /*| finetune_multitile*/ | finetune_bw
                   | finetune_no_progress_report;
            res[x + y * xsteps] = finetune (
                rparam, param, img,
                { { (coord_t)area.x + (x /*+ 0.5*/) * xstep, (coord_t)area.y + (y /*+ 0.5*/) * ystep } },
                nullptr, fparam, progress);
            if (progress)
              progress->inc_progress ();
          }
    }
  else if (!progress || !progress->cancel_requested ())
    {
      finetune_parameters fparam;
      fparam.flags |= finetune_position /*| finetune_multitile*/ | finetune_bw;
      res[0]
          = finetune (rparam, param, img,
                      { { (coord_t)area.x + /*(0.5) **/ xstep, (coord_t)area.y /*+ (0.5) * ystep*/ } },
                      nullptr, fparam, progress);
      if (progress)
        progress->inc_progress ();
    }
  if (progress && progress->cancel_requested ())
    return false;
  std::sort (res.begin (), res.end (),
             [] (finetune_result &a, finetune_result &b)
               { return a.uncertainty > b.uncertainty; });
  coord_t max_uncertainty = res[(xsteps * ysteps - 1) * (1 - fparam.uncertainty_ratio)].uncertainty;
  for (int x = 0; x < xsteps; x++)
    for (int y = 0; y < ysteps; y++)
      {
        finetune_result &r = res[x + y * xsteps];
        if (r.success && r.uncertainty <= max_uncertainty
	    && solver->find_point (r.solver_point_screen_location) < 0)
	  {
	    solver->add_point (r.solver_point_img_location,
			       r.solver_point_screen_location,
			       r.solver_point_color);
	  }
      }
  return true;
}

/* Simulate data collection of scan of given color screen (assumed to be
   blurred) and return collected red, green and blue.  This can be used to
   increase color saturation to compensate losses caused by the collection.

   RET_RED, RET_GREEN, RET_BLUE are returned colors.
   SCR is screen used to render the pattern, while COLLECTION_SCR is used to do
   the data collection.  SIMULATED_SCREEN is optional simulated screen.
   THRESHOLD is the collection threshold.  SHARPEN_PARAM are sharpen
   parameters. MAP is the scr-to-img map.  AREA defines the area.  */

bool
determine_color_loss (rgbdata *ret_red, rgbdata *ret_green, rgbdata *ret_blue,
                      screen &scr, screen &collection_scr,
                      simulated_screen *simulated_screen,
                      luminosity_t threshold,
                      const sharpen_parameters &sharpen_param, scr_to_img &map,
                      int_image_area area)
{
  double_rgbdata red = { 0, 0, 0 }, green = { 0, 0, 0 }, blue = { 0, 0, 0 };
  double wr = 0, wg = 0, wb = 0;
  const bool debugfiles = true;

  if (debugfiles)
    {
      scr.save_tiff ("/tmp/scr.tif", false, 3);
      collection_scr.save_tiff ("/tmp/collection-scr.tif", false, 3);
    }

  sharpen_parameters::sharpen_mode sharpen_mode = sharpen_param.get_mode ();
  if (simulated_screen)
    {
// FIXME: prallelism here seems to cause instability (race condition)
#if 0
#pragma omp declare reduction(+ : double_rgbdata : omp_out = omp_out + omp_in)
#pragma omp parallel for default(none) collapse(2)                            \
    shared(area, threshold, simulated_screen)                                 \
    reduction(+ : wr, wg, wb, red, green, blue)
#endif
      for (int y = area.y; y < area.y + area.height; y++)
        for (int x = area.x; x < area.x + area.width; x++)
          {
            /* Collection and screen colors are the same.  */
            rgbdata m = simulated_screen->get_pixel (y, x);
            if (m.red > threshold)
              {
                luminosity_t val = m.red - threshold;
                wr += val;
                red += m * val;
              }
            if (m.green > threshold)
              {
                luminosity_t val = m.green - threshold;
                wg += val;
                green += m * val;
              }
            if (m.blue > threshold)
              {
                luminosity_t val = m.blue - threshold;
                wb += val;
                blue += m * val;
              }
          }
    }
  /* If sharpening is not needed, we can avoid temporary buffer to store
     rendered screen.  */
  else if (sharpen_mode == sharpen_parameters::none)
    {
      bool antialias = !sharpen_param.scanner_mtf_scale;
// FIXME: prallelism here seems to cause instability (race condition)
#if 0
#pragma omp declare reduction(+ : double_rgbdata : omp_out = omp_out + omp_in)
#pragma omp parallel for default(none) collapse(2)                            \
    shared(area, threshold, map, scr, collection_scr,antialias)               \
    reduction(+ : wr, wg, wb, red, green, blue)
#endif
      for (int y = area.y; y < area.y + area.height; y++)
        for (int x = area.x; x < area.x + area.width; x++)
          {
            point_t p;
            rgbdata am;
            /* Render screen.
               Scanner MTF already estimates sensor loss; so we should not need
               to antialias.  */
            if (!antialias)
              am = noantialias_screen (scr, map, x, y, &p);
            else
              am = antialias_screen (scr, map, x, y, &p);
            /* Data collection does not antialias.  So just take pixel in the
               middle  */
            rgbdata m = collection_scr.noninterpolated_mult (p);
            if (m.red > threshold)
              {
                luminosity_t val = m.red - threshold;
                wr += val;
                red += am * val;
              }
            if (m.green > threshold)
              {
                luminosity_t val = m.green - threshold;
                wg += val;
                green += am * val;
              }
            if (m.blue > threshold)
              {
                luminosity_t val = m.blue - threshold;
                wb += val;
                blue += am * val;
              }
          }
    }
  else
    {
      int ext;
      if (sharpen_param.deconvolution_p ())
        {
          std::shared_ptr<mtf> cur_mtf
              = mtf::get_mtf (sharpen_param.scanner_mtf, nullptr);
          if (!cur_mtf->precompute ())
            return false;
          ext = cur_mtf->psf_size (sharpen_param.scanner_mtf_scale);
        }
      else
        ext = fir_blur::convolve_matrix_length (sharpen_param.usm_radius) / 2;
      int xsize = area.width + 2 * ext;
      int ysize = area.height + 2 * ext;
      std::vector<rgbdata> rendered (xsize * ysize);

      /* Render screen.
         Scanner MTF already estimates sensor loss; so we should not need to
         antialias.  */
      if (sharpen_param.scanner_mtf_scale)
        for (int y = area.y - ext; y < area.y + area.height + ext; y++)
          for (int x = area.x - ext; x < area.x + area.width + ext; x++)
            rendered[(y - area.y + ext) * xsize + x - area.x + ext]
                = noantialias_screen (scr, map, x, y);
      else
        for (int y = area.y - ext; y < area.y + area.height + ext; y++)
          for (int x = area.x - ext; x < area.x + area.width + ext; x++)
            rendered[(y - area.y + ext) * xsize + x - area.x + ext]
                = antialias_screen (scr, map, x, y);

      /* Sharpen it  */
      std::vector<rgbdata> rendered2 (xsize * ysize);
      /* FIXME: parallelism is disabled because sometimes we are called from
       * parallel block.  */
      if (!sharpen_param.deconvolution_p ())
        sharpen<rgbdata, rgbdata, rgbdata *, int, getdata_helper> (
            rendered2.data (), rendered.data (), xsize, ysize, ysize,
            sharpen_param.usm_radius, sharpen_param.usm_amount, nullptr,
            false);
      else
        {
          if (!deconvolve_rgb<rgbdata, rgbdata, rgbdata *, int, getdata_helper> (
		      rendered2.data (), rendered.data (), xsize, ysize, ysize,
		      sharpen_param, nullptr, false))
	    return false;
        }

      if (debugfiles)
        {
          tiff_writer_params p;
          void *buffer;
          size_t len = create_linear_srgb_profile (&buffer);
          p.icc_profile = buffer;
          p.icc_profile_len = len;
          p.filename = "/tmp/sharpened.tif";
          p.width = xsize;
          p.height = ysize;
          p.depth = 16;
          const char *error;
          {
            tiff_writer renderedt (p, &error);
            for (int y = area.y - ext; y < area.y + area.height + ext; y++)
              {
                for (int x = area.x - ext; x < area.x + area.width + ext; x++)
                  {
                    rgbdata d
                        = rendered2[(y - area.y + ext) * xsize + x - area.x + ext];
                    if (x == area.x - 1 || y == area.y - 1 || x == area.x + area.width
                        || y == area.y + area.height)
                      d = { 1, 1, 1 };
                    renderedt.put_pixel (
                        x - area.x + ext,
                        std::clamp (d.red, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535,
                        std::clamp (d.green, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535,
                        std::clamp (d.blue, (luminosity_t)0, (luminosity_t)1)
                            * (luminosity_t)65535);
                  }
                if (!renderedt.write_row ())
                  return false;
              }
          }
          {
            p.filename = "/tmp/unsharpened.tif";
            tiff_writer renderedt (p, &error);
            for (int y = area.y - ext; y < area.y + area.height + ext; y++)
              {
                for (int x = area.x - ext; x < area.x + area.width + ext; x++)
                  {
                    rgbdata d
                        = rendered[(y - area.y + ext) * xsize + x - area.x + ext];
                    renderedt.put_pixel (x - area.x + ext, d.red * (luminosity_t)65535,
                                         d.green * (luminosity_t)65535, d.blue * (luminosity_t)65535);
                  }
                if (!renderedt.write_row ())
                  return false;
              }
          }
          {
            p.filename = "/tmp/collection.tif";
            tiff_writer renderedu (p, &error);
            for (int y = area.y - ext; y < area.y + area.height + ext; y++)
              {
                for (int x = area.x - ext; x < area.x + area.width + ext; x++)
                  {
                    point_t p
                        = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
                    rgbdata m = collection_scr.noninterpolated_mult (p);
                    renderedu.put_pixel (x - area.x + ext, m.red * (luminosity_t)65535,
                                         m.green * (luminosity_t)65535, m.blue * (luminosity_t)65535);
                  }
                if (!renderedu.write_row ())
                  return false;
              }
          }
          free (buffer);
        }

      /* Collect data  */
// FIXME: prallelism here seems to cause instability (race condition)
#if 0
#pragma omp declare reduction(+ : double_rgbdata : omp_out = omp_out + omp_in)
#pragma omp parallel for default(none) collapse(2)                            \
    shared(area, threshold, map, scr, collection_scr,rendered2,ext,xsize)     \
    reduction(+ : wr, wg, wb, red, green, blue)
#endif
      for (int y = area.y; y < area.y + area.height; y++)
        for (int x = area.x; x < area.x + area.width; x++)
          {
            point_t p = map.to_scr ({ x + (coord_t)0.5, y + (coord_t)0.5 });
            rgbdata m = collection_scr.noninterpolated_mult (p);
            rgbdata am = rendered2[(y - area.y + ext) * xsize + x - area.x + ext];
            if (m.red > threshold)
              {
                luminosity_t val = m.red - threshold;
                wr += val;
                red += am * val;
              }
            if (m.green > threshold)
              {
                luminosity_t val = m.green - threshold;
                wg += val;
                green += am * val;
              }
            if (m.blue > threshold)
              {
                luminosity_t val = m.blue - threshold;
                wb += val;
                blue += am * val;
              }
          }
    }
  if (!(wr > 0 && wg > 0 && wb > 0))
    {
      *ret_red = { 1, 0, 0 };
      *ret_green = { 0, 1, 0 };
      *ret_blue = { 0, 0, 1 };
      return false;
    }
  red /= wr;
  green /= wg;
  blue /= wb;
#if 0
  *ret_red = red;
  *ret_green = green;
  *ret_blue = blue;
#else
  *ret_red = (rgbdata){ (luminosity_t)red.red, (luminosity_t)red.green,
                        (luminosity_t)red.blue };
  *ret_green = (rgbdata){ (luminosity_t)green.red, (luminosity_t)green.green,
                          (luminosity_t)green.blue };
  *ret_blue = (rgbdata){ (luminosity_t)blue.red, (luminosity_t)blue.green,
                         (luminosity_t)blue.blue };
#endif
#if 0
  printf ("Color loss info %i %i %i %i %f\n", area.x, area.y, area.width, area.height, map.pixel_size (area.x,area.y));
  ret_red->print (stdout);
  ret_green->print (stdout);
  ret_blue->print (stdout);
#endif
  return true;
}

/* Render simulated screen pattern to IMG using parameters PARAM, RPARAM and
   DPARAM.  The image is rendered in resolution WIDTH x HEIGHT.  */

bool
render_screen (image_data &img, const scr_to_img_parameters &param,
               const render_parameters &rparam, const scr_detect_parameters &dparam,
               int width, int height)
{
  scr_to_img map;
  if (!img.set_dimensions (width, height, true, false))
    return false;
  if (!map.set_parameters (param, img))
    return false;
  coord_t pixel_size = map.pixel_size (width, height);
  sharpen_parameters sharpen = rparam.sharpen;
  sharpen.usm_radius = rparam.screen_blur_radius * pixel_size;
  sharpen.scanner_mtf_scale *= pixel_size;
  std::shared_ptr<screen> scr = render_to_scr::get_screen (
      param.type, false, false, sharpen, rparam.red_strip_width,
      rparam.green_strip_width);
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++)
      {
        const int steps = 4;
        rgbdata d = { 0, 0, 0 };
        for (int xx = 0; xx < steps; xx++)
          for (int yy = 0; yy < steps; yy++)
            d += scr->interpolated_mult (
                map.to_scr ({ x + (xx + 1) / (coord_t)(steps + 1),
                              y + (yy + 1) / (coord_t)(steps + 1) }));
        d *= (coord_t)1 / (coord_t)(steps * steps);
        img.put_rgb_pixel (x, y, {
          (unsigned short)(invert_gamma (d.red, rparam.gamma) * 65535),
          (unsigned short)(invert_gamma (d.green, rparam.gamma) * 65535),
          (unsigned short)(invert_gamma (d.blue, rparam.gamma) * 65535)
        });
      }
  return true;
}
} // namespace colorscreen
