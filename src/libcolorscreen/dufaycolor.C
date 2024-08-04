#include "include/tiff-writer.h"
#include "include/spectrum-to-xyz.h"
#include "include/dufaycolor.h"
#include "icc.h"
#include "screen.h"
#include "lru-cache.h"
namespace colorscreen
{
constexpr xyY dufaycolor::red_dye;
constexpr xyY dufaycolor::green_dye;
constexpr xyY dufaycolor::blue_dye;
constexpr xyY dufaycolor::red_dye_color_cinematography_xyY;
constexpr xyY dufaycolor::green_dye_color_cinematography_xyY;
constexpr xyY dufaycolor::blue_dye_color_cinematography_xyY;
//constexpr xyY dufaycolor::correctedY_blue_dye;

bool
dufaycolor::tiff_with_primaries (const char *filename, bool corrected)
{
  xyz red = corrected ? red_dye : red_dye_color_cinematography_xyY;
  xyz green = corrected ? green_dye : green_dye_color_cinematography_xyY;
  xyz blue = corrected ? blue_dye : blue_dye_color_cinematography_xyY;
  return tiff_with_strips (filename, red, green, blue, (xyz){0, 0, 0},
			   red * (red_size / screen_size) + green * (green_size / screen_size) + blue * (blue_size / screen_size));
}

static void
tiff_with_screen (const char *filename, coord_t red_strip_width, coord_t green_strip_width, xyz red, xyz green, xyz blue, xyz whitepoint)
{
  const int width = 2048;
  const int height = 2048;
  screen scr;
  scr.initialize (Dufay, red_strip_width, green_strip_width);
  void *buffer;
  size_t len = create_pro_photo_rgb_profile (&buffer, whitepoint);
  tiff_writer_params par;
  par.filename=filename;
  par.width = width;
  par.height = height;
  par.depth = 32;
  par.hdr = true;
  par.icc_profile = buffer;
  par.icc_profile_len = len;
  const char *error;
  tiff_writer tiff (par, &error);
  xyz avg = {0,0,0};
  rgbdata avg2;
  avg2.red = 0;
  avg2.green = 0;
  avg2.blue = 0;
  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
	{
	  xyz color = {0,0,0};
#if 0
	  const int aa = 16;
	  for (int xx = 0 ; xx < aa; xx++)
	    for (int yy = 0 ; yy < aa; yy++)
	      {
		coord_t sx = x + xx / (coord_t)aa, sy = y + yy / (coord_t)aa;
		int ix = (uint64_t) nearest_int (sx) & (unsigned)(screen::size - 1);
		int iy = (uint64_t) nearest_int (sy) & (unsigned)(screen::size - 1);
		color = color + (red * scr.mult[ix][iy][0]) + (green * scr.mult[ix][iy][1]) + (blue * scr.mult[ix][iy][2]);
	      }
	  color.x = color.x / (aa * aa);
	  color.y = color.y / (aa * aa);
	  color.z = color.z / (aa * aa);
#else
	  int ix = x & (unsigned)(screen::size - 1);
	  int iy = y & (unsigned)(screen::size - 1);
	  color = (red * scr.mult[ix][iy][0]) + (green * scr.mult[ix][iy][1]) + (blue * scr.mult[ix][iy][2]);
#endif
	  luminosity_t r, g, b;
	  xyz_to_pro_photo_rgb (color.x, color.y, color.z, &r, &g, &b);
	  tiff.put_hdr_pixel (x, r, g, b);
	  avg = avg + color;
	  avg2.red += r;
	  avg2.green += g;
	  avg2.blue += b;
	}
      tiff.write_row ();
    }
  xyz iavg = ((red * red_strip_width) + (green * (green_strip_width * (1-red_strip_width))) + (blue * ((1-red_strip_width)*(1-green_strip_width))));
  avg.x /= width * height;
  avg.y /= width * height;
  avg.z /= width * height;
  if (deltaE (iavg, avg, whitepoint)>1)
    {
      printf ("Mismatch between average and intended average (luminosity_t == float?)");
      printf ("Intended average: ");
      iavg.print (stdout);
      printf ("Average: ");
      avg.print (stdout);
      printf ("Average prohoto colors %f %f %f\n", avg2.red / (width * height), avg2.green / (width * height), avg2.blue / (width * height));
      luminosity_t r, g, b;
      xyz_to_pro_photo_rgb (avg.x, avg.y, avg.z, &r, &g, &b);
      printf ("Intended prophoto colors %f %f %f\n", r, g, b);
    }
  free (buffer);
}

static void
report_color (const char *name, const char *illuminant, xyz f, xyz target, xyz backlight)
{
  xyY c = f;
  luminosity_t r, g, b;
  f.to_srgb (&r, &g, &b);
  int rr = std::min (std::max ((int)(r * 255 + 0.5), 0), 255);
  int gg = std::min (std::max ((int)(g * 255 + 0.5), 0), 255);
  int bb = std::min (std::max ((int)(b * 255 + 0.5), 0), 255);
  printf ("%-5s    %-7s %6.4f %6.4f %6.4f     %5.1f     %6.4f %6.4f %6.4f    %7.1f %7.1f %7.1f  #%02x%02x%02x  %5.1f %5.1f\n", name, illuminant, f.x, f.y, f.z, deltaE2000 (f, target, backlight), c.x, c.y, c.Y, r * 256, g * 256, b * 256, rr, gg, bb, dominant_wavelength (f), dominant_wavelength (f, backlight));
}

static void
report_xyz_dyes (const char *name, const char *fname, xyz my_red_dye, xyz my_green_dye, xyz my_blue_dye, xyz my_dufay_white, xyz white_xyz)
{
  xyz dufay_white = dufaycolor::get_corrected_dufay_white ();
  //xyz correctedY_dufay_white = dufaycolor::get_correctedY_dufay_white ();
  printf ("\ncolor    illmin. x      y      z           deltaE2k x      y      Y      sRGB r        g      b      \n");
  report_color ("red", name,  my_red_dye, dufaycolor::red_dye, white_xyz);
  report_color ("green", name,  my_green_dye, dufaycolor::green_dye, white_xyz);
  report_color ("blue", name,  my_blue_dye, dufaycolor::blue_dye, white_xyz);
  //report_color ("blueY", name,  my_blue_dye, dufaycolor::correctedY_blue_dye, white_xyz);
  report_color ("white", name,  my_dufay_white, dufay_white, white_xyz);
  //report_color ("whiteY", name,  my_dufay_white, correctedY_dufay_white, white_xyz);

  color_matrix m = matrix_by_dye_xyz (my_red_dye, my_green_dye, my_blue_dye);
  luminosity_t rw, gw, bw;
  m.normalize_grayscale (white_xyz.x, white_xyz.y, white_xyz.z, &rw, &gw, &bw);
  printf ("Normalizing to whitepoint: ");
  white_xyz.print (stdout);
  luminosity_t rw2 = rw / dufaycolor::red_portion;
  luminosity_t gw2 = gw / dufaycolor::green_portion;
  luminosity_t bw2 = bw / dufaycolor::blue_portion;

  xyz new_white = my_red_dye * (rw2 * dufaycolor::red_portion) + (my_green_dye * (gw2 * dufaycolor::green_portion)) + (my_blue_dye * (bw2 * dufaycolor::blue_portion));
  if (!new_white.almost_equal_p (white_xyz))
    printf ("Scaling factors computation failed\n");
  luminosity_t sum = rw + gw + bw;
  printf ("Neutral reseau red size %.1f%% green size %.1f%%, blue size %.1f%%;\n red strip width %.1f%%\n green strip width %.1f%%\n", rw * 100 / sum, gw * 100 / sum, bw * 100 / sum, rw*100/sum, gw * 100 / (sum - rw));
  printf ("\n");

  if (fname)
    tiff_with_screen (fname, rw/sum, gw/ (sum - rw),  my_red_dye, my_green_dye, my_blue_dye, white_xyz);
}
static void
initialize_spec_response (spectrum_dyes_to_xyz &spec)
{
  //spec.set_film_response (spectrum_dyes_to_xyz::ilford_fp4_plus);
  //spec.set_film_response (spectrum_dyes_to_xyz::ilford_panchromatic);
  //spec.set_film_response (spectrum_dyes_to_xyz::spicer_dufay_guess/*spectrum_dyes_to_xyz::ilford_sfx200*//*spectrum_dyes_to_xyz::neopan_100*/);
  spec.set_film_response (spectrum_dyes_to_xyz::aviphot_pan_40_pe0_cut/*spectrum_dyes_to_xyz::ilford_sfx200*//*spectrum_dyes_to_xyz::neopan_100*/);
  //spec.set_response_to_y ();
  //
  spec.adjust_film_response_for_zeiss_contact_prime_cp2_lens ();
  //spec.adjust_film_response_for_canon_CN_E_85mm_T1_3_lens ();
}

static void
initialize_spec (spectrum_dyes_to_xyz &spec, int mode, bool normalized_dyes)
{
  if (!mode)
    spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
  else if (mode == 1)
    spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_harrison_horner);
  else if (mode == 2)
    spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_photography_its_materials_and_processes);
  else if (mode == 3)
    spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_collins_giles);
  else
    abort ();

  /* Set scales so we simulate look of the overall screen.  */
  if (normalized_dyes)
    {
      spec.rscale = dufaycolor::red_portion;
      spec.gscale = dufaycolor::green_portion;
      spec.bscale = dufaycolor::blue_portion;
    }
  else
    spec.rscale = spec.gscale = spec.bscale = 1;

  initialize_spec_response (spec);
}

void
report_illuminant (spectrum_dyes_to_xyz &spec, const char *name, const char *filename, const char *filename2)
{

  xyz red_xyz = spec.dyes_rgb_to_xyz (1, 0, 0, 1931);
  xyz green_xyz = spec.dyes_rgb_to_xyz (0, 1, 0, 1931);
  xyz blue_xyz = spec.dyes_rgb_to_xyz (0, 0, 1, 1931);
  xyz dufay_white_xyz = spec.dyes_rgb_to_xyz (dufaycolor::red_size / dufaycolor::screen_size, dufaycolor::green_size / dufaycolor::screen_size, dufaycolor::blue_size / dufaycolor::screen_size, 1931);
  xyz white_xyz = spec.whitepoint_xyz (1931);
  xyz sim_white_xyz = (red_xyz * (dufaycolor::red_size / dufaycolor::screen_size)) + (green_xyz * (dufaycolor::green_size / dufaycolor::screen_size)) + (blue_xyz * (dufaycolor::blue_size / dufaycolor::screen_size));

  if (!sim_white_xyz.almost_equal_p (dufay_white_xyz))
    printf ("Nonlinearity in observer model\n");
  //initialize_spec (spec, color_cinematography);
  initialize_spec_response (spec);
  rgbdata scale = spec.determine_relative_patch_sizes_by_simulated_response ();
  report_xyz_dyes (name, filename, red_xyz, green_xyz, blue_xyz, dufay_white_xyz, white_xyz);
  luminosity_t sum = scale.red + scale.green + scale.blue;
  xyz screen = red_xyz * (scale.red/sum) + green_xyz * (scale.green / sum) + blue_xyz * (scale.blue / sum);
  printf ("Optimal response %.1f%% green size %.1f%%, blue size %.1f%%;\n red strip width %.1f%%\n green strip width %.1f%%\n screen color", scale.red * 100 / sum, scale.green * 100 / sum, scale.blue * 100 / sum, scale.red*100/sum, scale.green * 100 / (sum - scale.red));
  screen.print (stdout);
  if (filename2)
    tiff_with_screen (filename2, scale.red/sum, scale.green / (sum - scale.red), red_xyz, green_xyz, blue_xyz, white_xyz);
}
static void
render_green_dyes (spectrum_dyes_to_xyz &spec, const char *filename)
{
  const int width = 2048;
  const int height = 2048;
  xyz whitepoint = spec.whitepoint_xyz (1931);
  void *buffer;
  size_t len = create_pro_photo_rgb_profile (&buffer, whitepoint);
  tiff_writer_params par;
  par.filename=filename;
  par.width = width;
  par.height = height;
  par.depth = 32;
  par.hdr = true;
  par.icc_profile = buffer;
  par.icc_profile_len = len;
  const char *error;
  tiff_writer tiff (par, &error);

  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
        {
	  spec.synthetic_dufay_green (x, y);
	  xyz green = spec.dyes_rgb_to_xyz (0, 1, 0, 1931);
	  luminosity_t r, g, b;
	  xyz_to_pro_photo_rgb (green.x, green.y, green.z, &r, &g, &b);
	  tiff.put_hdr_pixel (x, r, g, b);
        }
      tiff.write_row ();
    }
  free (buffer);
}
static void
render_blue_dyes (spectrum_dyes_to_xyz &spec, const char *filename)
{
  const int width = 2048;
  const int height = 2048;
  xyz whitepoint = spec.whitepoint_xyz (1931);
  void *buffer;
  size_t len = create_pro_photo_rgb_profile (&buffer, whitepoint);
  tiff_writer_params par;
  par.filename=filename;
  par.width = width;
  par.height = height;
  par.depth = 32;
  par.hdr = true;
  par.icc_profile = buffer;
  par.icc_profile_len = len;
  const char *error;
  tiff_writer tiff (par, &error);

  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
        {
	  spec.synthetic_dufay_blue (x, y);
	  xyz blue = spec.dyes_rgb_to_xyz (0, 0, 1, 1931);
	  luminosity_t r, g, b;
	  xyz_to_pro_photo_rgb (blue.x, blue.y, blue.z, &r, &g, &b);
	  tiff.put_hdr_pixel (x, r, g, b);
        }
      tiff.write_row ();
    }
  free (buffer);
}
static void
render_red_dyes (spectrum_dyes_to_xyz &spec, const char *filename)
{
  const int width = 2048;
  const int height = 2048;
  xyz whitepoint = spec.whitepoint_xyz (1931);
  void *buffer;
  size_t len = create_pro_photo_rgb_profile (&buffer, whitepoint);
  tiff_writer_params par;
  par.filename=filename;
  par.width = width;
  par.height = height;
  par.depth = 32;
  par.hdr = true;
  par.icc_profile = buffer;
  par.icc_profile_len = len;
  const char *error;
  tiff_writer tiff (par, &error);

  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
        {
	  spec.synthetic_dufay_red (x, y);
	  xyz red = spec.dyes_rgb_to_xyz (1, 0, 0, 1931);
	  luminosity_t r, g, b;
	  xyz_to_pro_photo_rgb (red.x, red.y, red.z, &r, &g, &b);
	  tiff.put_hdr_pixel (x, r, g, b);
        }
      tiff.write_row ();
    }
  free (buffer);
}

void
dufaycolor::print_xyY_report ()
{
  printf ("Screen patern dimensions based on microscopic image of the filter structure of a Dufaycolor\n"
	  "film. The Emulsion has been removed. The visible structures are not silver\n"
	  "grain but the structure of the filter layers.\n"
	  "Credit: David Pfluger, ERC Advanced Grant FilmColors.\n"
	  "Imaging was performed with support of the Center for Microscopy and Image Analysis, University of Zurich\n\n");
  printf ("color    width   height  area   (relative)\n");
  printf ("red      %5.1f   %5.1f   %6.1f %6.1f%%\n", red_width, blue_height + green_height, red_size, red_size * 100 / screen_size);
  printf ("green    %5.1f   %5.1f   %6.1f %6.1f%%\n", green_blue_width, green_height, green_size, green_size * 100 / screen_size);
  printf ("blue     %5.1f   %5.1f   %6.1f %6.1f%%\n\n", green_blue_width, blue_height + green_height, blue_size, blue_size * 100 / screen_size);
  printf ("red strip width %.1f%%\n", red_width * 100 / (red_width + green_blue_width));
  printf ("green strip height %.1f%%\n\n", green_height * 100 / (green_height + blue_height));

  printf ("Colors specified by Color Cinematography table:\n");
  printf ("color     x     y     Y     dominating wavelength\n");
  printf ("red       %5.3f %5.3f %5.3f %5.1f\n", red_dye_color_cinematography_xyY.x, red_dye_color_cinematography_xyY.y, red_dye_color_cinematography_xyY.Y, dominant_wavelength_red);
  printf ("green     %5.3f %5.3f %5.3f %5.1f\n", green_dye_color_cinematography_xyY.x, green_dye_color_cinematography_xyY.y, green_dye_color_cinematography_xyY.Y, dominant_wavelength_green);
  printf ("blue      %5.3f %5.3f %5.3f %5.1f\n\n", blue_dye_color_cinematography_xyY.x, blue_dye_color_cinematography_xyY.y, blue_dye_color_cinematography_xyY.Y, dominant_wavelength_blue);
  printf ("Real dominating wavelengths relative to various whitepoints\n");
  xy_t best = find_best_whitepoint (red_dye_color_cinematography_xyY, green_dye_color_cinematography_xyY, blue_dye_color_cinematography_xyY,
				    dominant_wavelength_red,
				    dominant_wavelength_green,
				    dominant_wavelength_blue);
  printf ("color    il_A   il_B    il_C  neutral best (%f,%f)\n", best.x, best.y);
  printf ("red      %5.1f  %5.1f  %5.1f  %5.1f   %5.1f\n",
	  dominant_wavelength (red_dye_color_cinematography_xyY, il_A_white),
	  dominant_wavelength (red_dye_color_cinematography_xyY, il_B_white),
	  dominant_wavelength (red_dye_color_cinematography_xyY, il_C_white),
	  dominant_wavelength (red_dye_color_cinematography_xyY),
	  dominant_wavelength (red_dye_color_cinematography_xyY, best));
  printf ("green    %5.1f  %5.1f  %5.1f  %5.1f   %5.1f\n",
	  dominant_wavelength (green_dye_color_cinematography_xyY, il_A_white),
	  dominant_wavelength (green_dye_color_cinematography_xyY, il_B_white),
	  dominant_wavelength (green_dye_color_cinematography_xyY, il_C_white),
	  dominant_wavelength (green_dye_color_cinematography_xyY),
	  dominant_wavelength (green_dye_color_cinematography_xyY, best));
  printf ("blue     %5.1f  %5.1f  %5.1f  %5.1f   %5.1f\n\n",
	  dominant_wavelength (blue_dye_color_cinematography_xyY, il_A_white),
	  dominant_wavelength (blue_dye_color_cinematography_xyY, il_B_white),
	  dominant_wavelength (blue_dye_color_cinematography_xyY, il_C_white),
	  dominant_wavelength (blue_dye_color_cinematography_xyY),
	  dominant_wavelength (blue_dye_color_cinematography_xyY, best));
  spectrum_dyes_to_xyz spec;
  spec.set_backlight (spectrum_dyes_to_xyz::il_C);
  report_xyz_dyes ("xyY data", "corrected-screen.tif", red_dye, green_dye, blue_dye, get_corrected_dufay_white (), spec.whitepoint_xyz (1931));
  printf ("IL C neutral scrren for corrected dyes saved to corrected-screen.tif\n");
  report_xyz_dyes ("xyY data", "color-cinematography-screen.tif", red_dye_color_cinematography_xyY, green_dye_color_cinematography_xyY, blue_dye_color_cinematography_xyY, get_color_cinematography_xyY_dufay_white (), spec.whitepoint_xyz (1931));
  printf ("IL C neutral scrren for dyes specified in color-cinematography saved to color-cinematography-screen.tif\n");
  tiff_with_primaries ("xyY-primaries.tif",false);
  printf ("primaries rendered to xyY-primaries.tif\n");
  tiff_with_primaries ("xyY-primaries-corrected.tif",true);
  printf ("primaries with corrected Y of blue dye rendered to xyY-primaries-correctedY.tif\n");
}
void
dufaycolor::print_spectra_report ()
{
  spectrum_dyes_to_xyz spec;
  spec.set_backlight (spectrum_dyes_to_xyz::il_C);
  spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
  spec.set_backlight (spectrum_dyes_to_xyz::il_A);
  initialize_spec (spec, 1, false);
  report_illuminant (spec, "CIE A", "harrison-horner-spectra-ilA-screen.tif", "harrison-horner-spectra-ilA-screen-resp.tif");
  initialize_spec (spec, 0, false);
  report_illuminant (spec, "CIE A", "color-cinematography-spectra-ilA-screen.tif", "color-cinematography-spectra-ilA-screen-resp.tif");
  spec.set_backlight (spectrum_dyes_to_xyz::il_B);
  initialize_spec (spec, 1, false);
  report_illuminant (spec, "CIE B", "harrison-horner-spectra-ilB-screen.tif", "harrison-horner-spectra-ilB-screen-resp.tif");
  initialize_spec (spec, 0, false);
  report_illuminant (spec, "CIE B", "color-cinematography-spectra-ilB-screen.tif", "color-cinematography-spectra-ilB-screen-resp.tif");
  spec.set_backlight (spectrum_dyes_to_xyz::il_C);
  initialize_spec (spec, 1, false);
  report_illuminant (spec, "CIE C", "harrison-horner-spectra-ilC-screen.tif", "harrison-horner-spectra-ilC-screen-resp.tif");
  spec.tiff_with_primaries ("spec-C-primaries-harrison-horner.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size});
  initialize_spec (spec, 0, false);
  report_illuminant (spec, "CIE C", "color-cinematography-spectra-ilC-screen.tif", "color-cinematography-spectra-ilC-screen-resp.tif");
  spec.tiff_with_primaries ("spec-C-primaries-color-cinematography.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size});
  spec.set_backlight (spectrum_dyes_to_xyz::il_D, 5000);
  report_illuminant (spec, "5000K", "color-cinematography-spectra-5000-screen.tif", "color-cinematography-spectra-5000-screen-resp.tif");
  spec.tiff_with_overlapping_filters ("spec-d50-primaries-overlap.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size}, "combined-");
  spec.tiff_with_overlapping_filters_response ("spec-d50-primaries-overlap-response.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size});
  spec.set_backlight (spectrum_dyes_to_xyz::il_D, 5500);
  report_illuminant (spec, "5500K", "color-cinematography-spectra-5500-screen.tif", "color-cinematography-spectra-5500-screen-resp.tif");
  spec.tiff_with_overlapping_filters ("spec-d55-primaries-overlap.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size}, "combined-");
  spec.tiff_with_overlapping_filters_response ("spec-d55-primaries-overlap-response.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size});
  spec.set_backlight (spectrum_dyes_to_xyz::il_D, 6500);
  report_illuminant (spec, "6500K", "color-cinematography-spectra-6500-screen.tif", "color-cinematography-spectra-6500-screen-resp.tif");
  spec.tiff_with_primaries ("spec-d65-primaries.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size});
  spec.tiff_with_overlapping_filters ("spec-d65-primaries-overlap.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size}, "combined-");
  spec.tiff_with_overlapping_filters_response ("spec-d65-primaries-overlap-response.tif",(rgbdata){red_size / screen_size, green_size / screen_size, blue_size / screen_size});
  spec.set_backlight (spectrum_dyes_to_xyz::il_C);
  spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_aged_DC_MSI_NSMM11948_spicer_dufaycolor);
  report_illuminant (spec, "CIE C aged", "color-cinematography-spectra-ilC-screen-aged.tif", "color-cinematography-spectra-ilC-screen-resp-aged.tif");
}

void
dufaycolor::print_synthetic_dyes_report ()
{
  //bradford_whitepoint_adaptation_matrix m (xyz(1.09850,	1.00000, 	0.35585), xyz (0.98074, 	1.00000, 	1.18232));
	  //m.print (stdout);

  spectrum_dyes_to_xyz spec;
  spec.set_backlight (spectrum_dyes_to_xyz::il_B);
  //spec.set_dyes_to_dufay_color_cinematography ();
  spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_harrison_horner);

  
  render_green_dyes (spec, "green.tif");
  render_blue_dyes (spec, "blue.tif");
  render_red_dyes (spec, "red.tif");
  printf ("synthetic dyes saved to red.tif, green.tif and blue.tif\n");
  luminosity_t best_green_d1=1000, best_green_d2=1000, best_green_l = 1;
  luminosity_t best_cgreen_d1=1000, best_cgreen_d2=1000, best_cgreen_l = 1;
  luminosity_t best_tgreen_d1=1000, best_tgreen_d2=1000, best_tgreen_l = 1;
  //luminosity_t best_sgreen_d1=1000, best_sgreen_d2=1000, best_sgreen_l = 1;
  luminosity_t best_blue_d1=1000, best_blue_d2=1000, best_blue_l = 1;
  luminosity_t best_cblue_d1=1000, best_cblue_d2=1000, best_cblue_l = 1;
  luminosity_t best_tblue_d1=1000, best_tblue_d2=1000, best_tblue_l = 1;
  //luminosity_t best_sblue_d1=1000, best_sblue_d2=1000, best_sblue_l = 1;
  luminosity_t best_red_d1=1000, best_red_d2=1000, best_red_l = 1;
  luminosity_t best_cred_d1=1000, best_cred_d2=1000, best_cred_l = 1;
  luminosity_t best_tred_d1=1000, best_tred_d2=1000, best_tred_l = 1;
  //luminosity_t best_sred_d1=1000, best_sred_d2=1000, best_sred_l = 1;
  xyz best_green (0,0,0);
  xyz best_cgreen (0,0,0);
  xyz best_tgreen (0,0,0);
  //xyz best_sgreen (0,0,0);
  xyz best_blue (0,0,0);
  xyz best_cblue (0,0,0);
  xyz best_tblue (0,0,0);
  //xyz best_sblue (0,0,0);
  xyz best_red (0,0,0);
  xyz best_cred (0,0,0);
  xyz best_tred (0,0,0);
  //xyz best_sred (0,0,0);
  spec.set_backlight (spectrum_dyes_to_xyz::il_C);
  spec.set_dyes (spectrum_dyes_to_xyz::dufaycolor_color_cinematography);
  xyz target_red = spec.dyes_rgb_to_xyz (1, 0, 0, 1931);
  xyz target_green = spec.dyes_rgb_to_xyz (0, 1, 0, 1931);
  xyz target_blue = spec.dyes_rgb_to_xyz (0, 0, 1, 1931);
  xyz white = spec.whitepoint_xyz ();
#if 0
  target_red.print (stdout);
  target_green.print (stdout);
  target_blue.print (stdout);
#endif
  for (luminosity_t d1 = 0; d1 < 2048; d1+=1)
    for (luminosity_t d2 = 0; d2 < 2048; d2+=1)
      {
        spec.synthetic_dufay_green (d1, d2);
	xyz green1 = spec.dyes_rgb_to_xyz (0, 1, 0, 1931);
        spec.synthetic_dufay_blue (d1, d2);
	xyz blue1 = spec.dyes_rgb_to_xyz (0, 0, 1, 1931);
        spec.synthetic_dufay_red (d1, d2);
	xyz red1 = spec.dyes_rgb_to_xyz (1, 0, 0, 1931);
	for (luminosity_t l = 0.95; l <= 1; l += 0.01)
	  {
	    xyz red = red1 * l;
	    xyz green = green1 * l;
	    xyz blue = blue1 * l;
	    if (deltaE (green, dufaycolor::green_dye_color_cinematography_xyY, white) < deltaE (best_green, dufaycolor::green_dye_color_cinematography_xyY, white))
	      {
		best_green = green;
		best_green_d1 = d1;
		best_green_d2 = d2;
		best_green_l = l;
	      }
	    if (deltaE (green, dufaycolor::green_dye, white) < deltaE (best_cgreen, dufaycolor::green_dye, white))
	      {
		best_cgreen = green;
		best_cgreen_d1 = d1;
		best_cgreen_d2 = d2;
		best_cgreen_l = l;
	      }
	    if (deltaE (green, target_green, white) < deltaE (best_tgreen, target_green, white))
	      {
		best_tgreen = green;
		best_tgreen_d1 = d1;
		best_tgreen_d2 = d2;
		best_tgreen_l = l;
	      }

	    if (deltaE (blue, dufaycolor::blue_dye_color_cinematography_xyY, white) < deltaE (best_blue, dufaycolor::blue_dye_color_cinematography_xyY, white))
	      {
		best_blue = blue;
		best_blue_d1 = d1;
		best_blue_d2 = d2;
		best_blue_l = l;
	      }
	    if (deltaE (blue, dufaycolor::blue_dye, white) < deltaE (best_cblue, dufaycolor::blue_dye, white))
	      {
		best_cblue = blue;
		best_cblue_d1 = d1;
		best_cblue_d2 = d2;
		best_cblue_l = l;
	      }
	    if (deltaE (blue, target_blue, white) < deltaE (best_tblue, target_blue, white))
	      {
		best_tblue = blue;
		best_tblue_d1 = d1;
		best_tblue_d2 = d2;
		best_tblue_l = l;
	      }

	    if (deltaE (red, dufaycolor::red_dye_color_cinematography_xyY, white) < deltaE (best_red, dufaycolor::red_dye_color_cinematography_xyY, white))
	      {
		best_red = red;
		best_red_d1 = d1;
		best_red_d2 = d2;
		best_red_l = l;
	      }
	    if (deltaE (red, dufaycolor::red_dye, white) < deltaE (best_cred, dufaycolor::red_dye, white))
	      {
		best_cred = red;
		best_cred_d1 = d1;
		best_cred_d2 = d2;
		best_cred_l = l;
	      }
	    if (deltaE (red, target_red, white) < deltaE (best_tred, target_red, white))
	      {
		best_tred = red;
		best_tred_d1 = d1;
		best_tred_d2 = d2;
		best_tred_l = l;
	      }
	  }
      }

  printf ("Best macthing red: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_red, dufaycolor::red_dye_color_cinematography_xyY, white), deltaE2000 (best_red, dufaycolor::red_dye_color_cinematography_xyY, white), best_red_d1, best_red_d2, best_red_l);
  best_red.print (stdout);
  spec.synthetic_dufay_red (best_red_d1, best_red_d2);
  spec.write_spectra ("synthetic-dufay-red.dat", NULL, NULL, NULL, 400, 720);
  spec.write_spectra ("synthetic-dufay-red.abs.txt", NULL, NULL, NULL, 400, 720, true);
  printf ("Best macthing red: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_cred, dufaycolor::red_dye, white), deltaE2000 (best_cred, dufaycolor::red_dye, white), best_cred_d1, best_cred_d2, best_cred_l);
  best_cred.print (stdout);
  spec.synthetic_dufay_red (best_cred_d1, best_cred_d2);
  spec.write_spectra ("synthetic-dufay-red-corrected.dat", NULL, NULL, NULL, 400, 720);
  spec.write_spectra ("synthetic-dufay-red-corrected.abs.txt", NULL, NULL, NULL, 400, 720, true);
  printf ("Best macthing red: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_tred, target_red, white), deltaE2000 (best_tred, target_red, white), best_tred_d1, best_tred_d2, best_tred_l);
  best_tred.print (stdout);
  spec.synthetic_dufay_red (best_tred_d1, best_tred_d2);
  spec.write_spectra ("synthetic-dufay-red-spectra.dat", NULL, NULL, NULL, 400, 720);
  spec.write_spectra ("synthetic-dufay-red-spectra.abs.txt", NULL, NULL, NULL, 400, 720, true);

  printf ("Best macthing green: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_green, dufaycolor::green_dye_color_cinematography_xyY, white),deltaE2000 (best_green, dufaycolor::green_dye_color_cinematography_xyY, white), best_green_d1, best_green_d2, best_green_l);
  best_green.print (stdout);
  spec.synthetic_dufay_green (best_green_d1, best_green_d2);
  spec.write_spectra (NULL, "synthetic-dufay-green.dat", NULL, NULL, 400, 720);
  spec.write_spectra (NULL, "synthetic-dufay-green.abs.txt", NULL, NULL, 400, 720, true);
  printf ("Best macthing green: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_cgreen, dufaycolor::green_dye, white), deltaE2000 (best_cgreen, dufaycolor::green_dye, white), best_cgreen_d1, best_cgreen_d2, best_cgreen_l);
  best_cgreen.print (stdout);
  spec.synthetic_dufay_green (best_cgreen_d1, best_cgreen_d2);
  spec.write_spectra (NULL, "synthetic-dufay-green-corrected.dat", NULL, NULL, 400, 720);
  spec.write_spectra (NULL, "synthetic-dufay-green-corrected.abs.txt", NULL, NULL, 400, 720, true);
  printf ("Best macthing green: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_tgreen, target_green, white), deltaE2000 (best_tgreen, target_green, white), best_tgreen_d1, best_tgreen_d2, best_tgreen_l);
  best_tgreen.print (stdout);
  spec.synthetic_dufay_green (best_tgreen_d1, best_tgreen_d2);
  spec.write_spectra (NULL, "synthetic-dufay-green-spectra.dat", NULL, NULL, 400, 720);
  spec.write_spectra (NULL, "synthetic-dufay-green-spectra.abs.txt", NULL, NULL, 400, 720, true);

  printf ("Best macthing blue: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_blue, dufaycolor::blue_dye_color_cinematography_xyY, white), deltaE2000 (best_blue, dufaycolor::blue_dye_color_cinematography_xyY, white), best_blue_d1, best_blue_d2, best_blue_l);
  best_blue.print (stdout);
  spec.synthetic_dufay_blue (best_blue_d1, best_blue_d2);
  spec.write_spectra (NULL, NULL, "synthetic-dufay-blue.dat", NULL, 400, 720);
  spec.write_spectra (NULL, NULL, "synthetic-dufay-blue.abs.txt", NULL, 400, 720, true);
  printf ("Best macthing blue: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_cblue, dufaycolor::blue_dye, white), deltaE2000 (best_cblue, dufaycolor::blue_dye, white), best_cblue_d1, best_cblue_d2, best_cblue_l);
  best_cblue.print (stdout);
  spec.synthetic_dufay_blue (best_cblue_d1, best_cblue_d2);
  spec.write_spectra (NULL, NULL, "synthetic-dufay-blue-corrected.dat", NULL, 400, 720);
  spec.write_spectra (NULL, NULL, "synthetic-dufay-blue-corrected.abs.txt", NULL, 400, 720, true);
  printf ("Best macthing blue: deltaE %f deltaE2k %f density1 %f density2 %f l %f ", deltaE (best_tblue, target_blue, white), deltaE2000 (best_tblue, target_blue, white), best_tblue_d1, best_tblue_d2, best_tblue_l);
  best_tblue.print (stdout);
  spec.synthetic_dufay_blue (best_tblue_d1, best_tblue_d2);
  spec.write_spectra (NULL, NULL, "synthetic-dufay-blue-spectra.dat", NULL, 400, 720);
  spec.write_spectra (NULL, NULL, "synthetic-dufay-blue-spectra.abs.txt", NULL, 400, 720, true);
}

struct optimized_matrix_params
{
  luminosity_t temperature;
  luminosity_t backlight_temperature;
  int type;
  bool
  operator==(optimized_matrix_params &o)
  {
    return temperature == o.temperature && backlight_temperature == o.backlight_temperature && type == o.type;
  }
};

color_matrix *
get_new_optimized_matrix (struct optimized_matrix_params &p, progress_info *progress)
{
  spectrum_dyes_to_xyz spec;
  spec.set_backlight (spectrum_dyes_to_xyz::il_D, p.temperature);
  initialize_spec (spec, p.type, true);
  spectrum_dyes_to_xyz view_spec;
  view_spec.set_backlight (spectrum_dyes_to_xyz::il_D, p.backlight_temperature);
  color_matrix *m = new color_matrix;
  *m = spec.optimized_xyz_matrix (&view_spec, progress);
  return m;
}
static lru_cache <optimized_matrix_params, color_matrix, color_matrix *, get_new_optimized_matrix, 10> color_matrix_cache ("color_matrix_cache");

color_matrix
dufaycolor_correction_color_cinematography_matrix (luminosity_t temperature, luminosity_t backlight_temperature, progress_info *progress)
{
  optimized_matrix_params p = {temperature, backlight_temperature, 0};
  color_matrix *m = color_matrix_cache.get (p, progress);
  color_matrix ret = *m;
  color_matrix_cache.release (m);
  return ret;
}

color_matrix
dufaycolor_correction_harrison_horner_matrix (luminosity_t temperature, luminosity_t backlight_temperature, progress_info *progress)
{
  optimized_matrix_params p = {temperature, backlight_temperature, 1};
  color_matrix *m = color_matrix_cache.get (p, progress);
  color_matrix ret = *m;
  color_matrix_cache.release (m);
  return ret;
}

color_matrix
dufaycolor_correction_photography_its_materials_and_processes_matrix (luminosity_t temperature, luminosity_t backlight_temperature, progress_info *progress)
{
  optimized_matrix_params p = {temperature, backlight_temperature, 2};
  color_matrix *m = color_matrix_cache.get (p, progress);
  color_matrix ret = *m;
  color_matrix_cache.release (m);
  return ret;
}

color_matrix
dufaycolor_correction_collins_and_giles_matrix (luminosity_t temperature, luminosity_t backlight_temperature, progress_info *progress)
{
  optimized_matrix_params p = {temperature, backlight_temperature, 3};
  color_matrix *m = color_matrix_cache.get (p, progress);
  color_matrix ret = *m;
  color_matrix_cache.release (m);
  return ret;
}
}
