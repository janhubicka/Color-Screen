#include <locale>
#include "include/render.h"
#define HEADER "screen_alignment_version: 1\n"

static const char * const scr_names[max_scr_type] =
{
  "Paget",
  "Thames",
  "Finlay",
  "Dufay"
};

static const char * const bool_names[2] =
{
  "yes",
  "no"
};

bool
save_csp (FILE *f, scr_to_img_parameters &param, render_parameters &rparam)
{
  /* Save param.  */
  if (fprintf (f, HEADER) < 0
      || fprintf (f, "screen_type: %s\n", scr_names [param.type]) < 0
      || fprintf (f, "screen_shift: %f %f\n", param.center_x, param.center_y) < 0
      || fprintf (f, "coordinate_x: %f %f\n", param.coordinate1_x, param.coordinate1_y) < 0
      || fprintf (f, "coordinate_y: %f %f\n", param.coordinate2_x, param.coordinate2_y) < 0
      || fprintf (f, "tilt_x: %f %f\n", param.tilt_x_x, param.tilt_x_y) < 0
      || fprintf (f, "tilt_y: %f %f\n", param.tilt_y_x, param.tilt_y_y) < 0
      || fprintf (f, "k1: %f\n", param.k1) < 0)
    return false;
  if (fprintf (f, "gamma: %f\n", rparam.gamma) < 0
      || fprintf (f, "presaturation: %f\n", rparam.presaturation) < 0
      || fprintf (f, "saturation: %f\n", rparam.saturation) < 0
      || fprintf (f, "brightness: %f\n", rparam.brightness) < 0
      || fprintf (f, "scren_blur_radius: %f\n", rparam.screen_blur_radius) < 0
      || fprintf (f, "gray_range: %i %i\n", rparam.gray_min, rparam.gray_max) < 0
      || fprintf (f, "precise: %s\n", bool_names [(int)rparam.precise]) < 0
      || fprintf (f, "screen_compensation: %s\n", bool_names [(int)rparam.screen_compensation]) < 0
      || fprintf (f, "adjust_luminosity: %s\n", bool_names [(int)rparam.adjust_luminosity]) < 0
      || fprintf (f, "mix_gamma: %f\n", rparam.mix_gamma) < 0
      || fprintf (f, "mix_weights: %f %f %f\n", rparam.mix_red, rparam.mix_green, rparam.mix_blue) < 0)
    return false;
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
      if (c == EOF || !isalnum (c))
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
      *val = true;
      return true;
    }
  else if (c == 'n')
    {
      if (getc (f) != 'o')
	return false;
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
  *ll = l;
  return true;
}

static bool
read_luminosity (FILE *f, luminosity_t *ll)
{
  double l;
  if (fscanf (f, "%lf\n", &l) != 1)
    return false;
  *ll = l;
  return true;
}

static bool
read_vector (FILE *f, coord_t *xx, coord_t *yy)
{
  double x, y;
  if (fscanf (f, "%lf %lf\n", &x, &y) != 2)
    return false;
  *xx = x;
  *yy = y;
  return true;
}

static bool
read_rgb (FILE *f, luminosity_t *rr, luminosity_t *gg, luminosity_t *bb)
{
  double r, g, b;
  if (fscanf (f, "%lf %lf %lf\n", &r, &g, &b) != 3)
    return false;
  *rr = r;
  *gg = g;
  *bb = b;
  return true;
}

bool
load_csp (FILE *f, scr_to_img_parameters &param, render_parameters &rparam, const char **error)
{
  char buf[256];
  if (fread (buf, 1, strlen (HEADER), f) < 0
      || memcmp (buf, HEADER, strlen (HEADER)))
    {
      *error = "first line should be " HEADER;
      return false;
    }
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
	  param.type = (enum scr_type) j;
	}
      else if (!strcmp (buf, "screen_shift"))
	{
	  if (!read_vector (f, &param.center_x, &param.center_y))
	    {
	      *error = "error parsing screen_shift";
	      return false;
	    }
	}
      else if (!strcmp (buf, "coordinate_x"))
	{
	  if (!read_vector (f, &param.coordinate1_x, &param.coordinate1_y))
	    {
	      *error = "error parsing coordinate_x";
	      return false;
	    }
	}
      else if (!strcmp (buf, "coordinate_y"))
	{
	  if (!read_vector (f, &param.coordinate2_x, &param.coordinate2_y))
	    {
	      *error = "error parsing coordinate_y";
	      return false;
	    }
	}
      else if (!strcmp (buf, "tilt_x"))
	{
	  if (!read_vector (f, &param.tilt_x_x, &param.tilt_x_y))
	    {
	      *error = "error parsing tilt_x";
	      return false;
	    }
	}
      else if (!strcmp (buf, "tilt_y"))
	{
	  if (!read_vector (f, &param.tilt_y_x, &param.tilt_y_y))
	    {
	      *error = "error parsing tilt_y";
	      return false;
	    }
	}
      else if (!strcmp (buf, "k1"))
	{
	  if (!read_scalar (f, &param.k1))
	    {
	      *error = "error parsing k1";
	      return false;
	    }
	}
      else if (!strcmp (buf, "gamma"))
	{
	  if (!read_luminosity (f, &rparam.gamma))
	    {
	      *error = "error parsing gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "presaturation"))
	{
	  if (!read_luminosity (f, &rparam.presaturation))
	    {
	      *error = "error parsing presaturation";
	      return false;
	    }
	}
      else if (!strcmp (buf, "saturation"))
	{
	  if (!read_luminosity (f, &rparam.saturation))
	    {
	      *error = "error parsing saturation";
	      return false;
	    }
	}
      else if (!strcmp (buf, "brightness"))
	{
	  if (!read_luminosity (f, &rparam.brightness))
	    {
	      *error = "error parsing brightness";
	      return false;
	    }
	}
      else if (!strcmp (buf, "mix_gamma"))
	{
	  if (!read_luminosity (f, &rparam.mix_gamma))
	    {
	      *error = "error parsing mix_gamma";
	      return false;
	    }
	}
      else if (!strcmp (buf, "mix_weights"))
	{
	  if (!read_rgb (f, &rparam.mix_red, &rparam.mix_green, &rparam.mix_blue))
	    {
	      *error = "error parsing mix_weights";
	      return false;
	    }
	}
      else if (!strcmp (buf, "scren_blur_radius"))
	{
	  if (!read_scalar (f, &rparam.screen_blur_radius))
	    {
	      *error = "error parsing screen_blur_radius";
	      return false;
	    }
	}
      else if (!strcmp (buf, "gray_range"))
	{
	  if (fscanf (f, "%i %i\n", &rparam.gray_min, &rparam.gray_max) != 2)
	    {
	      *error = "error parsing gray_range";
	      return false;
	    }
	}
      else if (!strcmp (buf, "precise"))
	{
	  if (!parse_bool (f, &rparam.precise))
	    {
	      *error = "error parsing precise";
	      return false;
	    }
	}
      else if (!strcmp (buf, "screen_compensation"))
	{
	  if (!parse_bool (f, &rparam.screen_compensation))
	    {
	      *error = "error parsing screen_compensation";
	      return false;
	    }
	}
      else if (!strcmp (buf, "adjust_luminosity"))
	{
	  if (!parse_bool (f, &rparam.adjust_luminosity))
	    {
	      *error = "error parsing adjust_luminosity";
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
