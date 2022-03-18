//======================================================================
//  mandelbrot.c - An example of how to use the gtk image viewer
//  infinite scrolling with image generation on the fly.
//
//  Dov Grobgeld <dov.grobgeld@gmail.com>
//  Fri Oct  3 15:51:01 2008
//----------------------------------------------------------------------
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "gtk-image-viewer.h"


#define MAX_IT 200
static int color_table[MAX_IT][3];

// Build a nice color table
static void build_color_table()
{
  int i;
  for (i=0; i<MAX_IT; i++)
    {
      // Three arbitrary functions that start at 0 and end at 1
      double r = pow(1.0*i/(MAX_IT-1), 0.1);
      double g = pow(1.0*i/(MAX_IT-1), 0.5);
      double b = pow(1.0*i/(MAX_IT-1), 1);
      color_table[i][0] = (int)(255*r);
      color_table[i][1] = (int)(255*g);
      color_table[i][2] = (int)(255*b);
    }
}

// Callback for exposure
static void
cb_image_annotate(GtkImageViewer *imgv,
                  GdkPixbuf *pixbuf,
                  gint shift_x,
                  gint shift_y,
                  gdouble scale_x,
                  gdouble scale_y,
                  gpointer user_data
                  )
{
  int img_width = gdk_pixbuf_get_width(pixbuf);
  int img_height = gdk_pixbuf_get_height(pixbuf);
  int row_stride = gdk_pixbuf_get_rowstride(pixbuf);
  int pix_stride = 4; // Fixed for GdkPixbuf's in cb_image_annotate
  guint8 *buf = gdk_pixbuf_get_pixels(pixbuf);
  int col_idx, row_idx;

  for (row_idx=0; row_idx<img_height; row_idx++) {
    guint8 *row = buf + row_idx * row_stride;
    for (col_idx=0; col_idx<img_width; col_idx++) {
      double x=(col_idx+shift_x) / scale_x;
      double y=(row_idx+shift_y) / scale_y;
      double x0 = x;
      double y0 = y;
      int iteration = 0;
      
      // Run mandelbrot test for (x,y) with code from wikipedia
      while(x*x + y*y < (2*2) && iteration < MAX_IT) {
        double xtemp = x*x - y*y + x0;
        y = 2 * x * y + y0;
        x = xtemp;
        iteration++;
      }
      
      *(row+col_idx*pix_stride) = color_table[iteration][0];
      *(row+col_idx*pix_stride+1) = color_table[iteration][1];
      *(row+col_idx*pix_stride+2) = color_table[iteration][2];
    }
  }
}

static gint
cb_key_press_event(GtkWidget *widget, GdkEventKey *event)
{
  gint k = event->keyval;
  
  if (k == 'q')
      exit(0);

  return FALSE;
}

int main(int argc, char **argv)
{
  GtkWidget *window, *image_viewer, *scrolled_win;

  gtk_init (&argc, &argv);
  build_color_table();

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
    
  gtk_window_set_title (GTK_WINDOW (window), "Mandelbrot set");
    
  g_signal_connect (window, "destroy",
                    G_CALLBACK (gtk_exit), NULL);

  scrolled_win = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add (GTK_CONTAINER (window), scrolled_win);
  gtk_widget_show(scrolled_win);
    
  image_viewer = gtk_image_viewer_new(NULL);
  g_signal_connect(image_viewer,
                   "image-annotate",
                   G_CALLBACK(cb_image_annotate), NULL);

  gtk_widget_set_size_request (window, 500,500);

  gtk_signal_connect (GTK_OBJECT(window),     "key_press_event",
                      GTK_SIGNAL_FUNC(cb_key_press_event), NULL);
    
  gtk_container_add (GTK_CONTAINER (scrolled_win), image_viewer);
    
  gtk_widget_show (image_viewer);
  gtk_widget_show (window);

  // Set the scroll region and zoom range
  gtk_image_viewer_set_scroll_region(GTK_IMAGE_VIEWER(image_viewer),
                                     -1.9,-1,1,1);
  gtk_image_viewer_set_zoom_range(GTK_IMAGE_VIEWER(image_viewer),
                                  -HUGE, HUGE);

  // Need to do a manual zoom fit at creation because a bug when
  // not using an image.
  gtk_image_viewer_zoom_fit(GTK_IMAGE_VIEWER(image_viewer));
  gtk_main ();

  exit(0);
  return(0);
}
