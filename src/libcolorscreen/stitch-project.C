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
  if (progress)
    progress->set_task ("analyzing tiles", params.width * params.height);
  if (params.width == 1 && params.height == 1)
    {
      if (progress)
	progress->push ();
      bool ret = analyze (0, 0, progress);
      progress->pop ();
      return ret;
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
	  analyze (0, y - 1, progress);
	  analyze (0, y, progress);
	  if (!images[y - 1][0].get_analyzer ().
	      find_best_match (params.min_overlap_percentage,
			       params.max_overlap_percentage,
			       images[y][0].get_analyzer (), params.cpfind,
			       &xs, &ys, params.limit_directions ? 1 : -1,
			       images[y - 1][0].basic_scr_to_img_map,
			       images[y][0].basic_scr_to_img_map, report_file,
			       progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n",
		       images[y - 1][0].filename.c_str (),
		       images[y][0].filename.c_str ());
	      progress->resume_stdout ();
	      return false;
	    }
	  images[y][0].xpos = images[y - 1][0].xpos + xs;
	  images[y][0].ypos = images[y - 1][0].ypos + ys;
	  images[y - 1][0].compare_contrast_with (images[y][0], progress);
	  if (params.geometry_info || params.individual_geometry_info)
	    images[y - 1][0].output_common_points (NULL, images[y][0], 0, 0,
						   true, progress);
	  if (params.width)
	    {
	      images[y - 1][1].compare_contrast_with (images[y][0], progress);
	      if (params.geometry_info || params.individual_geometry_info)
		images[y - 1][1].output_common_points (NULL, images[y][0], 0,
						       0, true, progress);
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
	  if (progress)
	    progress->push ();
	  if (!analyze (x, y, progress)
	      || !analyze (x + 1, y, progress))
	    {
	      if (progress)
		progress->pop ();
	      return false;
	    }
	  if (!images[y][x].get_analyzer ().
	      find_best_match (params.min_overlap_percentage,
			       params.max_overlap_percentage,
			       images[y][x + 1].get_analyzer (),
			       params.cpfind, &xs, &ys,
			       params.limit_directions ? 0 : -1,
			       images[y][x].basic_scr_to_img_map,
			       images[y][x + 1].basic_scr_to_img_map,
			       report_file, progress))
	    {
	      progress->pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n",
		       images[y][x].filename.c_str (),
		       images[y][x + 1].filename.c_str ());
	      if (report_file)
		print_status (report_file);
	      if (progress)
		progress->pop ();
	      progress->resume_stdout ();
	      return false;
	    }
	  images[y][x + 1].xpos = images[y][x].xpos + xs;
	  images[y][x + 1].ypos = images[y][x].ypos + ys;
	  if (params.panorama_map)
	    {
	      progress->pause_stdout ();
	      print_panorama_map (stdout);
	      progress->resume_stdout ();
	    }
	  /* Confirm position.  */
	  if (y)
	    {
	      if (!images[y - 1][x + 1].get_analyzer ().
		  find_best_match (params.min_overlap_percentage,
				   params.max_overlap_percentage,
				   images[y][x + 1].get_analyzer (),
				   params.cpfind, &xs, &ys,
				   params.limit_directions ? 1 : -1,
				   images[y - 1][x + 1].basic_scr_to_img_map,
				   images[y][x + 1].basic_scr_to_img_map,
				   report_file, progress))
		{
		  progress->pause_stdout ();
		  fprintf (stderr, "Can not find good overlap of %s and %s\n",
			   images[y][x].filename.c_str (),
			   images[y][x + 1].filename.c_str ());
		  if (report_file)
		    print_status (report_file);
		  if (progress)
		    progress->pop ();
		  progress->resume_stdout ();
		  return false;
		}
	      if (images[y][x + 1].xpos != images[y - 1][x + 1].xpos + xs
		  || images[y][x + 1].ypos != images[y - 1][x + 1].ypos + ys)
		{
		  progress->pause_stdout ();
		  fprintf (stderr,
			   "Stitching mismatch in %s: %f,%f is not equal to %f,%f\n",
			   images[y][x + 1].filename.c_str (),
			   images[y][x + 1].xpos, images[y][x + 1].ypos,
			   images[y - 1][x + 1].xpos + xs,
			   images[y - 1][x + 1].ypos + ys);
		  if (report_file)
		    {
		      fprintf (report_file,
			       "Stitching mismatch in %s: %f,%f is not equal to %f,%f\n",
			       images[y][x + 1].filename.c_str (),
			       images[y][x + 1].xpos, images[y][x + 1].ypos,
			       images[y - 1][x + 1].xpos + xs,
			       images[y - 1][x + 1].ypos + ys);
		      print_status (report_file);
		    }
		}

	    }
	  if (y)
	    {
	      images[y - 1][x + 1].compare_contrast_with (images[y][x],
							  progress);
	      if (params.geometry_info || params.individual_geometry_info)
		images[y - 1][x + 1].output_common_points (NULL, images[y][x],
							   0, 0, true,
							   progress);
	      images[y - 1][x + 1].compare_contrast_with (images[y][x + 1],
							  progress);
	      if (params.geometry_info || params.individual_geometry_info)
		images[y - 1][x + 1].output_common_points (NULL,
							   images[y][x + 1],
							   0, 0, true,
							   progress);
	      if (x + 2 < params.width)
		{
		  images[y - 1][x +
				1].compare_contrast_with (images[y][x + 2],
							  progress);
		  if (params.geometry_info || params.individual_geometry_info)
		    images[y - 1][x + 1].output_common_points (NULL,
							       images[y][x +
									 2],
							       0, 0, true,
							       progress);
		}
	    }
	  images[y][x].compare_contrast_with (images[y][x + 1], progress);
	  if (params.geometry_info || params.individual_geometry_info)
	    images[y][x].output_common_points (NULL, images[y][x + 1], 0, 0,
					       true, progress);
	  if (report_file)
	    fflush (report_file);
	  if (progress)
	    progress->pop ();
	  if (progress)
	    {
	      progress->pop ();
	      progress->inc_progress ();
	      if (progress->cancel_requested ())
		return false;
	    }
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
      images[y][x].left = !y;
      images[y][x].top = !x;
      images[y][x].right = x == params.width - 1;
      images[y][x].bottom = y == params.height - 1;
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
  if (progress)
    progress->set_task ("rendering tiles", params.height * params.width);
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      {
	for (int i = 0; i < n; i++)
	  {
	    if (progress && progress->cancel_requested ())
	      return false;
	    const char *fname = rfparams[i].filename;
	    struct render_to_file_params rfparams2 = rfparams[i];
	    std::string fname2;
	    if (fname)
	      fname2 = adjusted_filename (fname, render_to_file_params::output_mode_properties[rfparams2.mode].name,".tif",x,y);
	    else
	      fname2 = adjusted_filename (images[y][x].filename, render_to_file_params::output_mode_properties[rfparams2.mode].name,".tif",-1,-1);
	    rfparams2.filename = fname2.c_str ();

	    if (progress)
	      progress->push ();
	      
	    if (!images[y][x].write_tile (rparam, rfparams2.xpos, rfparams2.ypos, rfparams2, error, progress))
	      return false;
	    if (progress)
	      progress->pop ();
	  }
	if (progress)
	  progress->inc_progress ();
      }
  return true;
}

namespace {
static int
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

static inline int
exp_index (stitch_project *p, int fx, int fy, int x, int y)
{
  int f = fy * p->params.width + fx;
  int i = y * p->params.width + x;
  if (f == i)
    return -1;
  if (i < f)
    return i;
  return i-1;
}

static inline int
black_index (stitch_project *p, int fx, int fy, int x, int y)
{
  int f = fy * p->params.width + fx;
  int i = y * p->params.width + x;
  int o = p->params.width * p->params.height - 1;
  if (f == i)
    return -1;
  if (i < f)
    return i + o;
  return i-1 + o;
}

const char *channels[] = {"red", "green", "blue", "ir"};

struct equation
{
  int x1,y1,x2,y2;
  int channel;
  uint64_t n;
  luminosity_t s1, s2;
  luminosity_t weight;
};

struct ratio 
  {
    luminosity_t ratio, val1, val2, weight;
    bool operator < (const struct ratio &other) const
    {
      return ratio < other.ratio;
    }
  };
struct ratios
{
  std::vector<ratio> channel[4];
};

void
add_equations (std::vector <equation> &eqns, stitch_image &i1, stitch_image &i2, int x, int y, int ix, int iy, stitch_image::common_samples &samples, render_parameters &rparams, bool verbose, progress_info *progress)
{
  struct ratios ratios;
  /* Apply backlight correction and produce array sof ratios.  */
  if (rparams.backlight_correction)
    {
      backlight_correction correction1 (*rparams.backlight_correction, i1.img_width, i1.img_height, rparams.backlight_correction_black, false, progress);
      backlight_correction correction2 (*rparams.backlight_correction, i2.img_width, i2.img_height, rparams.backlight_correction_black, false, progress);


      for (auto s: samples)
       for (int c = 0; c < 4; c++)
	 {
	   luminosity_t val1 = correction1.apply (s.channel1[c], s.x1, s.x2, (backlight_correction_parameters::channel)c, true);
	   luminosity_t val2 = correction1.apply (s.channel2[c], s.x1, s.x2, (backlight_correction_parameters::channel)c, true);
	   if (val1 > 0)
	     ratios.channel[c].push_back ({val2 / val1, val1, val2, s.weight});
	 }
     }
  else
    for (auto s: samples)
      for (int c = 0; c < 4; c++)
	if (s.channel1[c] > 0)
	  ratios.channel[c].push_back ({s.channel2[c] / s.channel1[c], s.channel1[c], s.channel2[c], s.weight});
  /* How many different grayscale values we want to collect.  */
  const int buckets = 256;
  const int histogram_size = 6556*2;
  for (int c = 0; c < 4; c++)
    {
      if (ratios.channel[c].size () > 1000)
	{
	  std::vector<uint64_t> vals (histogram_size);
	  for (auto &v:vals)
	    v = 0;
	  uint64_t crop0 = 0,cropmax = 0;

	  /* Compute histogram.  */
	  for (auto r:ratios.channel[c])
	    {
	      int idx = ((r.val1 + r.val2) * (histogram_size - 1) + 0.5);
	      if (idx < 0)
		idx = 0, crop0++;
	      if (idx >= histogram_size)
		idx = histogram_size - 1, cropmax++;
	      vals[idx]++;
	    }

	  /* Determine cutoffs between buckets so they are about of the same size.  */
	  luminosity_t cutoffs[buckets];
	  uint64_t csum = 0;
	  int pos = 0;
	  for (int bucket = 0; bucket < buckets; bucket++)
	    {
	      for (;csum <= ((bucket + 1) * ratios.channel[c].size ()) / buckets && pos < histogram_size;pos++)
		csum += vals[pos];
	      cutoffs[bucket]=(pos - 0.5) / (histogram_size - 1);
	    }

	  /* Distribute samples to buckets.  */
	  std::vector<ratio> ratios_buckets[buckets];
	  for (auto r:ratios.channel[c])
	    {
	      int b;
	      for (b = 0; b < buckets - 1; b++)
		if (cutoffs[b]>r.val1+r.val2)
		  break;
	      ratios_buckets[b].push_back (r);
	    }

	  /* Sort every bucket and eliminate samples that seems off.  */
	  for (int b =0; b < buckets; b++)
	    if (ratios_buckets[b].size ())
	      {
		 std::sort (ratios_buckets[b].begin (), ratios_buckets[b].end());
		 uint64_t n = 0;
		 luminosity_t wsum1b = 0, wsum2b = 0;
		 luminosity_t wsum3b = 0;
		 for (size_t i = ratios_buckets[b].size () / 4; i < (size_t)(3 * ratios_buckets[b].size () / 4); i++)
		 //for (size_t i = 0; i < ratios_buckets[b].size (); i++)
		   {
		     wsum1b += ratios_buckets[b][i].val1 * ratios_buckets[b][i].weight;
		     wsum2b += ratios_buckets[b][i].val2 * ratios_buckets[b][i].weight;
		     wsum3b += ratios_buckets[b][i].weight;
		     n++;
		   }
		 /* Record equation.  */
		 if (n)
		    eqns.push_back ({x,y,ix,iy,c,n,wsum1b, wsum2b, wsum3b});
		 if (verbose)
		   {
		     progress->pause_stdout ();
		     printf ("Found %li common points of tile %i,%i and %i,%i in channel %s. Bucket %i cutoff %f  Used samples %lu Ratio %f Sums %f %f Crops %li %li\n", (long int)ratios_buckets[b].size(), x, y, ix, iy, channels[c], b, cutoffs[b], n, wsum1b/wsum2b, wsum1b, wsum2b, crop0, cropmax);
		     progress->resume_stdout ();
		   }
	      }
	}
      else if (verbose)
	 {
	   progress->pause_stdout ();
	   printf ("Found only %li common points of tile %i,%i and %i,%i in channel %s. Ignoring.\n", (long int)ratios.channel[c].size(), x, y, ix, iy, channels[c]);
	   progress->resume_stdout ();
	 }
    }
}

/* Assume that eqns compare image1 and image2.
   Compute adjustmnet so itensities in image2 are intensitis in image1 * mul + add.  */
void
match_pair (std::vector <equation> &eqns, size_t from, luminosity_t *add, luminosity_t *mul, luminosity_t *weight, bool verbose, progress_info *progress)
{
  const int nvariables = 2;
  int nequations = eqns.size () - from;
  gsl_matrix *A, *cov;
  gsl_vector *y, *w, *c;
  A = gsl_matrix_alloc (nequations, nvariables);
  y = gsl_vector_alloc (nequations);
  w = gsl_vector_alloc (nequations);
  c = gsl_vector_alloc (nvariables);
  cov = gsl_matrix_alloc (nvariables, nvariables);
  int i;
  luminosity_t wsum = 0;
  for (i = 0; i < (int)eqns.size () - from; i++)
    {
      gsl_matrix_set (A, i, 0, eqns[i + from].s1);
      gsl_matrix_set (A, i, 1, eqns[i + from].weight);
      gsl_vector_set (y, i, eqns[i + from].s2);
      wsum += eqns[i + from].weight;
      /* The darker point is, more we care.  */
      if (eqns[i].s1>0)
	{
	  luminosity_t avg_density = eqns[i].s1/eqns[i].weight;
	  gsl_vector_set (w, i, /*1/avg_density **/ (invert_gamma (avg_density, 2.4)/avg_density));
	}
      else
        gsl_vector_set (w, i, 1);
    }
  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (nequations, nvariables);
  double chisq;
  gsl_multifit_wlinear (A, w, y, c, cov,
			&chisq, work);
  gsl_set_error_handler (old_handler);
  gsl_multifit_linear_free (work);
  progress->pause_stdout ();
  *mul = gsl_vector_get (c, 0);
  *add = gsl_vector_get (c, 1);
  *weight = wsum;
  printf ("Fitting images: Mul %f add %f weight %f\n", *mul, *add * 65535, *weight);
  luminosity_t max_cor = 0, avg_cor = 0, max_uncor = 0, avg_uncor = 0, cor_sq = 0, uncor_sq = 0;
  uint64_t n = 0;
  for (i = from; i < (int)eqns.size (); i++)
    {
      auto eqn = eqns[i];
      luminosity_t cor_diff = (eqn.s1/eqn.weight) * *mul + *add - eqn.s2/eqn.weight;
      luminosity_t uncor_diff = (eqn.s1 - eqn.s2)/eqn.weight;
      if (fabs (cor_diff) > max_cor)
	max_cor = fabs (cor_diff);
      avg_cor += fabs (cor_diff) * eqn.weight;
      cor_sq   +=   cor_diff * eqn.weight *   cor_diff * eqn.weight * gsl_vector_get (w, i - from);
      uncor_sq += uncor_diff * eqn.weight * uncor_diff * eqn.weight * gsl_vector_get (w, i - from);
      if (fabs (uncor_diff) > max_uncor)
	max_uncor = fabs (uncor_diff);
      avg_uncor += fabs (uncor_diff) * eqn.weight;
      n+= eqn.n;
      if (verbose)
	printf ("%s avg %f/%f=%f difference %f uncorected %f samples %lu weight %f adjusted weight %f\n",
		channels[eqn.channel], eqn.s1/eqn.weight, eqn.s2/eqn.weight, eqn.s1/eqn.s2, cor_diff, uncor_diff, eqn.n, eqn.weight, gsl_vector_get (w, i - from));
      i++;
    }
  printf ("Corrected diff %f (avg) %f (max) %f (sq), uncorrected %f %3.2f%% (avg) %f  %3.2f%% (max) %f %3.2f%% (sq), chisq %f\n", avg_cor / n * 65535, max_cor * 65535, cor_sq, avg_uncor /n * 65535, avg_uncor * 100 / avg_cor, max_uncor * 65535, max_uncor * 100 / max_cor, uncor_sq, uncor_sq * 100 / cor_sq, chisq);
  progress->resume_stdout ();

  gsl_matrix_free (A);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_matrix_free (cov);
}

}

bool
stitch_project::optimize_tile_adjustments (render_parameters *in_rparams, const char **rerror, progress_info *progress)
{
  /* Set rendering params so we get actual linearized scan data.  */
  render_parameters rparams;
  rparams.gamma = in_rparams->gamma;
  rparams.backlight_correction = in_rparams->backlight_correction;
  rparams.backlight_correction_black = in_rparams->backlight_correction_black;
  const char *error = NULL;

  /* Do not analyze 30% across the stitch border to not be bothered by lens flare.  */
  const int outerborder = 20;
  /* Ignore 5% of inner borders.  */
  const int innerborder = 3;

  const bool verbose = false;

  /* Determine how many scanlines we will inspect.  */
  int combined_height = 0;
  if (progress)
    {
      for (int y = 0; y < params.height; y++)
	for (int x = 0; x < params.width; x++)
	  {
	    int ymin = images[y][x].img_height * (images[y][x].top ? outerborder : innerborder) / 100;
	    int ymax = images[y][x].img_height * (images[y][x].bottom ? 100-outerborder : 100-innerborder) / 100;
	    for (int iy = y; iy < params.height; iy++)
	       for (int ix = (iy == y ? x + 1 : 0); ix < params.width; ix++)
		  combined_height += (ymax - ymin);
	  }
      progress->set_task ("analyzing images", combined_height);
    }

  std::vector <equation> eqns;

  /* For every stitched image.  */
  for (int y = 0; y < params.height; y++)
    for (int x = 0; x < params.width; x++)
      {
	if (progress && progress->cancel_requested ())
	  break;
	if (progress)
	  progress->push ();
	if (!images[y][x].load_img (&error, progress))
	  {
	    if (progress)
	      progress->pop ();
	    break;
	  }
	render render1 (*images[y][x].img, rparams, 255);
	if (!render1.precompute_all (images[y][x].img->data != NULL, progress))
	  {
	    error = "precomputation failed";
	    if (progress)
	      progress->pop ();
	    break;
	  }
	if (progress)
	  progress->pop ();

	/* Check for possible overlaps.  */
	if ((!progress || !progress->cancel_requested ()) && !error)
	  for (int iy = y; iy < params.height; iy++)
	    if ((!progress || !progress->cancel_requested ()) && !error)
	      for (int ix = (iy == y ? x + 1 : 0); ix < params.width && (!progress || !progress->cancel_requested ()) && !error; ix++)
		{
		  stitch_image::common_samples samples = images[y][x].find_common_points (images[iy][ix], outerborder, innerborder, rparams, progress, &error);
		  if ((!progress || !progress->cancel_requested ()) && !error && samples.size () > 10)
		    {
		      size_t first = eqns.size ();
		      add_equations (eqns, images[y][x], images[iy][ix], x, y, ix, iy, samples, rparams, verbose, progress);
		      luminosity_t add, mul, weight;
		      match_pair (eqns, first, &add, &mul, &weight, verbose, progress);
		    }
		}
	images[y][x].release_img ();
      }
  if ((progress && progress->cancel_requested ()) || error)
    {
      *rerror = error;
      return false;
    }

  if (verbose)
    {
      progress->pause_stdout ();

      /* Print equaltions.  */
      for (int i = 0; i < (int)eqns.size (); i++)
	printf ("e%i%i * %f + b%i%i*%lu = e%i%i * %f + b%i%i*%lu\n", eqns[i].x1, eqns[i].y1, eqns[i].s1, eqns[i].x1, eqns[i].y1, eqns[i].n, eqns[i].x2, eqns[i].y2, eqns[i].s2, eqns[i].x2, eqns[i].y2, eqns[i].n);
      progress->resume_stdout ();
    }

  /* Feed to GSL.  */
  int nvariables = 2 * params.width * params.height - 2;
  int nequations = eqns.size ();
  int fx = params.width / 2;
  int fy = params.height / 2;

  if (nvariables >= nequations)
    {
      *rerror = "Did not collect enough samples.";
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
  for (i = 0; i < (int)eqns.size (); i++)
    {
      for (int j = 0; j < nvariables; j++)
	gsl_matrix_set (A, i, j, 0);
      double rhs = 0;

      int idx = exp_index (this, fx, fy, eqns[i].x1, eqns[i].y1);
      if (idx == -1)
	rhs -= eqns[i].s1;
      else
        gsl_matrix_set (A, i, idx, eqns[i].s1);

      idx = exp_index (this, fx, fy, eqns[i].x2, eqns[i].y2);
      if (idx == -1)
	rhs += eqns[i].s2;
      else
        gsl_matrix_set (A, i, idx, -eqns[i].s2);

      idx = black_index (this, fx, fy, eqns[i].x1, eqns[i].y1);
      if (idx == -1)
        ;
      else
        gsl_matrix_set (A, i, idx, eqns[i].weight);

      idx = black_index (this, fx, fy, eqns[i].x2, eqns[i].y2);
      if (idx == -1)
        ;
      else
        gsl_matrix_set (A, i, idx, -eqns[i].weight);

      gsl_vector_set (y, i, rhs);
      /* The darker point is, more we care.  */
      if (eqns[i].s1>0)
	{
	  luminosity_t avg_density = eqns[i].s1/eqns[i].weight;
	  gsl_vector_set (w, i, /*1/avg_density **/ (invert_gamma (avg_density, 2.4)/avg_density));
	}
      else
        gsl_vector_set (w, i, 1);
    }

  if (verbose)
    {
      progress->pause_stdout ();
      print_system (stdout, A, y, w);
      progress->resume_stdout ();
    }

  gsl_error_handler_t *old_handler = gsl_set_error_handler_off ();
  gsl_multifit_linear_workspace * work
    = gsl_multifit_linear_alloc (nequations, nvariables);
  double chisq;
  gsl_multifit_wlinear (A, w, y, c, cov,
			&chisq, work);
  gsl_set_error_handler (old_handler);
  gsl_multifit_linear_free (work);
  /* Convert into our datastructure.  */
  if (in_rparams->tile_adjustments_width != params.width
      || in_rparams->tile_adjustments_height != params.height)
    in_rparams->set_tile_adjustments_dimensions (params.width, params.height);
  for (int y = 0; y < params.height; y++)
    {
      for (int x = 0; x < params.width; x++)
	{
	  if (x == fx && y == fy)
	    {
	      in_rparams->get_tile_adjustment (x,y).exposure = 1;
	      in_rparams->get_tile_adjustment (x,y).dark_point = 0;
	    }
	  else
	    {
	      in_rparams->get_tile_adjustment (x,y).exposure = gsl_vector_get (c, exp_index (this, fx, fy, x, y)) ;
	      in_rparams->get_tile_adjustment (x,y).dark_point = -gsl_vector_get (c, black_index (this, fx, fy, x, y)) / in_rparams->get_tile_adjustment (x,y).exposure;
	    }
	}
    }
  luminosity_t max_cor = 0, avg_cor = 0, max_uncor = 0, avg_uncor = 0, cor_sq = 0, uncor_sq = 0;
  uint64_t n = 0;
  i=0;

  progress->pause_stdout ();
  printf ("Final solution:\n");
  for (auto eqn : eqns)
    {
      int idx = exp_index (this, fx, fy, eqn.x1, eqn.y1);
      double e1 = idx == -1 ? 1 : gsl_vector_get (c, idx);
      idx = black_index (this, fx, fy, eqn.x1, eqn.y1);
      double b1 = idx == -1 ? 0 : gsl_vector_get (c, idx);
      idx = exp_index (this, fx, fy, eqn.x2, eqn.y2);
      double e2 = idx == -1 ? 1 : gsl_vector_get (c, idx);
      idx = black_index (this, fx, fy, eqn.x2, eqn.y2);
      double b2 = idx == -1 ? 0 : gsl_vector_get (c, idx);
      auto &a1 = in_rparams->get_tile_adjustment (eqn.x1, eqn.y1);
      auto &a2 = in_rparams->get_tile_adjustment (eqn.x2, eqn.y2);
      luminosity_t cor_diff = (eqn.s1/eqn.weight - a1.dark_point) * a1.exposure - (eqn.s2/eqn.weight - a2.dark_point) * a2.exposure;
      //luminosity_t cor_diff = (eqn.s1/eqn.n) * e1 + b1 - eqn.s2/eqn.n*e2 - b2;
      luminosity_t uncor_diff = (eqn.s1 - eqn.s2)/eqn.weight;
      n += eqn.n;
      if (fabs (cor_diff) > max_cor)
	max_cor = fabs (cor_diff);
      avg_cor += fabs (cor_diff) * eqn.weight;
      cor_sq   +=   cor_diff * eqn.weight *   cor_diff * eqn.weight * gsl_vector_get (w, i);
      uncor_sq += uncor_diff * eqn.weight * uncor_diff * eqn.weight * gsl_vector_get (w, i);
      if (fabs (uncor_diff) > max_uncor)
	max_uncor = fabs (uncor_diff);
      avg_uncor += fabs (uncor_diff) * eqn.weight;
      if (verbose)
	printf ("images %i,%i and %i,%i %s avg %f/%f=%f difference %f uncorected %f samples %lu weight %f e1 %f b1 %f e2 %f b2 %f weight %f\n",
		eqn.x1, eqn.y1, eqn.x2, eqn.y2, channels[eqn.channel], eqn.s1/eqn.weight, eqn.s2/eqn.weight, eqn.s1/eqn.s2, cor_diff, uncor_diff, eqn.n, eqn.weight, e1, b1, e2, b2, gsl_vector_get (w, i));
      i++;
    }
  for (int y = 0; y < params.height; y++)
    {
      for (int x = 0; x < params.width; x++)
	printf ("  %+6.2f*%1.8f", in_rparams->get_tile_adjustment (x,y).dark_point*65535, in_rparams->get_tile_adjustment (x,y).exposure);
      printf ("\n");
    }
  printf ("Corrected diff %f (avg) %f (max) %f (sq), uncorrected %f %3.2f%% (avg) %f  %3.2f%% (max) %f %3.2f%% (sq), chisq %f\n", avg_cor / n * 65535, max_cor * 65535, cor_sq, avg_uncor /n * 65535, avg_uncor * 100 / avg_cor, max_uncor * 65535, max_uncor * 100 / max_cor, uncor_sq, uncor_sq * 100 / cor_sq, chisq);
  progress->resume_stdout ();
  gsl_matrix_free (A);
  gsl_vector_free (y);
  gsl_vector_free (w);
  gsl_matrix_free (cov);
  return true;
}
