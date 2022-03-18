//  Copying transform from one widget to another.
#include <gtk/gtk.h>
#include "gtk-image-viewer.h"
#include <stdio.h>
#include <stdlib.h>

GtkWidget *image_widget1, *image_widget2;

/*======================================================================
//  This function is called whenever there is some repainting to do on
//  one of the widgets. It will then copy the transformation matrix
//  for the widget to the other widget. This has the potential for
//  causing a infinite recursion as the view_changed is continuously
//  called for each of the two widgets. But this recursion is broken
//  when gtk_image_viewer_set_transform is eventually called with the
//  same coordinates as are already set in the widget. When this
//  happens the view_changed function will not be called again, and
//  the recursion ends.
//  //----------------------------------------------------------------------*/
static void
cb_image_annotate(GtkWidget *imgv,
                  GdkPixbuf *pixbuf,
                  gint shift_x,
                  gint shift_y,
                  gdouble scale_x,
                  gdouble scale_y,
                  gpointer user_data
                  )
{
    double sx,sy;
    int x,y;

    // Get current transformation. Don't confuse that with the annotation
    // transformation above which is the transformation of the pixbuf
    // being painted.
    gtk_image_viewer_get_scale_and_shift(GTK_IMAGE_VIEWER(imgv),
                                         &sx,
                                         &sy,
                                         &x,
                                         &y);

    if (imgv == image_widget1)
	gtk_image_viewer_set_scale_and_shift(GTK_IMAGE_VIEWER(image_widget2),
                                             sx, sy, x, y);
    else
	gtk_image_viewer_set_scale_and_shift(GTK_IMAGE_VIEWER(image_widget1),
                                             sx, sy, x, y);
	
}

int 
main (int argc, char *argv[])
{
  GtkWidget *window, *hbox;
  GdkPixbuf *img;
  GError *error = NULL;
  
  gtk_init (&argc, &argv);

  printf("argc = %d\n", argc);
  if (argc < 2)
    {
      printf("Need name of image!\n");
      exit(0);
    }
  else
    img = gdk_pixbuf_new_from_file (argv[1], &error);

  /* Main window */
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
  
  gtk_window_set_title (GTK_WINDOW (window), "Test tracking");
  
  gtk_signal_connect (GTK_OBJECT (window), "destroy",
		      GTK_SIGNAL_FUNC (gtk_exit), NULL);

  /* hbox */
  hbox = gtk_hbox_new(FALSE, 5);
  gtk_container_add (GTK_CONTAINER (window), hbox);
  gtk_widget_show (hbox);
  
  /* Image widget */
  image_widget1 = gtk_image_viewer_new(img);
  
  gtk_box_pack_start (GTK_BOX (hbox), image_widget1, TRUE, TRUE, 0);
  gtk_widget_show (image_widget1);

  g_signal_connect (image_widget1, "image_annotate",
                    G_CALLBACK (cb_image_annotate), NULL);


  /* Image widget */
  image_widget2 = gtk_image_viewer_new(img);
  gtk_box_pack_start (GTK_BOX (hbox), image_widget2, TRUE, TRUE, 0);
  gtk_widget_show (image_widget2);

  g_signal_connect (image_widget2, "image_annotate",
                    G_CALLBACK (cb_image_annotate), NULL);

  gtk_widget_show (window);
  
  gtk_main ();

  return 0;
}
