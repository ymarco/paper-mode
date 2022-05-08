#ifndef __PAPER_MODULE_H_
#define __PAPER_MODULE_H_

#include "PaperView.h"
#include "emacs-module.h"
#include "symbols.h"
#include <gtk/gtk.h>

#define UNUSED(x) (void)(x)




typedef struct Client {
  GtkWidget *container;
  /* PaperView *view; */
  GtkWidget *view;
  int fd;
} Client;


#endif // __PAPER_MODULE_H_
