#ifndef COLORSCREEN_H
#define COLORSCREEN_H
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "render-parameters.h"
#include "render-type-parameters.h"
#include "solver-parameters.h"
#include "detect-regular-screen-parameters.h"
#include "scr-detect-parameters.h"
namespace colorscreen
{
class scr_to_img;

struct render_to_file_params
{
  const char *filename;
  int depth;
  bool verbose;
  bool hdr;
  bool dng;
  /* Scale relative to default size of the image.  */
  coord_t scale;
  /* Scale relative to screen coordinates  */
  coord_t screen_scale;
  const void *icc_profile;
  int icc_profile_len;
  /* Antialiasing will happen on NxN grid.  */
  int antialias;
  coord_t xdpi, ydpi;

  enum output_geometry
  {
    /* Resulting file will use screen geometry (correct possible deformations
       of the scan).  */
    screen_geometry,
    /* Resulting file will use scan geometry.  */
    scan_geometry,
    /* Default value.  Use scan geoemtry for scr based rendering modes and scan
       for scr_detect.  */
    default_geometry,
    max_geometry,
  } geometry;

  static const DLL_PUBLIC property_t geometry_names[(int)max_geometry];


  /* Width and height of the output file.  It will be computed if set to 0.  */
  int width, height;
  bool tile;
  /* Specifies top left corner in coordinates.  */
  point_t start;
  /* Size of single pixel.  If 0 default is computed using output mode and
     scale. */
  coord_t xstep, ystep;
  /* Pixel size used to determine antialiasing factor.  It needs to be same in
     whole stitch project. */
  coord_t pixel_size;
  /* Offset of a tile.  */
  int xoffset, yoffset;
  bool (*pixel_known_p) (void *data, coord_t x, coord_t y);
  void *pixel_known_p_data;
  /* Common map used for stitching project.  */
  scr_to_img *common_map;
  /* Position of rendered image in the project.  */
  point_t pos;
  render_to_file_params ()
      : filename (NULL), depth (16), verbose (false), hdr (false), dng (false),
        scale (1), screen_scale (0), icc_profile (NULL), icc_profile_len (0), antialias (0),
        xdpi (0), ydpi (0), geometry (default_geometry), width (0), height (0), tile (0),
        start ({ 0, 0 }), xstep (0), ystep (0), pixel_size (0), xoffset (0), yoffset (0),
        common_map (NULL), pos ({ 0, 0 })
  {
  }
};

/* Simple RGB image used to render various things for UI.  */
struct simple_image
{
  int width;
  int height;
  int stride;
  simple_image ()
  : width (0), height (0), stride (0)
  { }
  struct rgb
  {
    int red;
    int green;
    int blue;
  };
  void
  put_pixel (int x, int y, rgb color)
  {
    m_data[y * stride + x * 3] = color.red;
    m_data[y * stride + x * 3 + 1] = color.green;
    m_data[y * stride + x * 3 + 2] = color.blue;
  }
  void
  put_linear_pixel (int x, int y, rgbdata c)
  {
    put_pixel (x, y, {(int)(invert_gamma (std::clamp (c.red, (luminosity_t)0, (luminosity_t)1), -1) * 255 + 0.5),
		      (int)(invert_gamma (std::clamp (c.green, (luminosity_t)0, (luminosity_t)1), -1) * 255 + 0.5),
		      (int)(invert_gamma (std::clamp (c.blue, (luminosity_t)0, (luminosity_t)1), -1) * 255 + 0.5)});
  }
  rgb
  get_pixel (int x, int y) const
  {
    return {m_data[y * stride + x * 3], m_data[y * stride + x * 3 + 1], m_data[y * stride + x * 3 + 2]};
  }
  bool
  allocate (int set_width, int set_height)
  {
    width = set_width;
    height = set_height;
    stride = width * 3;
    m_data.resize (stride * height);
    return true;
  }
private:
  std::vector<uint8_t> m_data;
};

struct tile_parameters
{
  uint8_t *pixels;
  int rowstride;
  int pixelbytes;
  int width;
  int height;
  point_t pos;
  coord_t step;
};

struct color_match
{
  xyz profiled;
  xyz target;
  luminosity_t deltaE;
};

struct has_regular_screen_params
{
  /* Minimum and maximum period of screen to look for.  */
  coord_t min_period;
  coord_t max_period;

  /* Save individual tiles and fft?  */
  bool save_tiles;
  bool save_fft;
  std::string tile_base;
  std::string fft_base;

  /* width and height.  */
  int ntilesx;
  int ntilesy;

  /* Input gamma.  */
  luminosity_t gamma;

  coord_t threshold;
  coord_t tiles_treshold;

  bool verbose;
  FILE *report;

  has_regular_screen_params ()
  : min_period (2.01), max_period (30), save_tiles (false), save_fft (false), tile_base ("tile"), fft_base ("fft"), ntilesx (9), ntilesy (9), gamma (0), threshold (1.1), tiles_treshold (0.15), verbose (false), report (NULL)
  {
  }
};
struct has_regular_screen_ret
{
  bool found;
  const char *error;
  coord_t period;
  coord_t perc;
};
nodiscard_attr DLL_PUBLIC struct has_regular_screen_ret
has_regular_screen(image_data &scan, const has_regular_screen_params &params,
                   progress_info *progress = NULL);
nodiscard_attr DLL_PUBLIC bool
save_csp(FILE *f, const scr_to_img_parameters *param, const scr_detect_parameters *dparam,
         const render_parameters *rparam, const solver_parameters *sparam);
nodiscard_attr DLL_PUBLIC bool
load_csp(FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam,
         render_parameters *rparam, solver_parameters *sparam,
         const char **error);
nodiscard_attr DLL_PUBLIC bool
render_to_file(image_data &scan, scr_to_img_parameters &param,
               scr_detect_parameters &dparam, render_parameters &rparam,
               render_to_file_params rfarams, render_type_parameters &rtparam,
               progress_info *progress, const char **error);
nodiscard_attr DLL_PUBLIC bool
render_tile(image_data &scan, scr_to_img_parameters &param,
            scr_detect_parameters &dparam, render_parameters &rparam,
            render_type_parameters &rtparam, tile_parameters &tile,
            progress_info *progress = NULL);
enum render_screen_tile_type
{
  original_screen,
  blurred_screen,
  sharpened_screen,
  backlight_screen,
  detail_screen,
  full_screen,
  corrected_backlight_screen,
  corrected_detail_screen,
  corrected_full_screen,
  dot_spread,
};
nodiscard_attr DLL_PUBLIC bool
render_screen_tile(tile_parameters &tile, scr_type type,
                   const render_parameters &rparam, coord_t pixel_size,
                   enum render_screen_tile_type, progress_info *p);
nodiscard_attr DLL_PUBLIC bool
complete_rendered_file_parameters(render_type_parameters &rtparams,
                                  scr_to_img_parameters &param,
                                  image_data &scan, render_to_file_params *p);
nodiscard_attr DLL_PUBLIC bool
complete_rendered_file_parameters(render_type_parameters *rtparams,
                                  scr_to_img_parameters *param,
                                  image_data *scan, stitch_project *stitch,
                                  render_to_file_params *p);
DLL_PUBLIC rgbdata get_linearized_pixel(const image_data &img,
                                        render_parameters &rparam, int x, int y,
                                        int range = 4,
                                        progress_info *progress = NULL);
nodiscard_attr DLL_PUBLIC bool
dump_patch_density(FILE *out, image_data &scan, scr_to_img_parameters &param,
                   render_parameters &rparam, progress_info *progress = NULL);
nodiscard_attr DLL_PUBLIC bool
render_preview(image_data &scan, const scr_to_img_parameters &param,
               const render_parameters &rparams, unsigned char *pixels, int width,
               int height, int rowstride, progress_info *progress = NULL);
DLL_PUBLIC rgbdata analyze_color_proportions (
    scr_detect_parameters param, render_parameters &rparam, image_data &img,
    scr_to_img_parameters *map_param, int_image_area area,
    progress_info *p = nullptr);

DLL_PUBLIC coord_t solver (scr_to_img_parameters *param, const image_data &img_data,
                           const solver_parameters &sparam,
                           progress_info *progress = NULL);
DLL_PUBLIC std::unique_ptr<mesh> solver_mesh (const scr_to_img_parameters *param,
                                              const image_data &img_data,
                                              const solver_parameters &sparam,
                                              progress_info *progress = NULL);
DLL_PUBLIC detected_screen detect_regular_screen (
    const image_data &img, scr_detect_parameters &dparam,
    solver_parameters &sparam,
    const detect_regular_screen_params *dsparams,
    progress_info *progress = NULL,
    FILE *report_file = NULL);
DLL_PUBLIC color_matrix determine_color_matrix (
    rgbdata *colors, xyz *targets, rgbdata *rgbtargets, int n, xyz white,
    int dark_point_elts = 0, std::vector<color_match> *report = NULL,
    render *r = NULL, rgbdata proportions = { 1, 1, 1 },
    progress_info *progress = NULL);
nodiscard_attr DLL_PUBLIC bool
optimize_color_model_colors(scr_to_img_parameters *param, image_data &img,
                            render_parameters &rparam,
                            std::vector<point_t> &points,
                            std::vector<color_match> *report,
                            progress_info *progress);
nodiscard_attr DLL_PUBLIC bool
compare_deltae(image_data &img, scr_to_img_parameters &param1,
               render_parameters &rparam1, scr_to_img_parameters &param2,
               render_parameters &rparam2, const char *cmpname, double *,
               double *, progress_info *progress = NULL);
enum hd_axis_type { hd_axis_hd, hd_axis_gamma10, hd_axis_gamma22 };

static inline double
hd_linear_to_axis_x (double linear, hd_axis_type axis, luminosity_t preflash, luminosity_t exposure)
{
  double val = (linear + (preflash / 100)) * exposure;
  if (axis == hd_axis_hd)
    return std::log10 (std::max (0.0000001, val));
  if (axis == hd_axis_gamma22)
    return std::pow (std::max (0.0000001, val), 1.0 / 2.2);
  return val;
}

static inline double
hd_log_exposure_to_axis_x (double logE, hd_axis_type axis)
{
  if (axis == hd_axis_hd)
    return logE;
  if (axis == hd_axis_gamma22)
    return std::pow (std::pow (10.0, logE), 1.0 / 2.2);
  return std::pow (10.0, logE);
}

static inline double
hd_axis_x_to_log_exposure (double axisX, hd_axis_type axis)
{
  if (axis == hd_axis_hd)
    return axisX;
  if (axis == hd_axis_gamma22)
    return std::log10 (std::max (0.0000001, std::pow (axisX, 2.2)));
  return std::log10 (std::max (0.0000001, axisX));
}

static inline double
hd_density_to_axis_y (double density, double boost, hd_axis_type axis)
{
  if (axis == hd_axis_hd)
    return density;
  double transmittance = std::pow (10.0, -density * boost);
  if (axis == hd_axis_gamma10)
    return transmittance;
  return std::pow (transmittance, 1.0 / 2.2);
}

static inline double
hd_axis_y_to_density (double axisY, double boost, hd_axis_type axis)
{
  if (axis == hd_axis_hd)
    return axisY;
  double transmittance;
  if (axis == hd_axis_gamma10)
    transmittance = axisY;
  else
    transmittance = std::pow (std::max (0.0000001, axisY), 2.2);
  return -std::log10 (std::max (0.0000001, transmittance)) / std::max (0.01, boost);
}

static inline double
hd_axis_y_to_linear (double axisY, double boost, hd_axis_type axis)
{
  if (axis == hd_axis_hd)
    return std::pow (10.0, -axisY * boost);
  if (axis == hd_axis_gamma10)
    return axisY;
  return std::pow (std::max (0.0000001, axisY), 2.2);
}

/* Parameters controlling slanted-edge SFR measurement.  */
struct slanted_edge_parameters
{
  /* Window applied to the line-spread function before its Fourier transform.  */
  enum window_type
  {
    window_hann,
    window_hamming,
    window_rectangular
  };

  /* Supersampling rate for the edge-spread function.
     The implementation defaults to 10x; 4x is the traditional ISO-style rate.
     Values from 2x through 64x are accepted.  Nonpositive values select the
     default.  */
  int oversampling = 10;

  /* Half-width of the retained line-spread function in input pixels.
     Zero keeps the complete region of interest and preserves the historical
     Color-Screen measurement.  A finite value makes the measurement aperture
     explicit and is useful when comparing with programs that truncate the LSF.  */
  double lsf_half_width = 0;

  /* Window used on the retained LSF support.  */
  window_type window = window_hann;

  /* Wavelength in nanometers associated with the measured edge.  Zero means
     unknown.  This metadata is copied to the appended MTF measurement and is
     not used by the slanted-edge numerical calculation itself.  */
  double wavelength = 0;

  /* Measurement channel: -1 unknown, 0 red, 1 green, 2 blue, or 3 infrared.  */
  int channel = -1;

  /* Human-readable name stored with the appended MTF measurement.  */
  std::string name = "Slanted edge MTF";

  /* True when this edge belongs to the same capture as the preceding stored
     measurement and should therefore share fitted defocus.  */
  bool same_capture = false;
};

/* Reason why a slanted-edge measurement was rejected.  A failed
   measurement never appends an MTF curve to the render parameters.  */
enum slanted_edge_failure
{
  slanted_edge_failure_none,
  slanted_edge_failure_invalid_parameters,
  slanted_edge_failure_invalid_roi,
  slanted_edge_failure_precomputation,
  slanted_edge_failure_no_single_edge,
  slanted_edge_failure_nonlinear_edge,
  slanted_edge_failure_unsuitable_angle,
  slanted_edge_failure_edge_near_boundary,
  slanted_edge_failure_phase_coverage,
  slanted_edge_failure_low_contrast,
  slanted_edge_failure_unstable_esf,
  slanted_edge_failure_invalid_numerics,
  slanted_edge_failure_nonphysical_mtf
};

/* Result of slanted-edge SFR measurement.  */
struct slanted_edge_results
{
  /* True if edge was successfully found, qualified, and analyzed.  */
  bool success = false;

  /* Failure category.  This is SLANTED_EDGE_FAILURE_NONE on success.  */
  slanted_edge_failure failure = slanted_edge_failure_none;

  /* Human-readable explanation of a failure, including the measured quantity
     that caused rejection where useful.  This is empty on success.  */
  std::string error;

  /* Actual detected edge coordinates.  These remain zero on failure.  */
  point_t edge_p1;
  point_t edge_p2;

  /* Angle of the detected edge from the nearest image axis, in degrees.  */
  double edge_angle = 0;

  /* RMS residual of accepted line centroids from the fitted edge, in pixels.  */
  double edge_fit_rms = 0;

  /* Robust difference between the two edge plateaus in normalized image
     intensity.  */
  double edge_contrast = 0;

  /* Robust plateau signal-to-noise ratio used to qualify the ROI.  */
  double edge_snr = 0;

  /* Fraction of supersampled ESF bins populated by real pixels before empty
     bins are interpolated.  */
  double phase_coverage = 0;

  /* Edge-spread function for visualization and export.  This remains empty on
     failure so callers cannot accidentally use an unqualified curve.  */
  std::vector<luminosity_t> edge_histogram;

  /* Coordinate of EDGE_HISTOGRAM[0] in input pixels relative to the fitted
     edge line.  */
  double edge_histogram_origin = 0;

  /* Distance between adjacent EDGE_HISTOGRAM samples in input pixels.  */
  double edge_histogram_step = 0;
};

DLL_PUBLIC slanted_edge_results
slanted_edge_mtf (render_parameters &rparam, const image_data &img, int_image_area roi,
                  const slanted_edge_parameters &params, progress_info *progress = NULL);

DLL_PUBLIC std::vector <rgbdata>
hd_y_to_rgb (render_parameters &rparam, int steps, luminosity_t miny, luminosity_t maxy, rgbdata patch_proportions, hd_axis_type axis_type = hd_axis_hd);
DLL_PUBLIC std::vector <uint64_t>
hd_x_histogram (render_parameters &rparam, image_data &img, int steps, luminosity_t minx, luminosity_t maxx, hd_axis_type axis_type, progress_info *progress);
}
#endif
