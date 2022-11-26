#include <locale>
#include "include/render-to-scr.h"
#include "include/scr-detect.h"
#define HEADER "screen_alignment_version: 1"

static const char * const scr_names[max_scr_type] =
{
  "Paget",
  "Thames",
  "Finlay",
  "Dufay"
};

const char * const scanner_type_names[max_scanner_type] =
{
  "fixed-lens",
  "horisontally-moving-lens",
  "vertically-moving-lens"
};

static const char * const bool_names[2] =
{
  "yes",
  "no"
};

bool
save_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam)
{
  if (fprintf (f, HEADER) < 0)
    return false;
  /* TODO: hack.  */
  setlocale(LC_NUMERIC, "C");
  /* Save param.  */
  if (param)
    {
      if (fprintf (f, "screen_type: %s\n", scr_names [param->type]) < 0
	  || fprintf (f, "scanner_type: %s\n", scanner_type_names [param->scanner_type])
	  || fprintf (f, "screen_shift: %f %f\n", param->center_x, param->center_y) < 0
	  || fprintf (f, "coordinate_x: %f %f\n", param->coordinate1_x, param->coordinate1_y) < 0
	  || fprintf (f, "coordinate_y: %f %f\n", param->coordinate2_x, param->coordinate2_y) < 0
	  || fprintf (f, "tilt_x: %f %f\n", param->tilt_x_x, param->tilt_x_y) < 0
	  || fprintf (f, "tilt_y: %f %f\n", param->tilt_y_x, param->tilt_y_y) < 0
	  || fprintf (f, "k1: %f\n", param->k1) < 0)
	return false;
      for (int i = 0; i < param->n_motor_corrections; i++)
	{
	  if (fprintf (f, "motor_correction: %f %f\n", param->motor_correction_x[i], param->motor_correction_y[i]) < 0)
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
	  || fprintf (f, "scr_detect_min_ratio: %f\n", dparam->min_ratio) < 0)
	return false;
    }
  if (rparam)
    {
      if (fprintf (f, "gamma: %f\n", rparam->gamma) < 0
	  || fprintf (f, "white_balance: %f %f %f\n", rparam->white_balance.red, rparam->white_balance.green, rparam->white_balance.blue) < 0
	  || fprintf (f, "sharpen_radius: %f\n", rparam->sharpen_radius) < 0
	  || fprintf (f, "sharpen_amount: %f\n", rparam->sharpen_amount) < 0
	  || fprintf (f, "presaturation: %f\n", rparam->presaturation) < 0
	  || fprintf (f, "saturation: %f\n", rparam->saturation) < 0
	  || fprintf (f, "brightness: %f\n", rparam->brightness) < 0
	  || fprintf (f, "scren_blur_radius: %f\n", rparam->screen_blur_radius) < 0
	  || fprintf (f, "color_model: %s\n", render_parameters::color_model_names [rparam->color_model]) < 0
	  || fprintf (f, "backlight_temperature: %f\n", rparam->backlight_temperature) < 0
	  || fprintf (f, "dye_balance: %s\n", render_parameters::dye_balance_names [rparam->dye_balance]) < 0
	  || fprintf (f, "gray_range: %i %i\n", rparam->gray_min, rparam->gray_max) < 0
	  || fprintf (f, "precise: %s\n", bool_names [(int)rparam->precise]) < 0
	  || fprintf (f, "mix_weights: %f %f %f\n", rparam->mix_red, rparam->mix_green, rparam->mix_blue) < 0)
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

static bool
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
read_color (FILE *f, color_t *c)
{
  return read_rgb (f, c ? &c->red : NULL, c ? &c->green : NULL, c ? &c->blue : NULL);
}

#define param_check(name) param ? &param->name : NULL
#define rparam_check(name) rparam ? &rparam->name : NULL
#define dparam_check(name) dparam ? &dparam->name : NULL

bool
load_csp (FILE *f, scr_to_img_parameters *param, scr_detect_parameters *dparam, render_parameters *rparam, const char **error)
{
  char buf[256];
  if (fread (buf, 1, strlen (HEADER), f) < 0
      || memcmp (buf, HEADER, strlen (HEADER)))
    {
      *error = "first line should be " HEADER;
      return false;
    }
  /* TODO: hack.  */
  setlocale(LC_NUMERIC, "C");
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
	  if (c == EOF)
	    {
	      if (!l)
		return true;
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
      if (!strcmp (buf, "scanner_type"))
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
	    param->type = (enum scr_type) j;
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
      else if (!strcmp (buf, "tilt_x"))
	{
	  if (!read_vector (f, param_check (tilt_x_x), param_check (tilt_x_y)))
	    {
	      *error = "error parsing tilt_x";
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
      else if (!strcmp (buf, "tilt_y"))
	{
	  if (!read_vector (f, param_check (tilt_y_x), param_check (tilt_y_y)))
	    {
	      *error = "error parsing tilt_y";
	      return false;
	    }
	}
      else if (!strcmp (buf, "k1"))
	{
	  if (!read_scalar (f, param_check (k1)))
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
      else if (!strcmp (buf, "mix_weights"))
	{
	  if (!read_rgb (f, rparam_check (mix_red), rparam_check (mix_green), rparam_check (mix_blue)))
	    {
	      *error = "error parsing mix_weights";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scren_blur_radius"))
	{
	  if (!read_scalar (f, rparam_check (screen_blur_radius)))
	    {
	      *error = "error parsing screen_blur_radius";
	      return false;
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
      else if (!strcmp (buf, "backlight_temperature"))
	{
	  if (!read_luminosity (f, rparam_check (backlight_temperature)))
	    {
	      *error = "error parsing backlight temperature";
	      return false;
	    }
	}
      else if (!strcmp (buf, "gray_range"))
	{
	  if (fscanf (f, "%i %i\n", rparam_check (gray_min), rparam_check (gray_max)) != 2)
	    {
	      *error = "error parsing gray_range";
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
      else if (!strcmp (buf, "scr_detect_gamma"))
	{
	  if (!read_luminosity (f, NULL))
	    {
	      *error = "error parsing scr_detect_gamma";
	      return false;
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
  return true;
}
