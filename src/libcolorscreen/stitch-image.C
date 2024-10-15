#define HAVE_INLINE
#define GSL_RANGE_CHECK_OFF
#include <gsl/gsl_multifit.h>
#include <tiffio.h>
#include "include/stitch.h"
#include "include/tiff-writer.h"
#include "include/colorscreen.h"
#include "include/screen-map.h"
#include "analyze-dufay.h"
#include "analyze-paget.h"
#include "render-interpolate.h"
#include "loadsave.h"
extern const unsigned char sRGB_icc[];
extern const unsigned int sRGB_icc_len;
namespace colorscreen
{
uint64_t stitch_image::current_time;
int stitch_image::nloaded;

stitch_image::stitch_image ()
: filename (""), img (), mesh_trans (), xshift (0), yshift (0),
  width (0), height (0), final_xshift (0), final_yshift (0), final_width (0),
  final_height (0), screen_detected_patches (), known_pixels (),
  stitch_info (NULL), analyzed (false), refcount (0)
{
}

stitch_image::~stitch_image ()
{
  delete stitch_info;
}

void
stitch_image::release_image_data (progress_info *progress)
{
  //progress->pause_stdout ();
  //printf ("Releasing input tile %s\n", filename.c_str ());
  //progress->resume_stdout ();
  assert (!refcount && img);
  img = NULL;
  nloaded--;
}

bool
stitch_image::init_loader (const char **error, progress_info *progress)
{
  assert (!img);
  img = std::make_unique <image_data> ();
  if (!img->init_loader (m_prj->add_path (filename).c_str (), false, error, progress))
    return false;
  if (img->stitch)
    {
      *error = "Can not embedd stitch projects in sitch projects";
      img = NULL;
      return false;
    }
  return true;
}
bool
stitch_image::load_part (int *permille, const char **error, progress_info *progress)
{
  if (!img->load_part (permille, error, progress))
    {
      img = NULL;
      return false;
    }
  if (*permille == 1000)
    {
      img_width = img->width;
      img_height = img->height;
      if (m_prj->params.scan_xdpi && !img->xdpi)
	img->xdpi = m_prj->params.scan_xdpi;
      if (m_prj->params.scan_ydpi && !img->ydpi)
	img->ydpi = m_prj->params.scan_ydpi;
      if (!img->rgbdata)
	{
	  *error = "source image is not having color channels";
	  img = NULL;
	  return false;
	}
    }
  return true;
}

bool
stitch_image::load_img (const char **error, progress_info *progress)
{
  refcount++;
  lastused = ++current_time;
  if (img)
    return true;
  /* Perhaps make number of cached images user specified.  */
  if (nloaded >= 1 && m_prj->release_images)
    {
      int minx = -1, miny = -1;
      uint64_t minlast = 0;


      for (int y = 0; y < m_prj->params.height; y++)
	for (int x = 0; x < m_prj->params.width; x++)
	  if (m_prj->images[y][x].refcount)
	    ;
	  else if (m_prj->images[y][x].img
		   && (minx == -1 || m_prj->images[y][x].lastused < minlast))
	    {
	      minx = x;
	      miny = y;
	      minlast = m_prj->images[y][x].lastused;
	    }
      if (minx != -1)
	m_prj->images[miny][minx].release_image_data (progress);
    }
  nloaded++;
#if 0
  if (progress)
    progress->pause_stdout ();
  printf ("Loading input tile %s (%i tiles in memory)\n", filename.c_str (), nloaded);
  if (progress)
    progress->resume_stdout ();
#endif
  if (progress)
    progress->set_task ("loading image header",1);
  if (!init_loader (error, progress))
    return false;
  if (progress)
    progress->set_task ("loading",1000);
  if (!img->allocate ())
    {
      *error = "out of memory";
      img = NULL;
      return false;
    }
  int permille = 0;
  while (load_part (&permille, error, progress))
    {
      if (permille == 1000)
	return true;
      if (progress)
	progress->set_progress (permille);
      if (progress && progress->cancel_requested ())
	{
	  *error = "cancelled";
	  img = NULL;
	  return false;
	}
    }
  return false;
}

void
stitch_image::release_img ()
{
  refcount--;
}

bitmap_2d*
stitch_image::compute_known_pixels (scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress)
{
  bitmap_2d *known_pixels = new bitmap_2d (width, height);
  if (!known_pixels)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Out of memory allocating known pixels bitmap for %s\n", filename.c_str ());
      exit (1);
    }
  if (progress)
    progress->set_task ("determining known pixels", width * height);
  int xmin = img_width * skipleft / 100;
  int xmax = img_width * (100 - skipright) / 100;
  int ymin = img_height * skiptop / 100;
  int ymax = img_height * (100 - skipbottom) / 100;
  //progress->pause_stdout ();
  //printf ("Skip: %i %i %i %i Range: %i %i %i %i\n", skiptop, skipbottom, skipleft, skipright,xmin,xmax,ymin,ymax);
  //progress->resume_stdout ();
  for (int y = 0; y < height; y++)
    {
      for (int x = 0; x < width; x++)
	{
	  point_t p = scr_to_img.to_img ({(coord_t)(x - xshift - 1), (coord_t)(y - yshift - 1)});
	  if (p.x < xmin || p.x >= xmax || p.y < ymin || p.y >= ymax)
	    continue;
	  p = scr_to_img.to_img ({(coord_t)(x - xshift + 2), (coord_t)(y - yshift - 1)});
	  if (p.x < xmin || p.x >= xmax || p.y < ymin || p.y >= ymax)
	    continue;
	  p = scr_to_img.to_img ({(coord_t)(x - xshift - 1), (coord_t)(y - yshift + 2)});
	  if (p.x < xmin || p.x >= xmax || p.y < ymin || p.y >= ymax)
	    continue;
	  p = scr_to_img.to_img ({(coord_t)(x - xshift + 2), (coord_t)(y - yshift + 2)});
	  if (p.x < xmin || p.x >= xmax || p.y < ymin || p.y >= ymax)
	    continue;
	  known_pixels->set_bit (x, y);
	}
      if (progress)
	progress->inc_progress ();
    }
  return known_pixels;
}
void
stitch_image::clear_stitch_info ()
{
  if (!stitch_info)
    stitch_info = (struct stitch_info *)calloc ((img_width / m_prj->stitch_info_scale + 1) * (img_height / m_prj->stitch_info_scale + 1), sizeof (struct stitch_info));
  else
    memset (stitch_info, 0, (img_width / m_prj->stitch_info_scale + 1) * (img_height / m_prj->stitch_info_scale + 1) * sizeof (struct stitch_info));
}
bool
stitch_image::patch_detected_p (int sx, int sy)
{
  sx = sx - pos.x + xshift;
  sy = sy - pos.y + yshift;
  if (sx < 0 || sy < 0 || sx >= (int)screen_detected_patches->width || sy >= (int)screen_detected_patches->height)
    return false;
  return screen_detected_patches->test_range ({sx, sy}, 2);
}
bool
stitch_image::diff (stitch_image &other, progress_info *progress)
{
  bool found = false;
  int stack = 0;
  point_t s = scr_to_img_map.to_scr ({(coord_t)0, (coord_t)0}) + pos;
  if (other.img_pixel_known_p (s.x, s.y))
    found = true;
  s = scr_to_img_map.to_scr ({(coord_t)(img_width - 1), (coord_t)0}) + pos;
  if (other.img_pixel_known_p (s.x, s.y))
    found = true;
  s = scr_to_img_map.to_scr ({(coord_t)0, (coord_t)(img_height - 1)}) + pos;
  if (other.img_pixel_known_p (s.x, s.y))
    found = true;
  s = scr_to_img_map.to_scr ({(coord_t)(img_width - 1), (coord_t)(img_height - 1)}) + pos;
  if (other.img_pixel_known_p (s.x, s.y))
    found = true;
  if (!found)
    return false;
  if (progress)
    stack = progress->push ();
  const char *error;
  if (!load_img (&error, progress))
    {
      if (progress)
        progress->pop (stack);
      return false;
    }
  if (!other.load_img (&error, progress))
    {
      if (progress)
        progress->pop (stack);
      release_img ();
      return false;
    }
  render_parameters def;
  render_img render (param, *img, def, 65535);
  if (!render.precompute_all (progress))
    {
      if (progress)
        progress->pop (stack);
      release_img ();
      other.release_img ();
      return false;
    }
  render_img render2 (other.param, *other.img, def, 65535);
  if (!render2.precompute_all (progress))
    {
      if (progress)
        progress->pop (stack);
      release_img ();
      other.release_img ();
      return false;
    }

  if (progress)
    progress->pop (stack);
  int rxmin = INT_MAX, rxmax = INT_MIN, rymin = INT_MAX, rymax = INT_MIN;
  progress->set_task ("determining overlap range", 1);
  for (int y = 0; y < img_height; y += 10)
    for (int x = 0; x < img_width; x += 10)
      {
         point_t scr = scr_to_img_map.to_scr ({(coord_t)x, (coord_t)y}) + pos;
         if (other.img_pixel_known_p (scr.x, scr.y))
	   {
	     rxmin = std::min (rxmin, x);
	     rymin = std::min (rymin, y);
	     rxmax = std::max (rxmax, x);
	     rymax = std::max (rymax, y);
	   }
      }
  progress->pause_stdout ();
  printf ("Tiles %s and %s overlap in range x %i...%i y %i...%i\n", filename.c_str (), other.filename.c_str (), rxmin, rxmax, rymin, rymax);
  if (m_prj->report_file)
    fprintf (m_prj->report_file, "Tiles %s and %s overlap in range x %i...%i y %i...%i\n", filename.c_str (), other.filename.c_str (), rxmin, rxmax, rymin, rymax);
  if (rxmin >= rxmax || rymin >= rymax)
    {
      release_img ();
      other.release_img ();
    }
  progress->resume_stdout ();
  int rwidth = rxmax - rxmin + 1;
  int rheight = rymax - rymin + 1;
  std::string fname = (std::string)"diff" + filename + other.filename;
  tiff_writer_params p;
  p.filename = fname.c_str ();
  p.width = rwidth;
  p.height = rheight;
  p.depth = 16;
  tiff_writer out (p, &error);
  if (error)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Can not open %s: %s\n", fname.c_str (), error);
      release_img ();
      other.release_img ();
      progress->resume_stdout ();
      return false;
    }
  if (progress)
    progress->set_task ("Writting difference of two tiles", rheight);
  int nimg_pixels = 0;
  double sumdiff[3] = {0,0,0};
  int maxdiff[3] = {0,0,0};
  for (int y = rymin; y <= rymax; y++)
    {
      for (int x = rxmin; x <= rxmax; x++)
	{
          point_t scr = scr_to_img_map.to_scr ({(coord_t)x, (coord_t)y}) + pos;
          if (other.img_pixel_known_p (scr.x, scr.y))
	   {
	     rgbdata c1 = render.sample_pixel_scr (scr.x + pos.x, scr.y + pos.y);
	     rgbdata c2 = render.sample_pixel_scr (scr.x + other.pos.x, scr.y + other.pos.y);
	     int r = c1.red * 65535, g = c1.green * 65535, b = c1.blue * 65545;
	     int r2 = c2.red * 65535, g2 = c2.green * 65535, b2 = c2.blue * 65545;
#if 0
	     render_pixel (65535, sx + xpos, sy + ypos, &r, &g, &b, progress);
#endif
	     if (patch_detected_p (scr.x + pos.x, scr.y + pos.y) && other.patch_detected_p (scr.x + other.pos.x, scr.y + other.pos.y))
	       {
		 sumdiff[0] += abs (r2-r);
		 sumdiff[1] += abs (g2-b);
		 sumdiff[2] += abs (b2-b);
		 nimg_pixels++;
		 if (maxdiff[0] < abs (r2 - r))
		   maxdiff[0] = abs (r2 - r);
		 if (maxdiff[1] < abs (g2 - g))
		   maxdiff[1] = abs (g2 - g);
		 if (maxdiff[2] < abs (b2 - b))
		   maxdiff[2] = abs (b2 - b);
	       }
	     out.row16bit() [(x - rxmin) * 3 + 0] = (r2 - r) + 65536 / 2;
	     out.row16bit() [(x - rxmin) * 3 + 1] = (g2 - g) + 65536 / 2;
	     out.row16bit() [(x - rxmin) * 3 + 2] = (b2 - b) + 65536 / 2;
	   }
	  else
	   {
	     out.row16bit() [(x - rxmin) * 3 + 0] = 0;
	     out.row16bit() [(x - rxmin) * 3 + 1] = 0;
	     out.row16bit() [(x - rxmin) * 3 + 2] = 0;
	   }
	}
      if (!out.write_row ())
        {
	  other.release_img ();
	  release_img ();
	  progress->pause_stdout ();
	  fprintf (stderr, "Can not write %s\n", fname.c_str ());
	  progress->resume_stdout ();
	  return false;
        }
      if (progress)
	progress->inc_progress ();
    }
  if (nimg_pixels > 0)
  {
    progress->pause_stdout ();
    printf ("Tiles %s and %s avg color difference red:%3.1f green:%3.1f blue:%3.1f; max red: %i, max green: %i, max blue: %i\n", filename.c_str (), other.filename.c_str (), sumdiff[0] / (double) nimg_pixels, sumdiff[1] / (double) nimg_pixels, sumdiff[2] / (double) nimg_pixels, maxdiff[0], maxdiff[1], maxdiff[2]);
    if (m_prj->report_file)
      fprintf (m_prj->report_file, "Tiles %s and %s avg color difference red:%3.1f green:%3.1f blue:%3.1f; max red: %i, max green: %i, max blue: %i\n", filename.c_str (), other.filename.c_str (), sumdiff[0] / (double) nimg_pixels, sumdiff[1] / (double) nimg_pixels, sumdiff[2] / (double) nimg_pixels, maxdiff[0], maxdiff[1], maxdiff[2]);
    progress->resume_stdout ();
  }
  other.release_img ();
  release_img ();
  return true;
}

/* Output common points to hugin pto file.  */
int
stitch_image::output_common_points (FILE *f, stitch_image &other, int n1, int n2, bool collect_stitch_info, progress_info *progress)
{
  int n = 0;
  coord_t border = 5 / 100.0;
  const int range = 2;
  for (int y = -yshift; y < -yshift + height; y++)
    {
      coord_t yy = y + pos.y - other.pos.y;
      if (yy >= -other.yshift && yy < -other.yshift + other.height)
	for (int x = -xshift; x < -xshift + width; x++)
	  {
	    coord_t xx = x + pos.x - other.pos.x;
	    if (xx >= -other.xshift && xx < -other.xshift + other.width
		&& screen_detected_patches->test_range ({x + xshift, y + yshift}, range)
		&& other.screen_detected_patches->test_range ({(int64_t)floor (xx) + other.xshift, (int64_t)floor (yy) + other.yshift}, range))
	    {
	      point_t p1 = scr_to_img_map.to_img ({(coord_t)x, (coord_t)y});
	      point_t p2 = other.scr_to_img_map.to_img ({(coord_t)xx, (coord_t)yy});
	      if (p1.x < img_width * border || p1.x >= img_width * (1 - border) || p1.y < img_height * border || p1.y >= img_height * (1 - border)
	          || p2.x < other.img_width * border || p2.x >= other.img_width * (1 - border) || p2.y < other.img_height * border || p2.y >= other.img_height * (1 - border))
		continue;
	      n++;
	    }
	  }
    }
  if (!n)
    return 0;
  int step = std::max (n / m_prj->params.num_control_points, 1);
  int npoints = n / step;
  int nfound = 0;
  gsl_matrix *X = NULL, *cov = NULL;
  gsl_vector *vy = NULL, *w = NULL, *c = NULL;
  if (collect_stitch_info)
    {
      X = gsl_matrix_alloc (npoints * 2, 6);
      vy = gsl_vector_alloc (npoints * 2);
      w = gsl_vector_alloc (npoints * 2);
      c = gsl_vector_alloc (6);
      cov = gsl_matrix_alloc (6, 6);
    }

  for (int y = -yshift, m = 0, next = 0; y < -yshift + height; y++)
    {
      coord_t yy = y + pos.y - other.pos.y;
      if (yy >= -other.yshift && yy < -other.yshift + other.height)
	for (int x = -xshift; x < -xshift + width; x++)
	  {
	    coord_t xx = x + pos.x - other.pos.x;
	    if (xx >= -other.xshift && xx < -other.xshift + other.width
		&& screen_detected_patches->test_range ({x + xshift, y + yshift}, range)
		&& other.screen_detected_patches->test_range ({(int64_t)floor (xx) + other.xshift, (int64_t)floor (yy) + other.yshift}, range))
	      {
		point_t p1 = scr_to_img_map.to_img ({(coord_t)x, (coord_t)y});
		point_t p2 = other.scr_to_img_map.to_img ({(coord_t)xx, (coord_t)yy});
		if (p1.x < img_width * border || p1.x >= img_width * (1 - border) || p1.y < img_height * border || p1.y >= img_height * (1 - border)
		    || p2.x < other.img_width * border || p2.x >= other.img_width * (1 - border) || p2.y < other.img_height * border || p2.y >= other.img_height * (1 - border))
		  continue;
	        if (m++ == next)
		  {
		    next += step;
		    if (f)
		      fprintf (f,  "c n%i N%i x%f y%f X%f Y%f t0\n", n1, n2, p1.x, p1.y, p2.x, p2.y);

		    if (!collect_stitch_info || nfound >= npoints)
		      continue;

		    gsl_matrix_set (X, nfound * 2, 0, 1.0);
		    gsl_matrix_set (X, nfound * 2, 1, 0.0);
		    gsl_matrix_set (X, nfound * 2, 2, p1.x);
		    gsl_matrix_set (X, nfound * 2, 3, 0);
		    gsl_matrix_set (X, nfound * 2, 4, p1.y);
		    gsl_matrix_set (X, nfound * 2, 5, 0);

		    gsl_matrix_set (X, nfound * 2+1, 0, 0.0);
		    gsl_matrix_set (X, nfound * 2+1, 1, 1.0);
		    gsl_matrix_set (X, nfound * 2+1, 2, 0);
		    gsl_matrix_set (X, nfound * 2+1, 3, p1.x);
		    gsl_matrix_set (X, nfound * 2+1, 4, 0);
		    gsl_matrix_set (X, nfound * 2+1, 5, p1.y);

		    gsl_vector_set (vy, nfound * 2, p2.x);
		    gsl_vector_set (vy, nfound * 2 + 1, p2.y);
		    gsl_vector_set (w, nfound * 2, 1.0);
		    gsl_vector_set (w, nfound * 2 + 1, 1.0);
		    nfound++;
		  }
	      }
	  }
    }
  if (n < 1000)
    return 0;
  if (collect_stitch_info)
    {
      double chisq;
      if (nfound != npoints)
	abort ();
      gsl_multifit_linear_workspace * work
	= gsl_multifit_linear_alloc (npoints*2, 6);
      gsl_multifit_wlinear (X, w, vy, c, cov,
			    &chisq, work);
      gsl_multifit_linear_free (work);
      coord_t distsum = 0;
      coord_t maxdist = 0;
      if (!stitch_info)
        clear_stitch_info ();
      if (!other.stitch_info)
        other.clear_stitch_info ();
      npoints = 0;
#define C(i) (gsl_vector_get(c,(i)))
      for (int y = -yshift; y < -yshift + height; y++)
	{
	  coord_t yy = y + pos.y - other.pos.y;
	  if (yy >= -other.yshift && yy < -other.yshift + other.height)
	    for (int x = -xshift; x < -xshift + width; x++)
	      {
		coord_t xx = x + pos.x - other.pos.x;
		if (xx >= -other.xshift && xx < -other.xshift + other.width
		    && screen_detected_patches->test_bit (x + xshift, y + yshift)
		    && other.screen_detected_patches->test_bit (floor (xx) + other.xshift, floor (yy) + other.yshift))
		  {
		    point_t p1 = scr_to_img_map.to_img ({(coord_t)x, (coord_t)y});
		    point_t p2 = other.scr_to_img_map.to_img ({(coord_t)xx, (coord_t)yy});
		    if (p1.x < img_width * border || p1.x >= img_width * (1 - border) || p1.y < img_height * border || p1.y >= img_height * (1 - border)
			|| p2.x < other.img_width * border || p2.x >= other.img_width * (1 - border) || p2.y < other.img_height * border || p2.y >= other.img_height * (1 - border))
		      continue;
		    coord_t px = C(0) + p1.x * C(2) + p1.y * C(4);
		    coord_t py = C(1) + p1.x * C(3) + p1.y * C(5);
		    coord_t dist = sqrt ((p2.x - px) * (p2.x - px) + (p2.y - py) * (p2.y - py));
		    distsum += dist;
		    maxdist = std::max (maxdist, dist);
		    assert ((((int)p1.y) / m_prj->stitch_info_scale) * (img_width / m_prj->stitch_info_scale + 1) + ((int)p1.x) / m_prj->stitch_info_scale <= (img_width / m_prj->stitch_info_scale + 1) * (img_height / m_prj->stitch_info_scale + 1));
		    assert ((((int)p1.y) / m_prj->stitch_info_scale) * (img_width / m_prj->stitch_info_scale + 1) + ((int)p1.x) / m_prj->stitch_info_scale >= 0);
		    struct stitch_info &info = stitch_info[(((int)p1.y) / m_prj->stitch_info_scale) * (img_width / m_prj->stitch_info_scale + 1) + ((int)p1.x) / m_prj->stitch_info_scale];
		    info.x += fabs(p2.x-px);
		    info.y += fabs(p2.y-py);
		    info.sum++;
		    assert ((((int)p2.y) / m_prj->stitch_info_scale) * (other.img_width / m_prj->stitch_info_scale + 1) + ((int)p2.x) / m_prj->stitch_info_scale <= (other.img_width / m_prj->stitch_info_scale + 1) * (other.img_height / m_prj->stitch_info_scale + 1));
		    assert ((((int)p2.y) / m_prj->stitch_info_scale) * (other.img_width / m_prj->stitch_info_scale + 1) + ((int)p2.x) / m_prj->stitch_info_scale >= 0);
		    struct stitch_info &info2 = other.stitch_info[(((int)p2.y) / m_prj->stitch_info_scale) * (other.img_width / m_prj->stitch_info_scale + 1) + ((int)p2.x) / m_prj->stitch_info_scale];
		    info2.x += fabs(p2.x-px);
		    info2.y += fabs(p2.y-py);
		    info2.sum++;
		    npoints++;
		  }
	      }
	}

      progress->pause_stdout ();
      printf ("Overlap of %s and %s in %i points avg distance %f max distance %f\n", filename.c_str (), other.filename.c_str (), npoints, distsum / npoints, maxdist);
      if (m_prj->report_file)
        fprintf (m_prj->report_file, "Overlap of %s and %s in %i points avg distance %f max distance %f\n", filename.c_str (), other.filename.c_str (), npoints, distsum / npoints, maxdist);
      progress->resume_stdout ();
      gsl_matrix_free (X);
      gsl_vector_free (vy);
      gsl_vector_free (w);
      gsl_vector_free (c);
      gsl_matrix_free (cov);
      if (distsum / npoints > m_prj->params.max_avg_distance)
	{
  	  if (m_prj->params.geometry_info || m_prj->params.individual_geometry_info)
	    write_stitch_info (progress);
	  progress->pause_stdout ();
	  printf ("Average distance out of tolerance (--max-avg-distance parameter)\n");
	  if (m_prj->report_file)
	    fprintf (m_prj->report_file, "Average distance out of tolerance (--max-avg-distance parameter)\n");
	  exit (1);
	}
      if (maxdist > m_prj->params.max_max_distance)
	{
  	  if (m_prj->params.geometry_info || m_prj->params.individual_geometry_info)
	    write_stitch_info (progress);
	  progress->pause_stdout ();
	  printf ("Maximal distance out of tolerance (--max-max-distanace parameter)\n");
	  if (m_prj->report_file)
	    fprintf (m_prj->report_file, "Maximal distance out of tolerance (--max-max-distnace parameter)\n");
	  exit (1);
	}
    }
  return n;
}

void
stitch_image::update_scr_to_final_parameters (coord_t ratio, coord_t anlge)
{
  scr_to_img_map.update_scr_to_final_parameters (ratio, angle);
  basic_scr_to_img_map.update_scr_to_final_parameters (ratio, angle);
  scr_to_img_map.get_final_range (img_width, img_height, &final_xshift, &final_yshift, &final_width, &final_height);
}

bool
stitch_image::analyze (stitch_project *prj, detect_regular_screen_params *dsparamsptr, bool top_p, bool bottom_p, bool left_p, bool right_p, lens_warp_correction_parameters &lens_correction, progress_info *progress)
{
  if (analyzed)
    return true;
  m_prj = prj;
  top = top_p;
  bottom = bottom_p;
  left = left_p;
  right = right_p;
  if (m_prj->report_file)
    fprintf (m_prj->report_file, "\n\nAnalysis of %s\n", filename.c_str ());
  //bitmap_2d *bitmap;
  const char *error;
  if (!load_img (&error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Failed to load %s: %s", filename.c_str (), error);
      progress->resume_stdout ();
      return false;
    }
  //mesh_trans = detect_solver_points (*img, dparam, solver_param, progress, &xshift, &yshift, &width, &height, &bitmap);
#if 0
  if (m_prj->params.optimize_colors)
  {
    if (!optimize_screen_colors (&dparam, img, rparam.gamma, img->width / 2 - 500, img->height /2 - 500, 1000, 1000,  progress, report_file))
      {
	progress->pause_stdout ();
	fprintf (stderr, "Failed analyze screen colors of %s\n", filename.c_str ());
	exit (1);
      }
  }
#endif
  int inborder = m_prj->params.inner_tile_border;
  int skiptop = top ? m_prj->params.outer_tile_border : inborder;
  int skipbottom = bottom ? m_prj->params.outer_tile_border : inborder;
  int skipleft = left ? m_prj->params.outer_tile_border : inborder;
  int skipright = right ? m_prj->params.outer_tile_border : inborder;

  if (!m_prj->params.load_registration)
    {
      detect_regular_screen_params dsparams = *dsparamsptr;
      dsparams.border_top = skiptop;
      dsparams.border_bottom = skipbottom;
      dsparams.border_left = skipleft;
      dsparams.border_right = skipright;
      dsparams.top = top;
      dsparams.bottom = bottom;
      dsparams.left = left;
      dsparams.right = right;
      dsparams.return_known_patches = true;
      dsparams.return_screen_map = true;
      if (dsparams.gamma)
	dsparams.gamma = dsparams.gamma;
      else 
	dsparams.gamma = img->gamma != -2 ? img->gamma : 0;
      if (dsparams.scanner_type == max_scanner_type)
	dsparams.scanner_type = fixed_lens;
      if (dsparams.gamma == 0)
	{
	  fprintf (stderr, "Warning: unable to detect gamma and assuming 2.2\n");
	  dsparams.gamma = 2.2;
	}
      detected = detect_regular_screen (*img, m_prj->dparam, m_prj->solver_param, &dsparams, progress, m_prj->report_file);
      if (!detected.success)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Failed to analyze screen of %s\n", filename.c_str ());
	  exit (1);
	}
      mesh_trans = detected.mesh_trans;
      if (m_prj->params.reoptimize_colors)
	{
	  scr_detect_parameters optimized_dparam = m_prj->dparam;
	  optimize_screen_colors (&optimized_dparam, detected.param.type, img.get (), mesh_trans.get (), detected.xshift, detected.yshift, detected.known_patches, m_prj->rparam.gamma, progress, m_prj->report_file);
	  mesh_trans= NULL;
	  delete detected.known_patches;
	  delete detected.smap;
	  dsparams.optimize_colors = false;
	  detected = detect_regular_screen (*img, optimized_dparam, m_prj->solver_param, &dsparams, progress, m_prj->report_file);
	  mesh_trans = detected.mesh_trans;
	  if (!detected.success)
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Failed to analyze screen of %s after optimizing screen colors. Probably a bug\n", filename.c_str ());
	      exit (1);
	    }
	}
      param = detected.param;
    }
  else
    {
      std::string par_filename = filename;
      int len = filename.length ();
      par_filename[len-3]='p';
      par_filename[len-2]='a';
      par_filename[len-1]='r';
      scr_to_img_parameters p;
      FILE *f = fopen (par_filename.c_str (), "rt");
      if (!f)
        {
	  progress->pause_stdout ();
	  fprintf (stderr, "Failed to open %s\n", par_filename.c_str ());
	  return false;
        }
      const char *error;
      //scr_to_img_parameters nparam;
      if (!load_csp (f, &param, NULL, NULL, NULL, &error))
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Failed to load %s: %s\n", par_filename.c_str (), error);
	  fclose(f);
	  return false;
	}
      fclose (f);
      mesh_trans = param.mesh_trans;
    }


  gray_max = img->maxval;
  render_parameters my_rparam;
  my_rparam.gamma = m_prj->rparam.gamma;
  my_rparam.precise = true;
  my_rparam.screen_blur_radius = 0.5;
  my_rparam.mix_red = 0;
  my_rparam.mix_green = 0;
  my_rparam.mix_blue = 1;
  param.mesh_trans = mesh_trans;
  render_to_scr render (param, *img, my_rparam, 256);
  render.precompute_all (true, false, progress);
  if (!m_prj->my_screen)
    {
      m_prj->pixel_size = detected.pixel_size;
    }
  else
    {
      render_parameters dummy;
      render_img r (param, *img, dummy, 255);
      if (!r.precompute_all (progress))
	return false;
      m_prj->pixel_size = r.pixel_size ();
    }
  m_prj->my_screen = render_to_scr::get_screen (param.type, false, m_prj->pixel_size * my_rparam.screen_blur_radius, 0, 0, progress);
  scr_to_img_map.set_parameters (param, *img, m_prj->rotation_adjustment);
  m_prj->rotation_adjustment = scr_to_img_map.get_rotation_adjustment ();
  
  if (!m_prj->stitch_info_scale)
    m_prj->stitch_info_scale = param.coordinate1.length () + 1;
  if (m_prj->params.outliers_info && !detected.smap->write_outliers_info (((std::string)"outliers-"+ filename).c_str (), img->width, img->height, m_prj->stitch_info_scale, scr_to_img_map, &error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Failed to write outliers: %s\n", error);
      exit (1);
    }
  if (!m_prj->params.load_registration)
    {
      delete detected.smap;
      basic_scr_to_img_map.set_parameters (detected.param, *img);
    }
  else
    {
      scr_to_img_parameters p = param;
      p.mesh_trans = NULL;
      basic_scr_to_img_map.set_parameters (p, *img);
      known_pixels = (std::unique_ptr<bitmap_2d>)(compute_known_pixels (scr_to_img_map, 0, 0, 0, 0, NULL));
    }
  render.compute_final_range ();
  final_xshift = render.get_final_xshift ();
  final_yshift = render.get_final_yshift ();
  final_width = render.get_final_width ();
  final_height = render.get_final_height ();

  scr_to_img_map.get_range (img->width, img->height, &xshift, &yshift, &width, &height);
  if (!m_prj->params.load_registration)
    {
      screen_detected_patches =  std::make_unique<bitmap_2d> (width, height);
      for (int y = 0; y < height; y++)
	if (y - yshift +  detected.yshift > 0 && y - yshift +  detected.yshift < (int)detected.known_patches->height)
	  for (int x = 0; x < width; x++)
	    if (x - xshift +  detected.xshift > 0 && x - xshift +  detected.xshift < (int)detected.known_patches->width
		&& detected.known_patches->test_bit (x - xshift + detected.xshift, y - yshift +  detected.yshift))
	       screen_detected_patches->set_bit (x, y);
      delete detected.known_patches;
      detected.known_patches = NULL;
    }
  analyze_dufay *dufay;
  analyze_paget *paget;
  if (m_prj->scr_param.type == Random)
    {
      m_prj->scr_param.type = param.type;
      m_prj->scr_param.scanner_type = param.scanner_type;
    }
  else if (m_prj->scr_param.type != param.type)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Tiles with different screen types can not stitch together\n");
      return false;
    }
  if (param.type == Dufay)
    {
      dufay = new (analyze_dufay);
      dufay->analyze (&render, img.get(), &scr_to_img_map, m_prj->my_screen, width, height, xshift, yshift, analyze_base::precise, 0.7, progress);
      analyzer = (std::unique_ptr <analyze_base>) (dufay);
    }
  else
    {
      paget = new (analyze_paget);
      assert (detected.param.type != Dufay);
      paget->analyze (&render, img.get(), &scr_to_img_map, m_prj->my_screen, width, height, xshift, yshift, analyze_base::precise, 0.7, progress);
      analyzer = (std::unique_ptr <analyze_base>) (paget);
    }
  if (m_prj->params.max_contrast >= 0)
    dufay->analyze_contrast (&render, img.get(), &scr_to_img_map, progress);
  get_analyzer().set_known_pixels (compute_known_pixels (scr_to_img_map, skiptop, skipbottom, skipleft, skipright, progress) /*screen_detected_patches*/);
  screen_filename = (std::string)"screen"+(std::string)filename;
  known_screen_filename = (std::string)"known_screen"+(std::string)filename;
  known_pixels = (std::unique_ptr<bitmap_2d>)(compute_known_pixels (scr_to_img_map, 0, 0, 0, 0, progress));
  if (m_prj->params.screen_tiles && !get_analyzer().write_screen (screen_filename.c_str (), NULL, &error, progress, 0, 1, 0, 1, 0, 1))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", screen_filename.c_str (), error);
      exit (1);
    }
  if (m_prj->params.known_screen_tiles && !get_analyzer().write_screen (known_screen_filename.c_str (), screen_detected_patches.get (), &error, progress, 0, 1, 0, 1, 0, 1))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", known_screen_filename.c_str (), error);
      exit (1);
    }
  progress->pause_stdout ();
  angle = param.get_angle ();
  ratio = param.get_ylen () / param.get_xlen ();
  if (m_prj->report_file)
    fprintf (m_prj->report_file, "Screen angle %f, x length %f, y length %f, ratio %f\n", angle, param.get_xlen (), param.get_ylen (), ratio);
  progress->resume_stdout ();
  //dufay.set_known_pixels (bitmap);
  analyzed = true;
  release_img ();
  return true;
}

bool
stitch_image::pixel_known_p (coord_t sx, coord_t sy)
{
  int ax = floor (sx) + xshift - pos.x;
  int ay = floor (sy) + yshift - pos.y;
  if (ax < 0 || ay < 0 || ax >= width || ay >= height)
    return false;
  return known_pixels->test_range ({ax, ay}, 2);
}
bool
pixel_known_p_wrap (void *data, coord_t sx, coord_t sy)
{
  return ((stitch_image *)data)->pixel_known_p (sx, sy);
}
bool
stitch_image::img_pixel_known_p (coord_t sx, coord_t sy)
{
  point_t imgp = scr_to_img_map.to_img ({sx - pos.x, sy - pos.y});
  return imgp.x >= (left ? 5 : img_width * 0.02)
	 && imgp.y >= (top ? 5 : img_height * 0.02)
	 && imgp.x <= (right ? img_width - 5 : img_width * 0.98)
	 && imgp.y <= (bottom ? img_height - 5 : img_height * 0.98);
}
bool
img_pixel_known_p_wrap (void *data, coord_t sx, coord_t sy)
{
  return ((stitch_image *)data)->img_pixel_known_p (sx, sy);
}

/* Write one row.  */
bool
stitch_image::write_row (TIFF * out, int y, uint16_t * outrow, const char **error, progress_info *progress)
{
  if (progress && progress->cancel_requested ())
    {
      free (outrow);
      TIFFClose (out);
      *error = "Cancelled";
      return false;
    }
  if (TIFFWriteScanline (out, outrow, y, 0) < 0)
    {
      free (outrow);
      TIFFClose (out);
      *error = "Write error";
      return false;
    }
   if (progress)
     progress->inc_progress ();
  return true;
}

void
stitch_image::compare_contrast_with (stitch_image &other, progress_info *progress)
{
  int x1, y1, x2, y2;
  int xs = other.pos.x - pos.x;
  int ys = other.pos.y - pos.y;
  if (!m_prj)
    return;
  if (m_prj->params.max_contrast < 0)
    return;
  analyze_dufay *dufay = static_cast <analyze_dufay *>(analyzer.get ());
  analyze_dufay *other_dufay = static_cast <analyze_dufay *>(other.analyzer.get ());
  luminosity_t ratio = dufay->compare_contrast (*other_dufay, xs, ys, &x1, &y1, &x2, &y2, scr_to_img_map, other.scr_to_img_map, progress);
  if (ratio < 0)
    {
      if (progress)
	progress->pause_stdout ();
      printf ("Failed to compare contrast ratio of %s and %s\n", filename.c_str (), other.filename.c_str ());
      if (progress)
	progress->resume_stdout ();
      return;
    }
  if (m_prj->report_file)
    fprintf (m_prj->report_file, "Contrast difference of %s and %s: %f%%\n", filename.c_str (), other.filename.c_str (), (ratio - 1) * 100);
  if ((ratio - 1) * 100 < m_prj->params.max_contrast)
    return;
  if (progress)
    progress->pause_stdout ();
  printf ("Out of threshold contrast difference of %s and %s: %f%%\n", filename.c_str (), other.filename.c_str (), (ratio - 1) * 100);
  if (progress)
    progress->resume_stdout ();

  int range = 400;
  //TODO
  char buf[4096];
  buf[4095]=0;
  snprintf (buf, 4095, "contrast-%03i-%s-%s",(int)((ratio -1) * 100 + 0.5), filename.c_str(), other.filename.c_str());

  /* TODO: Error ignored.  */
  const char *error;
  if (!load_img (&error, progress)
      || !other.load_img (&error, progress))
    return;
  progress->pause_stdout ();
  printf ("Saving contrast diff %s\n", buf);
  progress->resume_stdout ();

  TIFF *out = TIFFOpen (buf, "wb");
  double dpi = 300;
  if (!out)
    {
      //*error = "can not open output file";
      return;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, range*3)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, range)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB)
      || !TIFFSetField (out, TIFFTAG_XRESOLUTION, dpi)
      || !TIFFSetField (out, TIFFTAG_YRESOLUTION, dpi)
      || (img->icc_profile && !TIFFSetField (out, TIFFTAG_ICCPROFILE, img->icc_profile_size, img->icc_profile)))
    {
      //*error = "write error";
      return;
    }
  uint16_t *outrow = (uint16_t *) malloc (range * 6 * 3);
  if (!outrow)
    {
      //*error = "Out of memory allocating output buffer";
      return;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving contrast info", range);
    }
  for (int y = 0; y < range; y++)
    {
      for (int x =0 ; x < range; x++)
        {
	  int yy = y + y1 - range / 2;
	  int xx = x + x1 - range / 2;
	  if (yy >= 0 && yy < img->height && xx >= 0 && xx <= img->width)
	    {
	      outrow[x*3] = img->rgbdata[yy][xx].r;
	      outrow[x*3+1] = img->rgbdata[yy][xx].g;
	      outrow[x*3+2] = img->rgbdata[yy][xx].b;
	      if (x < range/2)
		{
		  outrow[range * 6 + x*3] = img->rgbdata[yy][xx].r;
		  outrow[range * 6 + x*3+1] = img->rgbdata[yy][xx].g;
		  outrow[range * 6 + x*3+2] = img->rgbdata[yy][xx].b;
		}
	    }
	  else
	    {
	      outrow[x*3] = 0;
	      outrow[x*3+1] = 0;
	      outrow[x*3+2] = 0;
	      if (x < range / 2)
		{
		  outrow[range * 6 + x*3] = 0;
		  outrow[range * 6 + x*3+1] = 0;
		  outrow[range * 6 + x*3+2] = 0;
		}
	    }
	  yy = y + y2 - range / 2;
	  xx = x + x2 - range / 2;
	  if (yy >= 0 && yy < other.img->height && xx >= 0 && xx <= other.img->width)
	    {
	      outrow[range * 3 + x*3] = other.img->rgbdata[yy][xx].r;
	      outrow[range * 3 + x*3+1] = other.img->rgbdata[yy][xx].g;
	      outrow[range * 3 + x*3+2] = other.img->rgbdata[yy][xx].b;
	      if (x >= range/2)
		{
		  outrow[range * 6 + x*3] = other.img->rgbdata[yy][xx].r;
		  outrow[range * 6 + x*3+1] = other.img->rgbdata[yy][xx].g;
		  outrow[range * 6 + x*3+2] = other.img->rgbdata[yy][xx].b;
		}
	    }
	  else
	    {
	      outrow[range * 3 + x*3] = 0;
	      outrow[range * 3 + x*3+1] = 0;
	      outrow[range * 3 + x*3+2] = 0;
	      if (x >= range/2)
		{
		  outrow[range * 6 + x*3] = 0;
		  outrow[range * 6 + x*3+1] = 0;
		  outrow[range * 6 + x*3+2] = 0;
		}
	    }
        }
      const char *error;
      if (!write_row (out, y, outrow, &error, progress))
        {
	  free (outrow);
	  TIFFClose (out);
	  return;
        }
    }
  release_img ();
  other.release_img ();
  free (outrow);
  TIFFClose (out);
}
void
stitch_image::write_stitch_info (progress_info *progress, int x, int y, int xx, int yy)
{
  if (progress)
    progress->pause_stdout ();
  printf ("Writting geometry info for %s\n", filename.c_str ());
  if (progress)
    progress->resume_stdout ();

  std::string prefix = "geometry-";
  std::string tfilename;
  if (x < 0)
    tfilename = prefix + filename;
  else
    tfilename = prefix + std::to_string (y) + std::to_string (x) + "-" + std::to_string (yy) + std::to_string (xx) + ".tif";
  TIFF *out = TIFFOpen (tfilename.c_str (), "wb");
  if (!out)
    {
      //*error = "can not open output file";
      return;
    }
  if (!TIFFSetField (out, TIFFTAG_IMAGEWIDTH, img_width / m_prj->stitch_info_scale + 1)
      || !TIFFSetField (out, TIFFTAG_IMAGELENGTH, img_height / m_prj->stitch_info_scale + 1)
      || !TIFFSetField (out, TIFFTAG_SAMPLESPERPIXEL, 3)
      || !TIFFSetField (out, TIFFTAG_BITSPERSAMPLE, 16)
      || !TIFFSetField (out, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT)
      || !TIFFSetField (out, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT)
      || !TIFFSetField (out, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG)
      || !TIFFSetField (out, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB))
    {
      //*error = "write error";
      return;
    }
  uint16_t *outrow = (uint16_t *) malloc ((img_width / m_prj->stitch_info_scale + 1) * 2 * 3);
  if (!outrow)
    {
      //*error = "Out of memory allocating output buffer";
      return;
    }
  if (progress)
    {
      progress->set_task ("Rendering and saving geometry info", img_height / m_prj->stitch_info_scale + 1);
    }
  for (int y = 0; y < img_height / m_prj->stitch_info_scale + 1; y++)
    {
      for (int x =0 ; x < img_width / m_prj->stitch_info_scale + 1; x++)
        {
	  struct stitch_info &i = stitch_info[y * (img_width / m_prj->stitch_info_scale + 1) + x];
	  if (!i.sum)
	    {
	      outrow[x*3] = 0;
	      outrow[x*3+1] = 0;
	      outrow[x*3+2] = 65535;
	    }
	  else
	    {
	      outrow[x*3] = std::min ((i.x / i.sum) * 65535 / 2, (coord_t)65535);
	      outrow[x*3+1] = std::min ((i.y / i.sum) * 65535 / 2, (coord_t)65535);
	      outrow[x*3+2] = 0;
	    }
        }
      const char *error;
      if (!write_row (out, y, outrow, &error, progress))
        {
	  free (outrow);
	  TIFFClose (out);
	  return;
        }
    }
  free (outrow);
  TIFFClose (out);
}

bool
stitch_image::write_tile (render_parameters rparam, int stitch_xmin, int stitch_ymin, render_to_file_params rfparams, render_type_parameters &rtparam, const char **error, progress_info *progress)
{
  /* We must arrange whole panorama rotated same way.
     If this ever breaks, maybe while analyzing later images should get it from first one.  */
  if (fabs (scr_to_img_map.get_rotation_adjustment () - m_prj->common_scr_to_img.get_rotation_adjustment ()) > 1)
    abort ();

  point_t final_pos = m_prj->common_scr_to_img.scr_to_final (pos);
  int xmin = floor ((final_pos.x - final_xshift) / rfparams.xstep) * rfparams.xstep;
  int ymin = floor ((final_pos.y - final_yshift) / rfparams.ystep) * rfparams.ystep;
  coord_t xoffset = (xmin - stitch_xmin) / rfparams.xstep;
  coord_t yoffset = (ymin - stitch_ymin) / rfparams.ystep;

  /* Lame: we need to find our indexes to figure out what adjustment applies.  */
  int iy, ix = 0;
  for (iy = 0; iy < m_prj->params.height; iy++)
    {
      for (ix = 0; ix < m_prj->params.width; ix++)
	if (&m_prj->images[iy][ix] == this)
	  break;
      if (ix != m_prj->params.width)
	break;
    }
  assert (iy != m_prj->params.height);

  rparam.get_tile_adjustment (m_prj, ix, iy).apply (&rparam);
  rfparams.pixel_size = m_prj->pixel_size;
  rfparams.tile = true;
  rfparams.xoffset = floor (xoffset);
  rfparams.yoffset = floor (yoffset);
  /* Compensate sub-pixel differences.  */
  rfparams.xstart = xmin + (xmin - stitch_xmin) - rfparams.xoffset * rfparams.xstep;
  rfparams.ystart = ymin + (ymin - stitch_ymin) - rfparams.yoffset * rfparams.ystep;
  rfparams.common_map = &m_prj->common_scr_to_img;
  rfparams.xpos = pos.x;
  rfparams.ypos = pos.y;
  rfparams.width = final_width / rfparams.xstep;
  rfparams.height = final_height / rfparams.ystep;
  /* TODO, handle by flags.  Is it still needed? */
  rfparams.pixel_known_p = rtparam.type == render_type_original ? img_pixel_known_p_wrap : pixel_known_p_wrap;
  rfparams.pixel_known_p_data = (void *)this;
  if (!load_img (error, progress))
    return false;
  if (!render_to_file (*img, param, m_prj->dparam, rparam, rfparams, rtparam, progress, error))
    {
      release_img ();
      return false;
    }
  
  release_img ();
  return true;
}

bool
stitch_image::save (FILE *f)
{
  if (fprintf (f, "stitch_image_filename: %s\n", filename.c_str ()) < 0
      || fprintf (f, "stitch_image_angle: %f\n", angle) < 0
      || fprintf (f, "stitch_image_ratio: %f\n", ratio) < 0
      || fprintf (f, "stitch_image_position: %f %f\n", pos.x, pos.y) < 0
      || fprintf (f, "stitch_image_size: %i %i\n", img_width, img_height) < 0
      || fprintf (f, "stitch_image_scr_size: %i %i\n", width, height) < 0
      || fprintf (f, "stitch_image_scr_shift: %i %i\n", xshift, yshift) < 0)
    return false;
  if (!save_csp (f, &param, NULL, NULL, NULL))
    return false;
  /*if (fprintf (f, "stitch_image_mesh: %s\n", mesh_trans ? "yes" : "no") < 0)
    return false;
  if (mesh_trans && !mesh_trans->save (f))
    return false;*/
  return true;
}

bool
stitch_image::load (stitch_project *prj, FILE *f, const char **error)
{
  m_prj = prj;
  if (!expect_keyword (f, "stitch_image_filename: "))
    {
      *error = "expected stitch_image_filename";
      return false;
    }
  filename = "";
  char c;
  while ((c = getc (f)) != '\n')
    {
      filename.append (1, c);
      if (feof (f))
	{
	  *error = "can not parse stitch_image_filename";
	  return false;
	}
    }
  if (!expect_keyword (f, "stitch_image_angle: ")
      || !read_scalar (f, &angle))
    {
      *error = "expected stitch_image_angle";
      return false;
    }
  if (!expect_keyword (f, "stitch_image_ratio: ")
      || !read_scalar (f, &ratio))
    {
      *error = "expected stitch_image_ratio";
      return false;
    }
  if (!expect_keyword (f, "stitch_image_position: ")
      || !read_scalar (f, &pos.x)
      || !read_scalar (f, &pos.y))
    {
      *error = "expected stitch_image_position";
      return false;
    }
  if (!expect_keyword (f, "stitch_image_size: ")
      || fscanf (f, "%i %i", &img_width, &img_height) != 2)
    {
      *error = "expected stitch_image_size";
      return false;
    }
  if (!expect_keyword (f, "stitch_image_scr_size: ")
      || fscanf (f, "%i %i", &width, &height) != 2)
    {
      *error = "expected stitch_image_scr_size";
      return false;
    }
  if (!expect_keyword (f, "stitch_image_scr_shift: ")
      || fscanf (f, "%i %i", &xshift, &yshift) != 2)
    {
      *error = "expected stitch_image_scr_shift";
      return false;
    }
  if (!load_csp (f, &param, NULL, NULL, NULL, error))
    return false;
#if 0
  if (!expect_keyword (f, "stitch_image_mesh: "))
    {
      *error = "expected stitch_image_mesh";
      return false;
    }
  bool m;
  if (!parse_bool (f, &m))
    {
      *error = "error parsing stitch_image_mesh";
      return false;
    }
  if (m)
    {
      mesh_trans = mesh::load (f, error);
      if (!mesh_trans)
	return false;
    }
#endif
  image_data data;
  data.width=1000;
  data.height=1000;
  //param.mesh_trans = mesh_trans;
  //param.mesh_trans = NULL;
  scr_to_img_map.set_parameters (param, data, m_prj->rotation_adjustment);
  m_prj->rotation_adjustment = scr_to_img_map.get_rotation_adjustment ();
  mesh_trans = param.mesh_trans;
  param.mesh_trans = NULL;
  basic_scr_to_img_map.set_parameters (param, data, m_prj->rotation_adjustment);
  param.mesh_trans = mesh_trans;
  known_pixels = (std::unique_ptr<bitmap_2d>)(compute_known_pixels (scr_to_img_map, 5,5,5,5, NULL));
  analyzed = true;
  return true;
}

static inline rgbdata
sample_image_area (image_data *img, render *render, coord_t fx, coord_t fy,
		   int range)
{
  int x, y;
  coord_t rx = my_modf (fx, &x);
  coord_t ry = my_modf (fy, &y);
  int xmin = x - range;
  int xmin2 = xmin;
  int xmax = x + range + 1;
  int xmax2 = xmax;
  int ymin = y - range;
  int ymin2 = ymin;
  int ymax = y + range + 1;
  int ymax2 = ymax;
  if (xmin < 0)
    xmin = 0;
  if (xmax >= img->width)
    xmax = img->width - 1;
  if (ymin < 0)
    ymin = 0;
  if (ymax >= img->height)
    ymax = img->height - 1;
  rgbdata sum = { 0, 0, 0 };
  luminosity_t sumweight = 0;
  //printf ("Sampling %f %f\n",fx,fy);
  for (y = ymin; y < ymax; y++)
    {
      coord_t yweight = 1;
      if (y == ymin2)
	yweight = 1 - ry;
      if (y == ymax2 - 1)
	yweight = ry;

      if (xmin2 >= 0)
	{
	  sum += render->get_linearized_rgb_pixel (xmin2, y) * yweight * (1 - rx);
	  sumweight += yweight * (1 - rx);
	}
      for (x = xmin + 1; x < xmax - 1; x++)
	{
	  sum += render->get_linearized_rgb_pixel (x, y) * yweight;
	  sumweight += yweight;
	}
      if (xmax2 < img->width)
	{
	  sum += render->get_linearized_rgb_pixel (xmax2 - 1, y) * yweight * rx;
	  sumweight += yweight * rx;
	}
    }
  if (!sumweight)
  {
    printf ("Sampling %f %f\n",fx,fy);
    printf ("Weight %f\n",sumweight);
    abort ();
  }
  return {sum.red / sumweight, sum.green / sumweight, sum.blue / sumweight};
}

/* Return samples of common points between image[y][x] and image[ix][iy].  */
stitch_image::common_samples
stitch_image::find_common_points (stitch_image &other, int outerborder, int innerborder, render_parameters &rparams, progress_info *progress, const char **error)
{
  /* Every sample taken is square 2*range x 2xrange of pixels.  */
  const int range = 50;
  /* Make step big enough so samples does not overlap.  */
  const int step = 2 * range;
  int xmin = img_width * (left ? outerborder : innerborder) / 100 + range;
  int ymin = img_height * (top ? outerborder : innerborder) / 100 + range;
  int xmax = img_width * (right ? 100-outerborder : 100-innerborder) / 100 - range;
  int ymax = img_height * (bottom ? 100-outerborder : 100-innerborder) / 100 - range;

  int xmin2 = other.img_width * (other.left ? outerborder : innerborder) / 100 + range;
  int ymin2 = other.img_height * (other.top ? outerborder : innerborder) / 100 + range;
  int xmax2 = other.img_width * (other.right ? 100-outerborder : 100-innerborder) / 100 - range;
  int ymax2 = other.img_height * (other.bottom ? 100-outerborder : 100-innerborder) / 100 - range;

  common_samples samples;
  render *render1 = NULL;
  render *render2 = NULL;

#pragma omp parallel for default(none) shared(other,rparams,render1,render2,progress,error,samples,xmin,xmax,ymin,ymax,xmin2,xmax2,ymin2,ymax2)
  for (int yy = ymin; yy < ymax; yy+= step)
    {
      if ((!progress || !progress->cancel_requested ()) && !*error)
	for (int xx = xmin; xx < xmax; xx+= step)
	  {
	    point_t common = img_to_common_scr ({(coord_t)xx, (coord_t)yy});

	    if (!other.pixel_maybe_in_range_p (common))
	      continue;
	    point_t imgp = other.common_scr_to_img ({common.x, common.y});
	    if (imgp.x < xmin2 || imgp.x >=xmax2 || imgp.y < ymin2 || imgp.y >= ymax2)
	      continue;
	    luminosity_t weight = (luminosity_t)
	      std::min ((coord_t)std::min (std::min (xx, img_width - xx),
						std::min (yy, img_height - yy)),
			std::min (std::min (imgp.x, other.img_width - imgp.x),
			     std::min (imgp.y, other.img_height - imgp.y)));
	    if (!(weight > 0))
	      continue;
#pragma omp critical
	    if (!render1)
	      {
		int stack = 0;
		if (progress)
		  stack = progress->push ();
		if (load_img (error, progress)
		    && other.load_img (error, progress))
		  {
		    render1 = new render (*img, rparams, 255);
		    if (!render1->precompute_all (img->data != NULL, false, {1/3.0, 1/3.0, 1/3.0}, progress))
		      {
			*error = "precomputation failed";
			delete render1;
			render1 = 0;
		      }
		    render2 = new render (*other.img, rparams, 255);
		    if (!render2->precompute_all (img->data != NULL, false, {1/3.0, 1/3.0, 1/3.0}, progress))
		      {
			*error = "precomputation failed";
			delete render1;
			delete render2;
			render2 = 0;
		      }
		  }
		if (progress)
		  progress->pop (stack);
	      }
	    if (!render2)
	      break;
	    struct common_sample sample = {(coord_t)xx,(coord_t)yy,imgp.x,imgp.y,{0,0,0,0},{0,0,0,0},weight};
	    if (img->rgbdata)
	      {
		rgbdata d1 =  sample_image_area (img.get (), render1, xx, yy, range);
		rgbdata d2 =  sample_image_area (other.img.get (), render2, imgp.x, imgp.y, range);

		sample.channel1[0] = d1.red;
		sample.channel2[0] = d2.red;
		sample.channel1[1] = d1.green;
		sample.channel2[1] = d2.green;
		sample.channel1[2] = d1.blue;
		sample.channel2[2] = d2.blue;
	      }
	    /* Implement collection of IR channel.  */
	    if (img->data)
	      abort ();
#pragma omp critical
	     samples.push_back (sample);
	  }
	if (progress)
	  for (int i = 0; i < step; i++)
	    progress->inc_progress ();
      }
   if (render2)
     {
       other.release_img ();
       delete render2;
       release_img ();
       delete render1;
     }
   return samples;
}
}
