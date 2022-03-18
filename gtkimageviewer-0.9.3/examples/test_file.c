/* 
 * A trivial GtkImageViewer example shows the file given on the
 * command line.
 */
#include <stdlib.h>
#include "gtk-image-viewer.h"

int 
main (int argc, char *argv[])
{
  GtkWidget *window, *image_viewer;
  char *filename;
  
  gtk_init (&argc, &argv);

  if (argc < 2)
    {
      printf("Need name of image!\n");
      exit(0);
    }
  else
    filename = argv[1];
    
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
  gtk_window_set_title (GTK_WINDOW (window), filename);
  g_signal_connect (window, "destroy", G_CALLBACK (gtk_exit), NULL);

  image_viewer = gtk_image_viewer_new_from_file(filename);
  gtk_container_add (GTK_CONTAINER (window), image_viewer);

  gtk_widget_show_all (window);
  
  gtk_main ();
  
  return 0;
}
