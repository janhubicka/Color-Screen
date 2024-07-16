#include <locale>
#include "include/render-to-scr.h"
#include "include/scr-detect.h"
#include "include/solver.h"
#include "loadsave.h"
#define HEADER "screen_alignment_version: 1"

const char * const scr_names[max_scr_type] =
{
  "Random",
  "Paget",
  "Thames",
  "Finlay",
  "Dufay"
};

const char * const scanner_type_names[max_scanner_type] =
{
  "fixed-lens",
  "fixed-lens-horisontally-moving-sensor",
  "fixed-lens-vertically-moving-sensor",
  "horisontally-moving-lens",
  "vertically-moving-lens"
};

static const char * const bool_names[2] =
{
  "no",
  "yes"
};

bool
save_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam, solver_parameters *sparam)
{
  if (fprintf (f, "%s\n", HEADER) < 0)
    return false;
  /* TODO: hack.  */
  setlocale(LC_NUMERIC, "C");
  /* Save param.  */
  if (param)
    {
      if (fprintf (f, "screen_type: %s\n", scr_names [param->type]) < 0
	  || fprintf (f, "scanner_type: %s\n", scanner_type_names [param->scanner_type]) < 0
	  //|| fprintf (f, "lens_center: %f %f\n", param->lens_center_x, param->lens_center_y) < 0
	  || fprintf (f, "screen_shift: %f %f\n", param->center_x, param->center_y) < 0
	  || fprintf (f, "coordinate_x: %f %f\n", param->coordinate1_x, param->coordinate1_y) < 0
	  || fprintf (f, "coordinate_y: %f %f\n", param->coordinate2_x, param->coordinate2_y) < 0
	  || fprintf (f, "projection_distance: %f\n", param->projection_distance) < 0
	  /* Needs large precision close to zero - so use %g.  */
	  || fprintf (f, "tilt: %g %g\n", param->tilt_x, param->tilt_y) < 0
	  || fprintf (f, "final_rotation: %f\n", param->final_rotation) < 0
	  /* Needs large precision close to zero - so use %g.  */
	  || fprintf (f, "lens_warp_rectilinear: 1 %g %g %g %g 0 0 %g %g\n",
		      param->lens_correction.kr[0],
		      param->lens_correction.kr[1],
		      param->lens_correction.kr[2],
		      param->lens_correction.kr[3],
		      param->lens_correction.center.x,
		      param->lens_correction.center.y) < 0)
	return false;
      for (int i = 0; i < param->n_motor_corrections; i++)
	{
	  if (fprintf (f, "motor_correction: %f %f\n", param->motor_correction_x[i], param->motor_correction_y[i]) < 0)
	    return false;
	}
      if (param->mesh_trans)
        {
	  if (fprintf (f, "mesh: yes\n") < 0)
	    return false;
	  if (!param->mesh_trans->save (f))
	    return false;
        }
    }
  if (dparam)
    {
      if (fprintf (f, "scr_detect_red: %f %f %f\n", dparam->red.red, dparam->red.green, dparam->red.blue) < 0
	  || fprintf (f, "scr_detect_green: %f %f %f\n", dparam->green.red, dparam->green.green, dparam->green.blue) < 0
	  || fprintf (f, "scr_detect_blue: %f %f %f\n", dparam->blue.red, dparam->blue.green, dparam->blue.blue) < 0
	  || fprintf (f, "scr_detect_black: %f %f %f\n", dparam->black.red, dparam->black.green, dparam->black.blue) < 0
	  || fprintf (f, "scr_detect_min_luminosity: %f\n", dparam->min_luminosity) < 0
	  || fprintf (f, "scr_detect_min_ratio: %f\n", dparam->min_ratio) < 0
	  || fprintf (f, "scr_detect_sharpen_radius: %f\n", dparam->sharpen_radius) < 0
	  || fprintf (f, "scr_detect_sharpen_amount: %f\n", dparam->sharpen_amount) < 0)
	return false;
    }
  if (rparam)
    {
      if (fprintf (f, "gamma: %f\n", rparam->gamma) < 0
	  || fprintf (f, "film_gamma: %f\n", rparam->film_gamma) < 0
	  || fprintf (f, "target_film_gamma: %f\n", rparam->target_film_gamma) < 0
	  || fprintf (f, "white_balance: %f %f %f\n", rparam->white_balance.red, rparam->white_balance.green, rparam->white_balance.blue) < 0
	  || fprintf (f, "sharpen_radius: %f\n", rparam->sharpen_radius) < 0
	  || fprintf (f, "sharpen_amount: %f\n", rparam->sharpen_amount) < 0
	  || fprintf (f, "presaturation: %f\n", rparam->presaturation) < 0
	  || fprintf (f, "saturation: %f\n", rparam->saturation) < 0
	  || fprintf (f, "brightness: %f\n", rparam->brightness) < 0
	  || fprintf (f, "output_tone_curve: %s\n", tone_curve::tone_curve_names[(int)rparam->output_tone_curve]) < 0
	  || fprintf (f, "dufay_red_strip_width: %f\n", rparam->dufay_red_strip_width) < 0
	  || fprintf (f, "dufay_green_strip_width: %f\n", rparam->dufay_green_strip_width) < 0
	  || fprintf (f, "collection_threshold: %f\n", rparam->collection_threshold) < 0
	  || fprintf (f, "screen_blur_radius: %f\n", rparam->screen_blur_radius) < 0
	  || fprintf (f, "collection_threshold: %f\n", rparam->collection_threshold) < 0
	  || fprintf (f, "color_model: %s\n", render_parameters::color_model_names [rparam->color_model]) < 0
	  || fprintf (f, "backlight_temperature: %f\n", rparam->backlight_temperature) < 0
	  || fprintf (f, "temperature: %f\n", rparam->temperature) < 0
	  || fprintf (f, "dye_balance: %s\n", render_parameters::dye_balance_names [rparam->dye_balance]) < 0
	  //|| fprintf (f, "gray_range: %i %i\n", rparam->gray_min, rparam->gray_max) < 0
	  || fprintf (f, "scan_exposure: %f\n", rparam->scan_exposure) < 0
	  || fprintf (f, "dark_point: %f\n", rparam->dark_point) < 0
	  || fprintf (f, "backlight_correction_black: %f\n", rparam->backlight_correction_black) < 0
	  || fprintf (f, "invert: %s\n", bool_names [(int)rparam->invert]) < 0
	  || fprintf (f, "precise: %s\n", bool_names [(int)rparam->precise]) < 0
	  || fprintf (f, "mix_weights: %f %f %f\n", rparam->mix_red, rparam->mix_green, rparam->mix_blue) < 0
	  || fprintf (f, "mix_dark: %f %f %f\n", rparam->mix_dark.red, rparam->mix_dark.green, rparam->mix_dark.blue) < 0
	  || fprintf (f, "profiled_dark: %f %f %f\n", rparam->profiled_dark.red, rparam->profiled_dark.green, rparam->profiled_dark.blue) < 0
	  || fprintf (f, "profiled_red: %f %f %f\n", rparam->profiled_red.red, rparam->profiled_red.green, rparam->profiled_red.blue) < 0
	  || fprintf (f, "profiled_green: %f %f %f\n", rparam->profiled_green.red, rparam->profiled_green.green, rparam->profiled_green.blue) < 0
	  || fprintf (f, "profiled_blue: %f %f %f\n", rparam->profiled_blue.red, rparam->profiled_blue.green, rparam->profiled_blue.blue) < 0
	  || fprintf (f, "scanner_red: %f %f %f\n", rparam->scanner_red.x, rparam->scanner_red.y, rparam->scanner_red.z) < 0
	  || fprintf (f, "scanner_green: %f %f %f\n", rparam->scanner_green.x, rparam->scanner_green.y, rparam->scanner_green.z) < 0
	  || fprintf (f, "scanner_blue: %f %f %f\n", rparam->scanner_blue.x, rparam->scanner_blue.y, rparam->scanner_blue.z) < 0)
	return false;
      if (rparam->backlight_correction)
	{
	  if (fprintf (f, "backlight_correction: yes\n") < 0)
	    return false;
	  if (!rparam->backlight_correction->save (f))
	    return false;
	}
      if (rparam->tile_adjustments_width)
	{
	  if (fprintf (f, "tile_adjustments_dimensions: %i %i\n", rparam->tile_adjustments_width, rparam->tile_adjustments_height) < 0)
	    return false;
	  for (int y = 0; y < rparam->tile_adjustments_height; y++)
	    for (int x = 0; x < rparam->tile_adjustments_width; x++)
	      if (fprintf (f, "tile_adjustment_exposure: %i %i %f\n", x, y, rparam->get_tile_adjustment (x, y).exposure) < 0
	          || fprintf (f, "tile_adjustment_dark_point: %i %i %f\n", x, y, rparam->get_tile_adjustment (x, y).dark_point) < 0)
		return false;
	}
    }
  if (sparam)
    {
      if (fprintf (f, "solver_optimize_lens: %s\n", bool_names [(int)sparam->optimize_lens]) < 0
	  || fprintf (f, "solver_optimize_tilt: %s\n", bool_names [(int)sparam->optimize_tilt]) < 0)
	return false;
      for (int i = 0; i < sparam->npoints; i++)
	{
	  if (fprintf (f, "solver_point: %f %f %f %f %s\n",
		       sparam->point[i].img_x,
		       sparam->point[i].img_y,
		       sparam->point[i].screen_x,
		       sparam->point[i].screen_y,
		       solver_parameters::point_color_names [(int)sparam->point[i].color]) < 0)
	   return false;
	}
    }
  if (fprintf (f, "screen_alignment_end\n") < 0)
    {
      return false;
    }
  return true;
}
static bool
skipwhitespace (FILE *f)
{
  while (!feof (f))
    {
      int c = getc (f);
      if (c == EOF || c == '\n')
	return true;
      if (c != '\r' && !isspace (c))
	{
	  ungetc (c, f);
	  return true;
	}
    }
  return true;
}

static void
get_keyword (FILE *f, char *buf)
{
  int l;
  skipwhitespace (f);
  for (l = 0; ; l++)
    {
      if (l == 255)
	{
	  buf[l] = 0;
	  return;
	}
      int c = getc (f);
      if (c == EOF || (!isalnum (c) && c != '-' && c != '_'))
	{
	  buf[l] = 0;
	  return;
	}
      buf[l] = c;
    }
}

bool
parse_bool (FILE *f, bool *val)
{
  skipwhitespace (f);
  int c = getc (f);
  if (c == 'y')
    {
      if (getc (f) != 'e' || getc (f) != 's')
	return false;
      if (val)
        *val = true;
      return true;
    }
  else if (c == 'n')
    {
      if (getc (f) != 'o')
	return false;
      if (val)
        *val = false;
      return true;
    }
  return false;
}

bool
read_scalar (FILE *f, coord_t *ll)
{
  double l;
  if (fscanf (f, "%lf\n", &l) != 1)
    return false;
  if (ll)
    *ll = l;
  return true;
}

static bool
read_luminosity (FILE *f, luminosity_t *ll)
{
  double l;
  if (fscanf (f, "%lf\n", &l) != 1)
    return false;
  if (ll)
    *ll = l;
  return true;
}

static bool
read_vector (FILE *f, coord_t *xx, coord_t *yy)
{
  double x, y;
  if (fscanf (f, "%lf %lf\n", &x, &y) != 2)
    return false;
  if (xx)
    *xx = x;
  if (yy)
    *yy = y;
  return true;
}

static bool
read_rgb (FILE *f, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
{
  double r, g, b;
  if (fscanf (f, "%lf %lf %lf\n", &r, &g, &b) != 3)
    return false;
  if (rr)
    *rr = r;
  if (gg)
    *gg = g;
  if (bb)
    *bb = b;
  return true;
}
static bool
read_xyz (FILE *f, xyz *c)
{
  double x, y, z;
  if (fscanf (f, "%lf %lf %lf\n", &x, &y, &z) != 3)
    return false;
  if (c)
    *c = {x, y, z};
  return true;
}
static bool
read_color (FILE *f, color_t *c)
{
  return read_rgb (f, c ? &c->red : NULL, c ? &c->green : NULL, c ? &c->blue : NULL);
}

bool
expect_keyword (FILE *f, const char *keyword)
{
  skipwhitespace (f);
  for (int i = 0; keyword[i]; i++)
    {
      if (feof (f))
	{
	  for (int j = 0; j < i; j++)
	    ungetc (keyword[i], f);
	  return false;
	}
      char c = getc (f);
      if (!i && (c == '\r' || isspace (c)))
	{
	  skipwhitespace (f);
	  if (feof (f))
	    return false;
	  c = getc (f);
	}

      if (c != keyword[i])
        {
	  for (int j = 0; j < i; j++)
	    ungetc (keyword[i], f);
	  ungetc (c, f);
	  return false;
        }
    }
  return true;
}

#define param_check(name) param ? &param->name : NULL
#define rparam_check(name) rparam ? &rparam->name : NULL
#define dparam_check(name) dparam ? &dparam->name : NULL
#define sparam_check(name) sparam ? &sparam->name : NULL

bool
load_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam, solver_parameters *sparam, const char **error)
{
  char buf[256];
  skipwhitespace (f);
  int gray_min = -1;
  int gray_max = -1;
  if (fread (buf, 1, strlen (HEADER), f) < 0
      || memcmp (buf, HEADER, strlen (HEADER)))
    {
      *error = "first line should be " HEADER;
      return false;
    }
  /* TODO: hack.  */
  setlocale(LC_NUMERIC, "C");
  if (rparam)
    rparam->set_tile_adjustments_dimensions (0, 0);
  while (!feof (f))
    {
      char buf[256];
      char buf2[256];
      int l;
      skipwhitespace (f);
      for (l = 0; ; l++)
	{
 	  if (l == 255)
	    {
	      *error = "too long keyword";
	      return false;
	    }
	  int c = getc (f);
	  if (c == EOF || c=='\n' || c=='\r')
	    {
	      buf[l]=0;
	      if (!l && c == EOF)
		{
		  l = -1;
		  break;
		}
	      if (!strcmp (buf, "screen_alignment_end"))
		{
		  l = -1;
		  break;
		}
	      *error = "file truncated";
	      return false;
	    }
	  if (c == ':')
	    {
	      buf[l] = 0;
	      break;
	    }
	  buf[l] = c;
	}
      if (l < 0)
        break;
      if (!strcmp (buf, "screen_type"))
	{
	  get_keyword (f, buf2);
	  int j;
	  for (j = 0; j < max_scr_type; j++)
	    if (!strcmp (buf2, scr_names[j]))
	      break;
	  if (j == max_scr_type
	      && !strcmp (buf2, "PagetFinlay"))
	    j = Finlay;
	  if (j == max_scr_type)
	    {
	      *error = "unknown screen type";
	      return false;
	    }
	  if (param)
	    param->type = (enum scr_type) j;
	}
      else if (!strcmp (buf, "scanner_type"))
	{
	  get_keyword (f, buf2);
	  int j;
	  for (j = 0; j < max_scanner_type; j++)
	    if (!strcmp (buf2, scanner_type_names[j]))
	      break;
	  if (j == max_scanner_type)
	    {
	      *error = "unknown scanner type";
	      return false;
	    }
	  if (param)
	    param->scanner_type = (enum scanner_type) j;
	}
      else if (!strcmp (buf, "lens_warp_rectilinear"))
        {
	  coord_t a,t1,t2;
	  if (!read_scalar (f, &a)
	      || a != 1
	      || !read_scalar (f, param_check (lens_correction.kr[0]))
	      || !read_scalar (f, param_check (lens_correction.kr[1]))
	      || !read_scalar (f, param_check (lens_correction.kr[2]))
	      || !read_scalar (f, param_check (lens_correction.kr[3]))
	      || !read_scalar (f, &t1)
	      || t1
	      || !read_scalar (f, &t2)
	      || t2
	      || !read_vector (f, param_check (lens_correction.center.x), param_check (lens_correction.center.y)))
	    {
	      *error = "error parsing lens_warp_rectililnear";
	      return false;
	    }
        }
      /* Old lens correction parameter; now ignored.  */
      else if (!strcmp (buf, "lens_center"))
	{
	  if (!read_vector (f, /*param_check (lens_center_x), param_check (lens_center_y)*/ NULL, NULL))
	    {
	      *error = "error parsing screen_shift";
	      return false;
	    }
	}
      else if (!strcmp (buf, "screen_shift"))
	{
	  if (!read_vector (f, param_check (center_x), param_check (center_y)))
	    {
	      *error = "error parsing screen_shift";
	      return false;
	    }
	}
      else if (!strcmp (buf, "coordinate_x"))
	{
	  if (!read_vector (f, param_check (coordinate1_x), param_check (coordinate1_y)))
	    {
	      *error = "error parsing coordinate_x";
	      return false;
	    }
	}
      else if (!strcmp (buf, "coordinate_y"))
	{
	  if (!read_vector (f, param_check (coordinate2_x), param_check (coordinate2_y)))
	    {
	      *error = "error parsing coordinate_y";
	      return false;
	    }
	}
      else if (!strcmp (buf, "motor_correction"))
	{
	  coord_t x, y;
	  if (!read_vector (f, &x, &y))
	    {
	      *error = "error parsing motor_correction";
	      return false;
	    }
          if (param)
	    param->add_motor_correction_point (x, y);
	}
      else if (!strcmp (buf, "projection_distance"))
	{
	  if (!read_scalar (f, param_check (projection_distance)))
	    {
	      *error = "error parsing projection_distance";
	      return false;
	    }
	}
      else if (!strcmp (buf, "tilt"))
	{
	  if (!read_vector (f, param_check (tilt_x), param_check (tilt_y)))
	    {
	      *error = "error parsing tilt";
	      return false;
	    }
	}
      else if (!strcmp (buf, "final_rotation"))
	{
	  if (!read_scalar (f, param_check (final_rotation)))
	    {
	      *error = "error parsing final_rotation";
	      return false;
	    }
	}
      /* Old lens correction parameter; now ignored.  */
      else if (!strcmp (buf, "k1"))
	{
	  if (!read_scalar (f, /*param_check (k1)*/ NULL))
	    {
	      *error = "error parsing k1";
	      return false;
	    }
	}
      else if (!strcmp (buf, "gamma"))
	{
	  if (!read_luminosity (f, rparam_check (gamma)))
	    {
	      *error = "error parsing gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "film_gamma"))
	{
	  if (!read_luminosity (f, rparam_check (film_gamma)))
	    {
	      *error = "error parsing film_gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "target_film_gamma"))
	{
	  if (!read_luminosity (f, rparam_check (target_film_gamma)))
	    {
	      *error = "error parsing target_film_gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "white_balance"))
	{
	  if (!read_color (f, rparam_check (white_balance)))
	    {
	      *error = "error parsing scr_detect_green";
	      return false;
	    }
	}
      else if (!strcmp (buf, "sharpen_radius"))
	{
	  if (!read_luminosity (f, rparam_check (sharpen_radius)))
	    {
	      *error = "error parsing sharpen_radius";
	      return false;
	    }
	}
      else if (!strcmp (buf, "sharpen_amount"))
	{
	  if (!read_luminosity (f, rparam_check (sharpen_amount)))
	    {
	      *error = "error parsing sharpen_amount";
	      return false;
	    }
	}
      else if (!strcmp (buf, "presaturation"))
	{
	  if (!read_luminosity (f, rparam_check (presaturation)))
	    {
	      *error = "error parsing presaturation";
	      return false;
	    }
	}
      else if (!strcmp (buf, "saturation"))
	{
	  if (!read_luminosity (f, rparam_check (saturation)))
	    {
	      *error = "error parsing saturation";
	      return false;
	    }
	}
      else if (!strcmp (buf, "brightness"))
	{
	  if (!read_luminosity (f, rparam_check (brightness)))
	    {
	      *error = "error parsing brightness";
	      return false;
	    }
	}
      else if (!strcmp (buf, "output_tone_curve"))
	{
	  get_keyword (f, buf2);
	  int j;
	  for (j = 0; j < (int)tone_curve::tone_curve_max; j++)
	    if (!strcmp (buf2, tone_curve::tone_curve_names[j]))
	      break;
	  if (j == tone_curve::tone_curve_max)
	    {
	      *error = "unknown output tone curve";
	      return false;
	    }
	  if (rparam)
	    rparam->output_tone_curve = (tone_curve::tone_curves)j;
	}
      else if (!strcmp (buf, "dufay_red_strip_width"))
	{
	  if (!read_luminosity (f, rparam_check (dufay_red_strip_width)))
	    {
	      *error = "error parsing red strip width";
	      return false;
	    }
	}
      else if (!strcmp (buf, "dufay_green_strip_width"))
	{
	  if (!read_luminosity (f, rparam_check (dufay_green_strip_width)))
	    {
	      *error = "error parsing green strip width";
	      return false;
	    }
	}
      else if (!strcmp (buf, "mix_weights"))
	{
	  if (!read_rgb (f, rparam_check (mix_red), rparam_check (mix_green), rparam_check (mix_blue)))
	    {
	      *error = "error parsing mix_weights";
	      return false;
	    }
	}
      else if (!strcmp (buf, "mix_dark"))
	{
	  if (!read_rgb (f, rparam_check (mix_dark.red), rparam_check (mix_dark.green), rparam_check (mix_dark.blue)))
	    {
	      *error = "error parsing mix_weights";
	      return false;
	    }
	}
      else if (!strcmp (buf, "profiled_dark"))
	{
	  if (!read_color (f, rparam_check (profiled_dark)))
	    {
	      *error = "error parsing profiled_dark";
	      return false;
	    }
	}
      else if (!strcmp (buf, "profiled_red"))
	{
	  if (!read_color (f, rparam_check (profiled_red)))
	    {
	      *error = "error parsing profiled_red";
	      return false;
	    }
	}
      else if (!strcmp (buf, "profiled_green"))
	{
	  if (!read_color (f, rparam_check (profiled_green)))
	    {
	      *error = "error parsing profiled_green";
	      return false;
	    }
	}
      else if (!strcmp (buf, "profiled_blue"))
	{
	  if (!read_color (f, rparam_check (profiled_blue)))
	    {
	      *error = "error parsing profiled_blue";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scanner_red"))
	{
	  if (!read_xyz (f, rparam_check (scanner_red)))
	    {
	      *error = "error parsing scanner_red";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scanner_green"))
	{
	  if (!read_xyz (f, rparam_check (scanner_green)))
	    {
	      *error = "error parsing scanner_green";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scanner_blue"))
	{
	  if (!read_xyz (f, rparam_check (scanner_blue)))
	    {
	      *error = "error scanned_blue";
	      return false;
	    }
	}
      /* Handle typo which was present in old save implementation.  */
      else if (!strcmp (buf, "scren_blur_radius") || !strcmp (buf, "screen_blur_radius"))
	{
	  if (!read_scalar (f, rparam_check (screen_blur_radius)))
	    {
	      *error = "error parsing screen_blur_radius";
	      return false;
	    }
	}
      else if (!strcmp (buf, "collection_threshold"))
	{
	  if (!read_scalar (f, rparam_check (collection_threshold)))
	    {
	      *error = "error parsing collection_threshold";
	      return false;
	    }
	}
      else if (!strcmp (buf, "tile_adjustments_dimensions"))
	{
	  int w, h;
	  if (fscanf (f, "%i %i", &w, &h) != 2
	      || w <= 0
	      || h <= 0)
	    {
	      *error = "error parsing tile_adjustments_dimensions";
	      return false;
	    }
	  if (rparam)
	    rparam->set_tile_adjustments_dimensions (w, h);
	}
      else if (!strcmp (buf, "tile_adjustment_exposure"))
	{
	  int x, y;
	  luminosity_t val;
	  if (fscanf (f, "%i %i", &x, &y) != 2
	      || x < 0
	      || y < 0
	      || !read_luminosity (f, &val))
	    {
	      *error = "error parsing tile_adjustment_exposure";
	      return false;
	    }
	  if (rparam)
	    {
	      if (x >= rparam->tile_adjustments_width
		  || y >= rparam->tile_adjustments_height)
		{
		  *error = "tile_adjustment_exposure out of range";
		  return false;
		}
	      rparam->get_tile_adjustment (x, y).exposure = val;
	    }
	}
      else if (!strcmp (buf, "tile_adjustment_dark_point"))
	{
	  int x, y;
	  luminosity_t val;
	  if (fscanf (f, "%i %i", &x, &y) != 2
	      || x < 0
	      || y < 0
	      || !read_luminosity (f, &val))
	    {
	      *error = "error parsing tile_adjustment_dark_point";
	      return false;
	    }
	  if (rparam)
	    {
	      if (x >= rparam->tile_adjustments_width
		  || y >= rparam->tile_adjustments_height)
		{
		  *error = "tile_adjustment_exposure out of range";
		  return false;
		}
	      rparam->get_tile_adjustment (x, y).dark_point = val;
	    }
	}
      else if (!strcmp (buf, "color_model"))
	{
	  get_keyword (f, buf2);
	  int j;
	  for (j = 0; j < render_parameters::color_model_max; j++)
	    if (!strcmp (buf2, render_parameters::color_model_names[j]))
	      break;
	  if (j == render_parameters::color_model_max)
	    {
	      *error = "unknown color model";
	      return false;
	    }
	  if (rparam)
	    rparam->color_model = (enum render_parameters::color_model_t) j;
	}
      else if (!strcmp (buf, "dye_balance"))
	{
	  get_keyword (f, buf2);
	  int j;
	  for (j = 0; j < render_parameters::dye_balance_max; j++)
	    if (!strcmp (buf2, render_parameters::dye_balance_names[j]))
	      break;
	  if (j == render_parameters::dye_balance_max)
	    {
	      *error = "unknown color model";
	      return false;
	    }
	  if (rparam)
	    rparam->dye_balance = (enum render_parameters::dye_balance_t) j;
	}
      else if (!strcmp (buf, "temperature"))
	{
	  if (!read_luminosity (f, rparam_check (temperature)))
	    {
	      *error = "error parsing temperature";
	      return false;
	    }
	}
      else if (!strcmp (buf, "backlight_temperature"))
	{
	  if (!read_luminosity (f, rparam_check (backlight_temperature)))
	    {
	      *error = "error parsing backlight temperature";
	      return false;
	    }
	}
      /* This is for compatibility with old files; remove it eventually.  */
      else if (!strcmp (buf, "gray_range"))
	{
	  if (fscanf (f, "%i %i\n", &gray_min, &gray_max) != 2)
	    {
	      *error = "error parsing gray_range";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scan_exposure"))
	{
	  if (!read_luminosity (f, rparam_check (scan_exposure)))
	    {
	      *error = "error parsing scan_exposure";
	      return false;
	    }
	}
      else if (!strcmp (buf, "dark_point"))
	{
	  if (!read_luminosity (f, rparam_check (dark_point)))
	    {
	      *error = "error parsing dark_point";
	      return false;
	    }
	}
      else if (!strcmp (buf, "invert"))
	{
	  if (!parse_bool (f, rparam_check (invert)))
	    {
	      *error = "error parsing invert";
	      return false;
	    }
	}
      else if (!strcmp (buf, "precise"))
	{
	  if (!parse_bool (f, rparam_check (precise)))
	    {
	      *error = "error parsing precise";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_red"))
	{
	  if (!read_color (f, dparam_check (red)))
	    {
	      *error = "error parsing scr_detect_red";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_green"))
	{
	  if (!read_color (f, dparam_check (green)))
	    {
	      *error = "error parsing scr_detect_green";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_blue"))
	{
	  if (!read_color (f, dparam_check (blue)))
	    {
	      *error = "error parsing scr_detect_blue";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_black"))
	{
	  if (!read_color (f, dparam_check (black)))
	    {
	      *error = "error parsing scr_detect_black";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_min_luminosity"))
	{
	  if (!read_luminosity (f, dparam_check (min_luminosity)))
	    {
	      *error = "error parsing scr_detect_min_luminosity";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_min_ratio"))
	{
	  if (!read_luminosity (f, dparam_check (min_ratio)))
	    {
	      *error = "error parsing scr_detect_min_ratio";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_sharpen_radius"))
	{
	  if (!read_scalar (f, dparam_check (sharpen_radius)))
	    {
	      *error = "error parsing scr_detect_sharpen_radius";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scr_detect_sharpen_amount"))
	{
	  if (!read_luminosity (f, dparam_check (sharpen_amount)))
	    {
	      *error = "error parsing scr_detect_sharpen_amount";
	      return false;
	    }
	}
      else if (!strcmp (buf, "solver_optimize_lens"))
	{
	  if (!parse_bool (f, sparam_check (optimize_lens)))
	    {
	      *error = "error parsing solver_optimize_lens";
	      return false;
	    }
	}
      else if (!strcmp (buf, "solver_optimize_tilt"))
	{
	  if (!parse_bool (f, sparam_check (optimize_tilt)))
	    {
	      *error = "error parsing solver_optimize_tilt";
	      return false;
	    }
	}
      else if (!strcmp (buf, "solver_point"))
	{
	  double screen_x, screen_y, img_x, img_y;
	  if (fscanf (f, "%lf %lf %lf %lf", &img_x, &img_y, &screen_x, &screen_y) == 4)
	    {
	      get_keyword (f, buf2);
	      int j;
	      for (j = 0; j < solver_parameters::max_point_color; j++)
		if (!strcmp (buf2, solver_parameters::point_color_names[j]))
		  break;
	      if (j == solver_parameters::max_point_color)
		{
		  *error = "error parsing color in solver_point";
		  return false;
		}
	      if (sparam)
		sparam->add_point (img_x, img_y, screen_x, screen_y, (solver_parameters::point_color)j);
	    }
	  else
	    {
	      *error = "error parsing solver_point";
	      return false;
	    }
	}
      /* Silently ignore; we used to save these but we no longer need them.  */
      else if (!strcmp (buf, "screen_compensation"))
	{
	  if (!parse_bool (f, NULL))
	    {
	      *error = "error parsing screen_compensation";
	      return false;
	    }
	}
      else if (!strcmp (buf, "adjust_luminosity"))
	{
	  if (!parse_bool (f, NULL))
	    {
	      *error = "error parsing adjust_luminosity";
	      return false;
	    }
	}
      else if (!strcmp (buf, "mix_gamma"))
	{
	  if (!read_luminosity (f, NULL))
	    {
	      *error = "error parsing mix_gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "backlight_correction_black"))
	{
	  if (!read_luminosity (f, rparam_check (backlight_correction_black)))
	    {
	      *error = "error parsing mix_gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "backlight_correction"))
	{
	  bool correction;
	  if (!parse_bool (f, &correction))
	    {
	      *error = "error parsing backlight_correction";
	      return false;
	    }
	  if (correction)
	    {
	      backlight_correction_parameters *c = new backlight_correction_parameters ();
	      if (!c->load (f, error))
		return false;
	      if (rparam)
		rparam->backlight_correction = c;
	      else
		delete c;
	    }
	}
      else if (!strcmp (buf, "scr_detect_gamma"))
	{
	  if (!read_luminosity (f, NULL))
	    {
	      *error = "error parsing scr_detect_gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "tilt_x"))
	{
	  if (!read_vector (f, NULL, NULL))
	    {
	      *error = "error parsing tilt_x";
	      return false;
	    }
	}
      else if (!strcmp (buf, "tilt_y"))
	{
	  if (!read_vector (f, NULL, NULL))
	    {
	      *error = "error parsing tilt_y";
	      return false;
	    }
	}
      else if (!strcmp (buf, "mesh"))
	{
	  bool b;
	  if (!parse_bool (f, &b))
	    {
	      *error = "error parsing mesh";
	      return false;
	    }
	  if (b)
	    {
	      mesh *m = mesh::load (f, error);
	      if (!m)
		return false;
	      if (param)
		{
		  m->precompute_inverse ();
		  param->mesh_trans = m;
		}
	      else
		delete m;
	    }
	}
      else
	{
	  *error = "unexpected keyword";
	  fprintf (stderr, "keyword:%s\n", buf);
	  return false;
	}
      if (isspace (buf[l]) || buf[l] == '\n')
	{
	  *error = "trailing charactrs";
	  return false;
	}
      skipwhitespace (f);
    }
  if (rparam && gray_min >=0)
    rparam->set_gray_range (gray_min, gray_max, 65535);
  return true;
}
