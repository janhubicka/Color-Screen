#include <sys/time.h>
#include "../libcolorscreen/include/colorscreen.h"

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
  void load_img (progress_info *);
  void release_img ();
  void analyze ();
};

void
stitch_image::load_img (progress_info *progress)
{
  if (img)
    return;
  img = new image_data;
  const char *error;
  if (!img->load (filename, &error, progress))
    {
      fprintf (stderr, "Can not load %s: %s\n", filename, error);
      exit (1);
    }
  if (!img->rgbdata)
    {
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
stitch_image::analyze ()
{
  mesh_trans = detect_solver_points (*img, dparam, solver_param, NULL);
  if (!mesh_trans)
    {
      fprintf (stderr, "Failed to analyze screen of %s\n", filename);
      exit (1);
    }
}

stitch_image images[MAX_DIM][MAX_DIM];

void
print_help (void)
{
  printf ("Some useful help will come here\n");
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
    print_help ();
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
      print_help ();
      exit(1);
    }
  stitch_height = atoi(argv[4]);
  if (stitch_height <= 0 || stitch_height > MAX_DIM)
    {
      fprintf (stderr, "Invalid stich height %s\n", argv[4]);
      print_help ();
      exit(1);
    }
  if (argc != 5 + stitch_width * stitch_height)
    {
      fprintf (stderr, "Expected %i parameters, have %i\n", 5 + stitch_width * stitch_height, argc);
      print_help ();
      exit(1);
    }
  for (int y = 0, n = 5; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_height; x++)
	{
	  images[y][x].filename = argv[n++];
	  printf ("   %s", images[y][x].filename);
	}
      printf ("\n");
    }
  file_progress_info progress (stdout);
  for (int y = 0; y < stitch_height; y++)
    {
      for (int x = 0; x < stitch_height; x++)
	{
	  images[y][x].load_img (&progress);
	  images[y][x].analyze ();
	  images[y][x].release_img ();
	}
    }


  return 0;
}
