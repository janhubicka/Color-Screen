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

/* Undo history and the state of UI.  */
static struct scr_to_img_parameters undobuf[UNDOLEVELS];
static struct scr_to_img_parameters current;
static int undopos;

static char *oname, *paroname;
static void bigrender (int xoffset, int yoffset, double bigscale, GdkPixbuf * bigpixbuf);

/* The graymap with original scan is stored here.  */
static int xsize, ysize;
static gray **graydata;
static gray maxval;
static int initialized = 0;

/* Status of the main window.  */
static int offsetx = 8, offsety = 8;
static int bigscale = 4;

static bool display_scheduled = true;

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
#if 0
  current.xstart = gtk_spin_button_get_value (data.x1);
  current.ystart = gtk_spin_button_get_value (data.y1);
  current.xend = gtk_spin_button_get_value (data.x2);
  current.yend = gtk_spin_button_get_value (data.y2);
  current.xm = 1 / (gtk_spin_button_get_value (data.xdpi) / 1000);
  current.ym = 1 / (gtk_spin_button_get_value (data.ydpi) / 1000);
  /*printf ("%lf %lf %lf %lf %lf %lf %lf\n", current.xstart, current.ystart, current.xend, current.yend, num,
	  current.xm, current.ym);*/
#endif
}

/* Set values displayed by the UI.  */

static void
setvals (void)
{
  initialized = 0;
#if 0
  gtk_spin_button_set_value (data.x1, current.xstart);
  gtk_spin_button_set_value (data.y1, current.ystart);
  gtk_spin_button_set_value (data.x2, current.xend);
  gtk_spin_button_set_value (data.y2, current.yend);
  gtk_spin_button_set_value (data.xdpi, (1 / current.xm) * 1000 + 0.00000005);
  gtk_spin_button_set_value (data.ydpi, (1 / current.ym) * 1000 + 0.00000005);
#endif
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

static bool setcenter;
static bool freeze_x = false;
static bool freeze_y = false;

/* Handle all the magic keys.  */
static gint
cb_key_press_event (GtkWidget * widget, GdkEventKey * event)
{
  gint k = event->keyval;

  if (k == 'c')
    setcenter = true;
  if (k == 'x')
    {
      freeze_x = false;
      freeze_y = true;
    }
  if (k == 'y')
    {
      freeze_x = true;
      freeze_y = false;
    }
  if (k == 'a')
    {
      freeze_x = false;
      freeze_y = false;
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
#if 0
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
#endif
  return current;
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

static void
draw_circle (cairo_surface_t *surface, double bigscale,
    	     int xoffset, int yoffset,
    	     double x, double y, double r, double g, double b)
{
  cairo_t *cr = cairo_create (surface);
  cairo_translate (cr, -xoffset, -yoffset);
  cairo_scale (cr, bigscale, bigscale);

  cairo_set_source_rgba (cr, r, g, b, 0.5);
  cairo_arc (cr, x, y, 3, 0.0, 2 * G_PI);

  cairo_fill (cr);
  cairo_destroy (cr);
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
  draw_circle (surface, bigscale, xoffset, yoffset, current.center_x, current.center_y, 0, 0, 1);
  draw_circle (surface, bigscale, xoffset, yoffset, current.center_x + current.coordinate1_x, current.center_y + current.coordinate1_y, 1, 0, 0);
  draw_circle (surface, bigscale, xoffset, yoffset, current.center_x + current.coordinate2_x, current.center_y + current.coordinate2_y, 0, 1, 0);

  cairo_surface_destroy (surface);
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

static double xpress, ypress;
static double xpress1, ypress1;
static bool button1_pressed;
static bool button3_pressed;
static struct scr_to_img_parameters press_parameters;

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
      double newcenter_x;
      double newcenter_y;
      newcenter_x = (event->x + shift_x) / scale_x + 0.5;
      newcenter_y = (event->y + shift_y) / scale_y + 0.5;
      if (newcenter_x != current.center_x || newcenter_y != current.center_y)
	{
	  current.center_x = newcenter_x;
	  current.center_y = newcenter_y;
	  setcenter = false;
	  setvals ();
	  display_scheduled = true;
	}
    }
  press_parameters = current;
  if (event->button == 1)
    {
      xpress1 = event->x;
      ypress1 = event->y;
      button1_pressed = true;
    }
  else if (event->button == 3)
    {
      xpress = (event->x + shift_x) / scale_x + 0.5;
      ypress = (event->y + shift_y) / scale_y + 0.5;
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
      if (current.center_x == press_parameters.center_x + xoffset
          && current.center_y == press_parameters.center_y + yoffset)
	return;
      current.center_x = press_parameters.center_x + xoffset;
      current.center_y = press_parameters.center_y + yoffset;
      setvals ();
      display_scheduled = true;
    }
  else if (button == 3)
    {
      double x1 = (xpress - current.center_x);
      double y1 = (ypress - current.center_y);
      double x2 = (x + shift_x) / scale_x + 0.5 - current.center_x;
      double y2 = (y + shift_y) / scale_y + 0.5 - current.center_y;
      double scale = sqrt ((x2 * x2) + (y2 * y2))/sqrt ((x1*x1) + (y1*y1));
      double angle = atan2f (y2, x2) - atan2f (y1, x1);
      if (!angle)
	return;
      if (!freeze_x)
	{
	  current.coordinate1_x = (press_parameters.coordinate1_x * cos (angle)
				  + press_parameters.coordinate1_y * sin (angle)) * scale;
	  current.coordinate1_y = (press_parameters.coordinate1_x * sin (angle)
				  + press_parameters.coordinate1_y * cos (angle)) * scale;
	}
      if (!freeze_y)
	{
	  current.coordinate2_x = (press_parameters.coordinate2_x * cos (-angle)
				  + press_parameters.coordinate2_y * sin (-angle)) * scale;
	  current.coordinate2_y = (press_parameters.coordinate2_x * sin (-angle)
				  + press_parameters.coordinate2_y * cos (-angle)) * scale;
	}
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

#define HEADER "screen_alignment_version: 1\nscreen_type: PagetFinlay\n"
void write_current (FILE *out)
{
  fprintf (out, HEADER "screen_shift: %f %f\n", current.center_x, current.center_y);
  fprintf (out, "coordinate_x: %f %f\n", current.coordinate1_x, current.coordinate1_y);
  fprintf (out, "coordinate_y: %f %f\n", current.coordinate2_x, current.coordinate2_y);
}


G_MODULE_EXPORT void
cb_save (GtkButton * button, Data * data)
{
  pixel *outrow;
  pixel *outrows[16];
  FILE *out;
  int scale = 4;
  out = fopen (paroname, "w");
  write_current (out);
  fclose (out);

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


bool
expect_string (FILE *f, const char *str)
{
  int len = strlen (str);
  char s[len];
  if (fread (s, 1, strlen (str), f) != len
      || memcmp (s, str, len))
    return false;
  return true;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  double num;
  if (argc != 4)
    {
      fprintf (stderr, "Invocation: %s scan.pgm output.pnm scan.par\n\n"
	       "Here scan.pgm is the scan as a greyscale.\n"
	       "output.pnm is a filename where resulting image will be stored.\n"
	       "If scan.par exists then its parametrs will be read.\n"
	       "Parameters will be saved to scan.par after pressing save button.\n",
	       argv[0]);
      exit (1);
    }
  openimage (&argc, argv);
  oname = argv[2];
  paroname = argv[3];

  current.center_x = 0;
  current.center_y = 0;
  current.coordinate1_x = 5;
  current.coordinate1_y = 0;
  current.coordinate2_x = 0;
  current.coordinate2_y = 5;
  FILE *in = fopen (paroname, "r");
  if (in
      && expect_string (in, HEADER)
      && expect_string (in, "screen_shift:")
      && fscanf (in, "%lf %lf\n", &current.center_x, &current.center_y) == 2)
    {
      if (expect_string (in, "coordinate_x:")
          && fscanf (in, "%lf %lf\n", &current.coordinate1_x, &current.coordinate1_y) == 2)
	{
          if (expect_string (in, "coordinate_y:")
	      && fscanf (in, "%lf %lf\n", &current.coordinate2_x, &current.coordinate2_y))
	    printf ("Reading ok\n");
	}
    }
  if (in)
    fclose (in);
  else
    {
      fprintf (stderr, "Can not open param file \"%s\": ", paroname);
      perror ("");
    }
  write_current (stdout);
#if 0
  scanf ("%lf %lf %lf %lf %lf %lf %lf", &current.xstart, &current.ystart, &current.xend, &current.yend, &num,
	 &current.xm, &current.ym);
#endif
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
