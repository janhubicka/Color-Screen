#include <vector>
#include "include/stitch.h"
#include "include/tiff-writer.h"
#include "render-interpolate.h"
#include "screen-map.h"
#include "loadsave.h"
stitch_project::stitch_project ()
  : params (), report_file (NULL), images(), param (), rparam (),
    passthrough_rparam (), common_scr_to_img (), dparam (), solver_param (),
    pixel_size (0), my_screen (NULL), stitch_info_scale (0)
{}

stitch_project::~stitch_project ()
{
if (my_screen)
  render_to_scr::release_screen (my_screen);
}

bool
stitch_project::initialize ()
{
  passthrough_rparam.gamma = rparam.gamma;
  if (params.orig_tile_gamma > 0)
    passthrough_rparam.output_gamma = params.orig_tile_gamma;
  else
    passthrough_rparam.output_gamma = rparam.gamma;

  image_data data;
  scr_param.type = params.type;
  data.width=1000;
  data.height=1000;
  common_scr_to_img.set_parameters (scr_param, data);

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
  common_scr_to_img.set_parameters (scr_param, data);
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
  for (int y = 0; y < params.width; y++)
    for (int x = 0; x < params.height; x++)
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
  for (int y = 0; y < params.width; y++)
    for (int x = 0; x < params.height; x++)
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

std::string stitch_project::adjusted_filename (std::string filename, std::string suffix, std::string extension)
{
  size_t lastindex = filename.find_last_of("."); 
  if (lastindex == std::string::npos)
    lastindex = filename.length ();
  std::string ret = filename.substr (0, lastindex);
  return ret + suffix + extension;
}
