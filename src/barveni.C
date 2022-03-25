#include <time.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <gtk/gtkbuilder.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <netpbm/pgm.h>
#include <netpbm/ppm.h>
#include <math.h>
#include <cairo.h>
#include <gtkimageviewer-2.0/gtk-image-viewer.h>
#include <stdbool.h>

#include "matrix.h"
#include "scr-to-img.h"
#include "render-fast.h"
#include "screen.h"
#include "render-superposeimg.h"
#include "render-interpolate.h"

#define UNDOLEVELS 100 
#define PREVIEWSIZE 400

extern "C" {

/* Structure describing the position of the screen.  */
struct parameters {
  /* This is a center of rotation of screen (a green dot).  */
  double xstart, ystart;
  /* This defines horizontal vector of the screen.  */
  double xend, yend;
  /* I originally believed that Library of Congress scans are 1000 DPI
     and later found that they differs in their vertical and horisontal DPIs.
     XM is a correction to horisontal DPI and YM is a correction to vertical.
     Since the DPI information is inprecise this does not have any really 
     good meaning.  */
  double xm, ym;
  /* This was added later to allow distortions along each edge of the scan.
     It should be perpective correction in one direction and it is not.  */
  double xs,ys,xs2,ys2;
};

/* Undo history and the state of UI.  */
struct parameters undobuf[UNDOLEVELS];
struct parameters current;
int undopos;

char *oname, *paroname;
static void bigrender (int xoffset, int yoffset, double bigscale, GdkPixbuf * bigpixbuf);

/* The graymap with original scan is stored here.  */
int xsize, ysize;
gray **graydata;
gray maxval;
int initialized = 0;

/* Status of the main window.  */
int offsetx = 8, offsety = 8;
int bigscale = 4;

bool display_scheduled = true;

/* How much is the image scaled in the small view.  */
#define SCALE 16


void
save_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  undobuf[undopos] = current;
}
void
undo_parameters (void)
{
  current = undobuf[undopos];
  undopos  = (undopos + UNDOLEVELS - 1) % UNDOLEVELS;
}
void
redo_parameters (void)
{
  undopos  = (undopos + 1) % UNDOLEVELS;
  current = undobuf[undopos];
}

static inline double
cubicInterpolate (double p[4], double x)
{
  return p[1] + 0.5 * x * (p[2] - p[0] +
			   x * (2.0 * p[0] - 5.0 * p[1] + 4.0 * p[2] - p[3] +
				x * (3.0 * (p[1] - p[2]) + p[3] - p[0])));
}

static inline double
bicubicInterpolate (double p[4][4], double x, double y)
{
  double arr[4];
  if (x < 0 || x > 1 || y < 0 || y > 1)
    abort ();
  arr[0] = cubicInterpolate (p[0], y);
  arr[1] = cubicInterpolate (p[1], y);
  arr[2] = cubicInterpolate (p[2], y);
  arr[3] = cubicInterpolate (p[3], y);
  return cubicInterpolate (arr, x);
}


typedef struct _Data Data;
struct _Data
{
  GtkWidget *save;
  /*GtkImage *bigimage; */
  GtkImage *smallimage;
  GdkPixbuf *bigpixbuf;
  GdkPixbuf *smallpixbuf;
  GtkWidget *maindisplay_scroll;
  GtkWidget *image_viewer;
  GtkSpinButton *x1, *y1, *x2, *y2, *xdpi, *ydpi;
};
Data data;

G_MODULE_EXPORT void cb_press (GtkImage * image, GdkEventButton * event,
			       Data * data);
G_MODULE_EXPORT gboolean
cb_delete_event (GtkWidget * window, GdkEvent * event, Data * data)
{
  gint response = 1;

  /* Run dialog */
  /*response = gtk_dialog_run( GTK_DIALOG( data->quit ) );
     gtk_widget_hide( data->quit ); */

  return (1 != response);
}

G_MODULE_EXPORT void
cb_show_about (GtkButton * button, Data * data)
{
  /* Run dialog */
  /*gtk_dialog_run( GTK_DIALOG( data->about ) );
     gtk_widget_hide( data->about ); */
}

/* Load the input file. */

static void
openimage (int *argc, char **argv)
{
  FILE *in;
  pgm_init (argc, argv);
  ppm_init (argc, argv);
  in = fopen (argv[1], "r");
  if (!in)
    {
      perror (argv[1]);
      exit (1);
    }
  graydata = pgm_readpgm (fopen (argv[1], "r"), &xsize, &ysize, &maxval);
  maxval++;
}

/* Get values displayed in the UI.  */

static void
getvals (void)
{
  current.xstart = gtk_spin_button_get_value (data.x1);
  current.ystart = gtk_spin_button_get_value (data.y1);
  current.xend = gtk_spin_button_get_value (data.x2);
  current.yend = gtk_spin_button_get_value (data.y2);
  current.xm = 1 / (gtk_spin_button_get_value (data.xdpi) / 1000);
  current.ym = 1 / (gtk_spin_button_get_value (data.ydpi) / 1000);
  /*printf ("%lf %lf %lf %lf %lf %lf %lf\n", current.xstart, current.ystart, current.xend, current.yend, num,
	  current.xm, current.ym);*/
}

/* Set values displayed by the UI.  */

static void
setvals (void)
{
  initialized = 0;
  gtk_spin_button_set_value (data.x1, current.xstart);
  gtk_spin_button_set_value (data.y1, current.ystart);
  gtk_spin_button_set_value (data.x2, current.xend);
  gtk_spin_button_set_value (data.y2, current.yend);
  gtk_spin_button_set_value (data.xdpi, (1 / current.xm) * 1000 + 0.00000005);
  gtk_spin_button_set_value (data.ydpi, (1 / current.ym) * 1000 + 0.00000005);
  initialized = 1;
}

/* Render image into the main window.  */
static void
cb_image_annotate (GtkImageViewer * imgv,
		   GdkPixbuf * pixbuf,
		   gint shift_x,
		   gint shift_y,
		   gdouble scale_x, gdouble scale_y, gpointer user_data)
{
  int img_width = gdk_pixbuf_get_width (pixbuf);
  int img_height = gdk_pixbuf_get_height (pixbuf);
  int row_stride = gdk_pixbuf_get_rowstride (pixbuf);
  guint8 *buf = gdk_pixbuf_get_pixels (pixbuf);
  int col_idx, row_idx;

  assert (scale_x == scale_y);
  bigrender (shift_x, shift_y, scale_x, pixbuf);
}

int setcenter;

/* Handle all the magic keys.  */
static gint
cb_key_press_event (GtkWidget * widget, GdkEventKey * event)
{
  gint k = event->keyval;

  if (k == 'c')
    setcenter = 1;
  if (k == 'q')
    {
      save_parameters ();
      current.ys += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'w')
    {
      save_parameters ();
      current.ys -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'a')
    {
      save_parameters ();
      current.xs += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 's')
    {
      save_parameters ();
      current.xs -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'e')
    {
      save_parameters ();
      current.ys2 += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'r')
    {
      save_parameters ();
      current.ys2 -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'd')
    {
      save_parameters ();
      current.xs2 += 0.00000003;
      display_scheduled = 1;
    }
  if (k == 'f')
    {
      save_parameters ();
      current.xs2 -= 0.00000003;
      display_scheduled = 1;
    }
  if (k == 't')
{
      save_parameters ();
    current.xs=current.ys=current.xs2=current.ys2=0;
      display_scheduled = 1;
}
  if (k == 'u')
    {
      undo_parameters ();	
      setvals ();
      display_scheduled = 1;
    }
  if (k == 'U')
    {
      redo_parameters ();
      setvals ();
      display_scheduled = 1;
    }

  return FALSE;
}


/* Initialize the GUI.  */
static GtkWidget *
initgtk (int *argc, char **argv)
{
  GtkBuilder *builder;
  GtkWidget *window;
  GtkWidget *image_viewer, *scrolled_win;

  gtk_init (argc, &argv);

  /* Create builder and load interface */
  builder = gtk_builder_new ();
  if (!gtk_builder_add_from_file (builder, "barveni.glade", NULL))
    {
      fprintf (stderr, "Can not open barveni.glade\n");
      exit (1);
    }

  /* Obtain widgets that we need */
  window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));
  data.save = GTK_WIDGET (gtk_builder_get_object (builder, "save"));
  /*data.bigimage = GTK_IMAGE (gtk_builder_get_object (builder, "bigimage")); */
  data.smallimage =
    GTK_IMAGE (gtk_builder_get_object (builder, "smallimage"));
  data.maindisplay_scroll =
    GTK_WIDGET (gtk_builder_get_object (builder, "maindisplay-scroll"));

  /* Add image_viewer.  */
  image_viewer = gtk_image_viewer_new (NULL);
  g_signal_connect (image_viewer,
		    "image-annotate", G_CALLBACK (cb_image_annotate), NULL);

  gtk_signal_connect (GTK_OBJECT (image_viewer), "key_press_event",
		      GTK_SIGNAL_FUNC (cb_key_press_event), NULL);
  /*gtk_signal_connect (GTK_OBJECT(image_viewer),     "button_press_event",
     GTK_SIGNAL_FUNC(cb_press), NULL); */
  data.image_viewer = image_viewer;

  gtk_container_add (GTK_CONTAINER (data.maindisplay_scroll), image_viewer);

  gtk_widget_show (image_viewer);

  // Set the scroll region and zoom range
  gtk_image_viewer_set_scroll_region (GTK_IMAGE_VIEWER (image_viewer),
				      20, 20, xsize - 20, ysize - 20);
  gtk_image_viewer_set_zoom_range (GTK_IMAGE_VIEWER (image_viewer), 0.1, 64);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER (image_viewer), 4.0,
					4.0, 64, 64);

  // Need to do a manual zoom fit at creation because a bug when
  // not using an image.
  gtk_image_viewer_zoom_fit (GTK_IMAGE_VIEWER (image_viewer));
  data.x1 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "x1"));
  data.y1 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "y1"));
  data.x2 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "x2"));
  data.y2 = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "y2"));
  data.xdpi = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "xdpi"));
  data.ydpi = GTK_SPIN_BUTTON (gtk_builder_get_object (builder, "ydpi"));
  /*data.about = GTK_WIDGET( gtk_builder_get_object( builder, "aboutdialog1" ) ); */

  /* Connect callbacks */
  gtk_builder_connect_signals (builder, &data);

  /* Destroy builder */
  g_object_unref (G_OBJECT (builder));
  return window;
}

static struct scr_to_img_parameters
get_scr_to_img_parameters ()
{
  double a, b, c, d;
  double ox,oy;
  double num = sqrt ((current.xend - current.xstart) * (current.xend - current.xstart) * current.xm * current.xm
		     + (current.yend - current.ystart) * (current.yend - current.ystart) * current.ym * current.ym)
	       / (8.750032);	/* 8.75 pixels per screen in the LOC 1000DPI scans.  */
  ox = (current.xend - current.xstart) * current.xm / (double) num;
  oy = (current.yend - current.ystart) * current.ym / (double) num;

  struct scr_to_img_parameters param;
  param.center_x = current.xstart;
  param.center_y = current.ystart;
  param.coordinate1_x = ox / current.xm;
  param.coordinate1_y = -oy / current.xm;
  param.coordinate2_x = oy / current.ym;
  param.coordinate2_y = ox / current.ym;
  return param;
}

static inline void
init_transformation_data (scr_to_img *trans)
{
  trans->set_parameters (get_scr_to_img_parameters ());
}

/* Uused to draw into the previews.  Differs by data type.  */

static inline void
my_putpixel2 (guint8 * pixels, int rowstride, int x, int y, int r, int g,
	      int b)
{
  *(pixels + y * rowstride + x * 4) = r;
  *(pixels + y * rowstride + x * 4 + 1) = g;
  *(pixels + y * rowstride + x * 4 + 2) = b;
}

static inline void
my_putpixel (guchar * pixels, int rowstride, int x, int y, int r, int g,
	     int b)
{
  *(pixels + y * rowstride + x * 3) = r;
  *(pixels + y * rowstride + x * 3 + 1) = g;
  *(pixels + y * rowstride + x * 3 + 2) = b;
}

/* Determine grayscale value at a given position in the screen coordinates.  */

static double
sample (scr_to_img *map, double x, double y)
{
  double xp, yp;
  int sx, sy;
  double p[4][4];
  double val;
  map->to_img (x, y, &xp, &yp);
  sx = xp, sy = yp;

  if (xp < 2 || xp >= xsize - 2 || yp < 2 || yp >= ysize - 2)
    return 0;
  p[0][0] = graydata[sy - 1][sx - 1];
  p[1][0] = graydata[sy - 1][sx - 0];
  p[2][0] = graydata[sy - 1][sx + 1];
  p[3][0] = graydata[sy - 1][sx + 2];
  p[0][1] = graydata[sy - 0][sx - 1];
  p[1][1] = graydata[sy - 0][sx - 0];
  p[2][1] = graydata[sy - 0][sx + 1];
  p[3][1] = graydata[sy - 0][sx + 2];
  p[0][2] = graydata[sy + 1][sx - 1];
  p[1][2] = graydata[sy + 1][sx - 0];
  p[2][2] = graydata[sy + 1][sx + 1];
  p[3][2] = graydata[sy + 1][sx + 2];
  p[0][3] = graydata[sy + 2][sx - 1];
  p[1][3] = graydata[sy + 2][sx - 0];
  p[2][3] = graydata[sy + 2][sx + 1];
  p[3][3] = graydata[sy + 2][sx + 2];
  val = bicubicInterpolate (p, xp - sx, yp - sy);
  if (val < 0)
    val = 0;
  if (val > maxval - 1)
    val = maxval - 1;
  return val;
}

#define NBLUE 8			/* We need 6 rows of blue.  */
#define NRED 8			/* We need 7 rows of the others.  */

inline int
getmatrixsample (double **sample, int *shift, int pos, int xp, int x, int y)
{
  int line = (pos + NRED + x + y) % NRED;
  return sample[line][((xp + y * 2 - x * 2) - shift[line]) / 4];
}

/* This renders the small preview widget.   */

static void
previewrender (GdkPixbuf ** pixbuf)
{
  int x, y;
  guint8 *pixels;
  render_fast render (get_scr_to_img_parameters (), graydata, xsize, ysize, maxval, 256);
  int scr_xsize = render.get_width (), scr_ysize = render.get_height (), rowstride;
  int max_size = std::max (scr_xsize, scr_ysize);
  double step = max_size / (double)PREVIEWSIZE;
  int my_xsize = ceil (scr_xsize / step), my_ysize = ceil (scr_ysize / step);

  gdk_pixbuf_unref (*pixbuf);
  *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, my_xsize, my_ysize);

  pixels = gdk_pixbuf_get_pixels (*pixbuf);
  rowstride = gdk_pixbuf_get_rowstride (*pixbuf);

  for (y = 0; y < my_ysize; y ++)
    for (x = 0; x < my_xsize; x ++)
      {
	int red, green, blue;
	render.render_pixel (x * step, y * step, &red, &green, &blue);
	my_putpixel (pixels, rowstride, x, y, red, green, blue);
      }
}

struct samples
{
  int xshift, yshift;
  int xsize, ysize;
  double *redsample[8];
  double *greensample[8];
  double *bluesample[NBLUE];
  int bluepos[NBLUE];
  int redpos[8];
  int redshift[8];
  int greenpos[8];
  int greenshift[8];
  int redp, greenp, bluep;
  int scale;
};

static void
init_finalrender (struct samples *samples, int scale)
{
  int i;
  scr_to_img map;

  init_transformation_data (&map);
  map.get_range (xsize, ysize, &samples->xshift, &samples->yshift, &samples->xsize, &samples->ysize);
  samples->scale = scale;
  for (i = 0; i < 8; i++)
    {
      samples->redsample[i] = (double *)malloc (sizeof (double) * samples->xsize);
      samples->greensample[i] = (double *)malloc (sizeof (double) * samples->xsize);
    }
  for (i = 0; i < NBLUE; i++)
    samples->bluesample[i] = (double *)malloc (sizeof (double) * samples->xsize * 2);
  samples->bluep = samples->redp = samples->greenp = 0;
}

static void
finalrender_row (int y, pixel ** outrow, struct samples *samples)
{
  double **redsample = samples->redsample;
  double **greensample = samples->greensample;
  double **bluesample = samples->bluesample;
  int *bluepos = samples->bluepos;
  int *redpos = samples->redpos;
  int *redshift = samples->redshift;
  int *greenpos = samples->greenpos;
  int *greenshift = samples->greenshift;
  int x;
  int sx;
  int sy;
  int scale = samples->scale;
  scr_to_img map;

  init_transformation_data (&map);

  if (y % 4 == 0)
    {
      for (x = 0; x < samples->xsize; x++)
	greensample[samples->greenp][x] =
	  sample (&map, x - samples->xshift,
		  (y - samples->yshift * 4) / 4.0);
      samples->greenpos[samples->greenp] = y;
      greenshift[samples->greenp] = 0;
      samples->greenp++;
      samples->greenp %= 8;

      for (x = 0; x < samples->xsize; x++)
	redsample[samples->redp][x] =
	  sample (&map, x - samples->xshift + 0.5,
		  (y - samples->yshift * 4) / 4.0);
      redpos[samples->redp] = y;
      redshift[samples->redp] = 2;
      samples->redp++;
      samples->redp %= 8;
    }
  if (y % 4 == 2)
    {
      for (x = 0; x < samples->xsize; x++)
	redsample[samples->redp][x] =
	  sample (&map, x - samples->xshift,
		  (y - samples->yshift * 4) / 4.0);
      redpos[samples->redp] = y;
      redshift[samples->redp] = 0;
      samples->redp++;
      samples->redp %= 8;

      for (x = 0; x < samples->xsize; x++)
	greensample[samples->greenp][x] =
	  sample (&map, x - samples->xshift + 0.5,
		  (y - samples->yshift * 4) / 4.0);
      samples->greenpos[samples->greenp] = y;
      greenshift[samples->greenp] = 2;
      samples->greenp++;
      samples->greenp %= 8;
    }
  if (y % 4 == 1 || y % 4 == 3)
    {
      bluepos[samples->bluep] = y;
      for (x = 0; x < samples->xsize; x++)
	{
	  bluesample[samples->bluep][x * 2] =
	    sample (&map, x - samples->xshift + 0.25,
		    (y - samples->yshift * 4) / 4.0);
	  bluesample[samples->bluep][x * 2 + 1] =
	    sample (&map, x - samples->xshift + 0.75,
		    (y - samples->yshift * 4) / 4.0);
	}
      samples->bluep++;
      samples->bluep %= NBLUE;
    }
  if (y > 8 * 4)
    {
#define OFFSET  7
      int rendery = y - OFFSET;
      int bluestart =
	(samples->bluep + NBLUE - ((OFFSET + 1) / 2 + 2)) % NBLUE;
      double bluey;
      int redcenter = (samples->redp + NRED - ((OFFSET + 3) / 2)) % NRED;
      int greencenter = (samples->greenp + NRED - ((OFFSET + 3) / 2)) % NRED;
      int xx, yy;
      printf ("%i %i\n", y, scale);

      if (bluepos[(bluestart + 2) % NBLUE] == rendery)
	bluestart = (bluestart + 1) % NBLUE;
      /*fprintf (stderr, "baf:bp:%i rendery:%i:%i %i %i %f\n", bluepos[(bluestart + 1) % NBLUE], rendery,y, bluey, bluep, bluey); */
      assert (bluepos[(bluestart + 1) % NBLUE] <= rendery);
      assert (bluepos[(bluestart + 2) % NBLUE] > rendery);

      if (redpos[(redcenter + 1) % NRED] == rendery)
	redcenter = (redcenter + 1) % NRED;
      assert (redpos[(redcenter) % NBLUE] <= rendery);
      assert (redpos[(redcenter + 1) % NBLUE] > rendery);

      if (greenpos[(greencenter + 1) % NRED] == rendery)
	greencenter = (greencenter + 1) % NRED;
      assert (greenpos[(greencenter) % NBLUE] <= rendery);
      assert (greenpos[(greencenter + 1) % NBLUE] > rendery);
      for (yy = 0; yy < scale; yy++)
	{
	  bluey =
	    (rendery + ((double) yy) / scale -
	     bluepos[(bluestart + 1) % NBLUE]) / 2.0;
	  for (x = 8; x < samples->xsize * 4; x++)
	    for (xx = 0; xx < scale; xx++)
	      {
		double p[4][4];
		double red, green, blue;
		double xo, yo;
		int np;
		int bluex = (x - 1) / 2;
		int val;

		p[0][0] = bluesample[bluestart][bluex - 1];
		p[1][0] = bluesample[bluestart][bluex];
		p[2][0] = bluesample[bluestart][bluex + 1];
		p[3][0] = bluesample[bluestart][bluex + 2];
		p[0][1] = bluesample[(bluestart + 1) % NBLUE][bluex - 1];
		p[1][1] = bluesample[(bluestart + 1) % NBLUE][bluex];
		p[2][1] = bluesample[(bluestart + 1) % NBLUE][bluex + 1];
		p[3][1] = bluesample[(bluestart + 1) % NBLUE][bluex + 2];
		p[0][2] = bluesample[(bluestart + 2) % NBLUE][bluex - 1];
		p[1][2] = bluesample[(bluestart + 2) % NBLUE][bluex];
		p[2][2] = bluesample[(bluestart + 2) % NBLUE][bluex + 1];
		p[3][2] = bluesample[(bluestart + 2) % NBLUE][bluex + 2];
		p[0][3] = bluesample[(bluestart + 3) % NBLUE][bluex - 1];
		p[1][3] = bluesample[(bluestart + 3) % NBLUE][bluex];
		p[2][3] = bluesample[(bluestart + 3) % NBLUE][bluex + 1];
		p[3][3] = bluesample[(bluestart + 3) % NBLUE][bluex + 2];
		xo = (double) (x + ((double) xx) / scale - 1) / 2;
		blue = bicubicInterpolate (p, xo - (int) xo, bluey);
		if (blue < 0)
		  blue = 0;
		if (blue > maxval - 1)
		  blue = maxval - 1;
		{
		  int sx = ((x - redshift[redcenter]) + 2) / 4;
		  int dx = (x - redshift[redcenter]) - sx * 4;
		  int dy = rendery - redpos[redcenter];
		  int currcenter = redcenter;
		  int distx, disty;

		  if (abs (dx) > dy)
		    {
		      currcenter = (redcenter + NRED - 1) % NRED;
		      sx = ((x - redshift[currcenter]) + 2) / 4;
		    }
		  red = redsample[currcenter][sx];

		  /*red = getmatrixsample (redsample, redshift, currcenter, sx * 4 + redshift[currcenter], 0, 0); */
		  sx = sx * 4 + redshift[currcenter];
		  p[0][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     -1);
		  p[0][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     0);
		  p[0][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     1);
		  p[0][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -1,
				     2);
		  p[1][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     -1);
		  p[1][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     0);
		  p[1][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     1);
		  p[1][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, -0,
				     2);
		  p[2][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     -1);
		  p[2][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     0);
		  p[2][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     1);
		  p[2][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +1,
				     2);
		  p[3][0] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     -1);
		  p[3][1] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     0);
		  p[3][2] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     1);
		  p[3][3] =
		    getmatrixsample (redsample, redshift, currcenter, sx, +2,
				     2);
		  distx = x - sx;
		  disty = rendery - redpos[currcenter];
		  red =
		    bicubicInterpolate (p, (disty - distx) / 4.0,
					(distx + disty) / 4.0);
		  if (red < 0)
		    red = 0;
		  if (red > maxval - 1)
		    red = maxval - 1;
		}
		{
		  int sx = ((x - greenshift[greencenter]) + 2) / 4;
		  int dx = (x - greenshift[greencenter]) - sx * 4;
		  int dy = rendery - greenpos[greencenter];
		  int currcenter = greencenter;
		  int distx, disty;

		  if (abs (dx) > dy)
		    {
		      currcenter = (greencenter + NRED - 1) % NRED;
		      sx = ((x - greenshift[currcenter]) + 2) / 4;
		    }
		  green = greensample[currcenter][sx];

		  /*green = getmatrixsample (greensample, greenshift, currcenter, sx * 4 + greenshift[currcenter], 0, 0); */
		  sx = sx * 4 + greenshift[currcenter];
		  p[0][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, -1);
		  p[0][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 0);
		  p[0][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 1);
		  p[0][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -1, 2);
		  p[1][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, -1);
		  p[1][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 0);
		  p[1][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 1);
		  p[1][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     -0, 2);
		  p[2][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, -1);
		  p[2][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 0);
		  p[2][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 1);
		  p[2][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +1, 2);
		  p[3][0] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, -1);
		  p[3][1] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 0);
		  p[3][2] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 1);
		  p[3][3] =
		    getmatrixsample (greensample, greenshift, currcenter, sx,
				     +2, 2);
		  distx = x - sx;
		  disty = rendery - greenpos[currcenter];
		  green =
		    bicubicInterpolate (p, (disty - distx) / 4.0,
					(distx + disty) / 4.0);
		  if (green < 0)
		    green = 0;
		  if (green > maxval - 1)
		    green = maxval - 1;
		}


#if 1
		val =
		  sample (&map, (x - samples->xshift * 4 +
				   xx / (double) scale) / 4.0,
			  (rendery - samples->yshift * 4 +
			   yy / (double) scale) / 4.0) * 256;
		if (red != 0 || green != 0 || blue != 0)
		  {
		    double sum = (red + green + blue) / 3;
		    red = red * val / sum;
		    green = green * val / sum;
		    blue = blue * val / sum;
		  }
		else
		  red = green = blue = val;
#else
		red *= 256;
		green *= 256;
		blue *= 256;
#endif
		if (red > 65536 - 1)
		  red = 65536 - 1;
		if (red < 0)
		  red = 0;
		if (green > 65536 - 1)
		  green = 65536 - 1;
		if (green < 0)
		  green = 0;
		if (blue > 65536 - 1)
		  blue = 65536 - 1;
		if (blue < 0)
		  blue = 0;
		outrow[yy][(x * scale + xx)].r = red;
		outrow[yy][(x * scale + xx)].g = green;
		outrow[yy][(x * scale + xx)].b = blue;
	      }
	}
    }
}

static void
bigrender (int xoffset, int yoffset, double bigscale, GdkPixbuf * bigpixbuf)
{
  int bigrowstride = gdk_pixbuf_get_rowstride (bigpixbuf);
  guint8 *bigpixels = gdk_pixbuf_get_pixels (bigpixbuf);
  int pxsize = gdk_pixbuf_get_width (bigpixbuf);
  int pysize = gdk_pixbuf_get_height (bigpixbuf);
  screen screen;
  screen.preview (maxval);
  render_superpose_img render (get_scr_to_img_parameters (), graydata, xsize, ysize, maxval, 256, &screen);

  for (int y = 0; y < pysize; y++)
    {
      double py = (y + yoffset) / bigscale;
      for (int x = 0; x < pxsize; x++)
	{
	  int r, g, b;
	  render.render_pixel ((x + xoffset) / bigscale, py, &r, &g, &b);
	  my_putpixel2 (bigpixels, bigrowstride, x, y, r, g, b);
	}
    }

  cairo_surface_t *surface
    = cairo_image_surface_create_for_data (bigpixels,
					   CAIRO_FORMAT_RGB24,
					   pxsize,
					   pysize,
					   bigrowstride);
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_source_rgba (cr, 0, 0, 1.0, 0.5);
  cairo_arc (cr, current.xstart, current.ystart, 3, 0.0, 2 * G_PI);

  cairo_fill (cr);

  cairo_surface_destroy (surface);
  cairo_destroy (cr);
}

static void
display ()
{
  gtk_image_viewer_redraw ((GtkImageViewer *)data.image_viewer, 1);
  previewrender (&data.smallpixbuf);
  gtk_image_set_from_pixbuf (data.smallimage, data.smallpixbuf);
}

G_MODULE_EXPORT void
cb_redraw (GtkButton * button, Data * data)
{
  if (!initialized)
    return;
  getvals ();
  display ();
}

G_MODULE_EXPORT void
cb_press_small (GtkImage * image, GdkEventButton * event, Data * data)
{
  getvals ();
  printf ("Press small %i %i\n", event->x, event->y);
  if (event->button == 1)
    {
      offsetx =
	8 +
	(event->x) * xsize * bigscale /
	gdk_pixbuf_get_width (data->smallpixbuf);
      offsety =
	8 +
	(event->y) * ysize * bigscale /
	gdk_pixbuf_get_height (data->smallpixbuf);
    }
}

double xpress, ypress;
double xpress1, ypress1;
double pressxend, pressyend;
double pressxstart, pressystart;
bool button1_pressed;
bool button3_pressed;

G_MODULE_EXPORT void
cb_press (GtkImage * image, GdkEventButton * event, Data * data2)
{
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER (data.image_viewer),
					&scale_x, &scale_y, &shift_x,
					&shift_y);
  if (!initialized)
    return;
  printf ("Press x:%i y:%i zoomx:%f zoomy:%f shiftx:%i shifty:%i \n",
	  (int) event->x, (int) event->y, (double) scale_x, (double) scale_y,
	  (int) shift_x, (int) shift_y);
  if (event->button == 1 && setcenter)
    {
      double newxstart;
      double newystart;
      newxstart = (event->x + shift_x) / scale_x;
      newystart = (event->y + shift_y) / scale_y;
      if (newxstart != current.xstart || newystart != current.ystart)
	{
	  current.xend += newxstart - current.xstart;
	  current.yend += newystart - current.ystart;
	  current.xstart = newxstart;
	  current.ystart = newystart;
	  setcenter = 0;
	  setvals ();
	  display_scheduled = true;
	}
    }
  pressxstart = current.xstart;
  pressystart = current.ystart;
  pressxend = current.xend;
  pressyend = current.yend;
  if (event->button == 1)
    {
      xpress1 = event->x;
      ypress1 = event->y;
      button1_pressed = true;
    }
  else if (event->button == 3)
    {
      xpress = (event->x + shift_x) / scale_x;
      ypress = (event->y + shift_y) / scale_y;
      button3_pressed = true;
    }
}

void
handle_drag (int x, int y, int button)
{
  gdouble scale_x, scale_y;
  gint shift_x, shift_y;
  gtk_image_viewer_get_scale_and_shift (GTK_IMAGE_VIEWER
					(data.image_viewer), &scale_x,
					&scale_y, &shift_x, &shift_y);
  if (button == 1)
    {
      double xoffset = (x - xpress1) / scale_x;
      double yoffset = (y - ypress1) / scale_y;
      if (current.xstart == pressxstart + xoffset && current.ystart == pressystart + yoffset)
	return;
      current.xend = pressxend + xoffset;
      current.yend = pressyend + yoffset;
      current.xstart = pressxstart + xoffset;
      current.ystart = pressystart + yoffset;
      setvals ();
      display_scheduled = true;
    }
  else if (button == 3)
    {
      double x1 = (xpress - current.xstart);
      double y1 = (ypress - current.ystart);
      double x2 = (x + shift_x) / scale_x - current.xstart;
      double y2 = (y + shift_y) / scale_y - current.ystart;
      double angle = atan2f (y2, x2) - atan2f (y1, x1);
      if (!angle)
	return;
      current.xend =
	current.xstart + (pressxend - current.xstart) * cos (angle) + (pressyend -
						       current.ystart) * sin (angle);
      current.yend =
	current.ystart + (pressxend - current.xstart) * sin (angle) + (pressyend -
						       current.ystart) * cos (angle);

      setvals ();
      display_scheduled = true;
    }
}

G_MODULE_EXPORT void
cb_release (GtkImage * image, GdkEventButton * event, Data * data2)
{
  handle_drag (event->x, event->y, event->button);
  save_parameters ();
  if (event->button == 1)
    button1_pressed = false;
  if (event->button == 3)
    button3_pressed = false;
}

G_MODULE_EXPORT void
cb_drag (GtkImage * image, GdkEventMotion * event, Data * data2)
{
  handle_drag (event->x, event->y,
	       button1_pressed ? 1 : button3_pressed ? 3 : 0);
}

G_MODULE_EXPORT void
cb_save (GtkButton * button, Data * data)
{
  pixel *outrow;
  pixel *outrows[16];
  FILE *out;
  double xend2 = current.xend, yend2 = current.yend;
  struct samples samples;
  int scale;
  out = fopen (paroname, "w");
  fprintf (out, "%lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", current.xstart, current.ystart, current.xend, current.yend,
	   0.0, current.xm, current.ym,current.xs,current.ys,current.xs2,current.ys2);
  fclose (out);

  scale = 4;
  render_interpolate render (get_scr_to_img_parameters (), graydata, xsize, ysize, maxval, 256, scale);
  out = fopen (oname, "w");
  assert (scale < 16);
  for (int y = 0; y < scale; y++)
    outrows[y] = ppm_allocrow (render.get_width () * scale * 4);
  ppm_writeppminit (out, render.get_width () * scale * 4, render.get_height() * scale * 4,
		    65535, 0);
  for (int y = 0; y < render.get_height () * 4; y++)
    {
      render.render_row (y, outrows);
      for (int yy = 0; yy < scale; yy++)
	ppm_writeppmrow (out, outrows[yy], render.get_width() * scale * 4, 65535,
			 0);
    }
  fclose (out);
  for (int y = 0; y < scale; y++)
    free (outrows[y]);
}




int
main (int argc, char **argv)
{
  GtkWidget *window;
  double num;
  openimage (&argc, argv);
  oname = argv[2];
  paroname = argv[3];
  scanf ("%lf %lf %lf %lf %lf %lf %lf", &current.xstart, &current.ystart, &current.xend, &current.yend, &num,
	 &current.xm, &current.ym);
  window = initgtk (&argc, argv);
  setvals ();
  initialized = 1;
  data.smallpixbuf =
    gdk_pixbuf_new (GDK_COLORSPACE_RGB, 0, 8, (xsize) / SCALE + 1,
		    (ysize) / SCALE + 1);
  /* Show main window and start main loop */
  gtk_widget_show (window);
  gtk_image_viewer_set_scale_and_shift (GTK_IMAGE_VIEWER ((GtkImageViewer *)data.image_viewer),
					4.0, 4.0, 64, 64);
  gtk_image_viewer_redraw ((GtkImageViewer *)data.image_viewer, 1);

  while (true)
    {
      if (display_scheduled)
	{
	  display ();
	  display_scheduled = false;
	}
      gtk_main_iteration_do (true);
      while (gtk_events_pending ())
	{
	  gtk_main_iteration_do (FALSE);
	}
    }

  return (0);
}

}
