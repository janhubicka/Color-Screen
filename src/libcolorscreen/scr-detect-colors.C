#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/colorscreen.h"
#include "solver.h"
#include "render-to-scr.h"
#include "bitmap.h"
#include "gsl-utils.h"
#include "sharpen.h"
#include "nmsimplex.h"
namespace colorscreen
{
namespace {

/* Helper for sharpening part of the scan.  */
struct imgtile
{
  luminosity_t *lookup_table[3];
  int xstart, ystart;
  image_data *img;
};

static rgbdata
get_pixel (struct imgtile *sec, int x, int y, int, int)
{
  rgbdata ret = {0,0,0};
  x += sec->xstart;
  y += sec->ystart;
  if (x < 0 || y < 0 || x >= sec->img->width || y >= sec->img->height)
    return ret;
  ret.red = sec->lookup_table[0] [sec->img->rgbdata[y][x].r];
  ret.green = sec->lookup_table[1] [sec->img->rgbdata[y][x].g];
  ret.blue = sec->lookup_table[2] [sec->img->rgbdata[y][x].b];
  return ret;
}

struct entry {
	rgbdata sharpened_color;
	rgbdata orig_color;
	luminosity_t priority;
};

bool
compare_priorities(struct entry &e1, struct entry &e2)
{
  return e2.priority < e1.priority;
}

}

/* Given known portion of screen collect color samples and optimize to PARAM.
   M, XSHIFT, YSHIFT, KNOWN_PATCHES are results of screen analysis. 
   
   TODO: Add return value*/
void
optimize_screen_colors (scr_detect_parameters *param, scr_type type, image_data *img, mesh *m, int xshift, int yshift, bitmap_2d *known_patches, luminosity_t gamma, progress_info *progress, FILE *report)
{
  int count = 0;
  const int range = 2;
  for (int y = 0; y < (int)known_patches->height; y++)
    for (int x = 0; x < (int)known_patches->width; x++)
      if (known_patches->test_range ({x, y}, range))
	count++;
  const int samples = 1000;
  int nnr = 0, nng = 0, nnb = 0;
  rgbdata reds[samples*2];
  rgbdata greens[samples];
  rgbdata blues[samples];
  render::lookup_table_cache_t::cached_ptr lookup_table[3];
  if (!render::get_lookup_tables (lookup_table, gamma, img, progress))
    return;

  for (int y = -yshift, nf = 0, next =0, step = count / samples; y < (int)known_patches->height - yshift; y++)
    for (int x = -xshift; x < (int)known_patches->width - xshift; x++)
      if (known_patches->test_range ({x + xshift,y + yshift}, range) && nf++ > next)
	{
	  coord_t ix, iy;
	  next += step;
	  point_t p = m->apply ({(coord_t)x, (coord_t)y});
	  ix = p.x;
	  iy = p.y;
	  if (nng < samples && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))

	    {
	      greens[nng].red = lookup_table[0][img->rgbdata[(int)iy][(int)ix].r];
	      greens[nng].green = lookup_table[1][img->rgbdata[(int)iy][(int)ix].g];
	      greens[nng].blue = lookup_table[2][img->rgbdata[(int)iy][(int)ix].b];
	      nng++;
	    }
	  if (type == Dufay)
	    p = m->apply ({(x)+0.5, (coord_t)y});
	  else
	    p = m->apply ({(x)+0.25, y + 0.25});
	  ix = p.x;
	  iy = p.y;
	  if (nnb < samples && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))
	    {
	      blues[nnb].red = lookup_table[0][img->rgbdata[(int)iy][(int)ix].r];
	      blues[nnb].green = lookup_table[1][img->rgbdata[(int)iy][(int)ix].g];
	      blues[nnb].blue = lookup_table[2][img->rgbdata[(int)iy][(int)ix].b];
	      nnb++;
	    }
	  p = m->apply ({(coord_t)(x), y + 0.5});
	  ix = p.x;
	  iy = p.y;
	  if (nnr < samples * 2 && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))
	    {
	      reds[nnr].red = lookup_table[0][img->rgbdata[(int)iy][(int)ix].r];
	      reds[nnr].green = lookup_table[1][img->rgbdata[(int)iy][(int)ix].g];
	      reds[nnr].blue = lookup_table[2][img->rgbdata[(int)iy][(int)ix].b];
	      nnr++;
	    }
	  if (type == Dufay)
	    p = m->apply ({(x) + (coord_t)0.5, y + (coord_t)0.5});
	  else
	    p = m->apply ({(x) + (coord_t)0.5, (coord_t)y});
	  ix = p.x;
	  iy = p.y;
	  if (nnr < samples * 2 && ix >= 0 && iy >= 0 && ix < img->width && iy < img->height
	      && (img->rgbdata[(int)iy][(int)ix].r
		  || img->rgbdata[(int)iy][(int)ix].g
		  || img->rgbdata[(int)iy][(int)ix].b))
	    {
	      reds[nnr].red = lookup_table[0][img->rgbdata[(int)iy][(int)ix].r];
	      reds[nnr].green = lookup_table[1][img->rgbdata[(int)iy][(int)ix].g];
	      reds[nnr].blue = lookup_table[2][img->rgbdata[(int)iy][(int)ix].b];
	      nnr++;
	    }
	}
  if (nnr < 10 || nnb < 10 || nng < 10)
    {
      fprintf (stderr, "Failed to find enough samples\n");
      abort ();
    }
  optimize_screen_colors (param, reds, nnr, greens, nng, blues, nnb, progress, report);
}


bool
optimize_screen_colors (scr_detect_parameters *param, image_data *img, luminosity_t gamma, int x, int y, int width, int height, progress_info *progress, FILE *report)
{
  const double sharpen_amount = 0;
  const double sharpen_radius = 3;
  int clen = fir_blur::convolve_matrix_length (sharpen_radius);
  mem_rgbdata *sharpened = (mem_rgbdata*) malloc ((width + clen) * (height + clen) * sizeof (mem_rgbdata));
  if (!sharpened)
    return false;
  render::lookup_table_cache_t::cached_ptr lookup_table[3];
  if (!render::get_lookup_tables (lookup_table, gamma, img, progress))
    return false;
  struct imgtile section = {{lookup_table[0].get (), lookup_table[1].get (), lookup_table[2].get ()}, x - clen / 2, y - clen / 2, img};
  sharpen<rgbdata, mem_rgbdata, imgtile *, int, &get_pixel> (sharpened, &section, 0, width + clen, height + clen, sharpen_radius, sharpen_amount, progress);
  std::vector<entry> pixels;
  for (int yy = y ; yy < y + height; yy++)
    for (int xx = x ; xx < x + width; xx++)
      {
	struct entry e;
	e.orig_color = get_pixel (&section, xx-x+clen/2, yy-y+clen/2, 0, 0);
	e.sharpened_color = sharpened[(yy-y+clen/2) * (width + clen) + xx -x + clen/2];
	e.priority = 3 - (e.orig_color.red + e.orig_color.green + e.orig_color.blue);
	pixels.push_back (e);
      }
  free (sharpened);

  sort (pixels.begin (), pixels.end (), compare_priorities);
  int pos = pixels.size () / 2;
  luminosity_t min_density = pixels[pos].orig_color.red + pixels[pos].orig_color.green + pixels[pos].orig_color.blue;
  //printf ("\n min density %f\n", min_density);

  for (entry &e : pixels)
    e.priority = e.sharpened_color.red / std::max (e.sharpened_color.green + e.sharpened_color.blue, (luminosity_t)0.000001);
  sort (pixels.begin (), pixels.end (), compare_priorities);

  std::vector<rgbdata> reds;
  for (entry &e : pixels)
    {
      if (e.orig_color.red + e.orig_color.green + e.orig_color.blue < min_density)
	continue;
      reds.push_back ((rgbdata){e.orig_color.red, e.orig_color.green, e.orig_color.blue});
      //printf ("%f %f %f %f\n", e.orig_color.red, e.orig_color.green, e.orig_color.blue, e.priority);
      if (reds.size () > pixels.size () / 1000)
	break;
    }
  

  for (entry &e : pixels)
    e.priority = e.sharpened_color.green / std::max (e.sharpened_color.red + e.sharpened_color.blue, (luminosity_t)0.000001);
  sort (pixels.begin (), pixels.end (), compare_priorities);

  std::vector<rgbdata> greens;
  for (entry &e : pixels)
    {
      if (e.orig_color.red + e.orig_color.green + e.orig_color.blue < min_density)
	continue;
      greens.push_back ((rgbdata){e.orig_color.red, e.orig_color.green, e.orig_color.blue});
      if (greens.size () > pixels.size () / 1000)
	break;
    }
  

  for (entry &e : pixels)
    e.priority = e.sharpened_color.blue / std::max (e.sharpened_color.red + e.sharpened_color.green, (luminosity_t)0.000001);
  sort (pixels.begin (), pixels.end (), compare_priorities);

  std::vector<rgbdata> blues;
  for (entry &e : pixels)
    {
      if (e.orig_color.red + e.orig_color.green + e.orig_color.blue < min_density)
	continue;
      blues.push_back ((rgbdata){e.orig_color.red, e.orig_color.green, e.orig_color.blue});
      if (blues.size () > pixels.size () / 1000)
	break;
    }
  //printf ("%i %i %i\n",reds.size (), greens.size (), blues.size ());
  if (!reds.size () || !greens.size () || !blues.size ()
      || (reds.size () + greens.size () + blues.size ()) < 4 * 3)
    return false;
  optimize_screen_colors (param, reds.data (), reds.size (), greens.data (), greens.size (), blues.data (), blues.size (), progress, report);
  return true;
}

#define C(i) (gsl_vector_get(c,(i)))

namespace {
static rgbdata
optimize_chanel (rgbdata *colors, int n, int chanel)
{
  int matrixw = 2;
  int matrixh = n * 2;

  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (matrixh, matrixw);
  gsl_matrix *X = gsl_matrix_alloc (matrixh, matrixw);
  gsl_vector *y = gsl_vector_alloc (matrixh);
  gsl_vector *w = gsl_vector_alloc (matrixh);
  gsl_vector *c = gsl_vector_alloc (matrixw);
  gsl_matrix *cov = gsl_matrix_alloc (matrixw, matrixw);
  int c1, c2;
  if (chanel == 0)
    c1 = 1, c2 = 2;
  else if (chanel == 1)
    c1 = 0, c2 = 2;
  else
    c1 = 0, c2 = 1;

  /* Those are realy two independent linear regressions.  We could simplify this.  */
  for (int i = 0; i < n; i++)
    {
      int e = i * 2;
      gsl_matrix_set (X, e, 0, colors[i][chanel]);
      gsl_matrix_set (X, e, 1, 0);
      gsl_vector_set (y, e, colors[i][c1]);
      gsl_matrix_set (X, e + 1, 0, 0);
      gsl_matrix_set (X, e + 1, 1, colors[i][chanel]);
      gsl_vector_set (y, e + 1, colors[i][c2]);
      gsl_vector_set (w, e, 1);
      gsl_vector_set (w, e + 1, 1);
    }
  double chisq;
  gsl_multifit_wlinear (X, w, y, c, cov,
			&chisq, work);
  gsl_multifit_linear_free (work);

  rgbdata ret;
  ret[chanel] = 1;
  ret[c1] = C(0);
  ret[c2] = C(1);

  gsl_matrix_free (X);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_vector_free (c);
  gsl_matrix_free (cov);
  return ret;
}

/* Nonlinear optimizer to find black, red, green and blue that best
   separates the color samples.  */
struct
screen_color_solver
{
  /* Input colors  */
  rgbdata *reds;
  rgbdata *greens;
  rgbdata *blues;
  int ngreens;
  int nreds;
  int nblues;

  luminosity_t start[9];
  /* 0,1,2 is dark point, 3,4 red, 5,6 green and 7,8 blue.  */
  int num_values ()
  {
    return 9;
  }
  coord_t epsilon ()
  {
    return 0.000000001;
  }
  coord_t scale ()
  {
    return 2;
  }
  bool verbose ()
  {
    return false;
  }

  inline luminosity_t
  objfunc_chanel (rgbdata *colors, int n, int chanel, color_matrix &mat)
  {
    luminosity_t sum = 0;
    int c1, c2;
    if (chanel == 0)
      c1 = 1, c2 = 2;
    else if (chanel == 1)
      c1 = 0, c2 = 2;
    else
      c1 = 0, c2 = 1;
    for (int i = 0; i < n; i++)
      {
	luminosity_t c[3];
	mat.apply_to_rgb (colors[i].red, colors[i].green, colors[i].blue, &c[0], &c[1], &c[2]);
	luminosity_t lsum = c[0]+c[1]+c[2];

	// Negative value is bad.
	if (c[chanel] < 0.00000001)
	  {
	    c[chanel] = 0.00000001;
	    sum += c[chanel] * c[chanel];
	  }
	sum += c[c1] * c[c1] / (lsum * lsum);
	sum += c[c2] * c[c2] / (lsum * lsum);
      }
    return sum / n;
  }

  void
  constrain (luminosity_t *)
  {
  }

  luminosity_t
  objfunc (luminosity_t *vals)
  {
    /* Same as in scr_detect::set_parameters.  */
    color_matrix subtract_dark (1, 0, 0, -vals[0],
				0, 1, 0, -vals[1],
				0, 0, 1, -vals[2],
				0, 0, 0, 1);
    color_matrix process_colors (1,       vals[5], vals[7],  0,
				 vals[3], 1      , vals[8],  0,
				 vals[4], vals[6], 1      ,  0,
				 0, 0, 0, 1);
    color_matrix mat = process_colors.invert ();
    mat = mat * subtract_dark;
    return objfunc_chanel (reds, nreds, 0, mat)
	   + objfunc_chanel (greens, ngreens, 1, mat)
	   + objfunc_chanel (blues, nblues, 2, mat);
  }
};
}

/* Optimize screen colors based on known red, green and blue samples
   and store resulting black, red, green and blue colors to PARAM.  */
void
optimize_screen_colors (scr_detect_parameters *param,
			rgbdata *reds,
			int nreds,
			rgbdata *greens,
			int ngreens,
			rgbdata *blues,
			int nblues, progress_info *progress, FILE *report)
{
  if (!nreds || !ngreens || !nblues)
    abort ();
  if (progress) 
    progress->set_task ("determining screen colors", 1);

  param->black = {0, 0, 0};
  param->red = optimize_chanel (reds, nreds, 0);
  param->green = optimize_chanel (greens, ngreens, 1);
  param->blue = optimize_chanel (blues, nblues, 2);
  if (report)
    {
      fprintf (report, "Initial screen red:  ");
      param->red.print (report);
      fprintf (report, "Initial screen green:  ");
      param->green.print (report);
      fprintf (report, "Initial screen blue:  ");
      param->blue.print (report);
    }

  screen_color_solver solver;
  solver.reds = reds;
  solver.nreds = nreds;
  solver.greens = greens;
  solver.ngreens = ngreens;
  solver.blues = blues;
  solver.nblues = nblues;
  /* Dark should be 0,0,0  */
  solver.start[0] = solver.start[1] = solver.start[2] = 0;
  /* If existing values makes sense use them as start point.  */
  if (param->red.red > 0.0000001
      && param->green.green > 0.0000001
      && param->blue.blue > 0.0000001)
    {
      solver.start[3] = param->red.green / param->red.red;
      solver.start[4] = param->red.blue / param->red.red;
      solver.start[5] = param->green.red / param->green.green;
      solver.start[6] = param->green.blue / param->green.green;
      solver.start[7] = param->blue.red / param->blue.blue;
      solver.start[8] = param->blue.green / param->blue.blue;
    }
  else
    {
      solver.start[3] = 0;
      solver.start[4] = 0;
      solver.start[5] = 0;
      solver.start[6] = 0;
      solver.start[7] = 0;
      solver.start[8] = 0;
    }
  simplex<luminosity_t, screen_color_solver>(solver, "optimizing colors", progress);
  param->black = {solver.start[0], solver.start[1], solver.start[2]};
  param->red = {1, solver.start[3], solver.start[4]};
  param->green = {solver.start[5], 1, solver.start[6]};
  param->blue = {solver.start[7], solver.start[8], 1};
  if (report)
    {
      fprintf (report, "optimized screen black:  ");
      param->black.print (report);
      fprintf (report, "optimized screen red:  ");
      param->red.print (report);
      fprintf (report, "optimized screen green:  ");
      param->green.print (report);
      fprintf (report, "optimized screen blue:  ");
      param->blue.print (report);
    }

  if (report)
    {
      fprintf (report, "After screen color detection:\n");
      save_csp (report, NULL, param, NULL, NULL);
    }
  if (report)
    fflush (report);
}
}
