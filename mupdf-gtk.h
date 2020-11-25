#ifndef __MUPDF_GTK_H_
#define __MUPDF_GTK_H_
#include <gtk/gtk.h>

typedef struct Client {
  GtkWidget *container;
  GtkWidget *view;
  int fd;
} Client;



#endif // __MUPDF_GTK_H_
