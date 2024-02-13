#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/render-to-scr.h"
#include "gsl-utils.h"
#include "include/solver.h"
#include "sharpen.h"
#include "nmsimplex.h"
namespace {

/* Helper for sharpening part of the scan.  */
struct imgtile
{
  luminosity_t *lookup_table;
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
  ret.red = sec->lookup_table [sec->img->rgbdata[y][x].r];
  ret.green = sec->lookup_table [sec->img->rgbdata[y][x].g];
  ret.blue = sec->lookup_table [sec->img->rgbdata[y][x].b];
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
   M, XSHIFT, YSHIFT, KNOWN_PATCHES are results of screen analysis. */
void
optimize_screen_colors (scr_detect_parameters *param, scr_type type, image_data *img, mesh *m, int xshift, int yshift, bitmap_2d *known_patches, luminosity_t gamma, progress_info *progress, FILE *report)
{
  int count = 0;
  const int range = 2;
  for (int y = 0; y < known_patches->height; y++)
    for (int x = 0; x < known_patches->width; x++)
      if (known_patches->test_range (x, y, range))
	count++;
  const int samples = 1000;
  int nnr = 0, nng = 0, nnb = 0;
  rgbdata reds[samples*2];
  rgbdata greens[samples];
  rgbdata blues[samples];
  luminosity_t *lookup_table = render::get_lookup_table (gamma, img->maxval);

  for (int y = -yshift, nf = 0, next =0, step = count / samples; y < known_patches->height - yshift; y++)
    for (int x = -xshift; x < known_patches->width - xshift; x++)
      if (known_patches->test_range (x + xshift,y + yshift, range) && nf++ > next)
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
	      greens[nng].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      greens[nng].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      greens[nng].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
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
	      blues[nnb].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      blues[nnb].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      blues[nnb].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
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
	      reds[nnr].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      reds[nnr].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      reds[nnr].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
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
	      reds[nnr].red = lookup_table[img->rgbdata[(int)iy][(int)ix].r];
	      reds[nnr].green = lookup_table[img->rgbdata[(int)iy][(int)ix].g];
	      reds[nnr].blue = lookup_table[img->rgbdata[(int)iy][(int)ix].b];
	      nnr++;
	    }
	}
  render::release_lookup_table (lookup_table);
  if (nnr < 10 || nnb < 10 || nng < 10)
    {
      fprintf (stderr, "Failed to find enough samples\n");
      abort ();
    }
  optimize_screen_colors (param, gamma, reds, nnr, greens, nng, blues, nnb, progress, report);
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
  luminosity_t *lookup_table = render::get_lookup_table (gamma, img->maxval);
  struct imgtile section = {lookup_table, x - clen / 2, y - clen / 2, img};
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
  render::release_lookup_table (lookup_table);
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
  optimize_screen_colors (param, gamma, reds.data (), reds.size (), greens.data (), greens.size (), blues.data (), blues.size (), progress, report);
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
  int nreds;
  rgbdata *greens;
  int ngreens;
  rgbdata *blues;
  int nblues;

  luminosity_t start[9];
  /* 3x3 matrix plus dark point.  */
  int num_values ()
  {
    return 9;
  }
  coord_t epsilon ()
  {
    return 0.000001;
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
    luminosity_t sum;
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
	//sum += c[c1] * c[c1] + c[c2] * c[c2];

	// Negative value is bad.
	if (c[chanel] < 0.000001)
	  {
	    c[chanel] = 0.000001;
	    sum += c[chanel] * c[chanel];
	  }
	sum += c[c1] * c[c1] / (c[chanel] * c[chanel]);
	sum += c[c2] * c[c2] / (c[chanel] * c[chanel]);
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
    color_matrix subtract_dark (1, 0, 0, -vals[0],
				0, 1, 0, -vals[1],
				0, 0, 1, -vals[2],
				0, 0, 0, 1);
    color_matrix process_colors (1,       vals[5], vals[7],  0,
				 vals[3], 1      , vals[8], 0,
				 vals[4], vals[6], 1       , 0,
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
			luminosity_t gamma,
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
  solver.start[3] = param->red.green;
  solver.start[4] = param->red.blue;
  solver.start[5] = param->green.red;
  solver.start[6] = param->green.blue;
  solver.start[7] = param->blue.red;
  solver.start[8] = param->blue.green;
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

  /* Old optimization code that brute-forces dark point.  */
#if 0

  double min_chisq = 0;
  bool found = false;
  int n = nreds + ngreens + nblues;
  rgbdata bestdark, bestred, bestgreen, bestblue;
  luminosity_t bestrlum = 0, bestglum = 0, bestblum = 0;
  /* If true three dimenstional search is made for dark point.  */
  const bool threed = true;
  /* with 3 bit per step, about 12 bits should be enough for everybody.  */
  const int iterations = 4;
  if (progress) 
    progress->set_task ("optimizing colors", iterations * COLOR_SOLVER_STEPS * (threed ? COLOR_SOLVER_STEPS * COLOR_SOLVER_STEPS : 1));
/* MacOS clang does not accept steps to be const int.  */
#define COLOR_SOLVER_STEPS 8
  int matrixw = 4 * 3;
  int matrixh = n * 3;
  /* Determine max of search range.  If this is too large, the red/green/blue colors may become negative.  */
  std::vector<luminosity_t> rlums;
  std::vector<luminosity_t> glums;
  std::vector<luminosity_t> blums;
  for (int i = 0; i < nreds; i++)
    rlums.push_back (reds[i].red + reds[i].green + reds[i].blue);
  for (int i = 0; i < ngreens; i++)
    glums.push_back (greens[i].red + greens[i].green + greens[i].blue);
  for (int i = 0; i < nblues; i++)
    blums.push_back (blues[i].red + blues[i].green + blues[i].blue);
  sort(rlums.begin (), rlums.end ());
  sort(glums.begin (), glums.end ());
  sort(blums.begin (), blums.end ());

  luminosity_t minrlum = -0.2;
  luminosity_t maxrlum = rlums[(int)(rlums.size () * 0.5)];
  luminosity_t minglum = -0.2;
  luminosity_t maxglum = glums[(int)(glums.size () * 0.5)];
  luminosity_t minblum = -0.2;
  luminosity_t maxblum = blums[(int)(blums.size () * 0.5)];

  if (!threed)
    maxrlum = maxglum = maxblum = std::min (maxrlum, std::min (maxglum, maxblum));


  for (int iteration = 0; iteration < iterations; iteration++)
    {
#pragma omp parallel for default(none) shared(progress, minrlum, maxrlum, minglum, maxglum, minblum, maxblum, bestred, bestgreen, bestblue, bestrlum, bestglum, bestblum, reds, greens, blues, matrixh, matrixw, nreds, nblues, ngreens, found, min_chisq, bestdark)
      for (int rstep = 0; rstep < COLOR_SOLVER_STEPS ; rstep++)
	{
	  gsl_multifit_linear_workspace * work
	    = gsl_multifit_linear_alloc (matrixh, matrixw);
	  gsl_matrix *X = gsl_matrix_alloc (matrixh, matrixw);
	  gsl_vector *y = gsl_vector_alloc (matrixh);
	  gsl_vector *w = gsl_vector_alloc (matrixh);
	  gsl_vector *c = gsl_vector_alloc (matrixw);
	  gsl_matrix *cov = gsl_matrix_alloc (matrixw, matrixw);

	  for (int gstep = threed ? 0 : rstep; gstep < (threed ? COLOR_SOLVER_STEPS : rstep+1) ; gstep++)
	    for (int bstep = threed ? 0 : rstep; bstep < (threed? COLOR_SOLVER_STEPS : rstep+1) ; bstep++)
	      {
		luminosity_t rmm = rstep * (maxrlum - minrlum) / COLOR_SOLVER_STEPS + minrlum;
		luminosity_t gmm = gstep * (maxglum - minglum) / COLOR_SOLVER_STEPS + minglum;
		luminosity_t bmm = bstep * (maxblum - minblum) / COLOR_SOLVER_STEPS + minblum;
		if (progress)
		  progress->inc_progress ();
		for (int i = 0; i < nreds; i++)
		  {
		    int e = i * 3;
		    coord_t ii = reds[i].red + reds[i].green + reds[i].blue - rmm;
		    for (int j = 0; j < 3; j++)
		      {
			gsl_matrix_set (X, e+j, 0, j==0);
			gsl_matrix_set (X, e+j, 1, j==1);
			gsl_matrix_set (X, e+j, 2, j==2);
			gsl_matrix_set (X, e+j, 3, j==0 ? ii : 0);
			gsl_matrix_set (X, e+j, 4, j==1 ? ii : 0);
			gsl_matrix_set (X, e+j, 5, j==2 ? ii : 0);
			gsl_matrix_set (X, e+j, 6, 0);
			gsl_matrix_set (X, e+j, 7, 0);
			gsl_matrix_set (X, e+j, 8, 0);
			gsl_matrix_set (X, e+j, 9, 0);
			gsl_matrix_set (X, e+j, 10, 0);
			gsl_matrix_set (X, e+j, 11, 0);
			gsl_vector_set (w, e+j, 1);
		      }
		    gsl_vector_set (y, e, reds[i].red);
		    gsl_vector_set (y, e+1, reds[i].green);
		    gsl_vector_set (y, e+2, reds[i].blue);
		  }
		for (int i = 0; i < ngreens; i++)
		  {
		    int e = (i + nreds) * 3;
		    coord_t ii = greens[i].red + greens[i].green + greens[i].blue - gmm;
		    for (int j = 0; j < 3; j++)
		      {
			gsl_matrix_set (X, e+j, 0, j==0);
			gsl_matrix_set (X, e+j, 1, j==1);
			gsl_matrix_set (X, e+j, 2, j==2);
			gsl_matrix_set (X, e+j, 3, 0);
			gsl_matrix_set (X, e+j, 4, 0);
			gsl_matrix_set (X, e+j, 5, 0);
			gsl_matrix_set (X, e+j, 6, j==0 ? ii : 0);
			gsl_matrix_set (X, e+j, 7, j==1 ? ii : 0);
			gsl_matrix_set (X, e+j, 8, j==2 ? ii : 0);
			gsl_matrix_set (X, e+j, 9, 0);
			gsl_matrix_set (X, e+j, 10, 0);
			gsl_matrix_set (X, e+j, 11, 0);
			gsl_vector_set (w, e+j, 1);
		      }
		    gsl_vector_set (y, e, greens[i].red);
		    gsl_vector_set (y, e+1, greens[i].green);
		    gsl_vector_set (y, e+2, greens[i].blue);
		    //printf ("%f %f %f\n",greens[i].red, greens[i].green, greens[i].blue);
		  }
		for (int i = 0; i < nblues; i++)
		  {
		    int e = (i + nreds + ngreens) * 3;
		    coord_t ii = blues[i].red + blues[i].green + blues[i].blue - bmm;
		    for (int j = 0; j < 3; j++)
		      {
			gsl_matrix_set (X, e+j, 0, j==0);
			gsl_matrix_set (X, e+j, 1, j==1);
			gsl_matrix_set (X, e+j, 2, j==2);
			gsl_matrix_set (X, e+j, 3, 0);
			gsl_matrix_set (X, e+j, 4, 0);
			gsl_matrix_set (X, e+j, 5, 0);
			gsl_matrix_set (X, e+j, 6, 0);
			gsl_matrix_set (X, e+j, 7, 0);
			gsl_matrix_set (X, e+j, 8, 0);
			gsl_matrix_set (X, e+j, 9, j==0 ? ii : 0);
			gsl_matrix_set (X, e+j, 10, j==1 ? ii : 0);
			gsl_matrix_set (X, e+j, 11, j==2 ? ii : 0);
			gsl_vector_set (w, e+j, 1);
		      }
		    gsl_vector_set (y, e, blues[i].red);
		    gsl_vector_set (y, e+1, blues[i].green);
		    gsl_vector_set (y, e+2, blues[i].blue);
		  }
		double chisq;
		gsl_multifit_wlinear (X, w, y, c, cov,
				      &chisq, work);
#pragma omp critical
		if (!found || chisq < min_chisq)
		  {
		    //printf ("%f %f %f chisq %f\n",rmm,gmm,bmm,chisq);
		    min_chisq = chisq;
		    found = true;
		    bestdark.red = C(0);
		    bestdark.green = C(1);
		    bestdark.blue = C(2);
		    bestred.red = C(3);
		    bestred.green = C(4);
		    bestred.blue = C(5);
		    bestgreen.red = C(6);
		    bestgreen.green = C(7);
		    bestgreen.blue = C(8);
		    bestblue.red = C(9);
		    bestblue.green = C(10);
		    bestblue.blue = C(11);
		    bestrlum = rmm;
		    bestglum = gmm;
		    bestblum = bmm;
		  }
	      }
	  gsl_multifit_linear_free (work);
	  gsl_matrix_free (X);
	  gsl_vector_free (y);
	  gsl_vector_free (w);
	  gsl_vector_free (c);
	  gsl_matrix_free (cov);
	}
      minrlum = bestrlum - (maxrlum - minrlum) / COLOR_SOLVER_STEPS;
      maxrlum = bestrlum + (maxrlum - minrlum) / COLOR_SOLVER_STEPS;
      minglum = bestglum - (maxglum - minglum) / COLOR_SOLVER_STEPS;
      maxglum = bestglum + (maxglum - minglum) / COLOR_SOLVER_STEPS;
      minblum = bestblum - (maxblum - minblum) / COLOR_SOLVER_STEPS;
      maxblum = bestblum + (maxblum - minblum) / COLOR_SOLVER_STEPS;
    }
#if 0
  param->black = bestdark;
  param->red = (bestred + bestdark);
  param->green = (bestgreen + bestdark);
  param->blue = (bestblue + bestdark);
#else
  param->black = bestdark;
  param->red = bestred + bestdark;
  param->green = bestgreen + bestdark;
  param->blue = bestblue + bestdark;
#endif
  if (report)
    {
      fprintf (report, "Color optimization:\n  Dark %f %f %f (gamma %f lum %f %f %f chisq %f)\n", bestdark.red, bestdark.green, bestdark.blue, gamma, bestrlum, bestglum, bestblum, min_chisq);
      fprintf (report, "  Red %f %f %f\n", bestred.red, bestred.green, bestred.blue);
      fprintf (report, "  Green %f %f %f\n", bestgreen.red, bestgreen.green, bestgreen.blue);
      fprintf (report, "  Blue %f %f %f\n", bestblue.red, bestblue.green, bestblue.blue);
      save_csp (report, NULL, param, NULL, NULL);
    }
#endif
  if (report)
    {
      fprintf (report, "After screen color detection:\n");
      save_csp (report, NULL, param, NULL, NULL);
    }
  if (report)
    fflush (report);
}
