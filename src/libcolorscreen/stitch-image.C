#include <gsl/gsl_multifit.h>
#include <tiffio.h>
#include "include/stitch.h"
#include "include/tiff-writer.h"
#include "include/colorscreen.h"
#include "render-interpolate.h"
#include "screen-map.h"
#include "loadsave.h"
extern unsigned char sRGB_icc[];
extern unsigned int sRGB_icc_len;
long stitch_image::current_time;
int stitch_image::nloaded;

stitch_image::stitch_image ()
: filename (""), img (NULL), mesh_trans (NULL), xshift (0), yshift (0),
  width (0), height (0), final_xshift (0), final_yshift (0), final_width (0),
  final_height (0), screen_detected_patches (NULL), known_pixels (NULL),
  render2 (NULL), stitch_info (NULL), refcount (0)
{
}

stitch_image::~stitch_image ()
{
  delete render2;
  delete img;
  delete mesh_trans;
  delete known_pixels;
  delete screen_detected_patches;
  delete stitch_info;
}

void
stitch_image::release_image_data (progress_info *progress)
{
  //progress->pause_stdout ();
  //printf ("Releasing input tile %s\n", filename.c_str ());
  //progress->resume_stdout ();
  assert (!refcount && img);
  delete img;
  img = NULL;
  delete render2;
  render2 = NULL;
  nloaded--;
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
      long minlast = 0;
      int nref = 0;


#if 0
      progress->pause_stdout ();
      for (int y = 0; y < m_prj->params.height; y++)
      {
	for (int x = 0; x < m_prj->params.width; x++)
	  printf (" %i:%5i", images[y][x].img != NULL, (int)images[y][x].lastused);
        printf ("\n");
      }
      progress->resume_stdout ();
#endif

      for (int y = 0; y < m_prj->params.height; y++)
	for (int x = 0; x < m_prj->params.width; x++)
	  if (m_prj->images[y][x].refcount)
	    nref++;
	  else if (m_prj->images[y][x].img
		   && (minx == -1 || m_prj->images[y][x].lastused < minlast))
	    {
	      minx = x;
	      miny = y;
	      minlast = m_prj->images[y][x].lastused;
	    }
      if (minx != -1)
	m_prj->images[miny][minx].release_image_data (progress);
#if 0
      else
	printf ("Too many (%i) images referenced\n", nref);
#endif
    }
  nloaded++;
  if (progress)
    progress->pause_stdout ();
  printf ("Loading input tile %s (%i tiles in memory)\n", filename.c_str (), nloaded);
  if (progress)
    progress->resume_stdout ();
  img = new image_data;
  if (!img->load (m_prj->add_path (filename).c_str (), error, progress))
    return false;
  if (img->stitch)
    {
      *error = "Can not embedd stitch projects in sitch projects";
      return false;
    }
  img_width = img->width;
  img_height = img->height;
  if (m_prj->params.scan_dpi && !img->xdpi)
    img->xdpi = m_prj->params.scan_dpi;
  if (m_prj->params.scan_dpi && !img->ydpi)
    img->ydpi = m_prj->params.scan_dpi;
  if (!img->rgbdata)
    {
      *error = "source image is not having color channels";
      return false;
    }
  return true;
}

void
stitch_image::release_img ()
{
  refcount--;
}

bitmap_2d*
stitch_image::compute_known_pixels (image_data &img, scr_to_img &scr_to_img, int skiptop, int skipbottom, int skipleft, int skipright, progress_info *progress)
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
	  coord_t x1, y1;
	  scr_to_img.to_img (x - xshift - 1, y - yshift - 1, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  scr_to_img.to_img (x - xshift + 2, y - yshift - 1, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  scr_to_img.to_img (x - xshift - 1, y - yshift + 2, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
	    continue;
	  scr_to_img.to_img (x - xshift + 2, y - yshift + 2, &x1, &y1);
	  if (x1 < xmin || x1 >= xmax || y1 < ymin || y1 >= ymax)
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
  sx = sx - xpos + xshift;
  sy = sy - ypos + yshift;
  if (sx < 0 || sy < 0 || sx >= screen_detected_patches->width || sy >= screen_detected_patches->height)
    return false;
  return screen_detected_patches->test_range (sx, sy, 2);
}
bool
stitch_image::diff (stitch_image &other, progress_info *progress)
{
  coord_t sx, sy;
  bool found = false;
  scr_to_img_map.to_scr (0, 0, &sx, &sy);
  if (other.img_pixel_known_p (sx + xpos, sy + ypos))
    found = true;
  scr_to_img_map.to_scr (img_width - 1, 0, &sx, &sy);
  if (other.img_pixel_known_p (sx + xpos, sy + ypos))
    found = true;
  scr_to_img_map.to_scr (0, img_height - 1, &sx, &sy);
  if (other.img_pixel_known_p (sx + xpos, sy + ypos))
    found = true;
  scr_to_img_map.to_scr (img_width - 1, img_height - 1, &sx, &sy);
  if (other.img_pixel_known_p (sx + xpos, sy + ypos))
    found = true;
  if (!found)
    return false;
  int rxmin = INT_MAX, rxmax = INT_MIN, rymin = INT_MAX, rymax = INT_MIN;
  progress->set_task ("determining overlap range", 1);
  for (int y = 0; y < img_height; y += 10)
    for (int x = 0; x < img_width; x += 10)
      {
         scr_to_img_map.to_scr (x, y, &sx, &sy);
         if (other.img_pixel_known_p (sx + xpos, sy + ypos))
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
    return false;
  progress->resume_stdout ();
  int rwidth = rxmax - rxmin + 1;
  int rheight = rymax - rymin + 1;
  const char *error;
  /* TODO: Error ignored.  */
  if (!load_img (&error, progress)
      || !other.load_img (&error, progress))
    return false;
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
      exit (1);
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
	  int r = 0, g = 0, b = 0;
          scr_to_img_map.to_scr (x, y, &sx, &sy);
          if (other.img_pixel_known_p (sx + xpos, sy + ypos))
	   {
	     int r2 = 0, g2 = 0, b2 = 0;
	     render_pixel (65535, sx + xpos, sy + ypos, &r, &g, &b, progress);
	     other.render_pixel (65535, sx + xpos, sy + ypos, &r2, &g2, &b2, progress);
	     if (patch_detected_p (sx + xpos, sy + ypos) && other.patch_detected_p (sx + xpos, sy + ypos))
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
	  progress->pause_stdout ();
	  fprintf (stderr, "Can not write %s\n", fname.c_str ());
	  exit (1);
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
      coord_t yy = y + ypos - other.ypos;
      if (yy >= -other.yshift && yy < -other.yshift + other.height)
	for (int x = -xshift; x < -xshift + width; x++)
	  {
	    coord_t xx = x + xpos - other.xpos;
	    if (xx >= -other.xshift && xx < -other.xshift + other.width
		&& screen_detected_patches->test_range (x + xshift, y + yshift, range)
		&& other.screen_detected_patches->test_range (floor (xx) + other.xshift, floor (yy) + other.yshift, range))
	    {
	      coord_t x1, y1, x2, y2;
	      scr_to_img_map.to_img (x, y, &x1, &y1);
	      other.scr_to_img_map.to_img (xx, yy, &x2, &y2);
	      //mesh_trans->apply (x,y, &x1, &y1);
	      //other.mesh_trans->apply (xx, yy, &x2, &y2);
	      if (x1 < img_width * border || x1 >= img_width * (1 - border) || y1 < img_height * border || y1 >= img_height * (1 - border)
	          || x2 < other.img_width * border || x2 >= other.img_width * (1 - border) || y2 < other.img_height * border || y2 >= other.img_height * (1 - border))
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
      coord_t yy = y + ypos - other.ypos;
      if (yy >= -other.yshift && yy < -other.yshift + other.height)
	for (int x = -xshift; x < -xshift + width; x++)
	  {
	    coord_t xx = x + xpos - other.xpos;
	    if (xx >= -other.xshift && xx < -other.xshift + other.width
		&& screen_detected_patches->test_range (x + xshift, y + yshift, range)
		&& other.screen_detected_patches->test_range (floor (xx) + other.xshift, floor (yy) + other.yshift, range))
	      {
		coord_t x1, y1, x2, y2;
		//mesh_trans->apply (x,y, &x1, &y1);
		//other.mesh_trans->apply (xx, yy, &x2, &y2);
	        scr_to_img_map.to_img (x, y, &x1, &y1);
	        other.scr_to_img_map.to_img (xx, yy, &x2, &y2);
		if (x1 < img_width * border || x1 >= img_width * (1 - border) || y1 < img_height * border || y1 >= img_height * (1 - border)
		    || x2 < other.img_width * border || x2 >= other.img_width * (1 - border) || y2 < other.img_height * border || y2 >= other.img_height * (1 - border))
		  continue;
	        if (m++ == next)
		  {
		    next += step;
		    if (f)
		      fprintf (f,  "c n%i N%i x%f y%f X%f Y%f t0\n", n1, n2, x1, y1, x2, y2);

		    if (!collect_stitch_info || nfound >= npoints)
		      continue;

		    gsl_matrix_set (X, nfound * 2, 0, 1.0);
		    gsl_matrix_set (X, nfound * 2, 1, 0.0);
		    gsl_matrix_set (X, nfound * 2, 2, x1);
		    gsl_matrix_set (X, nfound * 2, 3, 0);
		    gsl_matrix_set (X, nfound * 2, 4, y1);
		    gsl_matrix_set (X, nfound * 2, 5, 0);

		    gsl_matrix_set (X, nfound * 2+1, 0, 0.0);
		    gsl_matrix_set (X, nfound * 2+1, 1, 1.0);
		    gsl_matrix_set (X, nfound * 2+1, 2, 0);
		    gsl_matrix_set (X, nfound * 2+1, 3, x1);
		    gsl_matrix_set (X, nfound * 2+1, 4, 0);
		    gsl_matrix_set (X, nfound * 2+1, 5, y1);

		    gsl_vector_set (vy, nfound * 2, x2);
		    gsl_vector_set (vy, nfound * 2 + 1, y2);
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
	  coord_t yy = y + ypos - other.ypos;
	  if (yy >= -other.yshift && yy < -other.yshift + other.height)
	    for (int x = -xshift; x < -xshift + width; x++)
	      {
		coord_t xx = x + xpos - other.xpos;
		if (xx >= -other.xshift && xx < -other.xshift + other.width
		    && screen_detected_patches->test_bit (x + xshift, y + yshift)
		    && other.screen_detected_patches->test_bit (floor (xx) + other.xshift, floor (yy) + other.yshift))
		  {
		    coord_t x1, y1, x2, y2;
		    //mesh_trans->apply (x,y, &x1, &y1);
		    //other.mesh_trans->apply (xx, yy, &x2, &y2);
	            scr_to_img_map.to_img (x, y, &x1, &y1);
	            other.scr_to_img_map.to_img (xx, yy, &x2, &y2);
		    if (x1 < 0 || x1 >= img_width || y1 < 0 || y1 >= img_height
			|| x2 < 0 || x2 >= other.img_width || y2 < 0 || y2 > other.img_height)
		      continue;
		    coord_t px = C(0) + x1 * C(2) + y1 * C(4);
		    coord_t py = C(1) + x1 * C(3) + y1 * C(5);
		    coord_t dist = sqrt ((x2 - px) * (x2 - px) + (y2 - py) * (y2 - py));
		    distsum += dist;
		    maxdist = std::max (maxdist, dist);
		    assert ((((int)y1) / m_prj->stitch_info_scale) * (img_width / m_prj->stitch_info_scale + 1) + ((int)x1) / m_prj->stitch_info_scale <= (img_width / m_prj->stitch_info_scale + 1) * (img_height / m_prj->stitch_info_scale + 1));
		    assert ((((int)y1) / m_prj->stitch_info_scale) * (img_width / m_prj->stitch_info_scale + 1) + ((int)x1) / m_prj->stitch_info_scale >= 0);
		    struct stitch_info &info = stitch_info[(((int)y1) / m_prj->stitch_info_scale) * (img_width / m_prj->stitch_info_scale + 1) + ((int)x1) / m_prj->stitch_info_scale];
		    info.x += fabs(x2-px);
		    info.y += fabs(y2-py);
		    info.sum++;
		    assert ((((int)y2) / m_prj->stitch_info_scale) * (other.img_width / m_prj->stitch_info_scale + 1) + ((int)x2) / m_prj->stitch_info_scale <= (other.img_width / m_prj->stitch_info_scale + 1) * (other.img_height / m_prj->stitch_info_scale + 1));
		    assert ((((int)y2) / m_prj->stitch_info_scale) * (other.img_width / m_prj->stitch_info_scale + 1) + ((int)x2) / m_prj->stitch_info_scale >= 0);
		    struct stitch_info &info2 = other.stitch_info[(((int)y2) / m_prj->stitch_info_scale) * (other.img_width / m_prj->stitch_info_scale + 1) + ((int)x2) / m_prj->stitch_info_scale];
		    info2.x += fabs(x2-px);
		    info2.y += fabs(y2-py);
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
stitch_image::analyze (stitch_project *prj, bool top_p, bool bottom_p, bool left_p, bool right_p, coord_t k1, progress_info *progress)
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
  detect_regular_screen_params dsparams;
  dsparams.min_screen_percentage = m_prj->params.min_screen_percentage;
  int inborder = m_prj->params.inner_tile_border;
  int skiptop = top ? m_prj->params.outer_tile_border : inborder;
  int skipbottom = bottom ? m_prj->params.outer_tile_border : inborder;
  int skipleft = left ? m_prj->params.outer_tile_border : inborder;
  int skipright = right ? m_prj->params.outer_tile_border : inborder;
  dsparams.border_top = skiptop;
  dsparams.border_bottom = skipbottom;
  dsparams.border_left = skipleft;
  dsparams.border_right = skipright;
  dsparams.top = top;
  dsparams.bottom = bottom;
  dsparams.k1 = k1;
  dsparams.left = left;
  dsparams.right = right;
  dsparams.optimize_colors = m_prj->params.optimize_colors;
  dsparams.slow_floodfill = m_prj->params.slow_floodfill;
  dsparams.fast_floodfill = m_prj->params.fast_floodfill;
  dsparams.max_unknown_screen_range = m_prj->params.max_unknown_screen_range;
  if (m_prj->params.min_patch_contrast > 0)
    dsparams.min_patch_contrast = m_prj->params.min_patch_contrast;
  dsparams.return_known_patches = true;
  dsparams.do_mesh = m_prj->params.mesh_trans;
  dsparams.return_screen_map = true;
  detected = detect_regular_screen (*img, m_prj->params.type, m_prj->dparam, m_prj->rparam.gamma, m_prj->solver_param, &dsparams, progress, m_prj->report_file);
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
      optimize_screen_colors (&optimized_dparam, m_prj->params.type, img, mesh_trans, detected.xshift, detected.yshift, detected.known_patches, m_prj->rparam.gamma, progress, m_prj->report_file);
      delete mesh_trans;
      delete detected.known_patches;
      delete detected.smap;
      dsparams.optimize_colors = false;
      detected = detect_regular_screen (*img, m_prj->params.type, optimized_dparam, m_prj->rparam.gamma, m_prj->solver_param, &dsparams, progress, m_prj->report_file);
      mesh_trans = detected.mesh_trans;
      if (!detected.success)
	{
	  progress->pause_stdout ();
	  fprintf (stderr, "Failed to analyze screen of %s after optimizing screen colors. Probably a bug\n", filename.c_str ());
	  exit (1);
	}
    }


  gray_max = img->maxval;
  render_parameters my_rparam;
  my_rparam.gamma = m_prj->rparam.gamma;
  my_rparam.precise = true;
  my_rparam.gray_max = img->maxval;
  my_rparam.screen_blur_radius = 0.5;
  my_rparam.mix_red = 0;
  my_rparam.mix_green = 0;
  my_rparam.mix_blue = 1;
  param = detected.param;
  param.mesh_trans = mesh_trans;
  param.type = m_prj->params.type;
  render_to_scr render (param, *img, my_rparam, 256);
  render.precompute_all (true, progress);
  if (!m_prj->my_screen)
    {
      m_prj->pixel_size = detected.pixel_size;
      m_prj->my_screen = render_to_scr::get_screen (m_prj->params.type, false, detected.pixel_size * my_rparam.screen_blur_radius, progress);
    }
  scr_to_img_map.set_parameters (param, *img, m_prj->rotation_adjustment);
  m_prj->rotation_adjustment = scr_to_img_map.get_rotation_adjustment ();
  
  if (!m_prj->stitch_info_scale)
    m_prj->stitch_info_scale = sqrt (param.coordinate1_x * param.coordinate1_x + param.coordinate1_y * param.coordinate1_y) + 1;
  if (m_prj->params.outliers_info && !detected.smap->write_outliers_info (((std::string)"outliers-"+ filename).c_str (), img->width, img->height, m_prj->stitch_info_scale, scr_to_img_map, &error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Failed to write outliers: %s\n", error);
      exit (1);
    }
  delete detected.smap;
  basic_scr_to_img_map.set_parameters (detected.param, *img);
  final_xshift = render.get_final_xshift ();
  final_yshift = render.get_final_yshift ();
  final_width = render.get_final_width ();
  final_height = render.get_final_height ();

  scr_to_img_map.get_range (img->width, img->height, &xshift, &yshift, &width, &height);
  screen_detected_patches = new bitmap_2d (width, height);
  for (int y = 0; y < height; y++)
    if (y - yshift +  detected.yshift > 0 && y - yshift +  detected.yshift < detected.known_patches->height)
      for (int x = 0; x < width; x++)
        if (x - xshift +  detected.xshift > 0 && x - xshift +  detected.xshift < detected.known_patches->width
	    && detected.known_patches->test_bit (x - xshift + detected.xshift, y - yshift +  detected.yshift))
           screen_detected_patches->set_bit (x, y);
  delete detected.known_patches;
  detected.known_patches = NULL;
  if (m_prj->params.type == Dufay)
    dufay.analyze (&render, img, &scr_to_img_map, m_prj->my_screen, width, height, xshift, yshift, true, 0.7, progress);
  else
    {
      assert (detected.param.type != Dufay);
      paget.analyze (&render, img, &scr_to_img_map, m_prj->my_screen, width, height, xshift, yshift, true, 0.7, progress);
    }
  if (m_prj->params.max_contrast >= 0)
    dufay.analyze_contrast (&render, img, &scr_to_img_map, progress);
  get_analyzer().set_known_pixels (compute_known_pixels (*img, scr_to_img_map, skiptop, skipbottom, skipleft, skipright, progress) /*screen_detected_patches*/);
  screen_filename = (std::string)"screen"+(std::string)filename;
  known_screen_filename = (std::string)"known_screen"+(std::string)filename;
  known_pixels = compute_known_pixels (*img, scr_to_img_map, 0, 0, 0, 0, progress);
  if (m_prj->params.screen_tiles && !get_analyzer().write_screen (screen_filename.c_str (), NULL, &error, progress, 0, 1, 0, 1, 0, 1))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", screen_filename.c_str (), error);
      exit (1);
    }
  if (m_prj->params.known_screen_tiles && !get_analyzer().write_screen (known_screen_filename.c_str (), screen_detected_patches, &error, progress, 0, 1, 0, 1, 0, 1))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Writting of screen file %s failed: %s\n", known_screen_filename.c_str (), error);
      exit (1);
    }
  progress->pause_stdout ();
  angle = param.get_angle ();
  ratio = param.get_ylen () / param.get_xlen ();
  printf ("Screen angle %f, x length %f, y length %f, ratio %f\n", angle, param.get_xlen (), param.get_ylen (), ratio);
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
  int ax = floor (sx) + xshift - xpos;
  int ay = floor (sy) + yshift - ypos;
  if (ax < 0 || ay < 0 || ax >= width || ay >= height)
    return false;
  return known_pixels->test_range (ax, ay, 2);
}
bool
pixel_known_p_wrap (void *data, coord_t sx, coord_t sy)
{
  return ((stitch_image *)data)->pixel_known_p (sx, sy);
}
bool
stitch_image::img_pixel_known_p (coord_t sx, coord_t sy)
{
  coord_t ix, iy;
  scr_to_img_map.to_img (sx - xpos, sy - ypos, &ix, &iy);
  return ix >= (left ? 5 : img_width * 0.02)
	 && iy >= (top ? 5 : img_height * 0.02)
	 && ix <= (right ? img_width - 5 : img_width * 0.98)
	 && iy <= (bottom ? img_height - 5 : img_height * 0.98);
}
bool
img_pixel_known_p_wrap (void *data, coord_t sx, coord_t sy)
{
  return ((stitch_image *)data)->img_pixel_known_p (sx, sy);
}

bool
stitch_image::render_pixel (int maxval, coord_t sx, coord_t sy, int *r, int *g, int *b, progress_info *progress)
{
  bool loaded = false;
  /* TODO: Ignored. */
  const char *error;
  if (!render2)
    {
      if (!load_img (&error, progress))
	return false;
      render2 = new render_img (param, *img, m_prj->passthrough_rparam, maxval);
      render2->set_color_display ();
      render2->precompute_all (progress);
      release_img ();
      loaded = true;
    }
  else
    lastused = ++current_time;
  return loaded;
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
  int xs = other.xpos - xpos;
  int ys = other.ypos - ypos;
  if (m_prj->params.max_contrast < 0)
    return;
  luminosity_t ratio = dufay.compare_contrast (other.dufay, xs, ys, &x1, &y1, &x2, &y2, scr_to_img_map, other.scr_to_img_map, progress);
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
  sprintf (buf, "contrast-%03i-%s-%s",(int)((ratio -1) * 100 + 0.5), filename.c_str(), other.filename.c_str());

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
stitch_image::write_tile (const char **error, scr_to_img &map, int stitch_xmin, int stitch_ymin, coord_t xstep, coord_t ystep, render_mode mode, progress_info *progress)
{
  std::string suffix;
  coord_t final_xpos, final_ypos;
  map.scr_to_final (xpos, ypos, &final_xpos, &final_ypos);
  int xmin = floor ((final_xpos - final_xshift) / xstep) * xstep;
  int ymin = floor ((final_ypos - final_yshift) / ystep) * ystep;

  /* We must arrange whole panorama rotated same way.
     If this ever breaks, maybe while analyzing later images should get it from first one.  */
  if (scr_to_img_map.get_rotation_adjustment () != m_prj->common_scr_to_img.get_rotation_adjustment ())
    abort ();

  render_to_file_params rfparams;
  switch(mode)
  {
  case render_demosaiced:
    suffix = "-demosaicedtile";
    rfparams.mode = interpolated;
    rfparams.hdr = m_prj->params.hdr;
    break;
  case render_original:
    rfparams.mode = corrected_color;
    suffix = "-tile";
    break;
  case render_predictive:
    suffix = "-predictivetile";
    rfparams.mode = predictive;
    rfparams.hdr = m_prj->params.hdr;
    break;
  case render_max:
    abort ();
  }

  if (!load_img (error, progress))
    return false;
  std::string name = m_prj->adjusted_filename (filename, suffix, ".tif");

  rfparams.filename = name.c_str ();
  rfparams.tile = true;
  coord_t xoffset = (xmin - stitch_xmin) / xstep;
  coord_t yoffset = (ymin - stitch_ymin) / ystep;
  rfparams.xoffset = floor (xoffset);
  rfparams.yoffset = floor (yoffset);
  /* Compensate sub-pixel differences.  */
  rfparams.xstart = xmin + (xmin - stitch_xmin) - rfparams.xoffset * xstep;
  rfparams.ystart = ymin + (ymin - stitch_ymin) - rfparams.yoffset * ystep;
  rfparams.xstep = xstep;
  rfparams.ystep = ystep;
  rfparams.pixel_known_p = mode == render_original ? img_pixel_known_p_wrap : pixel_known_p_wrap;
  rfparams.pixel_known_p_data = (void *)this;
  rfparams.common_map = &map;
  rfparams.xpos = xpos;
  rfparams.ypos = ypos;
  rfparams.width = final_width / xstep;
  rfparams.height = final_height / ystep;
  rfparams.pixel_size = m_prj->pixel_size;
  rfparams.verbose = true;
  rfparams.xdpi = m_prj->xdpi [(int)mode];
  rfparams.ydpi = m_prj->ydpi [(int)mode];
  if ((!rfparams.xdpi || !rfparams.ydpi) && img->xdpi && img->ydpi)
    {
      render_to_file_params rf2 = rfparams;
      complete_rendered_file_parameters (param, *img, &rf2);
      m_prj->xdpi[(int)mode] = rfparams.xdpi = rf2.xdpi;
      m_prj->ydpi[(int)mode] = rfparams.ydpi = rf2.ydpi;
    }
  if (!render_to_file (*img, param, m_prj->dparam, mode == render_original ? m_prj->passthrough_rparam : m_prj->rparam, rfparams, progress, error))
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
      || fprintf (f, "stitch_image_position: %f %f\n", xpos, ypos) < 0
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
      || !read_scalar (f, &xpos)
      || !read_scalar (f, &ypos))
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
  scr_to_img_map.set_parameters (param, data);
  param.mesh_trans = mesh_trans;
  known_pixels = compute_known_pixels (*img, scr_to_img_map, 0, 0, 0, 0, NULL);
  analyzed = true;
  return true;
}
