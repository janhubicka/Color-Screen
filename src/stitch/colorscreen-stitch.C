#include <sys/time.h>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/analyze-dufay.h"

namespace {
#define MAX_DIM 10
int stitch_width, stitch_height;
scr_to_img_parameters param;
render_parameters rparam;
scr_detect_parameters dparam;
solver_parameters solver_param;

class stitch_image
{
  public:
  char *filename;
  image_data *img;
  mesh *mesh_trans;
  scr_to_img scr_to_img_map;
  int xshift, yshift, width, height;
  int final_xshift, final_yshift;
  int final_width, final_height;
  analyze_dufay dufay;

  int xpos, ypos;
  bool analyzed;

  void load_img (progress_info *);
  void release_img ();
  void analyze (progress_info *);
};

void
stitch_image::load_img (progress_info *progress)
{
  if (img)
    return;
  progress->pause_stdout ();
  printf ("Loading %s\n", filename);
  progress->resume_stdout ();
  img = new image_data;
  const char *error;
  if (!img->load (filename, &error, progress))
    {
      progress->pause_stdout ();
      fprintf (stderr, "Can not load %s: %s\n", filename, error);
      exit (1);
    }
  if (!img->rgbdata)
    {
      progress->pause_stdout ();
      fprintf (stderr, "File %s is not having color channels\n", filename);
      exit (1);
    }
}

void
stitch_image::release_img ()
{
  delete img;
}
void
stitch_image::analyze (progress_info *progress)
{
  if (analyzed)
    return;
  load_img (progress);
  mesh_trans = detect_solver_points (*img, dparam, solver_param, progress);
  if (!mesh_trans)
    {
      progress->pause_stdout ();
      fprintf (stderr, "Failed to analyze screen of %s\n", filename);
      exit (1);
    }
  scr_to_img_parameters param;
  render_parameters rparam;
  param.mesh_trans = mesh_trans;
  render_to_scr render (param, *img, rparam, 256);
  render.precompute_all (progress);
  scr_to_img_map.set_parameters (param, *img);
  final_xshift = render.get_final_xshift ();
  final_yshift = render.get_final_yshift ();
  final_width = render.get_final_width ();
  final_height = render.get_final_height ();

  scr_to_img_map.get_range (img->width, img->height, &xshift, &yshift, &width, &height);
  dufay.analyze (&render, width, height, xshift, yshift, true, progress);
  dufay.compute_known_pixels (*img, scr_to_img_map, progress);
  analyzed = true;
  release_img ();
}

stitch_image images[MAX_DIM][MAX_DIM];

void
print_help (const char *filename)
{
  printf ("%s output.tif parameters.par <xdim> <ydim> imag11.tif img12.tif ....\n", filename);
}

void
print_status ()
{
  scr_to_img_parameters scr_param;
  image_data data;
  scr_param.type = Dufay;
  data.width=1000;
  data.height=1000;
  scr_to_img map;
  map.set_parameters (scr_param, data);

  int sx = images[0][0].final_xshift;
  int sy = images[0][0].final_yshift;

  for (int y = 0; y < stitch_height; y++)
    {
      for (int x = 1; x < stitch_width; x++)
      {
	coord_t rx, ry;
	map.scr_to_final (images[y][x-1].xpos, images[y][x-1].ypos, &rx, &ry);
	coord_t rx2, ry2;
	map.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx2, &ry2);
	rx -= images[y][x-1].xshift;
	ry -= images[y][y-1].xshift;
	rx2 -= images[y][x].xshift;
	ry2 -= images[y][y].xshift;
	printf (" from left %+5i, %+5i", (int)(rx2-rx), (int)(ry2-ry));
	//printf ("  %-5i,%-5i range: %-5i:%-5i,%-5i:%-5i", (int)rx,(int)ry,(int)rx-images[y][x].xshift+sx,(int)rx-images[y][x].xshift+images[y][x].final_width+sx,(int)ry-images[y][x].yshift+sy,(int)ry-images[y][x].yshift+images[y][x].final_height+sy);
      }
      printf ("\n");
    }
#if 0
  for (int y = 0; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_width; x++)
      {
	coord_t rx, ry;
	map.scr_to_final (images[y][x].xpos, images[y][x].ypos, &rx, &ry);
	printf ("  %-5i,%-5i range: %-5i:%-5i,%-5i:%-5i", (int)rx,(int)ry,(int)rx-images[y][x].xshift+sx,(int)rx-images[y][x].xshift+images[y][x].final_width+sx,(int)ry-images[y][x].yshift+sy,(int)ry-images[y][x].yshift+images[y][x].final_height+sy);
      }
      printf ("\n");
    }
#endif
}

}

int
main (int argc, char **argv)
{
  char *outfname;
  const char *error;
  const char *cspname;

  if (argc < 5)
  {
    print_help (argv[0]);
    exit(1);
  }
  outfname = argv[1];
  cspname = argv[2];
  /* Load color screen and rendering parameters.  */
  FILE *in = fopen (cspname, "rt");
  printf ("Loading color screen parameters: %s\n", cspname);
  if (!in)
    {
      perror (cspname);
      exit (1);
    }
  if (!load_csp (in, &param, &dparam, &rparam, &solver_param, &error))
    {
      fprintf (stderr, "Can not load %s: %s\n", cspname, error);
      exit (1);
    }
  fclose (in);

  stitch_width = atoi(argv[3]);
  if (stitch_width <= 0 || stitch_width > MAX_DIM)
    {
      fprintf (stderr, "Invalid stich width %s\n", argv[3]);
      print_help (argv[0]);
      exit(1);
    }
  stitch_height = atoi(argv[4]);
  if (stitch_height <= 0 || stitch_height > MAX_DIM)
    {
      fprintf (stderr, "Invalid stich height %s\n", argv[4]);
      print_help (argv[0]);
      exit(1);
    }
  if (argc != 5 + stitch_width * stitch_height)
    {
      fprintf (stderr, "Expected %i parameters, have %i\n", 5 + stitch_width * stitch_height, argc);
      print_help (argv[0]);
      exit(1);
    }
  for (int y = 0, n = 5; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_width; x++)
	{
	  images[y][x].filename = argv[n++];
	  printf ("   %s", images[y][x].filename);
	}
      printf ("\n");
    }
  file_progress_info progress (stdout);
#if 0
  for (int y = 0; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_width; x++)
	images[y][x].analyze (&progress);
    }
#endif
  int percentage = 5;
  for (int y = 0; y < stitch_height; y++)
    {
      if (!y)
	{
	  images[0][0].xpos = 0;
	  images[0][0].ypos = 0;
	}
      else
	{
	  int xs;
	  int ys;
	  images[y-1][0].analyze (&progress);
	  images[y][0].analyze (&progress);
	  if (!images[y-1][0].dufay.find_best_match (percentage, images[y][0].dufay, 0, 0, 30, 0, &xs, &ys, &progress))
	    {
	      progress.pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y-1][0].filename, images[y][0].filename);
	      exit (1);
	    }
	  images[y][0].xpos = images[y-1][0].xpos + xs;
	  images[y][0].ypos = images[y-1][0].ypos + ys;
	  progress.pause_stdout ();
	  print_status ();
	  progress.resume_stdout ();
	}
      int skiptop = y ? 0 : 30;
      int skipbottom = y == stitch_height - 1 ? 30 : 0;
      for (int x = 0; x < stitch_width - 1; x++)
	{
	  int xs;
	  int ys;
	  images[y][x].analyze (&progress);
	  images[y][x+1].analyze (&progress);
	  if (!images[y][x].dufay.find_best_match (percentage, images[y][x+1].dufay, skiptop, skipbottom, 0, 0, &xs, &ys, &progress))
	    {
	      progress.pause_stdout ();
	      fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename, images[y][x + 1].filename);
	      exit (1);
	    }
	  images[y][x+1].xpos = images[y][x].xpos + xs;
	  images[y][x+1].ypos = images[y][x].ypos + ys;
	  progress.pause_stdout ();
	  print_status ();
	  progress.resume_stdout ();
	  /* Confirm position.  */
	  if (y)
	    {
	      if (!images[y-1][x+1].dufay.find_best_match (percentage, images[y][x+1].dufay, 0, 0, 0, x == stitch_width - 1 ? 30 : 0, &xs, &ys, &progress))
		{
		  progress.pause_stdout ();
		  fprintf (stderr, "Can not find good overlap of %s and %s\n", images[y][x].filename, images[y][x + 1].filename);
		  exit (1);
		}
	      if (images[y][x+1].xpos != images[y-1][x+1].xpos + xs
		  || images[y][x+1].ypos != images[y-1][x+1].ypos + ys)
		{
		  progress.pause_stdout ();
		  fprintf (stderr, "Stitching mismatch in %s: %i,%i is not equal to %i,%i\n", images[y][x + 1].filename, images[y][x+1].xpos, images[y][x+1].ypos, images[y-1][x+1].xpos + xs, images[y-1][x+1].ypos + ys);
		  exit (1);
		}

	    }
	}
    }
  return 0;
}
