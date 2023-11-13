#include <vector>
#include <locale>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_linalg.h>
#include "include/stitch.h"
#include "render-interpolate.h"
#include "screen-map.h"
#include "loadsave.h"
stitch_project::stitch_project ()
  : params (), report_file (NULL), images(), param (), rparam (),
    passthrough_rparam (), common_scr_to_img (), dparam (), solver_param (),
    pixel_size (0), my_screen (NULL), stitch_info_scale (0), 
    release_images (true), rotation_adjustment (0)
{}

stitch_project::~stitch_project ()
{
  if (my_screen)
    render_to_scr::release_screen (my_screen);
  my_screen = NULL;
}

bool
stitch_project::initialize ()
{
  passthrough_rparam.gamma = rparam.gamma;
  if (params.orig_tile_gamma > 0)
    passthrough_rparam.output_gamma = params.orig_tile_gamma;
  else
    passthrough_rparam.output_gamma = rparam.gamma;

  for (int i = 0; i < (int)stitch_image::render_max; i++)
  {
    xdpi[i] = 0;
    ydpi[i] = 0;
  }

  image_data data;
  scr_param.type = params.type;
  data.width=1000;
  data.height=1000;
  common_scr_to_img.set_parameters (scr_param, data, rotation_adjustment);

  if ((params.width == 1 || params.height == 1) && params.outer_tile_border > 40)
    {
      fprintf (stderr, "Outer tile border is too large for single row or column stitching\n");
      return false;
    }
  if (params.outer_tile_border > 80)
    {
      fprintf (stderr, "Outer tile border is too large\n");
      return false;
    }
  if (params.report_filename.length ())
    {
      report_file = fopen (params.report_filename.c_str (), "wt");
      if (!report_file)
	{
	  fprintf (stderr, "Can not open report file: %s\n", params.report_filename.c_str ());
	  return false;
	}
    }
  return true;
}

bool
stitch_project::analyze (int x, int y, progress_info *progress)
{
  return images[y][x].analyze (this, !y, y == params.height - 1,
			       !x, x == params.width - 1, param.k1,
			       progress);
}

void
stitch_project::determine_viewport (int &xmin, int &xmax, int &ymin, int &ymax)
{
  xmin = 0;
  ymin = 0;
  xmax = 0;
  ymax = 0;
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      if (images[y][x].analyzed)
	{
	  coord_t x1,y1,x2,y2;
	  coord_t rxpos, rypos;
	  common_scr_to_img.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rxpos, &rypos);
	  x1 = -images[y][x].final_xshift + rxpos;
	  y1 = -images[y][x].final_yshift + rypos;
	  x2 = x1 + images[y][x].final_width;
	  y2 = y1 + images[y][x].final_height;

	  if (!y && !x)
	    {
	      xmin = xmax = x1;
	      ymin = ymax = y1;
	    }
	  xmin = std::min (xmin, (int)floor (x1));
	  xmax = std::max (xmax, (int)ceil (x1));
	  ymin = std::min (ymin, (int)floor (y1));
	  ymax = std::max (ymax, (int)ceil (y1));
	  xmin = std::min (xmin, (int)floor (x2));
	  xmax = std::max (xmax, (int)ceil (x2));
	  ymin = std::min (ymin, (int)floor (y2));
	  ymax = std::max (ymax, (int)ceil (y2));
	}
}

void
stitch_project::print_panorama_map (FILE *out)
{
  int xmin, ymin, xmax, ymax;
  determine_viewport (xmin, xmax, ymin, ymax);
  fprintf (out, "Viewport range %i %i %i %i\n", xmin, xmax, ymin, ymax);
  for (int y = 0; y < 20; y++)
    {
      for (int x = 0; x < 40; x++)
	{
	  coord_t fx = xmin + (xmax - xmin) * x / 40;
	  coord_t fy = ymin + (ymax - ymin) * y / 20;
	  coord_t sx, sy;
	  int ix = 0, iy = 0;
	  common_scr_to_img.final_to_scr (fx, fy, &sx, &sy);
	  for (iy = 0 ; iy < params.height; iy++)
	    {
	      for (ix = 0 ; ix < params.width; ix++)
		if (images[iy][ix].analyzed && images[iy][ix].pixel_known_p (sx, sy))
		  break;
	      if (ix != params.width)
		break;
	    }

#if 0
	  if (iy == params.height)
	    fprintf (out, "   ");
	  else
	    fprintf (out, " %i%i",iy+1,ix+1);
#endif
	  if (iy == params.height)
	    fprintf (out, " ");
	  else
	    fprintf (out, "%c",'a'+ix+iy*params.width);
	}
      fprintf (out, "\n");
    }
}

void
stitch_project::print_status (FILE *out)
{
  for (int y = 0; y < params.height; y++)
    {
      if (y)
	{
	  coord_t rx, ry;
	  common_scr_to_img.scr_to_final (images[y-1][0].xpos, images[y-1][0].ypos, &rx, &ry);
	  coord_t rx2, ry2;
	  common_scr_to_img.scr_to_final (images[y][0].xpos, images[y][0].ypos, &rx2, &ry2);
	  rx -= images[y-1][0].xshift;
	  ry -= images[y-1][0].yshift;
	  rx2 -= images[y][0].xshift;
	  ry2 -= images[y][0].yshift;
	  fprintf (out, " down %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	}
      else fprintf (out, "                  ");
      for (int x = 1; x < params.width; x++)
      {
	coord_t rx, ry;
	common_scr_to_img.scr_to_final (images[y][x-1].xpos, images[y][x-1].ypos, &rx, &ry);
	coord_t rx2, ry2;
	common_scr_to_img.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx2, &ry2);
	rx -= images[y][x-1].xshift;
	ry -= images[y][x-1].yshift;
	rx2 -= images[y][x].xshift;
	ry2 -= images[y][x].yshift;
	fprintf (out, " right %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	//printf ("  %-5i,%-5i range: %-5i:%-5i,%-5i:%-5i", (int)rx,(int)ry,(int)rx-images[y][x].xshift+sx,(int)rx-images[y][x].xshift+images[y][x].final_width+sx,(int)ry-images[y][x].yshift+sy,(int)ry-images[y][x].yshift+images[y][x].final_height+sy);
      }
      fprintf (out, "\n");
    }
  for (int y = 0; y < params.height; y++)
    {
      for (int x = 0; x < params.width; x++)
      {
	coord_t rx, ry;
	common_scr_to_img.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx, &ry);
	fprintf (out, "  %-5f,%-5f  rotated:%-5f,%-5f ", images[y][x].xpos, images[y][x].ypos, rx,ry);
      }
      fprintf (out, "\n");
    }
  print_panorama_map (out);
}

bool
stitch_project::analyze_images (progress_info *progress)
{
  if (params.width == 1 && params.height == 1)
    {
      return analyze (0, 0, progress);
    }
  for (int y = 0; y < params.height; y++)
    {
      if (!y)
	{
	  images[0][0].xpos = 0;
	  images[0][0].ypos = 0;
	}
      else
	{
	  coord_t xs;
	  coord_t ys;
	  analyze (0, y-1, progress);
	  analyze (0, y, progress);
	  if (!images[y-1][0].get_analyzer().find_best_match (params.min_overlap_percentage, params.max_overlap_percentage, images[y][0].get_analyzer(), params.cpfind, &xs, &ys, params.limit_directions ? 1 : -1, images[y-1][0].basic_scr_to_img_map, images[y][0].basic_scr_to_img_map, report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y-1][0].filename.c_str (), images[y][0].filename.c_str ());
	      exit (1);
	    }
	  images[y][0].xpos = images[y-1][0].xpos + xs;
	  images[y][0].ypos = images[y-1][0].ypos + ys;
	  images[y-1][0].compare_contrast_with (images[y][0], progress);
	  if (params.geometry_info || params.individual_geometry_info)
	    images[y-1][0].output_common_points (NULL, images[y][0], 0, 0, true, progress);
	  if (params.width)
	    {
	      images[y-1][1].compare_contrast_with (images[y][0], progress);
	      if (params.geometry_info || params.individual_geometry_info)
	        images[y-1][1].output_common_points (NULL, images[y][0], 0, 0, true, progress);
	    }
	  if (params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	}
      for (int x = 0; x < params.width - 1; x++)
	{
	  coord_t xs;
	  coord_t ys;
	  if (!analyze (x, y, progress))
	    return false;
	  if (!analyze (x + 1,y, progress))
	    return false;
	  if (!images[y][x].get_analyzer().find_best_match (params.min_overlap_percentage, params.max_overlap_percentage, images[y][x+1].get_analyzer(), params.cpfind, &xs, &ys, params.limit_directions ? 0 : -1, images[y][x].basic_scr_to_img_map, images[y][x+1].basic_scr_to_img_map, report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename.c_str (), images[y][x + 1].filename.c_str ());
	      if (report_file)
		print_status (report_file);
	      exit (1);
	    }
	  images[y][x+1].xpos = images[y][x].xpos + xs;
	  images[y][x+1].ypos = images[y][x].ypos + ys;
	  if (params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	  /* Confirm position.  */
	  if (y)
	    {
	      if (!images[y-1][x+1].get_analyzer().find_best_match (params.min_overlap_percentage, params.max_overlap_percentage, images[y][x+1].get_analyzer(), params.cpfind, &xs, &ys, params.limit_directions ? 1 : -1, images[y-1][x+1].basic_scr_to_img_map, images[y][x+1].basic_scr_to_img_map, report_file, progress))
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename.c_str (), images[y][x + 1].filename.c_str ());
		  if (report_file)
		    print_status (report_file);
		  exit (1);
		}
	      if (images[y][x+1].xpos != images[y-1][x+1].xpos + xs
		  || images[y][x+1].ypos != images[y-1][x+1].ypos + ys)
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Stitching mismatch in %s: %f,%f is not equal to %f,%f\n", images[y][x + 1].filename.c_str (), images[y][x+1].xpos, images[y][x+1].ypos, images[y-1][x+1].xpos + xs, images[y-1][x+1].ypos + ys);
		  if (report_file)
		  {
		    fprintf (report_file, "Stitching mismatch in %s: %f,%f is not equal to %f,%f\n", images[y][x + 1].filename.c_str (), images[y][x+1].xpos, images[y][x+1].ypos, images[y-1][x+1].xpos + xs, images[y-1][x+1].ypos + ys);
		    print_status (report_file);
		  }
		  exit (1);
		}

	    }
	  if (y)
	    {
	      images[y-1][x+1].compare_contrast_with (images[y][x], progress);
	      if (params.geometry_info || params.individual_geometry_info)
	        images[y-1][x+1].output_common_points (NULL, images[y][x], 0, 0, true, progress);
	      images[y-1][x+1].compare_contrast_with (images[y][x+1], progress);
	      if (params.geometry_info || params.individual_geometry_info)
	        images[y-1][x+1].output_common_points (NULL, images[y][x+1], 0, 0, true, progress);
	      if (x + 2 < params.width)
	      {
	         images[y-1][x+1].compare_contrast_with (images[y][x+2], progress);
		 if (params.geometry_info || params.individual_geometry_info)
		   images[y-1][x+1].output_common_points (NULL, images[y][x+2], 0, 0, true, progress);
	      }
	    }
	  images[y][x].compare_contrast_with (images[y][x+1], progress);
	 if (params.geometry_info || params.individual_geometry_info)
            images[y][x].output_common_points (NULL, images[y][x+1], 0, 0, true, progress);
	  if (report_file)
	    fflush (report_file);
	}
    }
  if (report_file)
    print_status (report_file);
  if (params.panorama_map)
    {
      progress->pause_stdout ();
      print_panorama_map (stdout);
      progress->resume_stdout ();
    }
  return true;
}

void
stitch_project::determine_angle ()
{
  std::vector<coord_t> angles;
  std::vector<coord_t> ratios;
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      {
	angles.push_back (images[y][x].angle);
	ratios.push_back (images[y][x].ratio);
      }
  sort(angles.begin(), angles.end());
  sort(ratios.begin(), ratios.end());
  int cap = (angles.size () + 3) / 4;
  int imin = cap;
  int imax = angles.size() - 1 - cap;
  if (imin > imax)
    imin = imax = angles.size () / 2;
  coord_t avgangle = 0;
  coord_t avgratio = 0;
  for (int i = imin; i <= imax; i++)
    {
      avgangle += angles[i];
      avgratio += ratios[i];
    }
  avgangle /= imax - imin + 1;
  avgratio /= imax - imin + 1;
  scr_param.final_angle = avgangle;
  scr_param.final_ratio = avgratio;
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      images[y][x].update_scr_to_final_parameters (avgratio, avgangle);
  if (report_file)
    fprintf (report_file, "Final angle %f ratio %f\n", scr_param.final_angle, scr_param.final_ratio);
  image_data data;
  data.width=1000;
  data.height=1000;
  common_scr_to_img.set_parameters (scr_param, data, rotation_adjustment);
}
#define HEADER "color_screen_stitch_project_version: 1"

bool
stitch_project::save (FILE *f)
{
  if (fprintf (f, "%s\n", HEADER) < 0)
    return false;
  /* TODO: hack.  */
  setlocale(LC_NUMERIC, "C");
  if (fprintf (f, "num_images: %i %i\n", params.width, params.height) < 0
      || fprintf (f, "pixel_size: %f\n", pixel_size) < 0)
    return false;
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      {
	if (fprintf (f, "color_screen_stitch_image: %i %i\n", x, y) < 0)
	  return false;
	if (!images[y][x].save (f))
	  return false;
      }
  if (fprintf (f, "color_screen_stitch_project_end\n") < 0)
    return false;
  return true;
}

static bool
is_dir_separator (char c)
{
  if (c=='/')
    return true;
#ifdef _WIN32
  if (c=='\\')
    return true;
#endif
  return false;
}

static bool
absolute_filename_p (std::string name)
{
  if (name.length () == 0)
    return false;
  if (is_dir_separator (name[0]))
    return true;
#ifdef _WIN32
  if (name.length () > 1
      && ((name[0]>='a' && name [0]<'z')
	  || (name[0]>='A' && name [0]<'Z'))
      && name[1]==':' && is_dir_separator (name[2]))
    return true;
#endif
  return false;
}

/* Initialize stitching project path so it can be loaded from
   different working path.  */

void
stitch_project::set_path_by_filename (std::string filename)
{
  int last = -1;
  for (unsigned i = 0; i < filename.length (); i++)
    if (is_dir_separator (filename[i]))
      last = i;
  if (last >= 0)
    params.path = filename.substr (0, last + 1);
}

bool
stitch_project::load (FILE *f, const char **error)
{
  /* TODO: hack.  */
  setlocale(LC_NUMERIC, "C");
  if (!expect_keyword (f, HEADER))
    {
      *error = "wrong file header";
      return false;
    }
  if (!expect_keyword (f, "num_images:")
      || fscanf (f, "%i %i", &params.width, &params.height) != 2)
    {
      *error = "error parsing num_images";
      return false;
    }
  if (!expect_keyword (f, "pixel_size:"))
    {
      *error = "expected pixel_size";
      return false;
    }
  if (!read_scalar (f, &pixel_size))
    {
      *error ="error parsing pixel_size";
      return false;
    }
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
    {
      if (!expect_keyword (f, "color_screen_stitch_image:"))
	{
	  *error = "expected color_screen_stitch_image";
	  return false;
	}
      int xx, yy;
      if (fscanf (f, "%i %i", &xx, &yy) != 2)
	{
	  *error = "error parsing color_screen_stitch_image";
	  return false;
	}
      if (xx != x || yy != y)
	{
	  *error = "wrong color_screen_stitch_image coordinates";
	  return false;
	}
      if (!images[y][x].load (this, f, error))
	return false;
      params.type = images[y][x].param.type;
    }
  if (stitch_info_scale)
    stitch_info_scale = sqrt (images[0][0].param.coordinate1_x * images[0][0].param.coordinate1_x + images[0][0].param.coordinate1_y * images[0][0].param.coordinate1_y) + 1;
  return true;
}
std::string stitch_project::add_path (std::string name)
{
  if (absolute_filename_p (name))
    return name;
  return params.path + name;
}

std::string stitch_project::adjusted_filename (std::string filename, std::string suffix, std::string extension, int x, int y)
{
  size_t lastindex = filename.find_last_of("."); 
  char buf[256];
  if (lastindex == std::string::npos)
    lastindex = filename.length ();
  std::string ret = filename.substr (0, lastindex);
  if (x == -1)
    buf[0]=0;
  else
    sprintf (buf,"-%i-%i",y, x);
  return ret + suffix + buf + extension;
}

/* Render individual tiles of the panorama in N different modes specified by RFPARAMS.
   We support multiple modes at once since it is more memory effective.  */

bool 
stitch_project::write_tiles (render_parameters rparam, struct render_to_file_params *rfparams, int n, progress_info * progress, const char **error)
{
  for (int i = 0; i < n; i++)
    if (!complete_rendered_file_parameters (NULL, NULL, this, &rfparams[i]))
      {
	*error = "Precomputation failed (out of memory)";
	return false;
      }
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      {
	for (int i = 0; i < n; i++)
	  {
	    const char *fname = rfparams[i].filename;
	    struct render_to_file_params rfparams2 = rfparams[i];
	    std::string fname2;
	    if (fname)
	      fname2 = adjusted_filename (fname, render_to_file_params::output_mode_properties[rfparams2.mode].name,".tif",x,y);
	    else
	      fname2 = adjusted_filename (images[y][x].filename, render_to_file_params::output_mode_properties[rfparams2.mode].name,".tif",-1,-1);
	    rfparams2.filename = fname2.c_str ();
	      
	    if (!images[y][x].write_tile (rparam, rfparams2.xpos, rfparams2.ypos, rfparams2, error, progress))
	      return false;
	  }
      }
  return true;
}

namespace {
int
print_system(FILE *f, const gsl_matrix *m, gsl_vector *v, gsl_vector *w)
{
        int status, n = 0;

        for (size_t i = 0; i < m->size1; i++) {
                for (size_t j = 0; j < m->size2; j++) {
                        if ((status = fprintf(f, "%4.2f ", gsl_matrix_get(m, i, j))) < 0)
                                return -1;
                        n += status;
                }

                if ((status = fprintf(f, "| %4.2f weight:%4.2f\n", gsl_vector_get (v, i), gsl_vector_get (w, i))) < 0)
                        return -1;
                n += status;
        }

        return n;
}
}

inline rgbdata
sample_image_area (image_data *img, render *render, int x, int y, int range)
{
  int xmin = x - range;
  int xmax = x + range;
  int ymin = y - range;
  int ymax = y + range;
  if (xmin < 0)
    xmin = 0;
  if (xmax > img->width)
    xmax = 0;
  if (ymin < 0)
    ymin = 0;
  if (ymax > img->height)
    ymax = 0;
  rgbdata sum = {0,0,0};
  for (y = ymin ; y < ymax; y++)
    for (x = xmin ; x < xmax; x++)
      sum += render->get_rgb_pixel (x, y);
  int c = (xmax - xmin) * (ymax - ymin);
  return {sum.red / c, sum.green / c, sum.blue / c};
}

bool
stitch_project::analyze_exposure_adjustments (render_parameters *in_rparams, const char **rerror, progress_info *progress)
{
  /* Set rendering params so we get actual linearized scan data.  */
  render_parameters rparams;
  rparams.gamma = in_rparams->gamma;
  rparams.backlight_correction = in_rparams->backlight_correction;
  const char *error = NULL;

  struct ratio 
    {
      luminosity_t ratio, val1, val2;
      bool operator < (const struct ratio &other)
      {
	return ratio < other.ratio;
      }
    };

  struct equation
    {
      int x1,y1,x2,y2;
      int channel;
      unsigned long n;
      luminosity_t s1, s2;
    };
  std::vector <equation> eqns;

  /* How many different grayscale values we want to collect.  */
  const int buckets = 128;
  /* Every sample taken is square 2*range x 2xrange of pixels.  */
  const int range = 3;
  /* Make step big enough so samples does not overlap.  */
  const int step = 2 * range;

  /* Do not analyze 30% across the stitch border to not be bothered by lens flare.  */
  const int outerborder = 30;
  /* Ignore 5% of inner borders.  */
  const int innerborder = 5;

  /* Determine how many scanlines we will inspect.  */
  int combined_height = 0;
  if (progress)
    {
      for (int y = 0; y < params.height; y++)
	for (int x = 0; x < params.width; x++)
	  {
	    int ymin = images[y][x].img_height * (!y ? outerborder : innerborder) / 100;
	    int ymax = images[y][x].img_height * (y == params.height - 1 ? 100-outerborder : 100-innerborder) / 100;
	    for (int iy = y; iy < params.height; iy++)
	       for (int ix = (iy == y ? x + 1 : 0); ix < params.width; ix++)
		  combined_height += (ymax - ymin) / step;
	  }
      progress->set_task ("analyzing images", combined_height);
    }

  /* For every stitched image.  */
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      {
	if (progress && progress->cancel_requested ())
	  break;
	if (!images[y][x].load_img (&error, progress))
	  break;
	render render1 (*images[y][x].img, rparams, 255);
	if (!render1.precompute_all (images[y][x].img->data != NULL, progress))
	  {
	    error = "precomputation failed";
	    break;
	  }
	int xmin = images[y][x].img_width * (!x ? outerborder : innerborder) / 100;
	int ymin = images[y][x].img_height * (!y ? outerborder : innerborder) / 100;
	int xmax = images[y][x].img_width * (x == params.width - 1 ? 100-outerborder : 100-innerborder) / 100;
	int ymax = images[y][x].img_height * (y == params.height - 1 ? 100-outerborder : 100-innerborder) / 100;

	/* Check for possible overlaps.  */
	if ((!progress || !progress->cancel_requested ()) && !error)
	  for (int iy = y; iy < params.height; iy++)
	    if ((!progress || !progress->cancel_requested ()) && !error)
	      for (int ix = (iy == y ? x + 1 : 0); ix < params.width; ix++)
		{
		  int xmin2 = images[iy][ix].img_width * (!ix ? outerborder : innerborder) / 100;
		  int ymin2 = images[iy][ix].img_height * (!iy ? outerborder : innerborder) / 100;
		  int xmax2 = images[iy][ix].img_width * (ix == params.width - 1 ? 100-outerborder : 100-innerborder) / 100;
		  int ymax2 = images[iy][ix].img_height * (iy == params.height - 1 ? 100-outerborder : 100-innerborder) / 100;
		  std::vector<ratio> ratios[4];
		  render *render2 = NULL;
		  if ((!progress || !progress->cancel_requested ()) && !error)
//#pragma omp parallel for default(none) shared(y,x,ix,iy,rparams,render1,render2,progress,error,ratios,img_height,img_width)
		    for (int yy = ymin; yy < ymax; yy+= step)
		      {
			if ((!progress || !progress->cancel_requested ()) && !error)
			  for (int xx = xmin; xx < xmax; xx+= step)
			    {
			      coord_t common_x, common_y;
			      images[y][x].img_to_common_scr (xx + 0.5, yy + 0.5, &common_x, &common_y);
			      if (!images[iy][ix].pixel_known_p (common_x, common_y)
				  || !images[y][x].pixel_known_p (common_x, common_y))
				continue;
			      coord_t iix, iiy;
			      images[iy][ix].common_scr_to_img (common_x, common_y, &iix, &iiy);
			      if (iix < xmin2 || iix >=xmax2 || iiy < ymin2 || iiy >= ymax2)
				continue;
//#pragma omp critical
			      if (!render2)
				{
				  if (images[iy][ix].load_img (&error, progress))
				    {
				      render2 = new render (*images[iy][ix].img, rparams, 255);
				      if (!render2->precompute_all (images[y][x].img->data != NULL, progress))
					{
					  progress->pause_stdout ();
					  printf ("Comparing tile %i %i and %i %i\n", x, y, ix, iy);
					  progress->resume_stdout ();
					  error = "precomputation failed";
					  delete render2;
					}
				    }
				}
			      if (!render2)
				break;
			      luminosity_t minv = 0;
			      if (images[y][x].img->rgbdata)
				{
				  rgbdata d = /*= render1.get_rgb_pixel (xx, yy)*/ sample_image_area (images[y][x].img, &render1, xx, yy, range);
				  //rgbdata d;
				  //render1.get_img_rgb_pixel (xx + 0.5, yy + 0.5, &d.red, &d.green, &d.blue);
				  luminosity_t red, green, blue;
				  //render2->get_img_rgb_pixel (iix, iiy, &red, &green, &blue);
				  rgbdata d2 = /*= render1.get_rgb_pixel (xx, yy)*/ sample_image_area (images[iy][ix].img, render2, iix+0.5, iiy+0.5, range);
				  red = d2.red;
				  green = d2.green;
				  blue = d2.blue;


				  if (d.red > minv && red > minv)
#pragma omp critical
				      ratios[0].push_back ((struct ratio){d.red / red, d.red, red});
				  //printf ("%i %i:%f,%f %i %i:%f,%f %f %f %f\n",x,y,xx+0.5,yy+0.5,ix,iy,iix,iiy,red, d.red, red/d.red);
				  if (d.green > minv && green > minv)
#pragma omp critical
				      ratios[1].push_back ((struct ratio){d.green / green, d.green, green});
				  if (d.blue > minv && blue > minv)
#pragma omp critical
				      ratios[2].push_back ((struct ratio){d.blue / blue, d.blue, blue});
				}
			      /* Implement collection of IR channel.  */
			      if (images[y][x].img->data)
				abort ();
			      }
			    if (progress)
			      progress->inc_progress ();
			  }
		       if (render2)
			 {
			   images[iy][ix].release_img ();
			   delete render2;
			 }
		       for (int c = 0; c < 4; c++)
			 {
			   const char *channels[] = {"red", "green", "blue", "ir"};
			   if (ratios[c].size () > 1000)
			     {
			       unsigned long vals[65536];
			       memset (vals, 0, sizeof (vals));
			       long int crop0 = 0,cropmax = 0;

			       /* Compute histogram.  */
			       for (auto r:ratios[c])
			         {
				   int idx = (r.val1 * 65535 + 0.5);
				   if (idx < 0)
				     idx = 0, crop0++;
				   if (idx > 65535)
				     idx = 65535, cropmax++;
				   vals[idx]++;
			         }

			       /* Determine cutoffs between buckets so they are about of the same size.  */
			       luminosity_t cutoffs[buckets];
			       unsigned long csum = 0;
			       int pos = 0;
			       for (int bucket = 0; bucket < buckets; bucket++)
				 {
				   for (;csum <= ((bucket + 1) * ratios[c].size ()) / buckets && pos < 65535;pos++)
				     csum += vals[pos];
				   cutoffs[bucket]=(pos - 0.5) / 65535;
				 }

			       /* Distribute samples to buckets.  */
			       std::vector<ratio> ratios_buckets[buckets];
			       for (auto r:ratios[c])
				 {
				   int b;
				   for (b = 0; b < buckets - 1; b++)
				     if (cutoffs[b]>r.val1)
				       break;
				   ratios_buckets[b].push_back (r);
				 }

			       /* Sort every bucket and eliminate samples that seems off.  */
			       for (int b =0; b < buckets; b++)
				 {
				    std::sort (ratios_buckets[b].begin (), ratios_buckets[b].end());
				    unsigned long int n = 0;
				    luminosity_t wsum1b = 0, wsum2b = 0;
				    for (size_t i = ratios_buckets[b].size () / 4; i < (size_t)(3 * ratios_buckets[b].size () / 4); i++)
				      {
					wsum1b += ratios_buckets[b][i].val1;
					wsum2b += ratios_buckets[b][i].val2;
					n++;
				      }
				    /* Record equation.  */
				    if (n)
				       eqns.push_back ({x,y,ix,iy,c,n,wsum1b, wsum2b});
				    progress->pause_stdout ();
				    printf ("Found %li common points of tile %i,%i and %i,%i in channel %s. Bucket %i cutoff %f  Used samples %lu Ratio %f Sums %f %f Crops %li %li\n", (long int)ratios_buckets[b].size(), x, y, ix, iy, channels[c], b, cutoffs[b], n, wsum1b/wsum2b, wsum1b, wsum2b, crop0, cropmax);
				    progress->resume_stdout ();
				 }
			     }
			 }
#if 0
		       luminosity_t wsum1=0, wsum2=0;
		       for (int c = 0; c < 4; c++)
			 {
			   const char *channels[] = {"red", "green", "blue", "ir"};
			   if (ratios[c].size () > 1000)
			     {
				std::sort (ratios[c].begin (), ratios[c].end());
				luminosity_t wsum1b = 0, wsum2b = 0;
				for (size_t i = ratios[c].size () / 4; i < (size_t)(3 * ratios[c].size () / 4); i++)
				  {
				    wsum1b += ratios[c][i].val1;
				    wsum2b += ratios[c][i].val2;
				  }
				wsum1 += wsum1b;
				wsum2 += wsum2b;
				progress->pause_stdout ();
				printf ("Found %i common points of tile %i,%i and %i,%i in channel %s. Ratio %f sums %f %f\n", (int)ratios[c].size (), x, y, ix, iy, channels[c], wsum1b/wsum2b, wsum1b, wsum2b);
				progress->resume_stdout ();
			     }
			 }
		       if (wsum1 > 0 && wsum2 > 0)
		         eqns.push_back ({x,y,ix,iy,wsum1, wsum2});
#endif
		  }
	images[y][x].release_img ();
      }
  if ((progress && progress->cancel_requested ()) || error)
    {
      *rerror = error;
      return false;
    }
  progress->pause_stdout ();

  /* Print equaltions.  */
  for (int i = 0; i < (int)eqns.size (); i++)
    {
      printf ("e%i%i * %f + b%i%i*%lu = e%i%i * %f + b%i%i*%lu\n", eqns[i].x1, eqns[i].y1, eqns[i].s1, eqns[i].x1, eqns[i].y1, eqns[i].n, eqns[i].x2, eqns[i].y2, eqns[i].s2, eqns[i].x2, eqns[i].y2, eqns[i].n);
    }
#if 0
  for (int i = 0; i < (int)eqns.size (); i++)
    {
      printf ("e%i%i * %f = e%i%i rep %f\n", eqns[i].x1, eqns[i].y1, eqns[i].s2/eqns[i].s1, eqns[i].x2, eqns[i].y2, eqns[i].s1/eqns[i].s2);
    }
#endif
  progress->resume_stdout ();

  /* Feed to GSL.  */
  int nvariables = params.width * params.height * 2;
  int nequations = eqns.size () + 2;

  if (nvariables >= nequations)
    {
      error = "Did not collect enough samples.";
      return false;
    }
  gsl_matrix *A, *cov;
  gsl_vector *y, *w, *c;
  A = gsl_matrix_alloc (nequations, nvariables);
  y = gsl_vector_alloc (nequations);
  w = gsl_vector_alloc (nequations);
  c = gsl_vector_alloc (nvariables);
  cov = gsl_matrix_alloc (nvariables, nvariables);
  int i;
  luminosity_t sum = 0;
  for (i = 0; i < (int)eqns.size (); i++)
    {
      for (int j = 0; j < nvariables; j++)
	gsl_matrix_set (A, i, j, 0);
      gsl_matrix_set (A, i, eqns[i].y1 * params.width + eqns[i].x1, eqns[i].s1);
      gsl_matrix_set (A, i, eqns[i].y2 * params.width + eqns[i].x2, -eqns[i].s2);
      gsl_matrix_set (A, i, nvariables / 2 + eqns[i].y1 * params.width + eqns[i].x1, eqns[i].n);
      gsl_matrix_set (A, i, nvariables / 2 + eqns[i].y2 * params.width + eqns[i].x2, -(double)eqns[i].n);
      gsl_vector_set (y, i, 0);
      /* The darker point is, more we care.  */
      if (eqns[i].s1>0)
	{
	  luminosity_t avg_density = eqns[i].s1/eqns[i].n;
	  gsl_vector_set (w, i, 1/avg_density * (invert_gamma (avg_density, 2.4)/avg_density));
	}
      else
        gsl_vector_set (w, i, 1);
      sum += eqns[i].s1;
      sum += eqns[i].s2;
    }
  /* Set one exposition to be 1.  */
  for (int j = 0; j < nvariables; j++)
    gsl_matrix_set (A, i, j, 0);
  gsl_vector_set (y, i, sum);
  gsl_vector_set (w, i, 1);
  gsl_matrix_set (A, i, params.width * params.height / 4, sum);
  /* Set one blackpoint to be 0.  */
  i++;
  for (int j = 0; j < nvariables; j++)
    gsl_matrix_set (A, i, j, 0);
  gsl_vector_set (y, i, 0);
  gsl_vector_set (w, i, 1);
  gsl_matrix_set (A, i, nvariables / 2 + params.width * params.height / 4, sum);

  progress->pause_stdout ();
  print_system (stdout, A, y, w);
  progress->resume_stdout ();

  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (nequations, nvariables);
  double chisq;
  gsl_multifit_wlinear (A, w, y, c, cov,
			&chisq, work);
  gsl_set_error_handler (old_handler);
  gsl_multifit_linear_free (work);
  progress->pause_stdout ();
  /* Print solution.  */
  for (int y = 0; y < params.height; y++)
    {
      for (int x = 0; x < params.width; x++)
	printf ("  +%4.8f*%4.8f", gsl_vector_get (c, nvariables / 2 + y * params.width + x), gsl_vector_get (c, y * params.width + x));
      printf ("\n");
    }
  /* Convert into our datastructure.  */
  in_rparams->set_tile_adjustments_dimensions (params.width, params.height);
  printf ("Final solutoin:\n");
  for (int y = 0; y < params.height; y++)
    {
      for (int x = 0; x < params.width; x++)
	{
	  in_rparams->get_tile_adjustment (x,y).exposure = gsl_vector_get (c, y * params.width + x) / gsl_vector_get (c, params.width * params.height / 4);
	  in_rparams->get_tile_adjustment (x,y).dark_point = -gsl_vector_get (c, nvariables / 2 + y * params.width + x) / gsl_vector_get (c, y * params.width + x);
	  printf ("  +%4.8f*%4.8f", in_rparams->get_tile_adjustment (x,y).dark_point, in_rparams->get_tile_adjustment (x,y).exposure);
	}
      printf ("\n");
    }
  progress->resume_stdout ();
  gsl_matrix_free (A);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_matrix_free (cov);
  return true;
}
